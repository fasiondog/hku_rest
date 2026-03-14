/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-03-15
 *      Author: fasiondog
 */

#pragma once

#include <chrono>
#include <cstddef>

namespace hku {

/**
 * HTTP 协议专用配置
 *
 * 包含 HTTP/HTTPS 请求处理的所有安全限制和超时配置
 */
struct HttpConfig {
    // ========== 请求大小限制 ==========
    static constexpr std::size_t MAX_BUFFER_SIZE = 1024 * 1024;     // 1MB - 读取缓冲区最大大小
    static constexpr std::size_t MAX_BODY_SIZE = 10 * 1024 * 1024;  // 10MB - 请求体最大大小
    static constexpr std::size_t MAX_HEADER_SIZE = 8192;            // 8KB - 请求头最大大小

    // ========== 超时控制 (针对高频低延迟场景优化) ==========
    // 注意：超时时间过短会导致频繁的定时器检查和连接重建，反而增加延迟
    static constexpr std::chrono::milliseconds HEADER_TIMEOUT{2000};  // 请求头首字节超时：2s (平衡性能与安全)
    static constexpr std::chrono::seconds READ_TIMEOUT{5};            // 读取请求超时：5s (降低从 30s)
    static constexpr std::chrono::seconds WRITE_TIMEOUT{5};           // 写入响应超时：5s (降低从 30s)
    static constexpr std::chrono::seconds TOTAL_TIMEOUT{30};          // 总处理超时：30s (降低从 60s)

    // ========== 连接管理 (针对高频短连接优化) ==========
    static constexpr int MAX_KEEPALIVE_REQUESTS = 0;         // 0=禁用请求数限制，仅依赖时间限制
    static constexpr std::chrono::minutes MAX_CONNECTION_AGE{30};  // 连接最大存活时间：30 分钟
    static constexpr int MAX_CONNECTIONS = 100000;                // 服务器最大连接数
    
    // ========== P99 延迟优化配置 ==========
    // 启用快速路径：对于简单请求跳过部分安全检查
    static constexpr bool ENABLE_FAST_PATH = true;
    
    // 缓冲区复用策略：保留最小容量避免频繁分配
    static constexpr std::size_t BUFFER_MIN_CAPACITY = 512;  // 保留 512 字节基础容量
};

/**
 * WebSocket 协议专用配置
 *
 * 包含 WebSocket 连接的所有安全限制、超时控制和心跳配置
 */
struct WebSocketConfig {
    // ========== 消息大小限制 ==========
    static constexpr std::size_t MAX_MESSAGE_SIZE =
      10 * 1024 * 1024;  // 10MB - 消息最大大小（与 HTTP 请求体一致）
    static constexpr std::size_t MAX_FRAME_SIZE =
      10 * 1024 * 1024;  // 10MB - 帧最大大小（允许完整消息的单帧传输）
    static constexpr std::size_t MAX_READ_BUFFER_SIZE = 10 * 1024 * 1024;  // 10MB - 读取缓冲区最大
    static constexpr std::size_t MAX_WRITE_QUEUE_SIZE = 1000;              // 最大待发送消息数

    // ========== 超时控制 ==========
    static constexpr std::chrono::seconds READ_TIMEOUT{30};   // 读取消息超时：30 秒
    static constexpr std::chrono::seconds WRITE_TIMEOUT{30};  // 写入消息超时：30 秒

    // ========== 心跳保活机制 ==========
    static constexpr std::chrono::seconds PING_INTERVAL{60};  // Ping 发送间隔：60 秒
    static constexpr std::chrono::seconds PING_TIMEOUT{10};   // Ping 响应超时：10 秒

    // ========== 连接管理 ==========
    // WebSocket 为长连接，无请求次数限制，依赖心跳检测维持连接
};

}  // namespace hku
