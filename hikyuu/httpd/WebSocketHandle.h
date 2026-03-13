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

    // 安全限制配置 - 复用 HTTP 模块的安全限制标准 (10MB)
    static constexpr std::size_t MAX_MESSAGE_SIZE =
      10 * 1024 * 1024;  // 10MB - 消息最大大小 (与 HTTP 请求体一致)
    static constexpr std::size_t MAX_FRAME_SIZE =
      10 * 1024 * 1024;  // 10MB - 帧最大大小 (允许完整消息的单帧传输)
    static constexpr std::size_t MAX_READ_BUFFER_SIZE = 10 * 1024 * 1024;  // 10MB - 读取缓冲区最大
    static constexpr std::size_t MAX_WRITE_QUEUE_SIZE = 1000;              // 最大待发送消息数

    // 超时配置
    static constexpr std::chrono::seconds READ_TIMEOUT{30};   // 读取超时
    static constexpr std::chrono::seconds WRITE_TIMEOUT{30};  // 写入超时
    static constexpr std::chrono::seconds PING_INTERVAL{60};  // Ping 间隔
    static constexpr std::chrono::seconds PING_TIMEOUT{10};   // Ping 响应超时

    WebSocketContext(net::io_context& io_ctx) : buffer(MAX_READ_BUFFER_SIZE), timer(io_ctx) {}

    WebSocketContext(const net::any_io_executor& exec)
    : buffer(MAX_READ_BUFFER_SIZE), timer(exec) {}
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
    net::awaitable<bool> send(std::string_view message, bool is_text = true);

    /**
     * 发送消息给所有连接的客户端（广播）
     * @param message 消息内容
     * @param is_text true 表示文本消息，false 表示二进制消息
     * @note 需要在派生类中实现广播逻辑
     */
    virtual net::awaitable<void> broadcast(std::string_view message, bool is_text = true);

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
    net::awaitable<void> close(ws::close_code code = ws::close_code::normal,
                               std::string_view reason = "");

protected:
    void* m_ws_context{nullptr};  // WebSocketContext 指针
};

#define WS_HANDLE_IMP(cls) \
public:                    \
    explicit cls(void* ws_context) : WebSocketHandle(ws_context) {}

}  // namespace hku
