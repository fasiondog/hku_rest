/*
 * Copyright (c) 2026 hikyuu.org
 *
 * Created on: 2026-03-28
 *     Author: fasiondog
 */

#include "RateLimit.h"
#include "hikyuu/utilities/Log.h"

namespace hku {

// 速率限制器方法实现
bool RateLimiter::allowRequest(const std::string& client_ip, const std::string& endpoint,
                               const std::string& method) {
    if (!m_config.enabled) {
        return true;  // 速率限制未启用，允许所有请求
    }

    // 检查白名单
    if (!client_ip.empty() && isIpWhitelisted(client_ip)) {
        return true;  // IP在白名单中，允许请求
    }

    if (!endpoint.empty() && isEndpointWhitelisted(endpoint)) {
        return true;  // 端点在白名单中，允许请求
    }

    if (!method.empty() && isMethodWhitelisted(method)) {
        return true;  // 方法在白名单中，允许请求
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // 更新时间并添加令牌
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_update);

    if (elapsed.count() > 0) {
        // 计算应该添加的令牌数
        double tokens_to_add = (elapsed.count() / 1000.0) * m_config.requests_per_second;
        m_tokens = std::min(static_cast<double>(m_config.burst_size), m_tokens + tokens_to_add);
        m_last_update = now;
    }

    // 更新统计信息
    m_stats.total_requests++;

    // 检查是否有足够令牌
    if (m_tokens >= 1.0) {
        // 消耗一个令牌
        m_tokens -= 1.0;
        m_stats.allowed_requests++;

        // 更新当前速率
        auto time_elapsed =
          std::chrono::duration_cast<std::chrono::seconds>(now - m_stats.last_update);
        if (time_elapsed.count() >= m_config.window_seconds) {
            m_stats.current_rate =
              static_cast<double>(m_stats.allowed_requests) / time_elapsed.count();
            m_stats.last_update = now;
            m_stats.allowed_requests = 0;  // 重置窗口计数
        }

        return true;
    }

    // 令牌不足，拒绝请求
    m_stats.rejected_requests++;
    return false;
}

double RateLimiter::getCurrentTokens() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 计算当前令牌数
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_update);

    if (elapsed.count() > 0) {
        double tokens_to_add = (elapsed.count() / 1000.0) * m_config.requests_per_second;
        return std::min(static_cast<double>(m_config.burst_size), m_tokens + tokens_to_add);
    }

    return m_tokens;
}

RateLimitStats RateLimiter::getStats() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 计算当前速率
    auto now = std::chrono::steady_clock::now();
    auto time_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_stats.last_update);

    RateLimitStats stats = m_stats;
    if (time_elapsed.count() >= m_config.window_seconds) {
        stats.current_rate = static_cast<double>(stats.allowed_requests) / time_elapsed.count();
    }

    return stats;
}

RateLimitConfig RateLimiter::getConfig() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config;
}

void RateLimiter::updateConfig(const RateLimitConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 保存当前的令牌比例
    double old_rate = m_config.requests_per_second;
    (void)old_rate;  // 防止未使用警告

    // 更新配置
    m_config = config;

    // 调整令牌数以保持比例
    if (old_rate > 0) {
        m_tokens = m_tokens * (config.requests_per_second / old_rate);
        m_tokens = std::min(static_cast<double>(config.burst_size), m_tokens);
    } else {
        m_tokens = config.burst_size;  // 如果之前禁用，现在填充令牌桶
    }
}

void RateLimiter::updateConfigPreserveWhitelist(const RateLimitConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 保存现有的白名单配置
    auto ip_whitelist = std::move(m_config.ip_whitelist);
    auto endpoint_whitelist = std::move(m_config.endpoint_whitelist);
    auto method_whitelist = std::move(m_config.method_whitelist);

    // 更新基本配置
    m_config.enabled = config.enabled;
    m_config.requests_per_second = config.requests_per_second;
    m_config.burst_size = config.burst_size;
    m_config.window_seconds = config.window_seconds;
    m_config.apply_to_all = config.apply_to_all;
    m_config.per_ip = config.per_ip;
    m_config.per_endpoint = config.per_endpoint;

    // 恢复白名单配置
    m_config.ip_whitelist = std::move(ip_whitelist);
    m_config.endpoint_whitelist = std::move(endpoint_whitelist);
    m_config.method_whitelist = std::move(method_whitelist);

    // 调整令牌数以保持比例
    if (config.requests_per_second > 0) {
        m_tokens = m_tokens * (config.requests_per_second / m_config.requests_per_second);
        m_tokens = std::min(static_cast<double>(config.burst_size), m_tokens);
    }
}

void RateLimiter::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_tokens = m_config.burst_size;
    m_last_update = std::chrono::steady_clock::now();
    m_stats = RateLimitStats();
}

bool RateLimiter::isIpWhitelisted(const std::string& ip) const {
    for (const auto& whitelisted_ip : m_config.ip_whitelist) {
        if (ip == whitelisted_ip) {
            return true;
        }
    }
    return false;
}

bool RateLimiter::isEndpointWhitelisted(const std::string& endpoint) const {
    for (const auto& whitelisted_endpoint : m_config.endpoint_whitelist) {
        if (endpoint == whitelisted_endpoint ||
            (whitelisted_endpoint.back() == '*' &&
             endpoint.find(whitelisted_endpoint.substr(0, whitelisted_endpoint.size() - 1)) == 0)) {
            return true;  // 精确匹配或通配符匹配
        }
    }
    return false;
}

bool RateLimiter::isMethodWhitelisted(const std::string& method) const {
    for (const auto& whitelisted_method : m_config.method_whitelist) {
        if (method == whitelisted_method) {
            return true;
        }
    }
    return false;
}

}  // namespace hku