/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-03-13
 *      Author: fasiondog
 */

#include "WebSocketHandle.h"
#include <hikyuu/utilities/Log.h>
#include <optional>
#include <boost/asio/use_awaitable.hpp>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ws = websocket;

namespace hku {

// ============================================================================
// WebSocketHandle 实现
// ============================================================================

WebSocketHandle::WebSocketHandle(void* ws_context) : m_ws_context(ws_context) {}

WebSocketHandle::~WebSocketHandle() {}

net::awaitable<void> WebSocketHandle::onOpen() {
    // 默认实现为空，派生类可重写
    co_return;
}

net::awaitable<void> WebSocketHandle::onClose(ws::close_code const& code, std::string_view reason) {
    HKU_DEBUG("WebSocket connection closed: code={}, reason={}", static_cast<int>(code), reason);
    co_return;
}

net::awaitable<void> WebSocketHandle::onError(beast::error_code const& ec,
                                              std::string_view message) {
    HKU_ERROR("WebSocket error: {} - {}", message, ec.message());
    co_return;
}

net::awaitable<void> WebSocketHandle::onPing() {
    // 默认实现为空，派生类可重写
    co_return;
}

net::awaitable<bool> WebSocketHandle::send(std::string_view message, bool is_text) {
    if (!m_ws_context) {
        HKU_ERROR("WebSocketContext is null");
        co_return false;
    }

    auto* ctx = static_cast<WebSocketContext*>(m_ws_context);
    
    // 使用 Context 中保存的回调函数发送消息
    if (ctx->send_callback) {
        HKU_DEBUG("WebSocketHandle::send called (is_text={})", is_text);
        co_return co_await ctx->send_callback(message, is_text);
    }
    
    HKU_WARN("WebSocketContext send_callback is not set");
    co_return false;
}

net::awaitable<void> WebSocketHandle::broadcast(std::string_view message, bool is_text) {
    // 默认实现为空，需要服务器维护连接列表
    // 派生类可通过单例或全局管理器实现广播
    co_return;
}

std::string WebSocketHandle::getClientIp() const noexcept {
    if (!m_ws_context) {
        return "unknown";
    }
    auto* ctx = static_cast<WebSocketContext*>(m_ws_context);
    return ctx->client_ip;
}

uint16_t WebSocketHandle::getClientPort() const noexcept {
    if (!m_ws_context) {
        return 0;
    }
    auto* ctx = static_cast<WebSocketContext*>(m_ws_context);
    return ctx->client_port;
}

net::awaitable<void> WebSocketHandle::close(ws::close_code code, std::string_view reason) {
    if (!m_ws_context) {
        HKU_ERROR("WebSocketContext is null");
        co_return;
    }

    auto* ctx = static_cast<WebSocketContext*>(m_ws_context);
    
    // 使用 Context 中保存的回调函数关闭连接
    if (ctx->close_callback) {
        co_await ctx->close_callback(code, reason);
    } else {
        HKU_WARN("WebSocketContext close_callback is not set");
    }
    co_return;
}

net::awaitable<void> WebSocketHandle::operator()() {
    if (!m_ws_context) {
        HKU_ERROR("WebSocketContext is null");
        co_return;
    }

    try {
        // 调用 onOpen 回调
        co_await onOpen();

        // 主循环由 WebSocketConnection 管理
        // Handle 只负责业务逻辑

    } catch (const beast::system_error& e) {
        // 忽略返回值以避免警告
        (void)onError(e.code(), e.what());
    } catch (const std::exception& e) {
        // 忽略返回值以避免警告
        (void)onError(beast::errc::make_error_code(beast::errc::connection_aborted), e.what());
    }

    co_return;
}

// ============================================================================
// 流式分批发送实现
// ============================================================================

net::awaitable<bool> WebSocketHandle::sendBatch(const std::vector<std::string>& messages,
                                                 bool is_text,
                                                 std::size_t batchSize,
                                                 std::chrono::milliseconds batchInterval) {
    if (messages.empty()) {
        co_return true;
    }

    auto* ctx = static_cast<WebSocketContext*>(m_ws_context);
    if (!ctx) {
        HKU_ERROR("WebSocketContext is null");
        co_return false;
    }

    // 如果消息总数小于批次大小，直接全部发送
    if (messages.size() <= batchSize) {
        for (const auto& msg : messages) {
            if (!co_await send(msg, is_text)) {
                co_return false;
            }
        }
        co_return true;
    }

    // 启用流式分批模式
    HKU_DEBUG("Starting batch send: total={}, batchSize={}, interval={}ms", 
              messages.size(), batchSize, batchInterval.count());
    
    const std::size_t totalBatches = (messages.size() + batchSize - 1) / batchSize;
    std::size_t currentBatch = 0;
    
    for (std::size_t i = 0; i < messages.size(); i += batchSize) {
        const std::size_t end = std::min(i + batchSize, messages.size());
        
        // 发送当前批次
        for (std::size_t j = i; j < end; ++j) {
            if (!co_await send(messages[j], is_text)) {
                HKU_WARN("Batch send failed at message {}/{}", j, messages.size());
                co_return false;
            }
        }
        
        ++currentBatch;
        HKU_DEBUG("Batch {}/{} sent (messages {}-{}/{})", 
                  currentBatch, totalBatches, i, end - 1, messages.size());
        
        // 如果不是最后一批，等待间隔时间
        if (currentBatch < totalBatches) {
            // 使用 steady_timer 实现异步等待
            net::steady_timer timer(co_await net::this_coro::executor);
            timer.expires_after(batchInterval);
            co_await timer.async_wait(boost::asio::use_awaitable);
        }
    }
    
    HKU_INFO("Batch send completed: total={} messages, {} batches, elapsed ~{}ms",
             messages.size(), totalBatches, totalBatches * batchInterval.count());
    co_return true;
}

net::awaitable<bool> WebSocketHandle::sendBatch(
    std::function<std::optional<std::string>()> generator,
    bool is_text,
    std::size_t batchSize,
    std::chrono::milliseconds batchInterval) {
    
    if (!generator) {
        HKU_ERROR("Generator function is null");
        co_return false;
    }

    auto* ctx = static_cast<WebSocketContext*>(m_ws_context);
    if (!ctx) {
        HKU_ERROR("WebSocketContext is null");
        co_return false;
    }

    HKU_DEBUG("Starting streaming batch send: batchSize={}, interval={}ms", 
              batchSize, batchInterval.count());
    
    std::size_t totalSent = 0;
    std::size_t currentBatch = 0;
    
    // 在循环外部声明 batch，避免 goto 跳转导致的作用域问题
    std::vector<std::string> batch;
    batch.reserve(batchSize);
    
    while (true) {
        // 收集当前批次的消息
        batch.clear();
        
        for (std::size_t i = 0; i < batchSize; ++i) {
            auto msg = generator();
            if (!msg.has_value()) {
                // 生成器返回空值，表示数据结束
                goto send_remaining;
            }
            batch.push_back(std::move(*msg));
        }
        
        // 发送当前批次
        for (const auto& msg : batch) {
            if (!co_await send(msg, is_text)) {
                HKU_WARN("Streaming batch send failed at message {}", totalSent);
                co_return false;
            }
            ++totalSent;
        }
        
        ++currentBatch;
        HKU_DEBUG("Streaming batch {} sent: {} messages (total: {})", 
                  currentBatch, batch.size(), totalSent);
        
        // 等待间隔时间（使用 steady_timer）
        net::steady_timer timer(co_await net::this_coro::executor);
        timer.expires_after(batchInterval);
        co_await timer.async_wait(boost::asio::use_awaitable);
    }
    
send_remaining:
    // 发送剩余的消息（不足一个完整批次）
    if (!batch.empty()) {
        HKU_DEBUG("Sending remaining {} messages in final batch", batch.size());
        for (const auto& msg : batch) {
            if (!co_await send(msg, is_text)) {
                HKU_WARN("Remaining batch send failed at message {}", totalSent);
                co_return false;
            }
            ++totalSent;
        }
        ++currentBatch;
    }
    
    if (currentBatch > 0) {
        HKU_INFO("Streaming batch send completed: total={} messages, {} batches",
                 totalSent, currentBatch);
    } else {
        HKU_DEBUG("No data to send from generator");
    }
    
    co_return true;
}

}  // namespace hku
