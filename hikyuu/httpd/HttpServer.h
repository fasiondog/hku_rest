/*
 *  Copyright(C) 2021 hikyuu.org
 *
 *  Create on: 2021-02-28
 *     Author: fasiondog
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

/**
 * HTTP 连接处理器 - 管理 TCP 连接（使用协程）
 */
class Connection : public std::enable_shared_from_this<Connection> {
public:
    static std::shared_ptr<Connection> create(tcp::socket&& socket, Router* router,
                                              net::io_context& io_ctx);
    ~Connection();

    void start();

private:
    Connection(tcp::socket&& socket, Router* router, net::io_context& io_ctx);

    // TCP 连接读取循环
    net::awaitable<void> readLoop(std::shared_ptr<Connection> self);

    // 处理请求（协程方式调用 Handle）
    net::awaitable<void> processHandle(std::shared_ptr<BeastContext> context);

    // 写入响应
    net::awaitable<void> writeResponse(std::shared_ptr<BeastContext> context);

    // 关闭连接
    void close();

    tcp::socket m_socket;
    Router* m_router;
    net::io_context& m_io_ctx;
    std::string m_client_ip;
    uint16_t m_client_port = 0;
};

/**
 * SSL 上下文配置
 */
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
 * SSL 连接处理器 - 管理 HTTPS TCP 连接
 */
class SslConnection : public std::enable_shared_from_this<SslConnection> {
public:
    static std::shared_ptr<SslConnection> create(tcp::socket&& socket, Router* router,
                                                 ssl::context& ssl_ctx, net::io_context& io_ctx);
    ~SslConnection();

    void start();

private:
    SslConnection(tcp::socket&& socket, Router* router, ssl::context& ssl_ctx,
                  net::io_context& io_ctx);

    // SSL 握手并进入会话循环
    net::awaitable<void> readLoop(std::shared_ptr<SslConnection> self);

    // 处理请求（协程方式调用 Handle）
    net::awaitable<void> processHandle(std::shared_ptr<BeastContext> context);

    // 写入响应（通过 SSL 流）
    net::awaitable<void> writeResponse(std::shared_ptr<BeastContext> context);

    // 关闭连接
    void close();

    tcp::socket m_socket;
    Router* m_router;
    ssl::stream<tcp::socket&> m_ssl_stream;
    net::io_context& m_io_ctx;
    std::string m_client_ip;
    uint16_t m_client_port = 0;
};

/**
 * HTTP 服务器 - 支持协程和 TLS/SSL
 */
class HKU_HTTPD_API HttpServer {
    CLASS_LOGGER_IMP(HttpServer)

public:
    HttpServer(const char* host, uint16_t port);
    virtual ~HttpServer();

    void start();

    /**
     * 设置 IO 工作线程数（可选）
     * @param thread_count 线程数量，0 表示使用硬件并发数（默认值）
     */
    void set_io_thread_count(size_t thread_count);

    static void loop();
    static void stop();
    static void http_exit();
    static void signal_handler(int signal);

    /**
     * 设置 handle 无法捕获的错误返回信息，如 404
     * @param http_status http 状态码
     * @param body 返回消息
     */
    static void set_error_msg(int16_t http_status, const std::string& body);

    /**
     * 设置 tls 配置，启动前设置
     * @param ca_key_file ca 文件路径 (同时包含 PEM 格式的 cert 和 key 的文件)
     * @param password ca 文件密码，无密码时指定空指针
     * @param mode 0 无需客户端认证 | 1 客户端认证可选 | 2 需客户端认证
     */
    static void set_tls(const char* ca_key_file, const char* password, int mode = 0);

    template <typename Handle>
    void GET(const char* path) {
        regHandle("GET", path, [](void* ctx) -> net::awaitable<void> {
            Handle x(ctx);
            co_await x();
        });
    }

    template <typename Handle>
    void POST(const char* path) {
        regHandle("POST", path, [](void* ctx) -> net::awaitable<void> {
            Handle x(ctx);
            co_await x();
        });
    }

    template <typename Handle>
    void PUT(const char* path) {
        regHandle("PUT", path, [](void* ctx) -> net::awaitable<void> {
            Handle x(ctx);
            co_await x();
        });
    }

    template <typename Handle>
    void DEL(const char* path) {
        regHandle("DELETE", path, [](void* ctx) -> net::awaitable<void> {
            Handle x(ctx);
            co_await x();
        });
    }

    template <typename Handle>
    void PATCH(const char* path) {
        regHandle("PATCH", path, [](void* ctx) -> net::awaitable<void> {
            Handle x(ctx);
            co_await x();
        });
    }

    template <typename Handle>
    void regHandle(const char* method, const char* path) {
        regHandle(method, path, [](void* ctx) -> net::awaitable<void> {
            Handle x(ctx);
            co_await x();
        });
    }

    net::io_context* get_io_context() const {
        return ms_io_context;
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
