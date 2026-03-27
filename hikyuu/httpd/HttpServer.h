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
#include "WebSocketHandle.h"
#include "HttpWebSocketConfig.h"
#include "ConnectionManager.h"
#include "WebSocketConnectionManager.h"

#ifndef HKU_HTTPD_API
#define HKU_HTTPD_API
#endif

namespace hku {
// 前向声明
class ConnectionPermit;
class ConnectionManager;
}  // namespace hku

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
 * WebSocket 专用路由器 - 负责根据路径创建 Handle 实例
 */
class WebSocketRouter {
public:
    using HandleFactory = std::function<std::shared_ptr<WebSocketHandle>(void*)>;

    void registerHandler(const std::string& path, HandleFactory factory);
    HandleFactory findHandler(const std::string& path);

private:
    std::vector<std::pair<std::string, HandleFactory>> m_routes;
};

// WebSocket 连接处理器 - 管理 WebSocket 连接的生命周期
class WebSocketConnection : public std::enable_shared_from_this<WebSocketConnection> {
public:
    static std::shared_ptr<WebSocketConnection> create(
      tcp::socket&& socket, WebSocketRouter* ws_router, net::io_context& io_ctx,
      ssl::context* ssl_ctx = nullptr,
      const http::request<http::string_body>* existing_req = nullptr);  // 已有的 HTTP 请求（可选）
    ~WebSocketConnection();

    void start();

private:
    WebSocketConnection(tcp::socket&& socket, WebSocketRouter* ws_router, net::io_context& io_ctx,
                        ssl::context* ssl_ctx,
                        const http::request<http::string_body>* existing_req);

    // SSL 握手（仅当 m_ssl_stream != nullptr 时调用）
    net::awaitable<bool> sslHandshake();

    // WebSocket 握手
    net::awaitable<bool> websocketHandshake(const http::request<http::string_body>& req);

    // WebSocket 消息读取循环
    net::awaitable<void> readLoop(std::shared_ptr<WebSocketConnection> self);

    // 发送消息
    net::awaitable<bool> send(std::string_view message, bool is_text);

    // 关闭连接
    net::awaitable<void> closeWebSocket(ws::close_code code, std::string_view reason);

    // 配置 WebSocket 安全选项
    static void configureWebSocketSecurity(websocket::stream<tcp::socket&>& ws);

    // 发送心跳 Ping
    net::awaitable<void> sendPing();

    // WebSocket 消息验证
    bool validateWebSocketMessage(const std::string& message, bool is_text);

    // 处理 WebSocket 消息（调用 Handle）
    net::awaitable<void> handleWebSocketMessage(std::shared_ptr<WebSocketHandle> ws_handle,
                                                std::string_view message, bool is_text);

    // 关闭连接
    void close();

    tcp::socket m_socket;
    WebSocketRouter* m_ws_router;
    std::unique_ptr<ssl::stream<tcp::socket&>> m_ssl_stream;
    std::unique_ptr<websocket::stream<tcp::socket&>> m_ws_stream;
    net::io_context& m_io_ctx;
    std::string m_client_ip;
    uint16_t m_client_port = 0;

    std::shared_ptr<WebSocketContext> m_ws_ctx;  // WebSocket 上下文
    std::chrono::steady_clock::time_point m_connection_start;
    http::request<http::string_body> m_existing_req;  // 已存在的 HTTP 请求（如果有）

    // ========== 为每个连接保存独立的 Handle 实例 ==========
    std::shared_ptr<WebSocketHandle> m_handle;  // 该连接对应的 Handle

    // ========== 心跳协程停止标志 ==========
    std::atomic<bool> m_ping_stopped{false};  // 标记心跳协程是否应该停止

    // ========== WebSocket 连接许可（如使用 ConnectionManager） ==========
    WebSocketPermit m_ws_permit;  // WebSocket 连接许可，析构时自动释放

    // ========== 写队列管理 ==========
    std::atomic<std::size_t> m_write_queue_size{0};  // 当前待发送消息数
};

// HTTP 连接处理器 - 管理 HTTP/HTTPS TCP 连接
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

    // ========== P99 延迟优化：复用 BeastContext 对象 ==========
    // 在 Connection 生命周期内复用 session，避免频繁内存分配
    std::shared_ptr<BeastContext> m_session;  // 复用的会话上下文

    // ========== 智能连接管理：RAII 许可令牌 ==========
    ConnectionPermit m_permit;  // 连接许可，析构时自动释放
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
 * @brief CORS (跨域资源共享) 配置结构
 */
struct CorsConfig {
    bool enabled{false};                                           // 是否启用 CORS
    std::string allow_origin{"*"};                                 // 允许的源，默认允许所有
    std::string allow_methods{"GET, POST, PUT, DELETE, OPTIONS"};  // 允许的方法
    std::string allow_headers{"Content-Type, Authorization, X-Requested-With"};  // 允许的头
    std::string expose_headers{""};                                              // 暴露给客户端的头
    std::string max_age{"86400"};   // 预检请求缓存时间 (秒)
    bool allow_credentials{false};  // 是否允许携带凭证

    CorsConfig() = default;

    /**
     * @brief 快速配置允许所有源 (开发环境使用)
     * @param methods 允许的方法列表
     * @param headers 允许的头列表
     * @return CorsConfig 引用
     */
    static CorsConfig allowAll(
      const std::string& methods = "GET, POST, PUT, DELETE, OPTIONS",
      const std::string& headers = "Content-Type, Authorization, X-Requested-With") {
        CorsConfig config;
        config.enabled = true;
        config.allow_origin = "*";
        config.allow_methods = methods;
        config.allow_headers = headers;
        config.max_age = "86400";
        return config;
    }

    /**
     * @brief 配置指定源的 CORS (生产环境推荐)
     * @param origin 允许的源 (如 https://example.com)
     * @param methods 允许的方法列表
     * @param headers 允许的头列表
     * @param credentials 是否允许凭证
     * @return CorsConfig 引用
     */
    static CorsConfig allowOrigin(const std::string& origin,
                                  const std::string& methods = "GET, POST, OPTIONS",
                                  const std::string& headers = "Content-Type",
                                  bool credentials = false) {
        CorsConfig config;
        config.enabled = true;
        config.allow_origin = origin;
        config.allow_methods = methods;
        config.allow_headers = headers;
        config.allow_credentials = credentials;
        if (credentials) {
            config.expose_headers = "Authorization, Content-Length, X-Requested-With";
        }
        config.max_age = "86400";
        return config;
    }
};

/**
 * HTTP 服务器 - 支持协程和 TLS/SSL
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
    using WsHandleFactory = std::function<std::shared_ptr<WebSocketHandle>(void*)>;

    HttpServer(const char* host, uint16_t port);
    virtual ~HttpServer();

    void start();

    /**
     * @brief 设置 IO 工作线程数（可选）
     * @param thread_count 线程数量，0 表示使用硬件并发数（默认值）
     */
    void set_io_thread_count(size_t thread_count);

    /**
     * @brief 绑定外部指定的 io_context（可选）
     *
     * 调用此方法后，HttpServer 将使用外部提供的 io_context 而非自行创建
     * 必须在 start() 之前调用，且调用后 set_io_thread_count() 将失效
     *
     * @param io_ctx 外部 io_context 引用
     */
    void bind_io_context(net::io_context& io_ctx);

    /**
     * @brief 获取当前服务器使用的 io_context
     *
     * @return net::io_context* io_context 指针，如果尚未初始化则返回 nullptr
     */
    static net::io_context* get_io_context();

    static void loop();
    static void stop();
    static void http_exit();
    static void signal_handler(int signal);

    /**
     * @brief 配置 CORS (跨域资源共享)
     * @param config CORS 配置对象
     *
     * 示例:
     *   // 允许所有源 (开发环境)
     *   server->setCors(CorsConfig::allowAll());
     *
     *   // 允许指定源 (生产环境)
     *   server->setCors(CorsConfig::allowOrigin("https://example.com"));
     */
    void setCors(const CorsConfig& config);

    /**
     * @brief 获取当前服务器实例的 CORS 配置
     * @return CorsConfig 指针，如果未启用则返回 nullptr
     */
    static CorsConfig* getCorsConfig() {
        return ms_server ? &ms_server->m_cors_config : nullptr;
    }

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
     * @brief 启用或禁用 WebSocket 支持（默认禁用）
     * @param enable true-启用 WebSocket，false-禁用 WebSocket
     *
     * 示例:
     *   // 启用 WebSocket
     *   server->enableWebSocket(true);
     *
     *   // 禁用 WebSocket
     *   server->enableWebSocket(false);
     */
    void enableWebSocket(bool enable = true) {
        ms_websocket_enabled = enable;
    }

    /**
     * @brief 检查是否启用了 WebSocket
     * @return true-已启用，false-未启用
     */
    bool isWebSocketEnabled() const {
        return ms_websocket_enabled;
    }

    /**
     * @brief 配置 SSL/TLS(同时作用于 HTTP 和 WebSocket)
     * @param ca_key_file CA 证书和私钥文件 (PEM 格式)
     * @param password 私钥密码 (可为空)
     * @param mode 客户端验证模式：0-无需认证 | 1-可选认证 | 2-必须认证
     */
    void setTls(const char* ca_key_file, const char* password = "", int mode = 0);

    // 全局连接池管理接口（public）
    /**
     * @brief 获取连接管理器实例
     * @return ConnectionManager* 连接管理器指针
     */
    static ConnectionManager* get_connection_manager() {
        return ms_connection_manager.get();
    }

    /**
     * @brief 获取 WebSocket 连接管理器实例
     * @return WebSocketConnectionManager* WebSocket 连接管理器指针
     */
    static WebSocketConnectionManager* get_websocket_connection_manager() {
        return ms_ws_connection_manager.get();
    }

    /**
     * @brief 设置最大并发连接数
     * @param max_concurrent 最大并发数
     * @param wait_timeout_ms 等待超时时间（毫秒），0 表示无限等待
     */
    void set_max_concurrent_connections(size_t max_concurrent, size_t wait_timeout_ms = 30000) {
        ms_connection_manager =
          std::make_shared<ConnectionManager>(max_concurrent, wait_timeout_ms);
    }

    /**
     * @brief 设置 WebSocket 最大并发连接数
     * @param max_concurrent 最大并发数
     * @param wait_timeout_ms 等待超时时间（毫秒），0 表示无限等待
     */
    void set_max_concurrent_websocket_connections(size_t max_concurrent,
                                                  size_t wait_timeout_ms = 30000) {
        ms_ws_connection_manager =
          std::make_shared<WebSocketConnectionManager>(max_concurrent, wait_timeout_ms);
    }

    // 全局连接池管理字段（public static）
public:
    // 智能连接管理器（替代简单的计数限流）
    static std::shared_ptr<ConnectionManager> ms_connection_manager;  // 连接管理器

    // WebSocket 连接管理器
    static std::shared_ptr<WebSocketConnectionManager>
      ms_ws_connection_manager;  // WebSocket 连接管理器

    // WebSocket 和 SSL 相关的静态成员（需要被 WebSocketConnection 访问）
    static WebSocketRouter ms_ws_router;  // WebSocket 路由器
    static ssl::context* ms_ssl_context;  // SSL 上下文

    // WebSocket 功能开关（全局控制）
    static bool ms_websocket_enabled;  // WebSocket 功能是否已启用（默认 false）

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
        registerWsHandle(path, [](void* ctx) -> std::shared_ptr<WebSocketHandle> {
            return std::make_shared<Handle>(ctx);
        });
    }

private:
    using HandlerFunc = std::function<net::awaitable<void>(void*)>;
    void configureSsl();
    net::awaitable<void> doAccept();
    net::awaitable<void> doAcceptSsl();

private:
    std::string m_root_url;
    std::string m_host;
    uint16_t m_port{80};
    static size_t ms_io_thread_count;  // 改为静态成员变量
    CorsConfig m_cors_config;          // CORS 配置
    static bool ms_use_external_io;    // 是否使用外部 io_context（静态）

    // 静态成员变量在 HttpServer.cpp 中定义
    static HttpServer* ms_server;
    static Router ms_router;
    static net::io_context* ms_io_context;
    static tcp::acceptor* ms_acceptor;
    static std::atomic<bool> ms_running;
    static SslConfig ms_ssl_config;
};

#define HTTP_HANDLE_IMP(cls) \
public:                      \
    explicit cls(void* beast_context) : HttpHandle(beast_context) {}

}  // namespace hku
