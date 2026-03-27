/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-03-27
 *      Author: fasiondog
 */

#pragma once

#include <string>
#include <map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace hku {

/**
 * @brief Prometheus 格式的监控指标导出器
 * 
 * 支持以下指标类型：
 * - counter: 只增不减的计数器（如总请求数）
 * - gauge: 可增可减的仪表（如当前连接数）
 * - histogram: 直方图（如延迟分布）
 * 
 * 使用示例：
 * ```cpp
 * auto& metrics = MetricsExporter::getInstance();
 * metrics.registerGauge("active_connections", "当前活跃连接数");
 * metrics.setGauge("active_connections", 100);
 * 
 * metrics.registerCounter("total_requests", "总请求数");
 * metrics.incrementCounter("total_requests");
 * ```
 */
class MetricsExporter {
public:
    /**
     * @brief 获取单例实例
     */
    static MetricsExporter& getInstance() {
        static MetricsExporter instance;
        return instance;
    }

    /**
     * @brief 注册 Gauge 指标
     * @param name 指标名称
     * @param help 指标描述
     */
    void registerGauge(const std::string& name, const std::string& help) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_gauges[name] = 0.0;
        m_gauge_help[name] = help;
    }

    /**
     * @brief 设置 Gauge 值
     * @param name 指标名称
     * @param value 指标值
     */
    void setGauge(const std::string& name, double value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_gauges.find(name) != m_gauges.end()) {
            m_gauges[name] = value;
        }
    }

    /**
     * @brief 增加 Gauge 值
     * @param name 指标名称
     * @param delta 增量
     */
    void addGauge(const std::string& name, double delta) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_gauges.find(name) != m_gauges.end()) {
            m_gauges[name] += delta;
        }
    }

    /**
     * @brief 注册 Counter 指标
     * @param name 指标名称
     * @param help 指标描述
     */
    void registerCounter(const std::string& name, const std::string& help) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_counters[name] = 0.0;
        m_counter_help[name] = help;
    }

    /**
     * @brief 增加 Counter 值
     * @param name 指标名称
     * @param delta 增量（默认为 1）
     */
    void incrementCounter(const std::string& name, double delta = 1.0) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_counters.find(name) != m_counters.end()) {
            m_counters[name] += delta;
        }
    }

    /**
     * @brief 获取 Counter 值
     * @param name 指标名称
     * @return 指标值
     */
    double getCounter(const std::string& name) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_counters.find(name);
        return (it != m_counters.end()) ? it->second : 0.0;
    }

    /**
     * @brief 设置 Counter 值（用于初始化或重置）
     * @param name 指标名称
     * @param value 指标值
     */
    void setCounter(const std::string& name, double value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_counters.find(name) != m_counters.end()) {
            m_counters[name] = value;
        }
    }

    /**
     * @brief 导出为 Prometheus 格式文本
     * @return Prometheus exposition format 字符串
     * 
     * 输出示例：
     * # HELP active_connections 当前活跃连接数
     * # TYPE active_connections gauge
     * active_connections 100
     * 
     * # HELP total_requests 总请求数
     * # TYPE total_requests counter
     * total_requests 1234567
     */
    std::string exportToPrometheusFormat() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::ostringstream oss;
        
        // 导出 Gauges
        for (const auto& [name, value] : m_gauges) {
            oss << "# HELP " << name << " " << m_gauge_help[name] << "\n";
            oss << "# TYPE " << name << " gauge\n";
            oss << name << " " << std::fixed << std::setprecision(0) << value << "\n\n";
        }
        
        // 导出 Counters
        for (const auto& [name, value] : m_counters) {
            oss << "# HELP " << name << " " << m_counter_help[name] << "\n";
            oss << "# TYPE " << name << " counter\n";
            oss << name << " " << std::fixed << std::setprecision(0) << value << "\n\n";
        }
        
        return oss.str();
    }

    /**
     * @brief 重置所有指标（用于测试）
     */
    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [name, value] : m_gauges) {
            value = 0.0;
        }
        for (auto& [name, value] : m_counters) {
            value = 0.0;
        }
    }

private:
    MetricsExporter() = default;
    ~MetricsExporter() = default;
    
    // 禁止拷贝和移动
    MetricsExporter(const MetricsExporter&) = delete;
    MetricsExporter& operator=(const MetricsExporter&) = delete;
    MetricsExporter(MetricsExporter&&) = delete;
    MetricsExporter& operator=(MetricsExporter&&) = delete;

    mutable std::mutex m_mutex;
    std::map<std::string, double> m_gauges;
    std::map<std::string, double> m_counters;
    std::map<std::string, std::string> m_gauge_help;
    std::map<std::string, std::string> m_counter_help;
};

} // namespace hku
