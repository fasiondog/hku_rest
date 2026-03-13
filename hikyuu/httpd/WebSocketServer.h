/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-03-13
 *      Author: fasiondog
 */

#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <coroutine>

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>

#include "WebSocketHandle.h"
#include "HttpServer.h"

#ifndef HKU_HTTPD_API
#define HKU_HTTPD_API
#endif

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

namespace hku {

/**
 * WebSocket 连接 - 管理单个 WebSocket 连接的生命周期
 */
class WebSocketConnection : public std::enable_shared_from_this<WebSocketConnection> {
public:
    static std::shared_ptr<WebSocketConnection> create(tcp::socket&& socket,
                                                       WebSocketHandle::HandlerFunc handler);

    ~WebSocketConnection();

    void start();

private:
    WebSocketConnection(tcp::socket&& socket, WebSocketHandle::HandlerFunc handler);

    // WebSocket 握手升级
    net::awaitable<void> doHandshake(std::shared_ptr<WebSocketConnection> self);

    // 读取循环
    net::awaitable<void> readLoop(std::shared_ptr<WebSocketConnection> self);

    // 写入循环
    net::awaitable<void> writeLoop(std::shared_ptr<WebSocketConnection> self);

    // 心跳检测
    net::awaitable<void> pingLoop(std::shared_ptr<WebSocketConnection> self);

    // 关闭连接
    void close();

    websocket::stream<tcp::socket> m_ws;
    WebSocketHandle::HandlerFunc m_handler;
    std::unique_ptr<WebSocketContext> m_context;
};

/**
 * SSL WebSocket 连接 - 支持 TLS 加密的 WebSocket 连接
 */
class WebSocketConnectionSSL : public std::enable_shared_from_this<WebSocketConnectionSSL> {
public:
    static std::shared_ptr<WebSocketConnectionSSL> create(tcp::socket&& socket,
                                                          WebSocketHandle::HandlerFunc handler,
                                                          ssl::context* ssl_ctx);

    ~WebSocketConnectionSSL();

    void start();

private:
    WebSocketConnectionSSL(tcp::socket&& socket, WebSocketHandle::HandlerFunc handler,
                           ssl::context* ssl_ctx);

    // SSL 握手
    net::awaitable<bool> sslHandshake();

    // WebSocket 握手升级
    net::awaitable<void> doHandshake(std::shared_ptr<WebSocketConnectionSSL> self);

    // 读取循环
    net::awaitable<void> readLoop(std::shared_ptr<WebSocketConnectionSSL> self);

    // 写入循环
    net::awaitable<void> writeLoop(std::shared_ptr<WebSocketConnectionSSL> self);

    // 心跳检测
    net::awaitable<void> pingLoop(std::shared_ptr<WebSocketConnectionSSL> self);

    // 关闭连接
    void close();

    tcp::socket m_socket;  // 存储 socket 用于 SSL stream 引用
    ssl::stream<tcp::socket&> m_ssl_stream;
    websocket::stream<ssl::stream<tcp::socket&>&> m_ws;
    WebSocketHandle::HandlerFunc m_handler;
    std::unique_ptr<WebSocketContext> m_context;
};

/**
 * WebSocket 路由器 - 负责注册和分发 WebSocket 连接到对应的 Handle
 */
class WebSocketRouter {
public:
    using HandlerFunc = std::function<net::awaitable<void>(void*)>;

    struct RouteKey {
        std::string path;

        bool operator==(const RouteKey& other) const {
            return path == other.path;
        }
    };

    void registerHandler(const std::string& path, HandlerFunc handler);
    HandlerFunc findHandler(const std::string& path);

private:
    // 使用 vector 存储路由表，避免 map 的哈希开销
    std::vector<std::pair<RouteKey, HandlerFunc>> m_routes;
};

/**
 * WebSocket 服务器 - 支持协程和 TLS/SSL
 */
class HKU_HTTPD_API WebSocketServer {
    CLASS_LOGGER_IMP(WebSocketServer)

public:
    WebSocketServer(const char* host, uint16_t port);
    virtual ~WebSocketServer();

    void start();

    /**
     * 设置 IO 工作线程数（可选）
     * @param thread_count 线程数量，0 表示使用硬件并发数（默认值）
     */
    void set_io_thread_count(size_t thread_count);

    /**
     * 运行 IO 循环（阻塞）
     */
    void loop();

    /**
     * 停止服务器
     */
    static void stop();

    /**
     * 设置 tls 配置，启动前设置
     * @param ca_key_file ca 文件路径 (同时包含 PEM 格式的 cert 和 key 的文件)
     * @param password ca 文件密码，无密码时指定空指针
     * @param mode 0 无需客户端认证 | 1 客户端认证可选 | 2 需客户端认证
     */
    static void set_tls(const char* ca_key_file, const char* password, int mode = 0);

    // 全局连接池管理接口（public）
    static int get_active_connections() {
        return ms_active_connections.load(std::memory_order_relaxed);
    }
    static constexpr int get_max_connections() {
        return MAX_CONNECTIONS;
    }

    // 全局连接池管理字段（public static）
    static std::atomic<int> ms_active_connections;  // 当前活跃连接数
    static constexpr int MAX_CONNECTIONS = 1000;    // 最大并发连接数限制

    template <typename Handle>
    void WS(const char* path) {
        regHandle(path, [](void* ctx) -> net::awaitable<void> {
            Handle x(ctx);
            co_await x();
        });
    }

    net::io_context* get_io_context() const {
        return ms_io_context;
    }

private:
    using HandlerFunc = std::function<net::awaitable<void>(void*)>;
    void regHandle(const char* path, HandlerFunc handler);
    void configureSsl();
    net::awaitable<void> doAccept();
    net::awaitable<void> doAcceptSsl();

private:
    std::string m_host;
    uint16_t m_port{80};
    static size_t ms_io_thread_count;

    static WebSocketServer* ms_server;
    static WebSocketRouter ms_router;
    static net::io_context* ms_io_context;
    static tcp::acceptor* ms_acceptor;
    static std::atomic<bool> ms_running;
    static SslConfig ms_ssl_config;
    static ssl::context* ms_ssl_context;
};

}  // namespace hku
