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

    // ========== 超时控制 ==========
    static constexpr std::chrono::seconds HEADER_TIMEOUT{10};  // 请求头首字节超时：10 秒
    static constexpr std::chrono::seconds READ_TIMEOUT{30};    // 读取请求超时：30 秒
    static constexpr std::chrono::seconds WRITE_TIMEOUT{30};   // 写入响应超时：30 秒
    static constexpr std::chrono::seconds TOTAL_TIMEOUT{60};   // 总处理超时：60 秒

    // ========== 连接管理 ==========
    static constexpr int MAX_KEEPALIVE_REQUESTS = 10000;  // 单个连接最大请求数（Keep-Alive 模式）
    static constexpr std::chrono::minutes MAX_CONNECTION_AGE{5};  // 连接最大存活时间：5 分钟
    static constexpr int MAX_CONNECTIONS = 1000;                  // 服务器最大连接数
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
