/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-03-13
 *      Author: fasiondog
 */

#include <csignal>
#include <thread>
#include <hikyuu/utilities/os.h>
#include "WebSocketServer.h"

#if HKU_OS_WINDOWS
#include <Windows.h>
#endif

#if !HKU_OS_WINDOWS
#include <sys/stat.h>
#endif

// Boost.beast WebSocket 相关头文件
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/awaitable.hpp>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

namespace hku {

// ============================================================================
// 静态成员初始化
// ============================================================================

WebSocketServer* WebSocketServer::ms_server = nullptr;
WebSocketRouter WebSocketServer::ms_router;
net::io_context* WebSocketServer::ms_io_context = nullptr;
tcp::acceptor* WebSocketServer::ms_acceptor = nullptr;
std::atomic<bool> WebSocketServer::ms_running{false};
SslConfig WebSocketServer::ms_ssl_config;
ssl::context* WebSocketServer::ms_ssl_context = nullptr;
size_t WebSocketServer::ms_io_thread_count = 0;

// 全局连接池管理初始化
std::atomic<int> WebSocketServer::ms_active_connections{0};

// 信号处理防重入标志（复用 HTTP Server 的标志）
extern std::atomic<bool> g_signal_handling;

// ============================================================================
// WebSocketRouter 实现
// ============================================================================

void WebSocketRouter::registerHandler(const std::string& path, HandlerFunc handler) {
    m_routes.emplace_back(RouteKey{path}, std::move(handler));
}

WebSocketRouter::HandlerFunc WebSocketRouter::findHandler(const std::string& path) {
    // 精确匹配优先（线性搜索）
    for (const auto& [key, handler] : m_routes) {
        if (key.path == path) {
            return handler;
        }
    }

    // 通配符路由匹配
    for (const auto& [key, handler] : m_routes) {
        if (!key.path.empty() && key.path.back() == '*') {
            std::string_view prefix(key.path.data(), key.path.size() - 1);
            if (path.find(prefix) == 0) {
                return handler;
            }
        }
    }

    return nullptr;
}

// ============================================================================
// WebSocketConnection 实现 - 非SSL 版本
// ============================================================================

std::shared_ptr<WebSocketConnection> WebSocketConnection::create(
    tcp::socket&& socket, WebSocketHandle::HandlerFunc handler) {
    return std::shared_ptr<WebSocketConnection>(
        new WebSocketConnection(std::move(socket), std::move(handler)));
}

WebSocketConnection::WebSocketConnection(tcp::socket&& socket, 
                                         WebSocketHandle::HandlerFunc handler)
: m_ws(std::move(socket)),
  m_handler(std::move(handler)) {
    
    // 在 m_ws 完全构造后创建 context
    m_context = std::make_unique<WebSocketContext>(m_ws.get_executor());
    
    // 获取客户端地址
    try {
        auto endpoint = m_ws.next_layer().remote_endpoint();
        m_context->client_ip = endpoint.address().to_string();
        m_context->client_port = endpoint.port();
    } catch (const std::exception& e) {
        HKU_WARN("Failed to get remote endpoint: {}", e.what());
        m_context->client_ip = "unknown";
        m_context->client_port = 0;
    }

    // 配置 WebSocket
    m_ws.set_option(websocket::stream_base::timeout::suggested(
        beast::role_type::server));
    m_ws.set_option(websocket::stream_base::decorator(
        [](websocket::response_type& res) {
            res.set(beast::http::field::server, "Hikuuu-WebSocket");
        }));
    
    // 设置大小限制
    m_ws.read_message_max(WebSocketContext::MAX_MESSAGE_SIZE);
    m_ws.control_callback([](websocket::frame_type kind, beast::string_view payload) {
        // 可在此处理控制帧
    });
}

WebSocketConnection::~WebSocketConnection() {
    // 减少连接计数
    WebSocketServer::ms_active_connections.fetch_sub(1, std::memory_order_relaxed);
}

void WebSocketConnection::start() {
    auto self = shared_from_this();
    net::co_spawn(m_ws.get_executor(), doHandshake(self), net::detached);
}

net::awaitable<void> WebSocketConnection::doHandshake(
    std::shared_ptr<WebSocketConnection> self) {
    try {
        // 异步接受 WebSocket 握手
        co_await m_ws.async_accept(net::use_awaitable);
        
        HKU_DEBUG("WebSocket handshake successful from {}:{}", 
                  m_context->client_ip, m_context->client_port);
        
        // 启动读取循环
        net::co_spawn(m_ws.get_executor(), readLoop(self), net::detached);
        
        // 启动写入循环
        net::co_spawn(m_ws.get_executor(), writeLoop(self), net::detached);
        
        // 启动心跳检测
        net::co_spawn(m_ws.get_executor(), pingLoop(self), net::detached);
        
    } catch (const beast::system_error& e) {
        HKU_ERROR("WebSocket handshake failed: {}", e.what());
        close();
    } catch (const std::exception& e) {
        HKU_ERROR("WebSocket handshake exception: {}", e.what());
        close();
    }
}

net::awaitable<void> WebSocketConnection::readLoop(
    std::shared_ptr<WebSocketConnection> self) {
    try {
        while (true) {
            // 设置读取超时
            m_context->timer.expires_after(WebSocketContext::READ_TIMEOUT);
            
            // 使用 weak_ptr 防止定时器回调访问已销毁的对象
            auto weak_ctx = std::weak_ptr<WebSocketContext>();
            m_context->timer.async_wait([weak_ctx](beast::error_code ec) {
                if (!ec || ec == boost::asio::error::operation_aborted) {
                    if (auto ctx = weak_ctx.lock()) {
                        ctx->cancel_signal.emit(net::cancellation_type::all);
                    }
                }
            });
            
            // 异步读取消息
            auto n = co_await m_ws.async_read(m_context->buffer, net::use_awaitable);
            
            // 取消定时器
            m_context->timer.cancel();
            
            // 检查是否为文本消息 (忽略返回值以避免警告)
            (void)m_ws.got_text();
            
            // 获取消息数据
            auto data = beast::buffers_to_string(m_context->buffer.data());
            m_context->buffer.consume(n);
            
            // 调用 Handle 处理消息
            if (m_handler) {
                co_await m_handler(m_context.get());
            }
            
        }
    } catch (const beast::system_error& e) {
        m_context->timer.cancel();
        
        if (e.code() == websocket::error::closed ||
            e.code() == net::error::eof ||
            e.code() == beast::errc::connection_reset) {
            HKU_DEBUG("Client disconnected: {}", e.code().message());
        } else {
            HKU_ERROR("Read error: {}", e.what());
        }
    } catch (const std::exception& e) {
        m_context->timer.cancel();
        HKU_ERROR("Read exception: {}", e.what());
    }
    
    close();
}

net::awaitable<void> WebSocketConnection::writeLoop(
    std::shared_ptr<WebSocketConnection> self) {
    // 写入循环由发送队列驱动
    // 当前简化实现，实际使用时通过 send() 方法发送
    co_return;
}

net::awaitable<void> WebSocketConnection::pingLoop(
    std::shared_ptr<WebSocketConnection> self) {
    try {
        while (true) {
            // 等待 Ping 间隔
            co_await net::this_coro::executor;
            co_await net::steady_timer(m_ws.get_executor(), 
                                       WebSocketContext::PING_INTERVAL).async_wait(net::use_awaitable);
            
            // 发送 Ping - 使用空 payload
            ws::ping_data ping_payload;
            co_await m_ws.async_ping(ping_payload, net::use_awaitable);
            
            // 调用 Handle 的 onPing 回调
            if (m_handler) {
                co_await m_handler(m_context.get());
            }
        }
    } catch (const beast::system_error& e) {
        if (e.code() != net::error::operation_aborted) {
            HKU_ERROR("Ping error: {}", e.what());
        }
    } catch (const std::exception& e) {
        HKU_ERROR("Ping exception: {}", e.what());
    }
}

void WebSocketConnection::close() {
    beast::error_code ec;
    m_ws.next_layer().shutdown(tcp::socket::shutdown_send, ec);
}

// ============================================================================
// WebSocketConnectionSSL 实现 - SSL 版本
// ============================================================================

std::shared_ptr<WebSocketConnectionSSL> WebSocketConnectionSSL::create(
    tcp::socket&& socket, WebSocketHandle::HandlerFunc handler, ssl::context* ssl_ctx) {
    return std::shared_ptr<WebSocketConnectionSSL>(
        new WebSocketConnectionSSL(std::move(socket), std::move(handler), ssl_ctx));
}

WebSocketConnectionSSL::WebSocketConnectionSSL(tcp::socket&& socket,
                                               WebSocketHandle::HandlerFunc handler,
                                               ssl::context* ssl_ctx)
: m_socket(std::move(socket)),
  m_ssl_stream(m_socket, *ssl_ctx),
  m_ws(m_ssl_stream),
  m_handler(std::move(handler)) {
    
    // 在 m_ws 完全构造后创建 context
    m_context = std::make_unique<WebSocketContext>(m_ws.get_executor());
    
    
    // 获取客户端地址
    try {
        auto endpoint = m_ws.next_layer().next_layer().remote_endpoint();
        m_context->client_ip = endpoint.address().to_string();
        m_context->client_port = endpoint.port();
    } catch (const std::exception& e) {
        HKU_WARN("Failed to get remote endpoint: {}", e.what());
        m_context->client_ip = "unknown";
        m_context->client_port = 0;
    }

    // 配置 WebSocket
    m_ws.set_option(websocket::stream_base::timeout::suggested(
        beast::role_type::server));
    m_ws.set_option(websocket::stream_base::decorator(
        [](websocket::response_type& res) {
            res.set(beast::http::field::server, "Hikuuu-WebSocket-SSL");
        }));
    
    // 设置大小限制
    m_ws.read_message_max(WebSocketContext::MAX_MESSAGE_SIZE);
}

WebSocketConnectionSSL::~WebSocketConnectionSSL() {
    WebSocketServer::ms_active_connections.fetch_sub(1, std::memory_order_relaxed);
}

void WebSocketConnectionSSL::start() {
    auto self = shared_from_this();
    net::co_spawn(m_ws.get_executor(), sslHandshake(), net::detached);
}

net::awaitable<bool> WebSocketConnectionSSL::sslHandshake() {
    try {
        co_await m_ssl_stream.async_handshake(ssl::stream_base::server, net::use_awaitable);
        HKU_DEBUG("SSL handshake successful");
        
        // SSL 握手成功后开始 WebSocket 握手
        net::co_spawn(m_ws.get_executor(), doHandshake(shared_from_this()), net::detached);
        co_return true;
    } catch (const beast::system_error& e) {
        HKU_ERROR("SSL handshake failed: {}", e.what());
        close();
        co_return false;
    } catch (const std::exception& e) {
        HKU_ERROR("SSL handshake exception: {}", e.what());
        close();
        co_return false;
    }
}

net::awaitable<void> WebSocketConnectionSSL::doHandshake(
    std::shared_ptr<WebSocketConnectionSSL> self) {
    try {
        co_await m_ws.async_accept(net::use_awaitable);
        
        HKU_DEBUG("WebSocket handshake successful (SSL) from {}:{}", 
                  m_context->client_ip, m_context->client_port);
        
        // 启动读取循环
        net::co_spawn(m_ws.get_executor(), readLoop(self), net::detached);
        
        // 启动写入循环
        net::co_spawn(m_ws.get_executor(), writeLoop(self), net::detached);
        
        // 启动心跳检测
        net::co_spawn(m_ws.get_executor(), pingLoop(self), net::detached);
        
    } catch (const beast::system_error& e) {
        HKU_ERROR("WebSocket handshake failed (SSL): {}", e.what());
        close();
    } catch (const std::exception& e) {
        HKU_ERROR("WebSocket handshake exception (SSL): {}", e.what());
        close();
    }
}

net::awaitable<void> WebSocketConnectionSSL::readLoop(
    std::shared_ptr<WebSocketConnectionSSL> self) {
    try {
        while (true) {
            m_context->timer.expires_after(WebSocketContext::READ_TIMEOUT);
            
            auto weak_ctx = std::weak_ptr<WebSocketContext>();
            m_context->timer.async_wait([weak_ctx](beast::error_code ec) {
                if (!ec || ec == boost::asio::error::operation_aborted) {
                    if (auto ctx = weak_ctx.lock()) {
                        ctx->cancel_signal.emit(net::cancellation_type::all);
                    }
                }
            });
            
            auto n = co_await m_ws.async_read(m_context->buffer, net::use_awaitable);
            m_context->timer.cancel();
            
            auto data = beast::buffers_to_string(m_context->buffer.data());
            (void)m_ws.got_text(); // 消费返回值以避免未使用变量警告
            m_context->buffer.consume(n);
            
            if (m_handler) {
                co_await m_handler(m_context.get());
            }
        }
    } catch (const beast::system_error& e) {
        m_context->timer.cancel();
        
        if (e.code() == websocket::error::closed ||
            e.code() == net::error::eof ||
            e.code() == beast::errc::connection_reset) {
            HKU_DEBUG("Client disconnected (SSL): {}", e.code().message());
        } else {
            HKU_ERROR("Read error (SSL): {}", e.what());
        }
    } catch (const std::exception& e) {
        m_context->timer.cancel();
        HKU_ERROR("Read exception (SSL): {}", e.what());
    }
    
    close();
}

net::awaitable<void> WebSocketConnectionSSL::writeLoop(
    std::shared_ptr<WebSocketConnectionSSL> self) {
    co_return;
}

net::awaitable<void> WebSocketConnectionSSL::pingLoop(
    std::shared_ptr<WebSocketConnectionSSL> self) {
    try {
        while (true) {
            co_await net::this_coro::executor;
            co_await net::steady_timer(m_ws.get_executor(), 
                                       WebSocketContext::PING_INTERVAL).async_wait(net::use_awaitable);
            
            // 发送 Ping - 使用空 payload
            ws::ping_data ping_payload;
            co_await m_ws.async_ping(ping_payload, net::use_awaitable);
            
            // 调用 Handle 的 onPing 回调
            if (m_handler) {
                co_await m_handler(m_context.get());
            }
        }
    } catch (const beast::system_error& e) {
        if (e.code() != net::error::operation_aborted) {
            HKU_ERROR("Ping error (SSL): {}", e.what());
        }
    } catch (const std::exception& e) {
        HKU_ERROR("Ping exception (SSL): {}", e.what());
    }
}

void WebSocketConnectionSSL::close() {
    beast::error_code ec;
    m_ws.next_layer().next_layer().shutdown(tcp::socket::shutdown_send, ec);
}

// ============================================================================
// WebSocketServer 实现
// ============================================================================

void WebSocketServer::stop() {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
#endif

    if (ms_running.load()) {
        ms_running.store(false);

        // 关闭 acceptor
        if (ms_acceptor) {
            ms_acceptor->cancel();
            ms_acceptor->close();
            delete ms_acceptor;
            ms_acceptor = nullptr;
        }

        // 停止 io_context
        if (ms_io_context) {
            ms_io_context->stop();
        }

        CLS_INFO("WebSocket Server stopped");
    }
}

WebSocketServer::WebSocketServer(const char* host, uint16_t port) 
: m_host(host), m_port(port) {
    HKU_CHECK(ms_server == nullptr, "Can only have one WebSocket server!");
    ms_server = this;
}

WebSocketServer::~WebSocketServer() {}

void WebSocketServer::set_io_thread_count(size_t thread_count) {
    ms_io_thread_count = thread_count;
}

void WebSocketServer::configureSsl() {
    HKU_CHECK(!ms_ssl_config.ca_key_file.empty(), "SSL CA file not specified");
    HKU_CHECK(existFile(ms_ssl_config.ca_key_file), "Not exist ca file: {}",
              ms_ssl_config.ca_key_file);

    // 创建 SSL 上下文（使用 TLS 1.2）
    ms_ssl_context = new ssl::context(ssl::context::tlsv12_server);

    // 设置安全选项
    ms_ssl_context->set_options(
      ssl::context::default_workarounds |
      ssl::context::no_sslv2 |
      ssl::context::no_sslv3 |
      ssl::context::no_tlsv1 |
      ssl::context::no_tlsv1_1 |
      ssl::context::single_dh_use
    );

    // 加载证书和私钥
    ms_ssl_context->use_certificate_chain_file(ms_ssl_config.ca_key_file);
    ms_ssl_context->use_private_key_file(ms_ssl_config.ca_key_file, ssl::context::pem);

    // 如果有密码
    if (!ms_ssl_config.password.empty()) {
        ms_ssl_context->set_password_callback(
          [pwd = ms_ssl_config.password](
            std::size_t max_length, ssl::context_base::password_purpose purpose) { return pwd; });
    }

    // 设置客户端验证模式
    switch (ms_ssl_config.verify_mode) {
        case 0:
            ms_ssl_context->set_verify_mode(ssl::verify_none);
            break;
        case 1:
        case 2:
            ms_ssl_context->set_verify_mode(ssl::verify_peer | ssl::verify_fail_if_no_peer_cert);
            break;
    }

    CLS_INFO("WebSocket SSL configured with CA file: {}", ms_ssl_config.ca_key_file);
}

void WebSocketServer::regHandle(const char* path, HandlerFunc handler) {
    ms_router.registerHandler(path, std::move(handler));
}

net::awaitable<void> WebSocketServer::doAccept() {
    while (ms_running.load()) {
        beast::error_code ec;

        // 异步接受新连接
        tcp::socket socket = co_await ms_acceptor->async_accept(net::use_awaitable);

        if (ec) {
            if (ec == net::error::operation_aborted) {
                co_return;
            }
            CLS_ERROR("Accept error: {}", ec.message());
            continue;
        }

        // 检查连接数限制
        int expected = ms_active_connections.load(std::memory_order_acquire);
        while (expected < MAX_CONNECTIONS) {
            if (ms_active_connections.compare_exchange_weak(
                  expected, expected + 1,
                  std::memory_order_acq_rel,
                  std::memory_order_acquire)) {
                break;
            }
        }

        if (expected >= MAX_CONNECTIONS) {
            CLS_WARN("WebSocket connection limit reached ({}), rejecting connection", MAX_CONNECTIONS);
            continue;
        }

        // 创建 WebSocket 连接处理器
        auto connection = WebSocketConnection::create(std::move(socket), nullptr);
        connection->start();
    }

    co_return;
}

net::awaitable<void> WebSocketServer::doAcceptSsl() {
    while (ms_running.load()) {
        beast::error_code ec;

        tcp::socket socket = co_await ms_acceptor->async_accept(net::use_awaitable);

        if (ec) {
            if (ec == net::error::operation_aborted) {
                co_return;
            }
            CLS_ERROR("Accept error: {}", ec.message());
            continue;
        }

        // 检查连接数限制
        int expected = ms_active_connections.load(std::memory_order_acquire);
        while (expected < MAX_CONNECTIONS) {
            if (ms_active_connections.compare_exchange_weak(
                  expected, expected + 1,
                  std::memory_order_acq_rel,
                  std::memory_order_acquire)) {
                break;
            }
        }

        if (expected >= MAX_CONNECTIONS) {
            CLS_WARN("WebSocket SSL connection limit reached ({}), rejecting connection", MAX_CONNECTIONS);
            continue;
        }

        // 创建 SSL WebSocket 连接处理器
        auto connection = WebSocketConnectionSSL::create(std::move(socket), nullptr, ms_ssl_context);
        connection->start();
    }

    co_return;
}

void WebSocketServer::start() {
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

            CLS_INFO("WebSocket Secure Server started on {}:{}", m_host, m_port);

            // 使用协程开始接受 SSL连接
            net::co_spawn(*ms_io_context, doAcceptSsl(), net::detached);
        } else {
            CLS_INFO("WebSocket Server started on {}:{}", m_host, m_port);

            // 使用协程开始接受连接
            net::co_spawn(*ms_io_context, doAccept(), net::detached);
        }

        ms_running.store(true);

#if defined(_WIN32)
        SetConsoleOutputCP(CP_UTF8);
#endif

    } catch (std::exception& e) {
        CLS_FATAL("Failed to start WebSocket server: {}", e.what());
        stop();
    }
}

void WebSocketServer::loop() {
    if (ms_io_context && ms_running.load()) {
        // 确定线程数：如果未设置或设置为 0，使用硬件并发数
        size_t thread_count = ms_io_thread_count;
        if (thread_count == 0) {
            thread_count = std::thread::hardware_concurrency();
        }

        // 如果只有 1 个线程，直接运行
        if (thread_count <= 1) {
            CLS_INFO("Running WebSocket io_context with single thread");
            ms_io_context->run();
            return;
        }

        // 创建线程池运行 io_context
        CLS_INFO("Running WebSocket io_context with {} threads", thread_count);
        std::vector<std::thread> threads;
        threads.reserve(thread_count);

        // 启动工作线程
        for (size_t i = 0; i < thread_count; ++i) {
            threads.emplace_back([]() {
                try {
                    ms_io_context->run();
                } catch (const std::exception& e) {
                    CLS_ERROR("Thread exception: {}", e.what());
                } catch (...) {
                    CLS_ERROR("Unknown thread exception");
                }
            });
        }

        // 等待所有线程完成
        for (auto& t : threads) {
            if (t.joinable()) {
                t.join();
            }
        }
    }
}

void WebSocketServer::set_tls(const char* ca_key_file, const char* password, int mode) {
    ms_ssl_config.ca_key_file = ca_key_file;
    ms_ssl_config.password = password ? password : "";
    ms_ssl_config.verify_mode = mode;
    ms_ssl_config.enabled = true;
}

}  // namespace hku
