/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-03-27
 *      Author: fasiondog
 */

#pragma once

#include "MetricsExporter.h"
#include <coroutine>
#include <boost/asio/awaitable.hpp>

namespace hku {

/**
 * @brief 连接管理器监控器
 * 
 * 定期从 ConnectionManager 收集指标并注册到 MetricsExporter
 * 
 * 使用示例：
 * ```cpp
 * // 在服务器启动时创建监控器
 * auto monitor = std::make_shared<ConnectionMonitor>(conn_mgr);
 * 
 * // 注册 HTTP 端点
 * server.registerHttpHandle("GET", "/metrics", [](void* ctx) -> net::awaitable<void> {
 *     ConnectionMonitor::handleMetrics(ctx);
 *     co_return;
 * });
 * ```
 */
class ConnectionMonitor {
public:
    /**
     * @brief 构造函数
     * @param conn_mgr 连接管理器指针
     * @param sample_interval_ms 采样间隔（毫秒），默认 1000ms
     */
    ConnectionMonitor(std::shared_ptr<ConnectionManager> conn_mgr, size_t sample_interval_ms = 1000)
    : m_conn_mgr(conn_mgr), m_sample_interval_ms(sample_interval_ms) {
        registerMetrics();
    }

    /**
     * @brief 注册所有监控指标
     */
    void registerMetrics() {
        auto& metrics = MetricsExporter::getInstance();
        
        // Gauge 指标
        metrics.registerGauge("hku_http_active_connections", "当前活跃连接数");
        metrics.registerGauge("hku_http_waiting_connections", "等待中的连接数");
        metrics.registerGauge("hku_http_max_concurrent_connections", "最大并发连接数配置值");
        
        // Counter 指标
        metrics.registerCounter("hku_http_total_permits_issued", "已分配的许可总数");
        metrics.registerCounter("hku_http_total_releases", "已释放的许可总数");
        metrics.registerCounter("hku_http_total_timeouts", "超时次数");
        metrics.registerCounter("hku_http_total_acquires", "获取许可请求总数");
        
        // 性能指标
        metrics.registerGauge("hku_http_acquire_queue_size", "当前获取许可的队列长度");
    }

    /**
     * @brief 采集一次指标
     */
    void sample() {
        if (!m_conn_mgr) return;
        
        auto& metrics = MetricsExporter::getInstance();
        
        // 采集 Gauge 指标
        metrics.setGauge("hku_http_active_connections", static_cast<double>(m_conn_mgr->getCurrentCount()));
        metrics.setGauge("hku_http_waiting_connections", static_cast<double>(m_conn_mgr->getWaitingCount()));
        metrics.setGauge("hku_http_max_concurrent_connections", static_cast<double>(m_conn_mgr->getMaxConcurrent()));
        metrics.setGauge("hku_http_acquire_queue_size", static_cast<double>(m_conn_mgr->getWaitingCount()));
        
        // 采集 Counter 指标
        metrics.setCounter("hku_http_total_permits_issued", static_cast<double>(m_conn_mgr->getTotalIssued()));
    }

    /**
     * @brief 开始后台采样线程
     */
    void startSampling() {
        m_running = true;
        m_sampling_thread = std::thread([this]() {
            while (m_running) {
                try {
                    sample();
                } catch (const std::exception& e) {
                    // 记录异常但不中断采样
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(m_sample_interval_ms));
            }
        });
    }

    /**
     * @brief 停止采样线程
     */
    void stopSampling() {
        m_running = false;
        if (m_sampling_thread.joinable()) {
            m_sampling_thread.join();
        }
    }

    /**
     * @brief HTTP /metrics 端点处理器
     * @param ctx HTTP 上下文
     * @return 协程
     * 
     * 返回 Prometheus 格式的监控指标文本
     */
    static boost::asio::awaitable<void> handleMetrics(void* ctx) {
        // TODO: 需要从上下文中提取 Response 对象
        // 这里提供伪代码示例
        /*
        auto& metrics = MetricsExporter::getInstance();
        std::string output = metrics.exportToPrometheusFormat();
        
        response.set(http::field::content_type, "text/plain; version=0.0.4");
        response.body() = output;
        response.prepare_payload();
        co_await http::async_write(socket, response, use_awaitable);
        */
        
        co_return;
    }

private:
    std::shared_ptr<ConnectionManager> m_conn_mgr;
    size_t m_sample_interval_ms;
    std::atomic<bool> m_running{false};
    std::thread m_sampling_thread;
};

} // namespace hku
