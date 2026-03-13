/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-03-13
 *      Author: fasiondog
 */

#include <csignal>
#include <thread>
#include <hikyuu/utilities/os.h>
#include "HttpServer.h"

#if HKU_OS_WINDOWS
#include <Windows.h>
#endif

#if !HKU_OS_WINDOWS
#include <sys/stat.h>
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
#include <boost/asio/awaitable.hpp>

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

// 全局连接池管理初始化
std::atomic<int> HttpServer::ms_active_connections{0};

#if defined(_WIN32)
static UINT g_old_cp;
#endif

// ============================================================================
// Router 实现
// ============================================================================

void Router::registerHandler(const std::string& method, const std::string& path,
                             HandlerFunc handler) {
    m_routes.emplace_back(RouteKey{method, path}, std::move(handler));
}

Router::HandlerFunc Router::findHandler(const std::string& method, const std::string& path) {
    // 精确匹配优先（线性搜索，路由数量有限时性能优于哈希表）
    for (const auto& [key, handler] : m_routes) {
        if (key.method == method && key.path == path) {
            return handler;
        }
    }

    // 通配符路由匹配（简单的前缀匹配）
    // 注意：通配符路由应该在注册时放在 vector 后部，避免每次都要遍历
    for (const auto& [key, handler] : m_routes) {
        if (key.method == method && !key.path.empty() && key.path.back() == '*') {
            // 前缀匹配：/api/* 匹配 /api/users
            std::string_view prefix(key.path.data(), key.path.size() - 1);
            if (path.find(prefix) == 0) {
                return handler;
            }
        }
    }

    return nullptr;
}

// ============================================================================
// Connection 实现 - 统一的 HTTP/HTTPS 连接处理器
// ============================================================================

std::shared_ptr<Connection> Connection::create(tcp::socket&& socket, Router* router,
                                               net::io_context& io_ctx, ssl::context* ssl_ctx) {
    if (ssl_ctx) {
        return std::shared_ptr<Connection>(
          new Connection(std::move(socket), router, io_ctx, ssl_ctx));
    } else {
        return std::shared_ptr<Connection>(
          new Connection(std::move(socket), router, io_ctx, nullptr));
    }
}

Connection::Connection(tcp::socket&& socket, Router* router, net::io_context& io_ctx,
                       ssl::context* ssl_ctx)
: m_socket(std::move(socket)),
  m_router(router),
  m_io_ctx(io_ctx),
  m_connection_start(std::chrono::steady_clock::now()) {
    // 如果提供了 SSL 上下文，初始化 SSL 流
    if (ssl_ctx) {
        m_ssl_stream = std::make_unique<ssl::stream<tcp::socket&>>(m_socket, *ssl_ctx);
    }

    // HKU_INFO("Connection::start: m_router={}, this={}, is_ssl={}", (void*)m_router, (void*)this,
    // IsSsl());

    // 全局连接池管理：检查并增加连接计数（使用 acquire-release 语义）
    int expected = HttpServer::ms_active_connections.load(std::memory_order_acquire);
    while (expected < HttpServer::MAX_CONNECTIONS) {
        if (HttpServer::ms_active_connections.compare_exchange_weak(
              expected, expected + 1,
              std::memory_order_acq_rel,     // 成功时使用 acquire-release
              std::memory_order_acquire)) {  // 失败时使用 acquire
            break;
        }
    }

    if (expected >= HttpServer::MAX_CONNECTIONS) {
        HKU_WARN("Global connection limit reached ({}), rejecting connection",
                 HttpServer::MAX_CONNECTIONS);
        throw std::runtime_error("Connection limit reached");
    }

    // 获取客户端地址
    try {
        auto endpoint = m_socket.remote_endpoint();
        m_client_ip = endpoint.address().to_string();
        m_client_port = endpoint.port();
    } catch (const std::exception& e) {
        HKU_WARN("Failed to get remote endpoint: {}, setting default values", e.what());
        m_client_ip = "unknown";
        m_client_port = 0;
    }
}

Connection::~Connection() {
    // 全局连接池管理：减少连接计数
    HttpServer::ms_active_connections.fetch_sub(1, std::memory_order_relaxed);
}

void Connection::start() {
    // HKU_INFO("Connection::start: m_router={}, this={}", (void*)m_router, (void*)this);
    // 使用 shared_from_this 确保 Connection 对象在协程执行期间不会被销毁
    auto self = shared_from_this();
    net::co_spawn(m_socket.get_executor(), readLoop(self), net::detached);
}

net::awaitable<bool> Connection::sslHandshake() {
    if (!m_ssl_stream) {
        co_return true;  // 非SSL连接直接成功
    }

    try {
        co_await m_ssl_stream->async_handshake(ssl::stream_base::server, net::use_awaitable);
        HKU_DEBUG("SSL handshake successful");
        co_return true;
    } catch (const beast::system_error& e) {
        HKU_ERROR("SSL handshake failed: {}", e.what());
        co_return false;
    } catch (const std::exception& e) {
        HKU_ERROR("SSL handshake exception: {}", e.what());
        co_return false;
    }
}

net::awaitable<void> Connection::readLoop(std::shared_ptr<Connection> self) {
    // TCP 连接级别的循环，管理多个 HTTP Session
    try {
        // SSL 握手（如果是 HTTPS）
        if (m_ssl_stream) {
            bool success = co_await sslHandshake();
            if (!success) {
                close();
                co_return;
            }
        }

        // 增加连接计数（使用 acquire-release 语义）
        if (HttpServer::ms_active_connections.fetch_add(1, std::memory_order_acq_rel) >=
            HttpServer::MAX_CONNECTIONS) {
            HttpServer::ms_active_connections.fetch_sub(1, std::memory_order_acq_rel);
            HKU_WARN("Global connection limit reached ({}), rejecting connection from {}:{}",
                     HttpServer::MAX_CONNECTIONS, m_client_ip, m_client_port);
            co_return;
        }

        // 确保在函数退出时减少连接计数
        auto decrement_connection = [&]() {
            HttpServer::ms_active_connections.fetch_sub(1, std::memory_order_release);
        };

        while (true) {
            // 检查 Keep-Alive 限制
            if (++m_request_count > BeastContext::MAX_KEEPALIVE_REQUESTS) {
                HKU_WARN("Keep-Alive limit reached ({} requests), closing connection from {}:{}",
                         m_request_count, m_client_ip, m_client_port);
                decrement_connection();
                break;
            }

            // 检查连接最大存活时间
            auto elapsed = std::chrono::steady_clock::now() - m_connection_start;
            if (elapsed > BeastContext::MAX_CONNECTION_AGE) {
                HKU_WARN("Connection age limit reached, closing from {}:{}", m_client_ip,
                         m_client_port);
                decrement_connection();
                break;
            }

            // 为每个 HTTP 请求创建独立的 Session
            // 注意：SSL 模式下需要使用 SSL stream 的 next_layer() 作为 socket
            auto& socket_ref = m_ssl_stream ? m_ssl_stream->next_layer() : m_socket;
            auto session = std::make_shared<BeastContext>(socket_ref, m_io_ctx);
            session->client_ip = m_client_ip;
            session->client_port = m_client_port;

            // 配置 HTTP 解析器选项，防止超大请求和慢速攻击
            http::request_parser<http::string_body> parser;
            parser.body_limit(BeastContext::MAX_BODY_SIZE);      // 限制请求体最大为 10MB
            parser.header_limit(BeastContext::MAX_HEADER_SIZE);  // 限制请求头最大为 8KB

            // 设置读取超时保护（带主动中断机制）
            session->timer.expires_after(BeastContext::HEADER_TIMEOUT);

            // 启动超时定时器，到期时主动取消异步操作
            session->timer.async_wait([session](beast::error_code ec) {
                if (!ec || ec == boost::asio::error::operation_aborted) {
                    // 主动取消正在进行的异步操作
                    session->cancel_signal.emit(net::cancellation_type::all);
                }
            });

            // 异步读取 HTTP 请求（根据 m_ssl_stream 是否为 nullptr 选择不同流）
            if (m_ssl_stream) {
                co_await http::async_read(*m_ssl_stream, session->buffer, parser,
                                          net::use_awaitable);
            } else {
                co_await http::async_read(session->socket, session->buffer, parser,
                                          net::use_awaitable);
            }

            // 取消定时器（读取成功）
            session->timer.cancel();

            // 将解析后的请求移动到 session 中
            session->req = parser.release();

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
        if (e.code() == http::error::end_of_stream || e.code() == net::error::eof ||
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

    HKU_INFO("Connection::processHandle: {} {}, m_router={}, is_ssl={}", method, target,
             (void*)m_router, m_ssl_stream ? "true" : "false");

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
        // 设置业务处理超时保护（简化版本）
        // 注意：先取消可能存在的旧定时器，再启动新定时器
        context->timer.cancel();
        context->timer.expires_after(BeastContext::TOTAL_TIMEOUT);

        // 启动超时定时器，到期时主动取消业务处理
        context->timer.async_wait([context](beast::error_code ec) {
            if (!ec) {
                // 超时时主动取消正在进行的业务处理
                context->cancel_signal.emit(net::cancellation_type::all);
            }
        });

        // 执行业务处理（绑定取消令牌到协程）
        // 注意：handler 内部需要通过 net::bind_cancellation 或检查 cancellation_state 来响应取消
        co_await handler(context.get());

        // 取消定时器（处理完成）
        context->timer.cancel();

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
        // 设置写入超时保护
        context->timer.expires_after(BeastContext::WRITE_TIMEOUT);

        // 异步写入 HTTP 响应（根据 m_ssl_stream 是否为 nullptr 选择不同流）
        if (m_ssl_stream) {
            co_await http::async_write(*m_ssl_stream, context->res, net::use_awaitable);
        } else {
            co_await http::async_write(context->socket, context->res, net::use_awaitable);
        }

        // 取消定时器（写入成功）
        context->timer.cancel();
    } catch (const beast::system_error& e) {
        HKU_ERROR("Write response error: {}", e.what());
        throw;
    }
}

void Connection::close() {
    beast::error_code ec;

    if (m_ssl_stream) {
        // SSL shutdown - 使用同步方式确保关闭完成
        try {
            // 首先尝试优雅关闭（带超时保护）
            auto close_timeout = std::chrono::seconds(2);
            bool shutdown_complete = false;

            // 启动异步关闭
            m_ssl_stream->async_shutdown([&shutdown_complete, &ec](beast::error_code shutdown_ec) {
                ec = shutdown_ec;
                shutdown_complete = true;
            });

            // 等待异步操作完成（带超时）
            auto start = std::chrono::steady_clock::now();
            while (!shutdown_complete) {
                auto elapsed = std::chrono::steady_clock::now() - start;
                if (elapsed > close_timeout) {
                    HKU_WARN("SSL shutdown timeout, forcing close");
                    break;
                }
                // 短暂休眠，让出 CPU 时间片
                std::this_thread::yield();
            }

            // TCP shutdown (through SSL stream)
            m_ssl_stream->next_layer().shutdown(tcp::socket::shutdown_send, ec);
        } catch (const std::exception& e) {
            HKU_ERROR("SSL shutdown exception: {}", e.what());
            // 异常时强制关闭底层 TCP 连接
            try {
                m_ssl_stream->next_layer().shutdown(tcp::socket::shutdown_both, ec);
            } catch (...) {
                // 忽略二次异常
            }
        }
    } else {
        // 普通 TCP shutdown
        m_socket.shutdown(tcp::socket::shutdown_send, ec);
    }
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

    // 检查证书文件权限（仅类 Unix 系统）
#if !HKU_OS_WINDOWS
    struct stat st;
    if (stat(ms_ssl_config.ca_key_file.c_str(), &st) == 0) {
        // 检查文件权限是否为 600（-rw-------）或更严格
        mode_t perms = st.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);
        if (perms & (S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH)) {
            HKU_WARN(
              "Certificate file '{}' has insecure permissions: {:o}. "
              "Recommended: 600 (-rw-------)",
              ms_ssl_config.ca_key_file, perms);
        }

        // 如果是私钥文件，检查应该更严格
        if (ms_ssl_config.ca_key_file.find(".key") != std::string::npos ||
            ms_ssl_config.ca_key_file.find(".pem") != std::string::npos) {
            if (perms & (S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH)) {
                HKU_ERROR(
                  "Private key file '{}' has world/group readable permissions! "
                  "This is a security risk. Please set permissions to 600",
                  ms_ssl_config.ca_key_file);
                throw std::runtime_error("Insecure private key file permissions");
            }
        }
    } else {
        HKU_WARN("Failed to stat certificate file: {}", ms_ssl_config.ca_key_file);
    }
#else
    // Windows 系统：检查文件是否存在即可，Windows 使用 ACL 而非 POSIX 权限
    HKU_DEBUG("Running on Windows, skipping POSIX permission check for '{}'",
              ms_ssl_config.ca_key_file);
#endif

    // 创建 SSL 上下文（使用 TLS 1.2）
    ms_ssl_context = new ssl::context(ssl::context::tlsv12_server);

    // 设置安全选项 - 禁用不安全的协议版本和特性
    ms_ssl_context->set_options(
      ssl::context::default_workarounds |  // OpenSSL 工作区
      ssl::context::no_sslv2 |             // 禁用 SSLv2（已废弃）
      ssl::context::no_sslv3 |             // 禁用 SSLv3（POODLE 漏洞）
      ssl::context::no_tlsv1 |             // 禁用 TLS 1.0（BEAST 攻击）
      ssl::context::no_tlsv1_1 |           // 禁用 TLS 1.1（弱加密）
      ssl::context::single_dh_use          // 每次握手使用新的 DH 参数，防止 Logjam 攻击
    );

    // 设置密码套件（强加密算法）
    // TLS 1.2 密码套件 - 按优先级排序
    SSL_CTX_set_cipher_list(ms_ssl_context->native_handle(),
                            // GCM 模式（认证加密，优先使用）
                            "ECDHE-RSA-AES256-GCM-SHA384:"
                            "ECDHE-RSA-AES128-GCM-SHA256:"
                            // CBC 模式（兼容性备用）
                            "ECDHE-RSA-AES256-SHA384:"
                            "ECDHE-RSA-AES128-SHA256:"
                            // 额外的安全套件
                            "ECDHE-RSA-CHACHA20-POLY1305-SHA256:"
                            "DHE-RSA-AES256-GCM-SHA384:"
                            "DHE-RSA-AES128-GCM-SHA256");

// TLS 1.3 密码套件（如果 OpenSSL 版本支持）
#if OPENSSL_VERSION_NUMBER >= 0x10101000L
    SSL_CTX_set_ciphersuites(ms_ssl_context->native_handle(),
                             "TLS_AES_256_GCM_SHA384:"
                             "TLS_CHACHA20_POLY1305_SHA256:"
                             "TLS_AES_128_GCM_SHA256");
#endif

// 启用椭圆曲线密钥交换（ECDH）- 自动选择最佳曲线
// 现代 OpenSSL 版本会自动选择安全的椭圆曲线参数
#if OPENSSL_VERSION_NUMBER >= 0x10002000L
    SSL_CTX_set_ecdh_auto(ms_ssl_context->native_handle(), 1);
#endif

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

        // 为 SSL连接创建处理器并启动协程（传入 SSL 上下文）
        auto connection =
          Connection::create(std::move(socket), &ms_router, *ms_io_context, ms_ssl_context);
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

        // 为新连接创建处理器并启动协程（非SSL模式）
        auto connection =
          Connection::create(std::move(socket), &ms_router, *ms_io_context, nullptr);
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