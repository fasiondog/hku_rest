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
size_t HttpServer::ms_io_thread_count = 0;  // 默认使用硬件并发数

#if defined(_WIN32)
static UINT g_old_cp;
#endif

// ============================================================================
// Router 实现
// ============================================================================

void Router::registerHandler(const std::string& method, const std::string& path,
                             HandlerFunc handler) {
    RouteKey key{method, path};
    m_routes[key] = handler;
}

Router::HandlerFunc Router::findHandler(const std::string& method, const std::string& path) {
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

std::shared_ptr<Connection> Connection::create(tcp::socket&& socket, Router* router,
                                               net::io_context& io_ctx) {
    return std::shared_ptr<Connection>(new Connection(std::move(socket), router, io_ctx));
}

Connection::Connection(tcp::socket&& socket, Router* router, net::io_context& io_ctx)
: m_socket(std::move(socket)), m_router(router), m_io_ctx(io_ctx) {
    // HKU_INFO("Connection constructor: m_router={}, &ms_router should be same", (void*)m_router);
    // HKU_ASSERT(m_router);
    // 获取客户端地址
    auto endpoint = m_socket.remote_endpoint();
    m_client_ip = endpoint.address().to_string();
    m_client_port = endpoint.port();
}

Connection::~Connection() {}

void Connection::start() {
    // HKU_INFO("Connection::start: m_router={}, this={}", (void*)m_router, (void*)this);
    // 使用 shared_from_this 确保 Connection 对象在协程执行期间不会被销毁
    auto self = shared_from_this();
    net::co_spawn(m_socket.get_executor(), readLoop(self), net::detached);
}

net::awaitable<void> Connection::readLoop(std::shared_ptr<Connection> self) {
    // TCP 连接级别的循环，管理多个 HTTP Session
    try {
        while (true) {
            // 为每个 HTTP 请求创建独立的 Session
            auto session = std::make_shared<BeastContext>(m_socket, m_io_ctx);
            session->client_ip = m_client_ip;
            session->client_port = m_client_port;

            // 读取一个完整的 HTTP 请求
            co_await http::async_read(session->socket, session->buffer, session->req,
                                      net::use_awaitable);

            // 检查是否为 HTTP/2 连接尝试（HTTP/2 Connection Preface）
            // HTTP/2 客户端会先发送 "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
            if (session->req.method_string() == "PRI") {
                HKU_DEBUG("HTTP/2 connection attempt detected on HTTP/1.1 server");
                // 拒绝 HTTP/2 连接，返回 400 或关闭连接
                session->res.result(http::status::bad_request);
                session->res.set(http::field::content_type, "text/plain");
                session->res.body() = "This server only supports HTTP/1.1";
                session->res.prepare_payload();
                co_await writeResponse(session);
                break;
            }

            // 请求读取完成后，处理该请求（调用 Handle）
            co_await processHandle(session);

            // 写入响应
            co_await writeResponse(session);

            // 检查是否需要保持连接
            bool keep_alive = session->req.keep_alive();

            if (!keep_alive) {
                HKU_DEBUG("Closing connection (not keep-alive)");
                break;
            }

            HKU_DEBUG("Keeping connection alive for next request");
        }
    } catch (const beast::system_error& e) {
        if (e.code() == http::error::end_of_stream || 
            e.code() == net::error::eof ||
            e.code() == beast::errc::connection_reset) {
            HKU_DEBUG("Client disconnected: {}", e.code().message());
        } else {
            HKU_ERROR("Connection error: {}", e.what());
        }
    } catch (const std::exception& e) {
        HKU_ERROR("Exception in readLoop: {}", e.what());
    }

    close();
}

net::awaitable<void> Connection::processHandle(std::shared_ptr<BeastContext> context) {
    // 查找对应的处理器
    auto method = std::string(context->req.method_string());
    auto target = std::string(context->req.target());

    // 安全检查 router 是否为空
    if (!m_router) {
        HKU_ERROR("Router is null, cannot process request");
        context->res.result(http::status::internal_server_error);
        context->res.set(http::field::content_type, "application/json");
        context->res.body() =
          R"({"ret":500,"errmsg":"Internal Server Error: Router not initialized"})";
        context->res.prepare_payload();
        co_return;
    }

    auto handler = m_router->findHandler(method, target);

    if (!handler) {
        // 未找到路由，返回 404
        context->res.result(http::status::not_found);
        context->res.set(http::field::content_type, "application/json");
        context->res.body() = R"({"ret":404,"errmsg":"Not Found"})";
        context->res.prepare_payload();
        co_return;
    }

    try {
        // 协程方式调用 Handle 处理请求
        co_await handler(context.get());

        // 设置响应
        context->res.version(context->req.version());
        context->res.keep_alive(true);  // 默认保持连接

    } catch (std::exception& e) {
        context->res.result(http::status::internal_server_error);
        context->res.set(http::field::content_type, "application/json");
        context->res.body() = fmt::format(R"({{"ret":500,"errmsg":"{}"}})", e.what());
        context->res.prepare_payload();
    }
}

net::awaitable<void> Connection::writeResponse(std::shared_ptr<BeastContext> context) {
    try {
        // 异步写入 HTTP 响应
        co_await http::async_write(context->socket, context->res, net::use_awaitable);
    } catch (const beast::system_error& e) {
        HKU_ERROR("Write response error: {}", e.what());
        throw;
    }
}

void Connection::close() {
    beast::error_code ec;
    m_socket.shutdown(tcp::socket::shutdown_send, ec);
}

// ============================================================================
// SslConnection 实现 - 使用协程（SSL/TLS）
// ============================================================================

std::shared_ptr<SslConnection> SslConnection::create(tcp::socket&& socket, Router* router,
                                                     ssl::context& ssl_ctx,
                                                     net::io_context& io_ctx) {
    return std::shared_ptr<SslConnection>(
      new SslConnection(std::move(socket), router, ssl_ctx, io_ctx));
}

SslConnection::SslConnection(tcp::socket&& socket, Router* router, ssl::context& ssl_ctx,
                             net::io_context& io_ctx)
: m_socket(std::move(socket)), m_router(router), m_ssl_stream(m_socket, ssl_ctx), m_io_ctx(io_ctx) {
    HKU_ASSERT(m_router);
    // 获取客户端地址
    auto endpoint = m_ssl_stream.next_layer().remote_endpoint();
    m_client_ip = endpoint.address().to_string();
    m_client_port = endpoint.port();
}

SslConnection::~SslConnection() {}

void SslConnection::start() {
    // 使用 shared_from_this 确保 SslConnection 对象在协程执行期间不会被销毁
    auto self = shared_from_this();
    net::co_spawn(m_ssl_stream.next_layer().get_executor(), readLoop(self), net::detached);
}

net::awaitable<void> SslConnection::readLoop(std::shared_ptr<SslConnection> self) {
    // SSL 握手
    try {
        co_await m_ssl_stream.async_handshake(ssl::stream_base::server, net::use_awaitable);
        HKU_DEBUG("SSL handshake successful");
    } catch (const beast::system_error& e) {
        HKU_ERROR("SSL handshake failed: {}", e.what());
        close();
        co_return;
    } catch (const std::exception& e) {
        HKU_ERROR("SSL handshake exception: {}", e.what());
        close();
        co_return;
    }

    // SSL 握手成功后，进入 HTTP 会话循环
    try {
        while (true) {
            // 为每个 HTTP 请求创建独立的 Session
            auto session = std::make_shared<BeastContext>(m_ssl_stream.next_layer(), m_io_ctx);
            session->client_ip = m_client_ip;
            session->client_port = m_client_port;

            // 读取一个完整的 HTTP 请求（通过 SSL 流）
            co_await http::async_read(m_ssl_stream, session->buffer, session->req,
                                      net::use_awaitable);

            // 检查是否为 HTTP/2 连接尝试（HTTP/2 Connection Preface）
            if (session->req.method_string() == "PRI") {
                HKU_DEBUG("HTTP/2 connection attempt detected on HTTP/1.1 server");
                // 拒绝 HTTP/2 连接，返回 400 或关闭连接
                session->res.result(http::status::bad_request);
                session->res.set(http::field::content_type, "text/plain");
                session->res.body() = "This server only supports HTTP/1.1";
                session->res.prepare_payload();
                co_await writeResponse(session);
                break;
            }

            // 请求读取完成后，处理该请求（调用 Handle）
            co_await processHandle(session);

            // 写入响应（通过 SSL 流）
            co_await writeResponse(session);

            // 检查是否需要保持连接
            bool keep_alive = session->req.keep_alive();

            if (!keep_alive) {
                HKU_DEBUG("Closing SSL connection (not keep-alive)");
                break;
            }

            HKU_DEBUG("Keeping SSL connection alive for next request");
        }
    } catch (const beast::system_error& e) {
        if (e.code() == http::error::end_of_stream || e.code() == net::error::eof) {
            HKU_DEBUG("Client disconnected: {}", e.code().message());
        } else {
            HKU_ERROR("SSL connection error: {}", e.what());
        }
    } catch (const std::exception& e) {
        HKU_ERROR("Exception in SslConnection::readLoop: {}", e.what());
    }

    close();
}

net::awaitable<void> SslConnection::processHandle(std::shared_ptr<BeastContext> context) {
    // 查找对应的处理器
    auto method = std::string(context->req.method_string());
    auto target = std::string(context->req.target());

    HKU_INFO("SslConnection::processHandle: {} {}, m_router={}", method, target, (void*)m_router);

    // 安全检查 router 是否为空
    if (!m_router) {
        HKU_ERROR("Router is null, cannot process request");
        context->res.result(http::status::internal_server_error);
        context->res.set(http::field::content_type, "application/json");
        context->res.body() =
          R"({"ret":500,"errmsg":"Internal Server Error: Router not initialized"})";
        context->res.prepare_payload();
        co_return;
    }

    auto handler = m_router->findHandler(method, target);

    if (!handler) {
        // 未找到路由，返回 404
        HKU_INFO("not found: {}", target);
        context->res.result(http::status::not_found);
        context->res.set(http::field::content_type, "application/json");
        context->res.body() = R"({"ret":404,"errmsg":"Not Found"})";
        context->res.prepare_payload();
        co_return;
    }

    try {
        // 协程方式调用 Handle 处理请求
        co_await handler(context.get());

        // 设置响应
        context->res.version(context->req.version());
        context->res.keep_alive(true);  // 默认保持连接

    } catch (std::exception& e) {
        context->res.result(http::status::internal_server_error);
        context->res.set(http::field::content_type, "application/json");
        context->res.body() = fmt::format(R"({{"ret":500,"errmsg":"{}"}})", e.what());
        context->res.prepare_payload();
    }
}

net::awaitable<void> SslConnection::writeResponse(std::shared_ptr<BeastContext> context) {
    try {
        // 异步写入 HTTP 响应（通过 SSL 流）
        co_await http::async_write(m_ssl_stream, context->res, net::use_awaitable);
    } catch (const beast::system_error& e) {
        HKU_ERROR("Write response error: {}", e.what());
        throw;
    }
}

void SslConnection::close() {
    beast::error_code ec;

    // SSL shutdown
    m_ssl_stream.async_shutdown([&ec](beast::error_code shutdown_ec) { ec = shutdown_ec; });

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

HttpServer::HttpServer(const char* host, uint16_t port) : m_host(host), m_port(port) {
    HKU_CHECK(ms_server == nullptr, "Can only one server!");
    ms_server = this;

    m_root_url = fmt::format("{}:{}", m_host, m_port);
}

HttpServer::~HttpServer() {}

void HttpServer::set_io_thread_count(size_t thread_count) {
    ms_io_thread_count = thread_count;  // 修改为静态成员变量
}

void HttpServer::configureSsl() {
    HKU_CHECK(!ms_ssl_config.ca_key_file.empty(), "SSL CA file not specified");
    HKU_CHECK(existFile(ms_ssl_config.ca_key_file), "Not exist ca file: {}",
              ms_ssl_config.ca_key_file);

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
          [pwd = ms_ssl_config.password](
            std::size_t max_length, ssl::context_base::password_purpose purpose) { return pwd; });
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

    // 禁用 HTTP/2 (ALPN)，强制使用 HTTP/1.1
    // 当前实现仅支持 HTTP/1.1，待后续集成 nghttp2 支持 HTTP/2
    SSL_CTX_set_alpn_select_cb(
      ms_ssl_context->native_handle(),
      [](SSL* /*ssl*/, const unsigned char** out, unsigned char* outlen, const unsigned char* in,
         unsigned int inlen, void* /*arg*/) -> int {
          // 遍历客户端提供的协议列表
          unsigned int i = 0;
          while (i < inlen) {
              unsigned char protocol_len = in[i];
              if (i + protocol_len >= inlen) {
                  return SSL_TLSEXT_ERR_NOACK;
              }

              // 检查是否为 http/1.1
              if (protocol_len == 8 && memcmp(&in[i + 1], "http/1.1", 8) == 0) {
                  *out = &in[i + 1];
                  *outlen = protocol_len;
                  return SSL_TLSEXT_ERR_OK;
              }

              // 如果是 h2 或 h2c，拒绝
              if ((protocol_len == 2 && memcmp(&in[i + 1], "h2", 2) == 0) ||
                  (protocol_len == 3 && memcmp(&in[i + 1], "h2c", 3) == 0)) {
                  HKU_DEBUG("Rejected HTTP/2 negotiation attempt");
              }

              i += protocol_len + 1;
          }

          // 没有找到支持的协议，默认返回 http/1.1
          static const unsigned char default_http11[] = {0x08, 'h', 't', 't', 'p',
                                                         '/',  '1', '.', '1'};
          *out = default_http11;
          *outlen = sizeof(default_http11);
          return SSL_TLSEXT_ERR_OK;
      },
      nullptr);

    CLS_INFO("SSL configured with CA file: {} (HTTP/2 disabled, using HTTP/1.1)",
             ms_ssl_config.ca_key_file);
}

net::awaitable<void> HttpServer::doAcceptSsl() {
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

        // 为 SSL连接创建处理器并启动协程
        auto connection =
          SslConnection::create(std::move(socket), &ms_router, *ms_ssl_context, *ms_io_context);
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

            // 使用协程开始接受 SSL连接
            net::co_spawn(*ms_io_context, doAcceptSsl(), net::detached);
        } else {
            CLS_INFO("HTTP Server started on {}:{}", m_host, m_port);

            // 使用协程开始接受连接
            net::co_spawn(*ms_io_context, doAccept(), net::detached);
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
        tcp::socket socket = co_await ms_acceptor->async_accept(net::use_awaitable);

        if (ec) {
            if (ec == net::error::operation_aborted) {
                co_return;
            }
            CLS_ERROR("Accept error: {}", ec.message());
            continue;
        }

        // 为新连接创建处理器并启动协程
        auto connection = Connection::create(std::move(socket), &ms_router, *ms_io_context);
        connection->start();
    }

    co_return;
}

void HttpServer::loop() {
    if (ms_io_context && ms_running.load()) {
        // 确定线程数：如果未设置或设置为 0，使用硬件并发数
        size_t thread_count = ms_io_thread_count;
        if (thread_count == 0) {
            thread_count = std::thread::hardware_concurrency();
        }

        // 如果只有 1 个线程，直接运行
        if (thread_count <= 1) {
            CLS_INFO("Running io_context with single thread");
            ms_io_context->run();
            return;
        }

        // 创建线程池运行 io_context
        CLS_INFO("Running io_context with {} threads", thread_count);
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