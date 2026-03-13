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
#include <coroutine>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/ssl.hpp>

#include "HttpHandle.h"

#ifndef HKU_HTTPD_API
#define HKU_HTTPD_API
#endif

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

namespace hku {

/**
 * HTTP 路由器 - 负责注册和分发请求到对应的 Handle
 */
class Router {
public:
    using HandlerFunc = std::function<net::awaitable<void>(void*)>;

    struct RouteKey {
        std::string method;
        std::string path;

        bool operator==(const RouteKey& other) const {
            return method == other.method && path == other.path;
        }
    };

    void registerHandler(const std::string& method, const std::string& path, HandlerFunc handler);
    HandlerFunc findHandler(const std::string& method, const std::string& path);

private:
    // 使用 vector 存储路由表，避免 map 的哈希开销和动态分配
    // 路由数量有限（通常 < 100），线性搜索性能足够且缓存友好
    std::vector<std::pair<RouteKey, HandlerFunc>> m_routes;
};

// 连接处理器 - 管理 HTTP/HTTPS TCP 连接
// 支持 SSL/TLS 和非 SSL 两种模式，通过 m_ssl_stream 是否为 nullptr 区分
class Connection : public std::enable_shared_from_this<Connection> {
public:
    static std::shared_ptr<Connection> create(tcp::socket&& socket, Router* router,
                                              net::io_context& io_ctx,
                                              ssl::context* ssl_ctx = nullptr);
    ~Connection();

    void start();

private:
    Connection(tcp::socket&& socket, Router* router, net::io_context& io_ctx,
               ssl::context* ssl_ctx);

    // 读取循环（根据 m_ssl_stream 是否为 nullptr 选择不同路径）
    net::awaitable<void> readLoop(std::shared_ptr<Connection> self);

    // SSL 握手（仅当 m_ssl_stream != nullptr 时调用）
    net::awaitable<bool> sslHandshake();

    // 处理请求（协程方式调用 Handle）
    net::awaitable<void> processHandle(std::shared_ptr<BeastContext> context);

    // 写入响应（根据 m_ssl_stream 是否为 nullptr 选择不同方式）
    net::awaitable<void> writeResponse(std::shared_ptr<BeastContext> context);

    // 关闭连接
    void close();

    tcp::socket m_socket;
    Router* m_router;

    // SSL 流（仅在 SSL 模式下初始化，通过是否为 nullptr 判断连接类型）
    std::unique_ptr<ssl::stream<tcp::socket&>> m_ssl_stream;

    net::io_context& m_io_ctx;
    std::string m_client_ip;
    uint16_t m_client_port = 0;

    // Keep-Alive 连接安全限制
    int m_request_count = 0;                                   // 当前连接已处理请求数
    std::chrono::steady_clock::time_point m_connection_start;  // 连接建立时间
};

// SSL 上下文配置
struct SslConfig {
    std::string ca_key_file;  // CA 证书和私钥文件（PEM 格式）
    std::string password;     // 私钥密码（可为空）
    int verify_mode = 0;      // 0: 无需客户端认证 | 1: 客户端认证可选 | 2: 需客户端认证
    bool enabled = false;     // 是否启用 SSL

    SslConfig() = default;

    SslConfig(const std::string& ca_file, const std::string& pwd = "", int mode = 0)
    : ca_key_file(ca_file), password(pwd), verify_mode(mode), enabled(true) {}
};

/**
 * HTTP服务器 - 支持协程和 TLS/SSL
 *
 * 增强版本：同时支持 HTTP/HTTPS 和 WebSocket 协议
 * - 自动检测请求类型并路由到对应处理器
 * - 共享 IO 上下文、SSL 配置和线程池
 * - 支持同一端口同时提供 HTTP 和 WebSocket 服务
 */
class HKU_HTTPD_API HttpServer {
    CLASS_LOGGER_IMP(HttpServer)

public:
    using HttpHandleFactory = std::function<net::awaitable<void>(void*)>;
    using WsHandleFactory = std::function<net::awaitable<void>(void*)>;

    // HTTP/WebSocket请求安全限制
    static constexpr std::size_t MAX_BUFFER_SIZE = 1024 * 1024;     // 1MB
    static constexpr std::size_t MAX_BODY_SIZE = 10 * 1024 * 1024;  // 10MB
    static constexpr std::size_t MAX_HEADER_SIZE = 8192;            // 8KB
    static constexpr int MAX_KEEPALIVE_REQUESTS = 10000;
    static constexpr int MAX_CONNECTIONS = 1000;

    HttpServer(const char* host, uint16_t port);
    virtual ~HttpServer();

    void start();

    /**
     * @brief 设置 IO 工作线程数（可选）
     * @param thread_count 线程数量，0 表示使用硬件并发数（默认值）
     */
    void set_io_thread_count(size_t thread_count);

    static void loop();
    static void stop();
    static void http_exit();
    static void signal_handler(int signal);

    /**
     * @brief 设置 handle 无法捕获的错误返回信息，如 404
     * @param http_status http 状态码
     * @param body 返回消息
     */
    static void set_error_msg(int16_t http_status, const std::string& body);

    /**
     * @brief 注册 HTTP Handle
     * @param method HTTP 方法 (GET/POST 等)
     * @param path 请求路径
     * @param handler Handle 工厂函数
     */
    void registerHttpHandle(const std::string& method, const std::string& path,
                            HttpHandleFactory handler);

    /**
     * @brief 注册 HTTP Handle (const char* 重载)
     */
    void registerHttpHandle(const char* method, const char* path, HttpHandleFactory handler);

    /**
     * @brief 注册 WebSocket Handle
     * @param path WebSocket 路径
     * @param handler WebSocketHandle 工厂函数
     */
    void registerWsHandle(const std::string& path, WsHandleFactory handler);

    /**
     * @brief 注册 WebSocket Handle (const char* 重载)
     */
    void registerWsHandle(const char* path, WsHandleFactory handler);

    /**
     * @brief 配置 SSL/TLS(同时作用于 HTTP 和 WebSocket)
     * @param ca_key_file CA 证书和私钥文件 (PEM 格式)
     * @param password 私钥密码 (可为空)
     * @param mode 客户端验证模式：0-无需认证 | 1-可选认证 | 2-必须认证
     */
    void setTls(const char* ca_key_file, const char* password = "", int mode = 0);

    // 全局连接池管理接口（public）
    static int get_active_connections() {
        return ms_active_connections.load(std::memory_order_relaxed);
    }
    static constexpr int get_max_connections() {
        return MAX_CONNECTIONS;
    }

    // 全局连接池管理字段（public static）
public:
    static std::atomic<int> ms_active_connections;  // 当前活跃连接数

    // HTTP 方法快捷注册 (模板方式)
    template <typename Handle>
    void GET(const char* path) {
        registerHttpHandle("GET", path, [](void* ctx) -> net::awaitable<void> {
            Handle x(ctx);
            co_await x();
        });
    }

    template <typename Handle>
    void POST(const char* path) {
        registerHttpHandle("POST", path, [](void* ctx) -> net::awaitable<void> {
            Handle x(ctx);
            co_await x();
        });
    }

    template <typename Handle>
    void PUT(const char* path) {
        registerHttpHandle("PUT", path, [](void* ctx) -> net::awaitable<void> {
            Handle x(ctx);
            co_await x();
        });
    }

    template <typename Handle>
    void DEL(const char* path) {
        registerHttpHandle("DELETE", path, [](void* ctx) -> net::awaitable<void> {
            Handle x(ctx);
            co_await x();
        });
    }

    template <typename Handle>
    void PATCH(const char* path) {
        registerHttpHandle("PATCH", path, [](void* ctx) -> net::awaitable<void> {
            Handle x(ctx);
            co_await x();
        });
    }

    // WebSocket 快捷注册
    template <typename Handle>
    void WS(const char* path) {
        registerWsHandle(path, [](void* ctx) -> net::awaitable<void> {
            Handle x(ctx);
            co_await x();
        });
    }

private:
    using HandlerFunc = std::function<net::awaitable<void>(void*)>;
    void regHandle(const char* method, const char* path, HandlerFunc rest_handle);
    void configureSsl();
    net::awaitable<void> doAccept();
    net::awaitable<void> doAcceptSsl();

private:
    std::string m_root_url;
    std::string m_host;
    uint16_t m_port{80};
    static size_t ms_io_thread_count;  // 改为静态成员变量

    // 静态成员变量在 HttpServer.cpp 中定义
    static HttpServer* ms_server;
    static Router ms_router;
    static Router ms_ws_router;  // WebSocket 路由器
    static net::io_context* ms_io_context;
    static tcp::acceptor* ms_acceptor;
    static std::atomic<bool> ms_running;
    static SslConfig ms_ssl_config;
    static ssl::context* ms_ssl_context;
};

#define HTTP_HANDLE_IMP(cls) \
public:                      \
    explicit cls(void* beast_context) : HttpHandle(beast_context) {}

}  // namespace hku
