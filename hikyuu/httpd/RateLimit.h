/*
 * Copyright (c) 2026 hikyuu.org
 *
 * Created on: 2026-03-28
 *     Author: fasiondog
 */

#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <chrono>

#ifndef HKU_HTTPD_API
#define HKU_HTTPD_API
#endif

namespace hku {

/**
 * @brief 速率限制统计信息结构
 */
struct RateLimitStats {
    uint64_t total_requests{0};                         // 总请求数
    uint64_t allowed_requests{0};                       // 允许的请求数
    uint64_t rejected_requests{0};                      // 被拒绝的请求数
    double current_rate{0.0};                           // 当前请求速率（请求/秒）
    std::chrono::steady_clock::time_point last_update;  // 最后更新时间

    RateLimitStats() : last_update(std::chrono::steady_clock::now()) {}
};

/**
 * @brief 速率限制配置结构
 */
struct RateLimitConfig {
    bool enabled{false};               // 是否启用速率限制
    uint32_t requests_per_second{10};  // 每秒允许的最大请求数
    uint32_t burst_size{20};           // 突发流量容忍度（令牌桶大小）
    uint32_t window_seconds{1};        // 时间窗口大小（秒）
    bool apply_to_all{true};           // 是否应用于所有请求
    bool per_ip{false};                // 是否按IP进行限制
    bool per_endpoint{false};          // 是否按端点进行限制

    // IP白名单（不受速率限制的IP）
    std::vector<std::string> ip_whitelist;

    // 端点白名单（不受速率限制的端点）
    std::vector<std::string> endpoint_whitelist;

    // 方法白名单（不受速率限制的HTTP方法）
    std::vector<std::string> method_whitelist;

    RateLimitConfig() = default;

    /**
     * @brief 快速配置：启用全局速率限制
     * @param rps 每秒请求数
     * @param burst 突发流量大小
     * @return RateLimitConfig 引用
     */
    static RateLimitConfig globalLimit(uint32_t rps = 10, uint32_t burst = 20) {
        RateLimitConfig config;
        config.enabled = true;
        config.requests_per_second = rps;
        config.burst_size = burst;
        config.apply_to_all = true;
        config.per_ip = false;
        config.per_endpoint = false;
        return config;
    }

    /**
     * @brief 快速配置：启用每IP速率限制
     * @param rps 每秒请求数
     * @param burst 突发流量大小
     * @return RateLimitConfig 引用
     */
    static RateLimitConfig perIpLimit(uint32_t rps = 5, uint32_t burst = 10) {
        RateLimitConfig config;
        config.enabled = true;
        config.requests_per_second = rps;
        config.burst_size = burst;
        config.apply_to_all = true;
        config.per_ip = true;
        config.per_endpoint = false;
        return config;
    }

    /**
     * @brief 快速配置：禁用速率限制
     * @return RateLimitConfig 引用
     */
    static RateLimitConfig disable() {
        RateLimitConfig config;
        config.enabled = false;
        return config;
    }
};

/**
 * @brief 速率限制器类（使用令牌桶算法）
 */
class HKU_HTTPD_API RateLimiter {
public:
    RateLimiter() = default;

    /**
     * @brief 使用配置初始化速率限制器
     * @param config 速率限制配置
     */
    explicit RateLimiter(const RateLimitConfig& config)
    : m_config(config),
      m_tokens(config.burst_size),
      m_last_update(std::chrono::steady_clock::now()) {}

    bool isEnabled() const noexcept {
        return m_config.enabled;
    }

    /**
     * @brief 检查是否允许请求
     * @param client_ip 客户端IP地址
     * @param endpoint 请求端点
     * @param method HTTP方法
     * @return 如果允许请求返回true
     */
    bool allowRequest(const std::string& client_ip = "", const std::string& endpoint = "",
                      const std::string& method = "");

    /**
     * @brief 获取当前令牌数
     * @return 当前可用令牌数
     */
    double getCurrentTokens() const;

    /**
     * @brief 获取速率限制统计信息
     * @return RateLimitStats 统计信息
     */
    RateLimitStats getStats() const;

    /**
     * @brief 获取当前配置
     * @return 当前速率限制配置
     */
    RateLimitConfig getConfig() const;

    /**
     * @brief 更新速率限制配置
     * @param config 新的速率限制配置
     */
    void updateConfig(const RateLimitConfig& config);

    /**
     * @brief 更新配置（保留现有白名单等设置）
     * @param config 新的配置，现有白名单会保留
     */
    void updateConfigPreserveWhitelist(const RateLimitConfig& config);

    /**
     * @brief 重置速率限制器
     */
    void reset();

private:
    /**
     * @brief 检查IP是否在白名单中
     * @param ip 要检查的IP地址
     * @return 如果在白名单中返回true
     */
    bool isIpWhitelisted(const std::string& ip) const;

    /**
     * @brief 检查端点是否在白名单中
     * @param endpoint 要检查的端点
     * @return 如果在白名单中返回true
     */
    bool isEndpointWhitelisted(const std::string& endpoint) const;

    /**
     * @brief 检查HTTP方法是否在白名单中
     * @param method 要检查的HTTP方法
     * @return 如果在白名单中返回true
     */
    bool isMethodWhitelisted(const std::string& method) const;

private:
    RateLimitConfig m_config;
    mutable std::mutex m_mutex;

    // 令牌桶算法相关
    double m_tokens;                                      // 当前令牌数
    std::chrono::steady_clock::time_point m_last_update;  // 最后更新时间

    // 统计信息
    RateLimitStats m_stats;
};

}  // namespace hku