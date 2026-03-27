/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-03-13
 *      Author: fasiondog
 */

#pragma once

#include <string>
#include <memory>
#include <functional>
#include <coroutine>

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/awaitable.hpp>

#include <hikyuu/utilities/Log.h>
#include "HttpWebSocketConfig.h"

#ifndef HKU_HTTPD_API
#define HKU_HTTPD_API
#endif

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ws = websocket;
using tcp = net::ip::tcp;

namespace hku {

/**
 * WebSocket 上下文 - 封装 WebSocket 连接的状态
 */
struct WebSocketContext {
    std::string client_ip;
    uint16_t client_port = 0;
    beast::flat_buffer buffer;
    net::steady_timer timer;
    net::cancellation_signal cancel_signal;

    // 保存发送和关闭的回调函数（由 WebSocketConnection 设置）
    std::function<net::awaitable<bool>(std::string_view, bool)> send_callback;
    std::function<net::awaitable<void>(ws::close_code, std::string_view)> close_callback;

    WebSocketContext(net::io_context& io_ctx)
    : buffer(WebSocketConfig::MAX_READ_BUFFER_SIZE), timer(io_ctx) {
        // 设置缓冲区大小限制，防止内存耗尽攻击
        buffer.max_size(WebSocketConfig::MAX_READ_BUFFER_SIZE);
    }

    WebSocketContext(const net::any_io_executor& exec)
    : buffer(WebSocketConfig::MAX_READ_BUFFER_SIZE), timer(exec) {
        // 设置缓冲区大小限制，防止内存耗尽攻击
        buffer.max_size(WebSocketConfig::MAX_READ_BUFFER_SIZE);
    }
};

/**
 * WebSocket Handle 基类 - 用户继承此类实现业务逻辑
 */
class HKU_HTTPD_API WebSocketHandle {
    CLASS_LOGGER_IMP(WebSocketHandle)

public:
    using HandlerFunc = std::function<net::awaitable<void>(void*)>;

    WebSocketHandle() = delete;
    explicit WebSocketHandle(void* ws_context);
    virtual ~WebSocketHandle();

    /**
     * 连接建立时回调
     * @note 在此进行初始化操作，如发送欢迎消息、订阅频道等
     */
    virtual net::awaitable<void> onOpen();

    /**
     * 收到消息时回调
     * @param message 收到的消息内容
     * @param is_text true 表示文本消息，false 表示二进制消息
     * @note 必须实现此方法处理业务逻辑
     */
    virtual net::awaitable<void> onMessage(std::string_view message, bool is_text) = 0;

    /**
     * 连接关闭时回调
     * @param code WebSocket 关闭码
     * @param reason 关闭原因
     * @note 在此进行清理操作
     */
    virtual net::awaitable<void> onClose(ws::close_code const& code, std::string_view reason);

    /**
     * 发生错误时回调
     * @param ec 错误码
     * @param message 错误信息
     * @note 默认记录错误日志
     */
    virtual net::awaitable<void> onError(beast::error_code const& ec, std::string_view message);

    /**
     * 定时心跳回调（可选实现）
     * @note 可用于定期广播、状态同步等
     */
    virtual net::awaitable<void> onPing();

    /**
     * 协程方式的调用入口
     * @note 框架内部使用，用户不应直接调用
     */
    net::awaitable<void> operator()();

protected:
    /**
     * 发送消息给客户端
     * @param message 消息内容
     * @param is_text true 表示文本消息，false 表示二进制消息
     * @return true 成功，false 失败
     */
    virtual net::awaitable<bool> send(std::string_view message, bool is_text = true);

    /**
     * 发送消息给所有连接的客户端（广播）
     * @param message 消息内容
     * @param is_text true 表示文本消息，false 表示二进制消息
     * @note 需要在派生类中实现广播逻辑
     */
    virtual net::awaitable<void> broadcast(std::string_view message, bool is_text = true);

    /**
     * @brief 流式分批发送消息（适用于大批量数据推送）
     *
     * 将大数据集分割成多个小批次依次发送，避免一次性推送造成的网络拥塞
     * 典型应用场景：推送 10000 只股票的行情数据
     *
     * @param messages 待发送的消息列表（每个元素为一条完整的消息）
     * @param is_text true 表示文本消息，false 表示二进制消息
     * @param batchSize 每批发送的消息数量，默认 500 条/批
     * @param batchIntervalMs 批次间的时间间隔（毫秒），默认 50ms
     * @return true 全部发送成功，false 中途失败
     *
     * @note 总消息数超过 batchSize 时自动启用流式模式
     * @note 预计总耗时 = (messages.size() / batchSize) * batchIntervalMs
     * @example
     * // 推送 10000 条行情数据，分 20 批，总耗时约 1 秒
     * std::vector<std::string> quotes = generateQuoteData(10000);
     * co_await sendBatch(quotes, true, 500, 50);
     */
    virtual net::awaitable<bool> sendBatch(
      const std::vector<std::string>& messages, bool is_text = true,
      std::size_t batchSize = 500,                                               // 默认 500 条/批
      std::chrono::milliseconds batchInterval = std::chrono::milliseconds(50));  // 默认 50ms 间隔

    /**
     * @brief 流式分批发送消息（自定义数据生成器）
     *
     * 通过生成器函数动态产生消息内容，适合内存敏感场景
     *
     * @param generator 消息生成器函数，返回 std::optional<std::string>，空值表示结束
     * @param is_text true 表示文本消息，false 表示二进制消息
     * @param batchSize 每批发送的消息数量，默认 500 条/批
     * @param batchIntervalMs 批次间的时间间隔（毫秒），默认 50ms
     * @return true 全部发送成功，false 中途失败
     *
     * @note 生成器应返回 std::nullopt 表示数据结束
     * @example
     * // 从数据库流式读取并推送
     * auto generator = [this, offset = 0, &db]() mutable -> std::optional<std::string> {
     *     if (offset >= totalRecords) return std::nullopt;
     *     auto data = db.query(offset, 500);
     *     offset += data.size();
     *     return serializeToJson(data);
     * };
     * co_await sendBatch(generator, true, 500, 50);
     */
    virtual net::awaitable<bool> sendBatch(
      std::function<std::optional<std::string>()> generator, bool is_text = true,
      std::size_t batchSize = 500,                                               // 默认 500 条/批
      std::chrono::milliseconds batchInterval = std::chrono::milliseconds(50));  // 默认 50ms 间隔

    /**
     * 获取客户端 IP 地址
     * @return 客户端 IP 地址
     */
    std::string getClientIp() const noexcept;

    /**
     * 获取客户端端口
     * @return 客户端端口
     */
    uint16_t getClientPort() const noexcept;

    /**
     * 关闭连接
     * @param code WebSocket 关闭码
     * @param reason 关闭原因
     */
    virtual net::awaitable<void> close(ws::close_code code = ws::close_code::normal,
                                       std::string_view reason = "");

    /**
     * @brief 配置 WebSocket 安全选项
     *
     * 必须在 websocket::stream 创建后立即调用，设置以下安全限制:
     * - 消息最大大小：10MB (与 HTTP 请求体限制一致)
     * - 帧最大大小：10MB
     * - 自动 Fragmentation: 禁用 (强制应用层控制分片)
     *
     * @tparam StreamType websocket::stream 类型
     * @param ws WebSocket stream 引用
     */
    template <typename StreamType>
    static void configureWebSocketSecurity(StreamType& ws) {
        // 设置消息最大大小 (防止攻击者通过超大消息消耗内存)
        ws.read_message_max(WebSocketConfig::MAX_MESSAGE_SIZE);

        // 设置帧最大大小 (允许完整消息的单帧传输)
        ws.write_message_max(WebSocketConfig::MAX_FRAME_SIZE);

        // 禁用自动 Fragmentation，由应用层控制分片策略
        ws.auto_fragment(false);

        HKU_DEBUG("WebSocket security configured: max_message_size={}, max_frame_size={}",
                  WebSocketConfig::MAX_MESSAGE_SIZE, WebSocketConfig::MAX_FRAME_SIZE);
    }

protected:
    void* m_ws_context{nullptr};  // WebSocketContext 指针
};

#define WS_HANDLE_IMP(cls) \
public:                    \
    explicit cls(void* ws_context) : WebSocketHandle(ws_context) {}

}  // namespace hku
