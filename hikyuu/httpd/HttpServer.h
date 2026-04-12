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

#include "Router.h"
#include "HttpConfig.h"
#include "ConnectionManager.h"
#include "WebSocketConnectionManager.h"
#include "RateLimit.h"

#ifndef HKU_HTTPD_API
#define HKU_HTTPD_API
#endif

namespace hku {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

class HKU_HTTPD_API HttpServer;

// WebSocket 连接处理器 - 管理 WebSocket 连接的生命周期
class WebSocketConnection : public std::enable_shared_from_this<WebSocketConnection> {
public:
    static std::shared_ptr<WebSocketConnection> create(
      HttpServer* server, tcp::socket&& socket, WebSocketRouter* ws_router, net::io_context& io_ctx,
      ssl::context* ssl_ctx = nullptr,
      const http::request<http::string_body>* existing_req = nullptr);  // 已有的 HTTP 请求（可选）
    ~WebSocketConnection();

    void start();

private:
    WebSocketConnection(HttpServer* server, tcp::socket&& socket, WebSocketRouter* ws_router,
                        net::io_context& io_ctx, ssl::context* ssl_ctx,
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

private:
    HttpServer* m_server{nullptr};
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
                                              net::io_context& io_ctx, ssl::context* ssl_ctx,
                                              HttpServer* server);
    ~Connection();

    void start();

private:
    Connection(tcp::socket&& socket, Router* router, net::io_context& io_ctx, ssl::context* ssl_ctx,
               HttpServer* m_server);

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

private:
    tcp::socket m_socket;
    Router* m_router;
    HttpServer* m_server;

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
 * @brief IP 子网配置结构
 */
struct SubnetConfig {
    std::string network_address;  // 网络地址，如 "192.168.1.0"
    std::string subnet_mask;      // 子网掩码，如 "255.255.255.0"
    std::string cidr_notation;    // CIDR表示法，如 "192.168.1.0/24"

    SubnetConfig() = default;

    SubnetConfig(const std::string& network, const std::string& mask)
    : network_address(network), subnet_mask(mask), cidr_notation("") {}

    SubnetConfig(const std::string& cidr)
    : network_address(""), subnet_mask(""), cidr_notation(cidr) {}

    /**
     * @brief 检查IP是否在子网内
     * @param ip 要检查的IP地址
     * @return 如果在子网内返回true
     */
    bool isIpInSubnet(const std::string& ip) const;
};

/**
 * @brief IP访问控制列表配置
 */
struct AccessControlConfig {
    bool enabled{false};                        // 是否启用访问控制
    std::vector<SubnetConfig> allowed_subnets;  // 允许的子网列表
    std::vector<std::string> allowed_ips;       // 允许的单个IP列表
    bool default_allow{false};                  // 默认是否允许（当列表为空时）
    bool strict_mode{true};  // 严格模式：true=只允许列表中的IP，false=拒绝列表中的IP

    AccessControlConfig() = default;

    /**
     * @brief 快速配置允许所有IP（默认）
     * @return AccessControlConfig 引用
     */
    static AccessControlConfig allowAll() {
        AccessControlConfig config;
        config.enabled = false;
        config.default_allow = true;
        return config;
    }

    /**
     * @brief 配置仅允许特定子网
     * @param subnets 允许的子网列表
     * @return AccessControlConfig 引用
     */
    static AccessControlConfig allowSubnets(const std::vector<std::string>& subnets) {
        AccessControlConfig config;
        config.enabled = true;
        config.default_allow = false;
        config.strict_mode = true;
        for (const auto& subnet : subnets) {
            config.allowed_subnets.emplace_back(subnet);
        }
        return config;
    }

    /**
     * @brief 配置仅允许特定IP
     * @param ips 允许的IP列表
     * @return AccessControlConfig 引用
     */
    static AccessControlConfig allowIPs(const std::vector<std::string>& ips) {
        AccessControlConfig config;
        config.enabled = true;
        config.default_allow = false;
        config.strict_mode = true;
        config.allowed_ips = ips;
        return config;
    }

    /**
     * @brief 检查IP是否被允许访问
     * @param ip 要检查的IP地址
     * @return 如果允许访问返回true
     */
    bool isIpAllowed(const std::string& ip) const;
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
    friend class Connection;
    friend class WebSocketConnection;

public:
    using HttpHandleFactory = std::function<net::awaitable<void>(void*)>;
    using WsHandleFactory = std::function<std::shared_ptr<WebSocketHandle>(void*)>;

    HttpServer(const char* host, uint16_t port);
    virtual ~HttpServer();

    void start();
    void loop();
    void stop();

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
    net::io_context* get_io_context();

    /**
     * @brief 获取服务器监听的端口
     * @return 服务器端口号
     */
    uint16_t getPort() const noexcept {
        return m_port;
    }

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
    const CorsConfig& getCorsConfig() const noexcept {
        return m_cors_config;
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
        m_websocket_enabled = enable;
    }

    /**
     * @brief 检查是否启用了 WebSocket
     * @return true-已启用，false-未启用
     */
    bool isWebSocketEnabled() const {
        return m_websocket_enabled;
    }

    /**
     * @brief 启用或禁用快速路径（P99 延迟优化）
     * @param enable true-启用快速路径，false-禁用快速路径
     *
     * 快速路径功能会跳过一些不必要的安全检查，以提高简单GET请求的处理性能。
     * 仅对以下请求启用：
     * 1. GET方法
     * 2. URL长度小于256字符
     * 3. 无请求体
     *
     * 示例:
     *   // 启用快速路径（默认）
     *   server->enableFastPath(true);
     *
     *   // 禁用快速路径（所有请求都进行完整安全检查）
     *   server->enableFastPath(false);
     */
    void enableFastPath(bool enable = true) {
        m_enable_fast_path = enable;
    }

    /**
     * @brief 检查是否启用了快速路径
     * @return true-已启用，false-未启用
     */
    bool isFastPathEnabled() const {
        return m_enable_fast_path;
    }

    /**
     * @brief 启用或禁用探测连接快速关闭（解决 cpolar 探测导致 5s 超时问题）
     * @param enable true-启用探测关闭，false-禁用（默认）
     *
     * cpolar 等反向代理会定期发送 TCP 探测连接（只建立连接不发送 HTTP 请求），
     * 启用此选项后，服务器会在接受连接后立即检测 socket 是否有可用数据，
     * 如果没有数据则立即关闭连接，避免空等超时。
     *
     * 示例:
     *   // 启用探测连接快速关闭（用于反向代理场景）
     *   server->enableProbeConnectionClose(true);
     */
    void enableProbeConnectionClose(bool enable = true) {
        m_probe_close_enabled = enable;
    }

    /**
     * @brief 检查是否启用了探测连接快速关闭
     * @return true-已启用，false-未启用
     */
    bool isProbeConnectionCloseEnabled() const {
        return m_probe_close_enabled;
    }

    /**
     * @brief 配置 SSL/TLS(同时作用于 HTTP 和 WebSocket)
     * @param ca_key_file CA 证书和私钥文件 (PEM 格式)
     * @param password 私钥密码 (可为空)
     * @param mode 客户端验证模式：0-无需认证 | 1-可选认证 | 2-必须认证
     */
    void setTls(const char* ca_key_file, const char* password = "", int mode = 0);

    /**
     * @brief 设置IP访问控制配置
     * @param config 访问控制配置
     */
    void setAccessControl(const AccessControlConfig& config);

    /**
     * @brief 快速设置：允许所有IP访问（默认）
     */
    void allowAllIPs();

    /**
     * @brief 快速设置：仅允许指定子网的IP访问
     * @param subnets CIDR表示法的子网列表，如 {"192.168.1.0/24", "10.0.0.0/8"}
     */
    void allowSubnets(const std::vector<std::string>& subnets);

    /**
     * @brief 快速设置：仅允许指定单个IP访问
     * @param ips IP地址列表，如 {"192.168.1.100", "10.0.0.1"}
     */
    void allowIPs(const std::vector<std::string>& ips);

    /**
     * @brief 快速设置：拒绝指定子网的IP访问（黑名单模式）
     * @param subnets CIDR表示法的子网列表
     */
    void denySubnets(const std::vector<std::string>& subnets);

    /**
     * @brief 快速设置：拒绝指定单个IP访问（黑名单模式）
     * @param ips IP地址列表
     */
    void denyIPs(const std::vector<std::string>& ips);

    /**
     * @brief 检查IP是否被允许访问
     * @param ip IP地址
     * @return 如果允许访问返回true
     */
    bool isIpAllowed(const std::string& ip) const;

    /**
     * @brief 配置速率限制
     * @param config 速率限制配置对象
     */
    void setRateLimit(const RateLimitConfig& config);

    /**
     * @brief 快速配置：启用全局速率限制
     * @param rps 每秒请求数（默认10）
     * @param burst 突发流量大小（默认20）
     */
    void enableGlobalRateLimit(uint32_t rps = 10, uint32_t burst = 20);

    /**
     * @brief 快速配置：启用每IP速率限制
     * @param rps 每秒请求数（默认5）
     * @param burst 突发流量大小（默认10）
     */
    void enablePerIpRateLimit(uint32_t rps = 5, uint32_t burst = 10);

    /**
     * @brief 禁用速率限制
     */
    void disableRateLimit();

    /**
     * @brief 检查速率限制是否已启用
     * @return true-已启用，false-未启用
     */
    bool isRateLimitEnabled() const noexcept {
        return m_rate_limiter.isEnabled();
    }

    /**
     * @brief 添加IP到速率限制白名单
     * @param ip IP地址
     */
    void addRateLimitIpWhitelist(const std::string& ip);

    /**
     * @brief 添加IP列表到速率限制白名单
     * @param ips IP地址列表
     */
    void addRateLimitIpWhitelist(const std::vector<std::string>& ips);

    /**
     * @brief 添加端点到速率限制白名单
     * @param endpoint 端点路径（支持通配符*结尾，如 "/api/&#42;"）
     */
    void addRateLimitEndpointWhitelist(const std::string& endpoint);

    /**
     * @brief 检查请求是否受速率限制影响
     * @param client_ip 客户端IP地址
     * @param endpoint 请求端点
     * @param method HTTP方法
     * @return 如果允许请求返回true
     */
    bool checkRateLimit(const std::string& client_ip, const std::string& endpoint,
                        const std::string& method);

    /**
     * @brief 获取速率限制统计信息
     * @return RateLimitStats 统计信息
     */
    RateLimitStats getRateLimitStats() const;

    // 全局连接池管理接口（public）
    /**
     * @brief 获取连接管理器实例
     * @return ConnectionManager* 连接管理器指针
     */
    std::shared_ptr<ConnectionManager> get_connection_manager() const noexcept {
        return m_connection_manager;
    }

    /**
     * @brief 获取 WebSocket 连接管理器实例
     * @return WebSocketConnectionManager* WebSocket 连接管理器指针
     */
    std::shared_ptr<WebSocketConnectionManager> get_websocket_connection_manager() const noexcept {
        return m_ws_connection_manager;
    }

    /**
     * @brief 设置最大并发连接数
     * @param max_concurrent 最大并发数
     * @param wait_timeout_ms 等待超时时间（毫秒），0 表示无限等待
     */
    void set_max_concurrent_connections(size_t max_concurrent, size_t wait_timeout_ms = 5000) {
        m_max_concurrent_connections = max_concurrent;
        m_wait_timeout_ms = wait_timeout_ms;
    }

    /**
     * @brief 设置 WebSocket 最大并发连接数
     * @param max_concurrent 最大并发数
     * @param wait_timeout_ms 等待超时时间（毫秒），0 表示无限等待
     */
    void set_max_concurrent_websocket_connections(size_t max_concurrent,
                                                  size_t wait_timeout_ms = 5000) {
        m_ws_max_concurrent_connections = max_concurrent;
        m_ws_wait_timeout_ms = wait_timeout_ms;
    }

public:
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

public:
    // 全局连接池管理字段（public static）

private:
    std::string m_root_url;
    std::string m_host;
    uint16_t m_port{80};
    CorsConfig m_cors_config;              // CORS 配置
    AccessControlConfig m_access_control;  // IP访问控制配置
    RateLimiter m_rate_limiter;            // 速率限制器
    Router m_router;
    WebSocketRouter m_ws_router;  // WebSocket 路由器

    SslConfig m_ssl_config;
    std::unique_ptr<ssl::context> m_ssl_context;  // SSL 上下文

    std::unique_ptr<tcp::acceptor> m_acceptor;  // TCP 接收器

    size_t m_io_thread_count{0};
    net::io_context* m_io_context{nullptr};
    bool m_use_external_io{false};    // 是否使用外部 io_context
    bool m_websocket_enabled{false};  // WebSocket 功能是否已启用（默认 false）
    bool m_enable_fast_path{true};                           // 是否启用快速路径（P99 延迟优化）
    bool m_probe_close_enabled{false};  // 是否启用探测连接快速关闭（识别 cpolar 探测）
    std::atomic<bool> m_running{false};

    std::shared_ptr<ConnectionManager> m_connection_manager;              // 连接管理器
    std::shared_ptr<WebSocketConnectionManager> m_ws_connection_manager;  // WebSocket 连接管理器

    size_t m_max_concurrent_connections{128};  // 默认最大并发连接数
    size_t m_wait_timeout_ms{5000};            // 默认等待超时时间（毫秒）

    size_t m_ws_max_concurrent_connections{128};  // 默认 WebSocket 最大并发连接数
    size_t m_ws_wait_timeout_ms{5000};            // 默认 WebSocket 等待超时时间（毫秒）
};

#define HTTP_HANDLE_IMP(cls) \
public:                      \
    explicit cls(void* beast_context) : HttpHandle(beast_context) {}

}  // namespace hku
