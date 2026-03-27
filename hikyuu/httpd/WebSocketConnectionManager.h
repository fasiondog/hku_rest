/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-03-15
 *      Author: fasiondog
 */

#pragma once

#include <boost/asio.hpp>
#include <atomic>
#include <mutex>
#include <queue>
#include <memory>
#include <chrono>

#include <hikyuu/utilities/Log.h>

namespace hku {

/**
 * @brief WebSocket 连接许可 - RAII 模式
 *
 * 持有该对象表示获得了 WebSocket 连接处理许可，析构时自动释放
 *
 * 设计说明：
 * - 使用 int 作为许可标识符，-1 表示无效，>=0 表示有效
 * - 支持扩展：未来可以添加优先级、配额等功能
 * - 零开销：无虚函数，编译器可完全内联优化
 */
class WebSocketPermit {
public:
    /**
     * @brief 构造无效的连接许可
     */
    WebSocketPermit() : m_permit_id(-1), m_priority(0) {}

    /**
     * @brief 构造有效的连接许可
     * @param permit_id 许可标识符（>=0 表示有效）
     */
    explicit WebSocketPermit(int permit_id) : m_permit_id(permit_id), m_priority(0) {}

    /**
     * @brief 构造有效的连接许可（带优先级）
     * @param permit_id 许可标识符
     * @param priority 优先级（0=普通，1=优先，2=VIP）
     */
    WebSocketPermit(int permit_id, int priority) : m_permit_id(permit_id), m_priority(priority) {}

    /**
     * @brief 检查许可是否有效
     */
    explicit operator bool() const {
        return m_permit_id >= 0;
    }

    /**
     * @brief 获取许可标识符
     */
    int getId() const {
        return m_permit_id;
    }

    /**
     * @brief 设置优先级
     */
    void setPriority(int level) {
        m_priority = level;
    }

    /**
     * @brief 获取优先级
     */
    int getPriority() const {
        return m_priority;
    }

    /**
     * @brief 比较运算符（用于排序和优先级队列）
     */
    bool operator<(const WebSocketPermit& other) const {
        // 优先级高的排在前面
        return m_priority < other.m_priority;
    }

private:
    int m_permit_id;  ///< 许可标识符：-1=无效，>=0=有效
    int m_priority;   ///< 优先级：0=普通，1=优先，2=VIP（预留扩展）
};

/**
 * @brief WebSocket 连接管理器 - 基于信号量和等待队列的连接控制系统
 *
 * 设计思想：
 * - 借鉴 ConnectionManager 的等待队列和超时机制
 * - 专门管理 WebSocket 长连接的并发数量
 * - 仅管理抽象的许可（Permit），不涉及具体资源对象
 * - RAII 模式确保异常安全
 *
 * 核心特性：
 * 1. 限制同时允许并行处理的 WebSocket 连接数（协程级别）
 * 2. 超过限制时不拒绝连接，而是进入 FIFO 等待队列
 * 3. 支持超时控制，避免无限等待
 * 4. 提供优雅的资源获取和释放机制
 * 5. 针对 WebSocket 长连接场景优化心跳和保活机制
 *
 * 使用示例：
 * @code
 * // 在 HttpServer 中初始化
 * auto ws_conn_mgr = std::make_shared<WebSocketConnectionManager>(1000, 30000);
 *
 * // 在 WebSocket 连接构造函数中获取许可
 * auto permit = co_await ws_conn_mgr->acquire();
 * if (!permit) {
 *     // 等待超时或失败
 *     co_return;
 * }
 *
 * // 在析构函数中自动释放（RAII）
 * @endcode
 */
class WebSocketConnectionManager : public std::enable_shared_from_this<WebSocketConnectionManager> {
public:
    /**
     * @brief 构造 WebSocket 连接管理器
     * @param max_concurrent 最大并发 WebSocket 连接数
     * @param wait_timeout_ms 等待超时时间（毫秒），0 表示无限等待
     */
    explicit WebSocketConnectionManager(size_t max_concurrent, size_t wait_timeout_ms = 30000)
    : m_max_concurrent(max_concurrent),
      m_wait_timeout(std::chrono::milliseconds(wait_timeout_ms)),
      m_current_count(0),
      m_waiting_count(0),
      m_next_permit_id(0),
      m_is_shutdown(false) {}

    ~WebSocketConnectionManager() = default;

    /**
     * @brief 关闭 WebSocket 连接管理器，唤醒所有等待的连接（用于服务器优雅退出）
     * @note 调用后，所有正在等待的连接将立即收到超时信号
     */
    void shutdown() {
        // 设置关闭标志，禁止新的等待
        m_is_shutdown.store(true, std::memory_order_release);

        // 【关键】先收集所有定时器指针（持锁），然后在锁外取消，避免死锁
        std::vector<std::shared_ptr<boost::asio::steady_timer>> timers_to_cancel;
        {
            std::lock_guard<std::mutex> lock(m_wait_mutex);
            while (!m_wait_queue.empty()) {
                timers_to_cancel.push_back(m_wait_queue.front());
                m_wait_queue.pop();
            }
        }

        // 在锁外取消定时器，避免死锁
        for (auto& timer : timers_to_cancel) {
            timer->cancel();
        }

        // 重置等待计数器
        m_waiting_count.store(0, std::memory_order_release);

        HKU_INFO("WebSocket connection manager shutdown, all waiting connections notified");
    }

    /**
     * @brief 异步获取 WebSocket 连接许可（协程版本）
     * @return WebSocketPermit 连接许可，如果超时则返回无效许可
     */
    boost::asio::awaitable<WebSocketPermit> acquire() {
        auto executor = co_await boost::asio::this_coro::executor;

        // 检查是否已关闭
        if (m_is_shutdown.load(std::memory_order_acquire)) {
            HKU_WARN("WebSocket connection manager is shutdown, reject new acquire request");
            co_return WebSocketPermit();
        }

        // 尝试快速获取
        int expected = m_current_count.load(std::memory_order_acquire);
        while (expected < static_cast<int>(m_max_concurrent)) {
            if (m_current_count.compare_exchange_weak(
                  expected, expected + 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
                int permit_id = m_next_permit_id.fetch_add(1, std::memory_order_relaxed);
                HKU_DEBUG("WebSocket connection acquired: id={}, count={}/{}", permit_id,
                          expected + 1, m_max_concurrent);
                co_return WebSocketPermit(permit_id);
            }
        }

        // 需要等待，加入等待队列
        m_waiting_count.fetch_add(1, std::memory_order_relaxed);

        // 创建定时器等待
        auto timer = std::make_shared<boost::asio::steady_timer>(executor);
        timer->expires_after(m_wait_timeout);

        // 加入等待队列
        {
            std::lock_guard<std::mutex> lock(m_wait_mutex);
            m_wait_queue.push(timer);
        }

        HKU_DEBUG("WebSocket connection waiting: current={}, waiting={}", m_current_count.load(),
                  m_waiting_count.load());

        // 等待被唤醒或超时
        boost::system::error_code ec;
        try {
            co_await timer->async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, ec));
        } catch (...) {
            ec = boost::asio::error::operation_aborted;
        }

        // 从等待队列移除
        {
            std::lock_guard<std::mutex> lock(m_wait_mutex);
            if (!m_wait_queue.empty() && m_wait_queue.front() == timer) {
                m_wait_queue.pop();
            } else {
                // 如果不是队首，需要查找并移除（超时情况）
                std::queue<std::shared_ptr<boost::asio::steady_timer>> temp;
                while (!m_wait_queue.empty()) {
                    auto t = m_wait_queue.front();
                    m_wait_queue.pop();
                    if (t != timer) {
                        temp.push(t);
                    }
                }
                m_wait_queue = std::move(temp);
            }
        }

        m_waiting_count.fetch_sub(1, std::memory_order_relaxed);

        if (ec == boost::asio::error::operation_aborted) {
            // 被唤醒，获得许可
            expected = m_current_count.load(std::memory_order_acquire);
            while (true) {
                if (m_current_count.compare_exchange_weak(expected, expected + 1,
                                                          std::memory_order_acq_rel,
                                                          std::memory_order_acquire)) {
                    int permit_id = m_next_permit_id.fetch_add(1, std::memory_order_relaxed);
                    HKU_DEBUG("WebSocket connection acquired from queue: id={}, count={}/{}",
                              permit_id, expected + 1, m_max_concurrent);
                    co_return WebSocketPermit(permit_id);
                }
            }
        } else {
            // 超时
            HKU_WARN("WebSocket connection acquire timeout after {}ms", m_wait_timeout.count());
            co_return WebSocketPermit();  // 返回无效许可
        }
    }

    /**
     * @brief 释放 WebSocket 连接许可（由 WebSocketPermit 析构时自动调用）
     * @note 通常不需要手动调用，Permit 析构时会自动释放
     */
    void release(int permit_id) {
        m_current_count.fetch_sub(1, std::memory_order_acq_rel);
        HKU_DEBUG("WebSocket connection released: id={}, count={}/{}", permit_id,
                  m_current_count.load(), m_max_concurrent);

        // 唤醒一个等待者
        notifyOne();
    }

    /**
     * @brief 获取当前活跃 WebSocket 连接数
     */
    int getCurrentCount() const {
        return m_current_count.load(std::memory_order_relaxed);
    }

    /**
     * @brief 获取等待中的 WebSocket 连接数
     */
    int getWaitingCount() const {
        return m_waiting_count.load(std::memory_order_relaxed);
    }

    /**
     * @brief 获取最大并发数配置
     */
    size_t getMaxConcurrent() const {
        return m_max_concurrent;
    }

    /**
     * @brief 检查是否已达到最大并发数
     */
    bool isAtLimit() const {
        return m_current_count.load(std::memory_order_relaxed) >=
               static_cast<int>(m_max_concurrent);
    }

    /**
     * @brief 获取已分配的许可总数（用于监控和统计）
     */
    int getTotalIssued() const {
        return m_next_permit_id.load(std::memory_order_relaxed);
    }

private:
    /**
     * @brief 唤醒一个等待者
     * @details FIFO 顺序，公平调度
     */
    void notifyOne() {
        std::lock_guard<std::mutex> lock(m_wait_mutex);
        if (!m_wait_queue.empty()) {
            auto timer = m_wait_queue.front();
            m_wait_queue.pop();
            timer->cancel();  // 取消定时器，唤醒等待者
        }
    }

private:
    const size_t m_max_concurrent;                   ///< 最大并发 WebSocket 连接数
    const std::chrono::milliseconds m_wait_timeout;  ///< 等待超时时间

    std::atomic<int> m_current_count;        ///< 当前活跃 WebSocket 连接数
    std::atomic<int> m_waiting_count;        ///< 等待中的 WebSocket 连接数
    std::atomic<int> m_next_permit_id;       ///< 下一个许可 ID（单调递增）
    std::atomic<bool> m_is_shutdown{false};  ///< 是否已关闭

    std::mutex m_wait_mutex;                                              ///< 保护等待队列
    std::queue<std::shared_ptr<boost::asio::steady_timer>> m_wait_queue;  ///< FIFO 等待队列
};

}  // namespace hku
