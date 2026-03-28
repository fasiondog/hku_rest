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
 * 针对交易平台行情推送场景优化
 */
struct HttpConfig {
    // ========== 请求大小限制 ==========
    static constexpr std::size_t MAX_BUFFER_SIZE =
      2 * 1024 * 1024;  // 2MB - 读取缓冲区最大大小（生产环境标准）
    static constexpr std::size_t MAX_BODY_SIZE = 10 * 1024 * 1024;  // 10MB - 请求体最大大小
    static constexpr std::size_t MAX_HEADER_SIZE = 8192;            // 8KB - 请求头最大大小

    // ========== 超时控制 (针对高频低延迟场景优化) ==========
    // 注意：超时时间过短会导致频繁的定时器检查和连接重建，反而增加延迟
    static constexpr std::chrono::milliseconds HEADER_TIMEOUT{
      10000};  // 请求头首字节超时：10s (生产环境宽松值)
    static constexpr std::chrono::seconds READ_TIMEOUT{60};   // 读取请求超时：60s (生产环境标准)
    static constexpr std::chrono::seconds WRITE_TIMEOUT{60};  // 写入响应超时：60s (支持批量推送)
    static constexpr std::chrono::seconds TOTAL_TIMEOUT{
      180};  // 总处理超时：180s (支持复杂批量操作)

    // ========== 连接管理 (针对高频短连接优化) ==========
    static constexpr int MAX_KEEPALIVE_REQUESTS = 100000;  // 10K 次请求后关闭连接（防止资源泄漏）
    static constexpr std::chrono::minutes MAX_CONNECTION_AGE{
      5};                                          // 连接最大存活时间：5 分钟（快速轮换）
    static constexpr int MAX_CONNECTIONS = 10000;  // 服务器最大连接数：10K（防资源耗尽）

    // ========== P99 延迟优化配置 ==========
    // 启用快速路径：对于简单请求跳过部分安全检查
    static constexpr bool ENABLE_FAST_PATH = true;

    // 缓冲区复用策略：保留最小容量避免频繁分配
    static constexpr std::size_t BUFFER_MIN_CAPACITY = 4 * 1024;  // 保留 4KB 基础容量（适应大负载）

    // ========== 分块传输与批量响应 ==========
    static constexpr std::size_t CHUNK_SIZE = 32 * 1024;  // 推荐分块大小：32KB
    static constexpr bool ENABLE_BATCH_RESPONSE = true;   // 启用批量响应模式
    static constexpr std::size_t BATCH_THRESHOLD = 5000;  // 批量阈值：5000 条记录自动分页或流式响应
};

/**
 * WebSocket 协议专用配置
 *
 * 包含 WebSocket 连接的所有安全限制、超时控制和心跳配置
 * 针对交易平台推送 10000 支股票行情数据场景优化
 */
struct WebSocketConfig {
    // ========== 消息大小限制 ==========
    static constexpr std::size_t MAX_MESSAGE_SIZE =
      15 * 1024 * 1024;  // 15MB - 消息最大大小（支持单次全量推送 10000 只股票）
    static constexpr std::size_t MAX_FRAME_SIZE =
      15 * 1024 * 1024;  // 15MB - 帧最大大小（避免分帧，完整消息传输）
    static constexpr std::size_t MAX_READ_BUFFER_SIZE =
      32 * 1024 * 1024;  // 32MB - 读取缓冲区最大（缓存 4-5 次全量推送）
    static constexpr std::size_t MAX_WRITE_QUEUE_SIZE =
      3000;  // 最大待发送消息数（防止高频推送阻塞）

    // ========== 超时控制 ==========
    static constexpr std::chrono::seconds READ_TIMEOUT{60};   // 读取消息超时：60 秒（生产环境标准）
    static constexpr std::chrono::seconds WRITE_TIMEOUT{30};  // 写入消息超时：30 秒（快速失败）

    // ========== 心跳保活机制 ==========
    static constexpr std::chrono::seconds PING_INTERVAL{
      30};                                                   // Ping 发送间隔：30 秒（快速检测断线）
    static constexpr std::chrono::seconds PING_TIMEOUT{15};  // Ping 响应超时：15 秒（容忍网络波动）
    // 断线检测时间控制在 45 秒内（30s + 15s）

    // ========== 连接管理 ==========
    // WebSocket 为长连接，无请求次数限制，依赖心跳检测维持连接
    static constexpr int MAX_CONNECTIONS = 1000;  // 单实例最大连接数：1000（合理并发）
};

}  // namespace hku
