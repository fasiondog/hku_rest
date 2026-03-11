/*
 *  Copyright(C) 2021 hikyuu.org
 *
 *  Create on: 2021-02-28
 *     Author: fasiondog
 */

#include <csignal>
#include <thread>
#include <hikyuu/utilities/os.h>
#include "HttpServer.h"

#if defined(_WIN32)
#include <Windows.h>
#endif

// Boost.beast 相关头文件
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ssl.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

// SSL 配置结构
struct SslConfig {
    std::string ca_key_file;
    std::string password;
    int verify_mode{0};  // 0: none, 1: optional, 2: required
    bool enabled{false};
};


namespace hku {

// 静态成员初始化
HttpServer* HttpServer::ms_server = nullptr;
Router HttpServer::ms_router;
net::io_context* HttpServer::ms_io_context = nullptr;
tcp::acceptor* HttpServer::ms_acceptor = nullptr;
std::atomic<bool> HttpServer::ms_running{false};
SslConfig HttpServer::ms_ssl_config;
ssl::context* HttpServer::ms_ssl_context = nullptr;

#if defined(_WIN32)
static UINT g_old_cp;
#endif

// ============================================================================
// Router 实现
// ============================================================================

void Router::registerHandler(const std::string& method, const std::string& path, HandlerFunc handler) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RouteKey key{method, path};
    m_routes[key] = handler;
}

Router::HandlerFunc Router::findHandler(const std::string& method, const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RouteKey key{method, path};
    auto it = m_routes.find(key);
    if (it != m_routes.end()) {
        return it->second;
    }
    
    // 尝试查找通配符路由（简单实现，可以扩展为更复杂的路由匹配）
    for (const auto& [route_key, handler] : m_routes) {
        if (route_key.method == method) {
            // 支持简单的路径前缀匹配
            if (path.find(route_key.path) == 0) {
                return handler;
            }
        }
    }
    
    return nullptr;
}

// ============================================================================
// Connection 实现
// ============================================================================

std::shared_ptr<Connection> Connection::create(tcp::socket&& socket, Router& router, net::io_context& io_ctx) {
    return std::shared_ptr<Connection>(new Connection(std::move(socket), router, io_ctx));
}

Connection::Connection(tcp::socket&& socket, Router& router, net::io_context& io_ctx)
    : m_context(std::make_shared<BeastContext>(std::move(socket), io_ctx))
    , m_router(router) {
    
    // 获取客户端地址
    auto endpoint = m_context->socket.remote_endpoint();
    m_context->client_ip = endpoint.address().to_string();
    m_context->client_port = endpoint.port();
}

Connection::~Connection() {}

void Connection::start() {
    // 使用协程启动连接处理
    net::co_spawn(
        m_context->socket.get_executor(),
        readRequest(),
        net::detached);
}

net::awaitable<void> Connection::readRequest() {
    beast::error_code ec;
    
    // 异步读取 HTTP 请求
    co_await http::async_read(
        m_context->socket,
        m_buffer,
        m_context->req,
        net::use_awaitable);
    
    if (ec == http::error::end_of_stream) {
        // 客户端关闭连接
        close();
        co_return;
    }
    
    if (ec) {
        // 其他错误
        close();
        co_return;
    }
    
    // 处理请求（调用 Handle）
    co_await handleRequest();
    
    // 异步写入响应
    co_await writeResponse();
    
    if (ec) {
        close();
        co_return;
    }
    
    // 重置请求和缓冲区，准备处理下一个请求
    m_context->req = {};
    m_buffer.consume(m_buffer.size());
    
    close();
}

net::awaitable<void> Connection::handleRequest() {
    // 查找对应的处理器
    auto method = std::string(m_context->req.method_string());
    auto target = std::string(m_context->req.target());
    
    auto handler = m_router.findHandler(method, target);
    
    if (!handler) {
        // 未找到路由，返回 404
        m_context->res.result(http::status::not_found);
        m_context->res.set(http::field::content_type, "application/json");
        m_context->res.body() = R"({"ret":404,"errmsg":"Not Found"})";
        m_context->res.prepare_payload();
        co_return;
    }
    
    try {
        // 协程方式调用 Handle 处理请求
        co_await handler(m_context.get());
        
        // 设置响应
        m_context->res.version(m_context->req.version());
        m_context->res.keep_alive(true);  // 默认保持连接
        
    } catch (std::exception& e) {
        m_context->res.result(http::status::internal_server_error);
        m_context->res.set(http::field::content_type, "application/json");
        m_context->res.body() = fmt::format(R"({{"ret":500,"errmsg":"{}"}})", e.what());
        m_context->res.prepare_payload();
    }
}

net::awaitable<void> Connection::writeResponse() {
    // 异步写入 HTTP 响应
    co_await http::async_write(
        m_context->socket,
        m_context->res,
        net::use_awaitable);
}

void Connection::close() {
    beast::error_code ec;
    m_context->socket.shutdown(tcp::socket::shutdown_send, ec);
}

// ============================================================================
// SslConnection 实现 - 使用协程（SSL/TLS）
// ============================================================================


std::shared_ptr<SslConnection> SslConnection::create(
    tcp::socket&& socket, 
    Router& router, 
    ssl::context& ssl_ctx,
    net::io_context& io_ctx) {
    return std::shared_ptr<SslConnection>(
        new SslConnection(std::move(socket), router, ssl_ctx, io_ctx));
}

SslConnection::SslConnection(tcp::socket&& socket, Router& router, ssl::context& ssl_ctx, net::io_context& io_ctx)
    : m_context(std::make_shared<BeastContext>(std::move(socket), io_ctx))
    , m_router(router)
    , m_ssl_stream(socket, ssl_ctx) {
    
    // 获取客户端地址
    auto endpoint = m_ssl_stream.next_layer().remote_endpoint();
    m_context->client_ip = endpoint.address().to_string();
    m_context->client_port = endpoint.port();
}

SslConnection::~SslConnection() {}

void SslConnection::start() {
    // 使用协程启动 SSL 连接处理
    net::co_spawn(
        m_ssl_stream.next_layer().get_executor(),
        sslHandshake(),
        net::detached);
}

net::awaitable<void> SslConnection::sslHandshake() {
    beast::error_code ec;
    
    // 执行 SSL 握手
    co_await m_ssl_stream.async_handshake(
        ssl::stream_base::server,
        net::use_awaitable);
    
    if (ec) {
        HKU_ERROR("SSL handshake failed: {}", ec.message());
        close();
        co_return;
    }
    
    HKU_DEBUG("SSL handshake successful");
    
    // 握手成功后开始读取请求
    co_await readRequest();
}

net::awaitable<void> SslConnection::readRequest() {
    beast::error_code ec;
    
    try {
        while (!ec) {
            // 异步读取 HTTP 请求（SSL）
            co_await http::async_read(
                m_ssl_stream,
                m_buffer,
                m_context->req,
                net::use_awaitable);
            
            if (ec == http::error::end_of_stream) {
                // 客户端关闭连接
                HKU_DEBUG("Client disconnected: {}", ec.message());
                close();
                co_return;
            }
            
            if (ec) {
                HKU_ERROR("SSL read error: {}", ec.message());
                close();
                co_return;
            }
            
            // 协程方式处理请求（调用 Handle）
            co_await handleRequest();
            
            // 异步写入响应（SSL）
            co_await writeResponse();
            
            if (ec) {
                close();
                co_return;
            }
            
            // 重置请求和缓冲区，准备处理下一个请求
            m_context->req = {};
            m_buffer.consume(m_buffer.size());
        }
    } catch (std::exception& e) {
        HKU_ERROR("SSL read exception: {}", e.what());
        close();
    }
    
    close();
}

net::awaitable<void> SslConnection::handleRequest() {
    // 查找对应的处理器
    auto method = std::string(m_context->req.method_string());
    auto target = std::string(m_context->req.target());
    
    auto handler = m_router.findHandler(method, target);
    
    if (!handler) {
        // 未找到路由，返回 404
        m_context->res.result(http::status::not_found);
        m_context->res.set(http::field::content_type, "application/json");
        m_context->res.body() = R"({"ret":404,"errmsg":"Not Found"})";
        m_context->res.prepare_payload();
        co_return;
    }
    
    try {
        // 协程方式调用 Handle 处理请求
        co_await handler(m_context.get());
        
        // 设置响应
        m_context->res.version(m_context->req.version());
        m_context->res.keep_alive(true);  // 默认保持连接
        
    } catch (std::exception& e) {
        m_context->res.result(http::status::internal_server_error);
        m_context->res.set(http::field::content_type, "application/json");
        m_context->res.body() = fmt::format(R"({{"ret":500,"errmsg":"{}"}})", e.what());
        m_context->res.prepare_payload();
    }
}

net::awaitable<void> SslConnection::writeResponse() {
    beast::error_code ec;
    
    try {
        // 异步写入 HTTP 响应（SSL）
        co_await http::async_write(
            m_ssl_stream,
            m_context->res,
            net::use_awaitable);
        
        if (ec) {
            HKU_ERROR("SSL write error: {}", ec.message());
        }
    } catch (std::exception& e) {
        HKU_ERROR("SSL write exception: {}", e.what());
    }
}

void SslConnection::close() {
    beast::error_code ec;
    
    // SSL shutdown
    m_ssl_stream.async_shutdown(
        [&ec](beast::error_code shutdown_ec) {
            ec = shutdown_ec;
        });
    
    // TCP shutdown
    m_ssl_stream.next_layer().shutdown(tcp::socket::shutdown_send, ec);
}

// ============================================================================
// HttpServer 实现 - 使用协程 + SSL/TLS
// ============================================================================

void HttpServer::http_exit() {
    stop();
    exit(0);
}

void HttpServer::signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        http_exit();
    }
}

HttpServer::HttpServer(const char* host, uint16_t port) 
    : m_host(host), m_port(port) {
    HKU_CHECK(ms_server == nullptr, "Can only one server!");
    ms_server = this;
    
    m_root_url = fmt::format("{}:{}", m_host, m_port);
}

HttpServer::~HttpServer() {}

void HttpServer::configureSsl() {
    HKU_CHECK(!ms_ssl_config.ca_key_file.empty(), "SSL CA file not specified");
    HKU_CHECK(existFile(ms_ssl_config.ca_key_file), "Not exist ca file: {}", ms_ssl_config.ca_key_file);
    
    // 创建 SSL 上下文
    ms_ssl_context = new ssl::context(ssl::context::tlsv12_server);
    
    // 设置密码套件
    SSL_CTX_set_cipher_list(ms_ssl_context->native_handle(), 
        "ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-GCM-SHA256");
    
    // 加载证书和私钥
    ms_ssl_context->use_certificate_chain_file(ms_ssl_config.ca_key_file);
    ms_ssl_context->use_private_key_file(ms_ssl_config.ca_key_file, ssl::context::pem);
    
    // 如果有密码
    if (!ms_ssl_config.password.empty()) {
        ms_ssl_context->set_password_callback(
            [pwd = ms_ssl_config.password](std::size_t max_length, ssl::context_base::password_purpose purpose) {
                return pwd;
            });
    }
    
    // 设置客户端验证模式
    switch (ms_ssl_config.verify_mode) {
        case 0:  // 无需客户端认证
            ms_ssl_context->set_verify_mode(ssl::verify_none);
            break;
        case 1:  // 客户端认证可选
            ms_ssl_context->set_verify_mode(ssl::verify_peer | ssl::verify_fail_if_no_peer_cert);
            break;
        case 2:  // 需客户端认证
            ms_ssl_context->set_verify_mode(ssl::verify_peer | ssl::verify_fail_if_no_peer_cert);
            break;
    }
    
    CLS_INFO("SSL configured with CA file: {}", ms_ssl_config.ca_key_file);
}


net::awaitable<void> HttpServer::doAcceptSsl() {
    while (ms_running.load()) {
        beast::error_code ec;
        
        // 异步接受新连接
        tcp::socket socket = co_await ms_acceptor->async_accept(
            net::use_awaitable);
        
        if (ec) {
            if (ec == net::error::operation_aborted) {
                co_return;
            }
            CLS_ERROR("Accept error: {}", ec.message());
            continue;
        }
        
        // 为 SSL 连接创建处理器并启动协程
        auto connection = SslConnection::create(
            std::move(socket), 
            ms_router, 
            *ms_ssl_context, 
            *ms_io_context);
        connection->start();
    }
    
    co_return;
}

void HttpServer::start() {
    if (ms_running.load()) {
        return;
    }
    
    try {
        ms_io_context = new net::io_context();
        
        // 创建 acceptor
        auto addr = net::ip::make_address(m_host.c_str());
        ms_acceptor = new tcp::acceptor(*ms_io_context, {addr, m_port});
        
        // 配置 SSL（如果启用）
        if (ms_ssl_config.enabled) {
            configureSsl();
            
            CLS_INFO("HTTPS Server started on {}:{}", m_host, m_port);
            
            // 使用协程开始接受 SSL 连接
            net::co_spawn(
                *ms_io_context,
                doAcceptSsl(),
                net::detached);
        } else {
            CLS_INFO("HTTP Server started on {}:{}", m_host, m_port);
            
            // 使用协程开始接受连接
            net::co_spawn(
                *ms_io_context,
                doAccept(),
                net::detached);
        }
        
        ms_running.store(true);
        
#if defined(_WIN32)
        // Windows 下设置控制台程序输出代码页为 UTF8
        g_old_cp = GetConsoleOutputCP();
        SetConsoleOutputCP(CP_UTF8);
#endif
        
    } catch (std::exception& e) {
        CLS_FATAL("Failed to start server: {}", e.what());
        http_exit();
    }
}

net::awaitable<void> HttpServer::doAccept() {
    while (ms_running.load()) {
        beast::error_code ec;
        
        // 异步接受新连接
        tcp::socket socket = co_await ms_acceptor->async_accept(
            net::use_awaitable);
        
        if (ec) {
            if (ec == net::error::operation_aborted) {
                co_return;
            }
            CLS_ERROR("Accept error: {}", ec.message());
            continue;
        }
        
        // 为新连接创建处理器并启动协程
        auto connection = Connection::create(std::move(socket), ms_router, *ms_io_context);
        connection->start();
    }
    
    co_return;
}

void HttpServer::loop() {
    if (ms_io_context && ms_running.load()) {
        ms_io_context->run();
    }
}

void HttpServer::stop() {
#if defined(_WIN32)
    SetConsoleOutputCP(g_old_cp);
#endif
    
    if (ms_running.load()) {
        ms_running.store(false);
        
        if (ms_acceptor) {
            ms_acceptor->cancel();
            ms_acceptor->close();
            delete ms_acceptor;
            ms_acceptor = nullptr;
        }
        
        if (ms_ssl_context) {
            delete ms_ssl_context;
            ms_ssl_context = nullptr;
        }
        
        if (ms_io_context) {
            ms_io_context->stop();
            delete ms_io_context;
            ms_io_context = nullptr;
        }
        
        if (ms_server) {
            CLS_INFO("Quit Http server");
            ms_server = nullptr;
        }
    }
}

void HttpServer::set_error_msg(int16_t http_status, const std::string& body) {
    // TODO: 实现错误页面设置
    CLS_WARN("set_error_msg not implemented yet: status={}, body={}", http_status, body);
}

void HttpServer::set_tls(const char* ca_key_file, const char* password, int mode) {
    HKU_CHECK(ca_key_file != nullptr, "CA file cannot be null");
    HKU_CHECK(existFile(ca_key_file), "Not exist ca file: {}", ca_key_file);
    
    ms_ssl_config.ca_key_file = ca_key_file;
    ms_ssl_config.password = password ? password : "";
    ms_ssl_config.verify_mode = mode;
    ms_ssl_config.enabled = true;
    
    CLS_INFO("TLS configured successfully");
}

void HttpServer::regHandle(const char* method, const char* path, HandlerFunc rest_handle) {
    try {
        HKU_CHECK(strlen(path) > 1, "Invalid api path!");
        HKU_CHECK(path[0] == '/', "The api path must start with '/', but current is '{}'", path[0]);
    } catch (std::exception& e) {
        CLS_FATAL("Failed to register handler: {}", e.what());
        http_exit();
    }
    
    // 直接使用传入的 handler（已经是协程方式）
    ms_router.registerHandler(method, path, rest_handle);
    CLS_DEBUG("Registered handler: {} {}", method, path);
}

}  // namespace hku