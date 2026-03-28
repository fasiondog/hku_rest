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
#include "WebSocketHandle.h"
#include "HttpWebSocketConfig.h"

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

// ============================================================================
// 静态成员初始化
// ============================================================================

HttpServer* HttpServer::ms_server = nullptr;

ssl::context* HttpServer::ms_ssl_context = nullptr;
net::io_context* HttpServer::ms_io_context = nullptr;
tcp::acceptor* HttpServer::ms_acceptor = nullptr;
std::atomic<bool> HttpServer::ms_running{false};
std::shared_ptr<ConnectionManager> HttpServer::ms_connection_manager{nullptr};  // 连接管理器
std::shared_ptr<WebSocketConnectionManager> HttpServer::ms_ws_connection_manager{
  nullptr};  // WebSocket 连接管理器
WebSocketRouter HttpServer::ms_ws_router;
bool HttpServer::ms_use_external_io{false};    // 初始化静态成员
bool HttpServer::ms_websocket_enabled{false};  // WebSocket 功能默认禁用

// 信号处理防重入标志
std::atomic<bool> g_signal_handling{false};

#if defined(_WIN32)
static UINT g_old_cp;
#endif

// ============================================================================
// 工具函数实现
// ============================================================================

/**
 * @brief 为 HTTP 响应设置安全响应头和 CORS 头
 *
 * 包含以下安全头:
 * - Strict-Transport-Security (HSTS): 强制 HTTPS
 * - X-Content-Type-Options: 防止 MIME 类型嗅探
 * - X-Frame-Options: 防止点击劫持攻击
 * - X-XSS-Protection: XSS 防护
 * - Referrer-Policy: 控制 Referer 信息泄露
 * - Content-Security-Policy: 内容安全策略（防止XSS）
 * - Permissions-Policy: 权限策略（限制浏览器功能）
 * - CORS 头 (如果启用)
 *
 * @tparam ResponseType 响应类型 (http::response 或其引用)
 * @param response HTTP 响应对象
 * @param cors_config CORS 配置对象 (可选)
 */
template <typename ResponseType>
static void setResponseHeaders(ResponseType& response, const CorsConfig* cors_config = nullptr) {
    // 设置安全响应头
    response.set(http::field::strict_transport_security, "max-age=31536000; includeSubDomains");
    response.set(http::field::x_content_type_options, "nosniff");
    response.set(http::field::x_frame_options, "DENY");
    response.set(http::field::x_xss_protection, "1; mode=block");
    response.set(http::field::referrer_policy, "no-referrer-when-downgrade");

    // 增强安全头
    response.set(http::field::content_security_policy,
                 "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src "
                 "'self' data:;");
    response.set(http::field::cache_control, "no-store, no-cache, must-revalidate, max-age=0");

    // 设置 CORS 头 (如果提供了配置且已启用)
    if (cors_config && cors_config->enabled) {
        setCorsHeaders(response, *cors_config);
    }
}

/**
 * @brief 为 HTTP 响应设置 CORS 头
 *
 * @tparam ResponseType 响应类型
 * @param response HTTP 响应对象
 * @param config CORS 配置对象
 */
template <typename ResponseType>
static void setCorsHeaders(ResponseType& response, const CorsConfig& config) {
    if (!config.enabled) {
        return;
    }

    // Access-Control-Allow-Origin
    response.set(http::field::access_control_allow_origin, config.allow_origin);

    // Access-Control-Allow-Methods
    if (!config.allow_methods.empty()) {
        response.set(http::field::access_control_allow_methods, config.allow_methods);
    }

    // Access-Control-Allow-Headers
    if (!config.allow_headers.empty()) {
        response.set(http::field::access_control_allow_headers, config.allow_headers);
    }

    // Access-Control-Expose-Headers
    if (!config.expose_headers.empty()) {
        response.set(http::field::access_control_expose_headers, config.expose_headers);
    }

    // Access-Control-Max-Age (预检请求缓存时间)
    if (!config.max_age.empty()) {
        response.set(http::field::access_control_max_age, config.max_age);
    }

    // Access-Control-Allow-Credentials
    if (config.allow_credentials) {
        response.set(http::field::access_control_allow_credentials, "true");
    }
}

// ============================================================================
// Router 实现
// ============================================================================

void Router::registerHandler(const std::string& method, const std::string& path,
                             HandlerFunc handler) {
    m_routes.emplace_back(RouteKey{method, path}, std::move(handler));
}

Router::HandlerFunc Router::findHandler(const std::string& method, const std::string& path) {
    // 从 path 中提取路径部分（去掉查询参数）
    // 例如：/api/download?file=xxx -> /api/download
    std::string_view path_view(path);
    auto query_pos = path_view.find('?');
    std::string_view path_only =
      (query_pos != std::string_view::npos) ? path_view.substr(0, query_pos) : path_view;

    // 精确匹配优先（线性搜索，路由数量有限时性能优于哈希表）
    for (const auto& [key, handler] : m_routes) {
        if (key.method == method && key.path == path_only) {
            return handler;
        }
    }

    // 通配符路由匹配（简单的前缀匹配）
    // 注意：通配符路由应该在注册时放在 vector 后部，避免每次都要遍历
    for (const auto& [key, handler] : m_routes) {
        if (key.method == method && !key.path.empty() && key.path.back() == '*') {
            // 前缀匹配：/api/* 匹配 /api/users
            std::string_view prefix(key.path.data(), key.path.size() - 1);
            if (path_only.find(prefix) == 0) {
                return handler;
            }
        }
    }

    return nullptr;
}

// ============================================================================
// WebSocketRouter 实现
// ============================================================================

void WebSocketRouter::registerHandler(const std::string& path, HandleFactory factory) {
    m_routes.emplace_back(path, std::move(factory));
}

WebSocketRouter::HandleFactory WebSocketRouter::findHandler(const std::string& path) {
    // 精确匹配优先（线性搜索）
    for (const auto& [route_path, factory] : m_routes) {
        if (route_path == path) {
            return factory;
        }
    }

    return nullptr;
}

// ============================================================================
// WebSocketConnection 实现 - WebSocket 连接处理器
// ============================================================================

std::shared_ptr<WebSocketConnection> WebSocketConnection::create(
  tcp::socket&& socket, WebSocketRouter* ws_router, net::io_context& io_ctx, ssl::context* ssl_ctx,
  const http::request<http::string_body>* existing_req) {
    if (ssl_ctx) {
        return std::shared_ptr<WebSocketConnection>(
          new WebSocketConnection(std::move(socket), ws_router, io_ctx, ssl_ctx, existing_req));
    } else {
        return std::shared_ptr<WebSocketConnection>(
          new WebSocketConnection(std::move(socket), ws_router, io_ctx, nullptr, existing_req));
    }
}

WebSocketConnection::WebSocketConnection(tcp::socket&& socket, WebSocketRouter* ws_router,
                                         net::io_context& io_ctx, ssl::context* ssl_ctx,
                                         const http::request<http::string_body>* existing_req)
: m_socket(std::move(socket)),
  m_ws_router(ws_router),
  m_io_ctx(io_ctx),
  m_connection_start(std::chrono::steady_clock::now()) {
    // 如果提供了 SSL 上下文，初始化 SSL 流
    if (ssl_ctx) {
        m_ssl_stream = std::make_unique<ssl::stream<tcp::socket&>>(m_socket, *ssl_ctx);
    }

    // 注意：不在构造函数中获取许可，避免阻塞 IO 线程
    // 许可将在 readLoop 协程中异步获取（支持等待队列）

    // 保存已有的 HTTP 请求（如果有），用于 WebSocket 升级
    if (existing_req) {
        m_existing_req = *existing_req;
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

WebSocketConnection::~WebSocketConnection() {
    // 设置停止标志，确保心跳协程退出
    m_ping_stopped.store(true);

    // 【关键】取消并重置定时器，从 io_context 中移除
    if (m_ws_ctx) {
        m_ws_ctx->timer.cancel();
        m_ws_ctx->timer = decltype(m_ws_ctx->timer)(m_ws_ctx->timer.get_executor());
    }

    // ========== 释放 WebSocket 连接许可 ==========
    // 通过 WebSocketConnectionManager 来释放
    auto ws_conn_mgr = HttpServer::get_websocket_connection_manager();
    if (ws_conn_mgr && m_ws_permit) {
        ws_conn_mgr->release(m_ws_permit.getId());
    }
}

void WebSocketConnection::start() {
    auto self = shared_from_this();
    net::co_spawn(m_socket.get_executor(), readLoop(self), net::detached);
}

net::awaitable<bool> WebSocketConnection::sslHandshake() {
    if (!m_ssl_stream) {
        co_return true;  // 非 SSL 连接直接成功
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

net::awaitable<bool> WebSocketConnection::websocketHandshake(
  const http::request<http::string_body>& req) {
    try {
        // 创建 WebSocket stream
        if (m_ssl_stream) {
            m_ws_stream =
              std::make_unique<websocket::stream<tcp::socket&>>(m_ssl_stream->next_layer());
        } else {
            m_ws_stream = std::make_unique<websocket::stream<tcp::socket&>>(m_socket);
        }

        // 设置 WebSocket 选项
        m_ws_stream->set_option(
          websocket::stream_base::decorator([](websocket::response_type& res) {
              res.set(http::field::server, "Hikyuu-WebSocketServer");
          }));

        // 执行 WebSocket 握手
        co_await m_ws_stream->async_accept(req, net::use_awaitable);

        HKU_DEBUG("WebSocket handshake successful for client {}: {}", m_client_ip, m_client_port);
        co_return true;

    } catch (const beast::system_error& e) {
        HKU_ERROR("WebSocket handshake failed: {}", e.what());
        co_return false;
    } catch (const std::exception& e) {
        HKU_ERROR("WebSocket handshake exception: {}", e.what());
        co_return false;
    }
}

net::awaitable<void> WebSocketConnection::readLoop(std::shared_ptr<WebSocketConnection> self) {
    try {
        // SSL 握手（如果是 WSS）
        if (m_ssl_stream) {
            bool success = co_await sslHandshake();
            if (!success) {
                close();
                co_return;
            }
        }

        // 如果没有已有的 HTTP 请求，需要重新读取
        const http::request<http::string_body>* req_ptr = nullptr;

        if (m_existing_req.method() != http::verb::unknown) {
            // 使用已有的请求
            req_ptr = &m_existing_req;
            HKU_DEBUG("Using existing HTTP request for WebSocket upgrade");
        } else {
            // 需要重新读取 HTTP 请求
            beast::flat_buffer buffer;
            http::request_parser<http::string_body> parser;
            parser.body_limit(HttpConfig::MAX_BODY_SIZE);
            parser.header_limit(HttpConfig::MAX_HEADER_SIZE);

            // 异步读取请求
            if (m_ssl_stream) {
                co_await http::async_read(*m_ssl_stream, buffer, parser, net::use_awaitable);
            } else {
                co_await http::async_read(m_socket, buffer, parser, net::use_awaitable);
            }

            m_existing_req = parser.release();
            req_ptr = &m_existing_req;
        }

        auto& req = *req_ptr;

        // 验证 WebSocket 升级请求
        if (req.method() != http::verb::get || req.find(http::field::upgrade) == req.end() ||
            !beast::iequals(req[http::field::upgrade], "websocket")) {
            HKU_ERROR("Invalid WebSocket upgrade request from {}:{}", m_client_ip, m_client_port);
            close();
            co_return;
        }

        // WebSocket 握手
        bool handshake_success = co_await websocketHandshake(req);
        if (!handshake_success) {
            close();
            co_return;
        }

        // 创建 WebSocketContext
        m_ws_ctx = std::make_shared<WebSocketContext>(m_io_ctx);
        m_ws_ctx->client_ip = m_client_ip;
        m_ws_ctx->client_port = m_client_port;

        // 设置发送和关闭的回调函数（这样 Handle 就可以通过 Context 发送消息了）
        auto weak_self = weak_from_this();
        m_ws_ctx->send_callback = [weak_self](std::string_view msg,
                                              bool is_text) -> net::awaitable<bool> {
            HKU_DEBUG("WebSocketContext send_callback called (is_text={})", is_text);
            if (auto self = weak_self.lock()) {
                HKU_DEBUG("Sending message via WebSocketConnection::send()");
                co_return co_await self->send(msg, is_text);
            }
            HKU_WARN("WebSocketContext send_callback: weak_self.lock() failed");
            co_return false;
        };

        m_ws_ctx->close_callback = [weak_self](ws::close_code code,
                                               std::string_view reason) -> net::awaitable<void> {
            if (auto self = weak_self.lock()) {
                co_await self->closeWebSocket(code, reason);
            }
            co_return;
        };

        // 配置 WebSocket 安全选项（使用 public 静态方法）
        configureWebSocketSecurity(*m_ws_stream);

        // 查找对应的 WebSocket Handle
        std::string_view target_view(req.target());
        auto query_pos = target_view.find('?');
        std::string path = (query_pos != std::string_view::npos)
                             ? std::string(target_view.substr(0, query_pos))
                             : std::string(target_view);

        auto handler = m_ws_router->findHandler(path);

        if (!handler) {
            HKU_TRACE("WebSocket handler not found for path: {}", path);
            // 发送 404 响应并关闭
            m_ws_stream->close(websocket::close_code::policy_error);
            close();
            co_return;
        }

        // ========== WebSocket Handle 集成方案 ==========
        // 保存工厂函数以便在消息循环中使用
        auto ws_handle_factory = handler;

        HKU_DEBUG("Starting WebSocket message loop for path: {}", path);

        // ========== 在连接建立时立即创建 Handle 并调用 onOpen ==========
        std::shared_ptr<WebSocketHandle> active_handle;
        try {
            // 使用工厂函数创建 Handle 实例
            active_handle = ws_handle_factory(m_ws_ctx.get());

            if (!active_handle) {
                HKU_ERROR("Failed to create WebSocket Handle for path: {}", path);
                close();
                co_return;
            }

            // 调用 onOpen() 发送欢迎消息
            co_await active_handle->onOpen();

            HKU_DEBUG("Handle initialized and onOpen() called for path: {}", path);

        } catch (const std::exception& e) {
            HKU_ERROR("Failed to initialize Handle: {}", e.what());
            close();
            co_return;
        }

        // 【新增】启动心跳保活机制（按 PING_INTERVAL 周期性发送 Ping）
        net::co_spawn(m_io_ctx, sendPing(), net::detached);

        // WebSocket 消息读取循环
        while (true) {
            try {
                // 设置读取超时定时器
                m_ws_ctx->timer.expires_after(WebSocketConfig::READ_TIMEOUT);

                // 启动超时定时器
                auto weak_self = weak_from_this();
                m_ws_ctx->timer.async_wait([weak_self](beast::error_code ec) {
                    if (!ec || ec == boost::asio::error::operation_aborted) {
                        if (auto self = weak_self.lock()) {
                            // 取消正在进行的异步操作
                            self->m_socket.cancel();
                        }
                    }
                });

                // 读取 WebSocket 消息
                m_ws_ctx->buffer.consume(m_ws_ctx->buffer.size());
                co_await m_ws_stream->async_read(m_ws_ctx->buffer, net::use_awaitable);

                // 读取成功，取消定时器
                m_ws_ctx->timer.cancel();

                // 获取消息内容
                auto data = m_ws_ctx->buffer.data();
                std::string message(static_cast<const char*>(data.data()), data.size());
                m_ws_ctx->buffer.consume(data.size());

                // 判断消息类型
                bool is_text = m_ws_stream->got_text();

                // ========== 增强的 WebSocket 消息验证 ==========
                // 在传递给业务处理前进行严格验证
                if (!validateWebSocketMessage(message, is_text)) {
                    HKU_WARN("Invalid WebSocket message rejected from {}:{}", m_client_ip,
                             m_client_port);
                    continue;  // 跳过此消息，不传递给业务处理
                }

                // ========== 调用用户 Handle 的 onMessage ==========
                if (active_handle) {
                    co_await active_handle->onMessage(message, is_text);
                }

            } catch (const beast::system_error& e) {
                if (e.code() == websocket::error::closed || e.code() == net::error::eof ||
                    e.code() == beast::errc::connection_reset) {
                    HKU_DEBUG("Client disconnected: {}:{} - {}", m_client_ip, m_client_port,
                              e.code().message());
                } else {
                    HKU_ERROR("WebSocket read error: {}", e.what());
                }
                break;
            } catch (const std::exception& e) {
                HKU_ERROR("WebSocket exception: {}", e.what());
                // 注意：不能在 catch 块中使用 co_await
                // bridge->onError(beast::errc::make_error_code(beast::errc::connection_aborted),
                // e.what());
                break;
            }
        }

        // ========== 连接关闭，调用 Handle 的 onClose ==========
        if (active_handle) {
            try {
                auto close_reason = m_ws_stream->reason();
                co_await active_handle->onClose(static_cast<ws::close_code>(close_reason.code),
                                                close_reason.reason);

                HKU_DEBUG("WebSocket connection closed: code={}, reason={}",
                          static_cast<int>(close_reason.code), close_reason.reason);
            } catch (const std::exception& e) {
                HKU_ERROR("onClose exception: {}", e.what());
            }
        }

    } catch (const beast::system_error& e) {
        if (e.code() == http::error::end_of_stream || e.code() == net::error::eof) {
            HKU_DEBUG("Client disconnected during WebSocket setup: {}:{}", m_client_ip,
                      m_client_port);
        } else {
            HKU_ERROR("WebSocket connection error: {}", e.what());
        }
    } catch (const std::exception& e) {
        HKU_ERROR("WebSocket exception in readLoop: {}", e.what());
    }

    close();
}

net::awaitable<void> WebSocketConnection::sendPing() {
    if (!m_ws_stream || !m_ws_stream->is_open()) {
        co_return;
    }

    try {
        while (m_ws_stream && m_ws_stream->is_open() && !m_ping_stopped.load()) {
            // 等待 PING_INTERVAL，但使用可取消的定时器
            net::steady_timer timer(m_io_ctx);
            timer.expires_after(WebSocketConfig::PING_INTERVAL);

            // 使用可取消的等待，这样当服务器停止时能快速退出
            try {
                co_await timer.async_wait(net::use_awaitable);
            } catch (const boost::system::system_error& e) {
                // 定时器被取消是正常现象，直接退出
                if (e.code() == boost::asio::error::operation_aborted) {
                    HKU_DEBUG("Ping timer cancelled, stopping ping loop");
                    break;
                }
                throw;  // 重新抛出其他异常
            }

            // 检查停止标志和连接状态
            if (!m_ws_stream || !m_ws_stream->is_open() || m_ping_stopped.load()) {
                break;
            }

            // 取消之前的定时器（如果有）
            if (m_ws_ctx->timer.expiry() != net::steady_timer::time_point()) {
                m_ws_ctx->timer.cancel();
            }

            // 设置超时定时器
            m_ws_ctx->timer.expires_after(WebSocketConfig::PING_TIMEOUT);

            auto weak_self = weak_from_this();

            // 异步发送 Ping（使用空 payload）
            auto ping_sent = std::make_shared<bool>(false);
            websocket::ping_data ping_payload;
            m_ws_stream->async_ping(ping_payload,
                                    [ping_sent](beast::error_code ec) { *ping_sent = !ec; });

            // 等待 Ping 完成或超时
            try {
                co_await m_ws_ctx->timer.async_wait(net::use_awaitable);
            } catch (const boost::system::system_error& e) {
                if (e.code() == boost::asio::error::operation_aborted) {
                    // 定时器被取消，继续下一次循环
                    continue;
                }
                throw;
            }

            if (!*ping_sent) {
                HKU_WARN("Ping failed, closing connection: {}:{}", m_client_ip, m_client_port);
                close();
                co_return;
            }
        }
    } catch (const std::exception& e) {
        HKU_ERROR("Ping loop exception: {}", e.what());
    }

    HKU_DEBUG("Ping loop stopped for client {}:{}", m_client_ip, m_client_port);
    co_return;
}

/**
 * @brief WebSocket 消息验证 - 在传递给业务处理前进行严格验证
 *
 * 根据"WebSocket 消息输入验证规范"实施以下检查:
 * 1. 检查消息非空
 * 2. 验证文本消息的 UTF-8 编码合法性
 * 3. 限制单条消息最大长度（复用 MAX_BODY_SIZE 配置）
 * 4. 过滤特殊控制字符
 * 5. 对二进制消息进行基本格式校验
 *
 * @param message 待验证的消息内容
 * @param is_text 是否为文本消息
 * @return true 验证通过，false 验证失败
 */
bool WebSocketConnection::validateWebSocketMessage(const std::string& message, bool is_text) {
    // 1. 检查消息非空
    if (message.empty()) {
        HKU_DEBUG("Rejected empty WebSocket message");
        return false;
    }

    // 2. ========== MAX_FRAME_SIZE 帧大小检查 ==========
    // 单帧不应超过配置的最大帧大小
    if (message.size() > WebSocketConfig::MAX_FRAME_SIZE) {
        HKU_WARN("Rejected oversized WebSocket frame: {} bytes (max: {})", message.size(),
                 WebSocketConfig::MAX_FRAME_SIZE);
        return false;
    }

    // 3. 检查消息长度限制（使用 WebSocketConfig::MAX_MESSAGE_SIZE）
    if (message.size() > WebSocketConfig::MAX_MESSAGE_SIZE) {
        HKU_WARN("Rejected oversized WebSocket message: {} bytes (max: {})", message.size(),
                 WebSocketConfig::MAX_MESSAGE_SIZE);
        return false;
    }

    // 4. 文本消息的 UTF-8 编码验证
    if (is_text) {
        try {
            // 增强的 UTF-8 验证逻辑
            size_t i = 0;
            while (i < message.size()) {
                unsigned char c = static_cast<unsigned char>(message[i]);

                // 检查 UTF-8 序列长度
                size_t seq_len = 0;
                if (c < 0x80) {
                    // 单字节 ASCII (0x00-0x7F)
                    seq_len = 1;
                } else if ((c & 0xE0) == 0xC0) {
                    // 2 字节序列 (110xxxxx)
                    seq_len = 2;
                } else if ((c & 0xF0) == 0xE0) {
                    // 3 字节序列 (1110xxxx)
                    seq_len = 3;
                } else if ((c & 0xF8) == 0xF0) {
                    // 4 字节序列 (11110xxx)
                    seq_len = 4;
                } else {
                    // 非法起始字节
                    HKU_DEBUG("Invalid UTF-8 start byte at position {}: 0x{:02x}", i,
                              static_cast<int>(c));
                    return false;
                }

                // 检查是否有足够的字节
                if (i + seq_len > message.size()) {
                    HKU_DEBUG("Incomplete UTF-8 sequence at position {}", i);
                    return false;
                }

                // 检查延续字节
                for (size_t j = 1; j < seq_len; ++j) {
                    unsigned char cont = static_cast<unsigned char>(message[i + j]);
                    if ((cont & 0xC0) != 0x80) {
                        HKU_DEBUG("Invalid UTF-8 continuation byte at position {}: 0x{:02x}", i + j,
                                  static_cast<int>(cont));
                        return false;
                    }
                }

                // 检查过长的编码（overlong encoding）
                if (seq_len == 2 && c < 0xC2) {
                    HKU_DEBUG("Overlong UTF-8 encoding at position {}", i);
                    return false;
                }

                // 检查 Unicode 码点范围
                if (seq_len == 4) {
                    // 4字节序列的最高位不应超过 0x10FFFF（Unicode最大码点）
                    unsigned int codepoint =
                      ((c & 0x07) << 18) |
                      ((static_cast<unsigned char>(message[i + 1]) & 0x3F) << 12) |
                      ((static_cast<unsigned char>(message[i + 2]) & 0x3F) << 6) |
                      (static_cast<unsigned char>(message[i + 3]) & 0x3F);

                    if (codepoint > 0x10FFFF) {
                        HKU_DEBUG("UTF-8 codepoint out of range: 0x{:x}", codepoint);
                        return false;
                    }

                    // 检查代理对（surrogate pairs）
                    if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
                        HKU_DEBUG("UTF-8 surrogate pair detected at position {}", i);
                        return false;
                    }
                }

                i += seq_len;
            }

            // 额外检查：过滤特殊控制字符（除了 \n, \r, \t 外）
            for (char c : message) {
                unsigned char uc = static_cast<unsigned char>(c);
                // 允许的控制字符：\n (0x0A), \r (0x0D), \t (0x09)
                // 其他控制字符（0x00-0x08, 0x0B, 0x0C, 0x0E-0x1F, 0x7F）被过滤
                if ((uc < 0x20 && uc != 0x09 && uc != 0x0A && uc != 0x0D) || uc == 0x7F) {
                    HKU_DEBUG("Rejected message containing control character: 0x{:02x}",
                              static_cast<int>(uc));
                    return false;
                }
            }

        } catch (const std::exception& e) {
            HKU_ERROR("UTF-8 validation exception: {}", e.what());
            return false;
        }
    }

    // 4. 二进制消息的基本格式校验
    if (!is_text) {
        // 当前仅做基础检查，未来可根据具体协议扩展
        // 例如：如果是 JSON 二进制数据，可以尝试解析验证

        // 检查是否包含明显的非法二进制数据（可选）
        // 这里暂时不做特殊处理，保持灵活性
    }

    // 所有验证通过
    HKU_DEBUG("WebSocket message validation passed: type={}, size={}", is_text ? "text" : "binary",
              message.size());
    return true;
}

void WebSocketConnection::configureWebSocketSecurity(websocket::stream<tcp::socket&>& ws) {
    // 设置消息最大大小 (防止攻击者通过超大消息消耗内存)
    ws.read_message_max(WebSocketConfig::MAX_MESSAGE_SIZE);

    // 注意：Boost.Beast 没有 write_message_max，只有 read_message_max
    // 写入大小由应用层控制

    // ========== MAX_FRAME_SIZE 帧大小限制 ==========
    // 通过 control_callback 监控控制帧的大小（Ping/Pong/Close）
    ws.control_callback([max_frame_size = WebSocketConfig::MAX_FRAME_SIZE](
                          websocket::frame_type kind, beast::string_view payload) {
        if (payload.size() > max_frame_size) {
            HKU_WARN("Control frame too large: {} bytes (max: {}), closing connection",
                     payload.size(), max_frame_size);
            // 注意：control_callback 中不能直接关闭连接，仅记录日志
        }
    });

    // 禁用自动 Fragmentation，由应用层控制分片策略
    ws.auto_fragment(false);

    HKU_DEBUG(
      "WebSocket security configured: max_message_size={}, max_frame_size={}, auto_fragment=false",
      WebSocketConfig::MAX_MESSAGE_SIZE, WebSocketConfig::MAX_FRAME_SIZE);
}

net::awaitable<bool> WebSocketConnection::send(std::string_view message, bool is_text) {
    if (!m_ws_stream || !m_ws_stream->is_open()) {
        co_return false;
    }

    // 检查写队列大小限制（防止高频推送阻塞）
    if (m_write_queue_size.load() >= WebSocketConfig::MAX_WRITE_QUEUE_SIZE) {
        HKU_WARN("Write queue full ({} >= {}), rejecting message", m_write_queue_size.load(),
                 WebSocketConfig::MAX_WRITE_QUEUE_SIZE);
        co_return false;
    }

    // 增加队列计数
    m_write_queue_size.fetch_add(1);

    try {
        // 设置写入超时定时器
        m_ws_ctx->timer.expires_after(WebSocketConfig::WRITE_TIMEOUT);

        // 启动超时定时器
        auto weak_self = weak_from_this();
        m_ws_ctx->timer.async_wait([weak_self](beast::error_code ec) {
            if (!ec || ec == boost::asio::error::operation_aborted) {
                if (auto self = weak_self.lock()) {
                    // 取消正在进行的异步操作
                    self->m_socket.cancel();
                }
            }
        });

        // 异步发送消息（使用 buffer）
        co_await m_ws_stream->async_write(net::buffer(message), net::use_awaitable);

        // 发送成功，取消定时器
        m_ws_ctx->timer.cancel();

        // 减少队列计数
        m_write_queue_size.fetch_sub(1);

        co_return true;
    } catch (const beast::system_error& e) {
        HKU_ERROR("WebSocket send error: {}", e.what());
        // 减少队列计数（异常情况下也要释放）
        m_write_queue_size.fetch_sub(1);
        co_return false;
    }
}

net::awaitable<void> WebSocketConnection::closeWebSocket(ws::close_code code,
                                                         std::string_view reason) {
    if (!m_ws_stream || !m_ws_stream->is_open()) {
        co_return;
    }

    try {
        // 异步关闭 WebSocket（构造 close_reason）
        websocket::close_reason cr(code, std::string(reason));
        co_await m_ws_stream->async_close(cr, net::use_awaitable);
    } catch (const beast::system_error& e) {
        HKU_ERROR("WebSocket close error: {}", e.what());
    }
}

net::awaitable<void> WebSocketConnection::handleWebSocketMessage(
  std::shared_ptr<WebSocketHandle> ws_handle, std::string_view message, bool is_text) {
    if (!ws_handle) {
        co_return;
    }

    try {
        co_await ws_handle->onMessage(message, is_text);
    } catch (const std::exception& e) {
        HKU_ERROR("handleWebSocketMessage exception: {}", e.what());
        // 注意：不能在 catch 块中使用 co_await
        // ws_handle->onError(beast::errc::make_error_code(beast::errc::connection_aborted),
        // e.what());
    }
}

void WebSocketConnection::close() {
    // 设置停止标志，确保心跳协程退出
    m_ping_stopped.store(true);

    // 取消所有定时器
    if (m_ws_ctx) {
        m_ws_ctx->timer.cancel();
    }

    beast::error_code ec;

    if (m_ws_stream && m_ws_stream->is_open()) {
        // 关闭 WebSocket stream
        try {
            m_ws_stream->async_close(websocket::close_code::normal, [](beast::error_code close_ec) {
                // 异步关闭完成，忽略错误
            });
        } catch (...) {
            // 忽略关闭异常
        }
    }

    if (m_ssl_stream) {
        // SSL shutdown
        try {
            m_ssl_stream->next_layer().shutdown(tcp::socket::shutdown_send, ec);
        } catch (...) {
            // 忽略异常
        }
    } else {
        // TCP shutdown
        m_socket.shutdown(tcp::socket::shutdown_send, ec);
    }
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
  m_connection_start(std::chrono::steady_clock::now()),
  m_permit() {  // 初始化为无效许可
    // 如果提供了 SSL 上下文，初始化 SSL 流
    if (ssl_ctx) {
        m_ssl_stream = std::make_unique<ssl::stream<tcp::socket&>>(m_socket, *ssl_ctx);
    }

    // 注意：不在构造函数中获取许可，避免阻塞 IO 线程
    // 许可将在 readLoop 协程中异步获取（支持等待队列）

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
    // ========== 智能连接管理：释放连接许可（RAII） ==========
    if (m_permit && HttpServer::ms_connection_manager) {
        try {
            HttpServer::ms_connection_manager->release(m_permit.getId());
            HKU_DEBUG("Connection {} released", m_permit.getId());
        } catch (const std::exception& e) {
            HKU_WARN("Failed to release permit {}: {}", m_permit.getId(), e.what());
        }
    }
}

void Connection::start() {
    // HKU_TRACE("Connection::start: m_router={}, this={}", (void*)m_router, (void*)this);
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

        // ========== 智能连接管理：异步获取连接许可 ==========
        // 使用协程版本的 acquire()，支持等待队列和超时控制
        // 如果达到最大并发数，会进入 FIFO 等待队列而不是直接拒绝
        m_permit = co_await HttpServer::ms_connection_manager->acquire();

        if (!m_permit) {
            // 等待超时，返回无效许可
            HKU_WARN("Connection acquire timeout from {}:{}", m_client_ip, m_client_port);
            co_return;
        }

        HKU_DEBUG("Connection {} acquired: active={}, waiting={}", m_permit.getId(),
                  HttpServer::get_active_connections(),
                  HttpServer::ms_connection_manager->getWaitingCount());

        while (true) {
            // 检查连接最大存活时间
            auto elapsed = std::chrono::steady_clock::now() - m_connection_start;
            if (elapsed > HttpConfig::MAX_CONNECTION_AGE) {
                HKU_TRACE("Connection age limit reached, closing from {}:{}", m_client_ip,
                          m_client_port);
                break;
            }

            // ========== P99 延迟优化：复用 BeastContext 对象 ==========
            // 重要说明：每个 Connection 对象独立维护自己的 m_session，不会跨连接共享
            // 复用目的：避免在 Keep-Alive 连接的多次请求间频繁创建/销毁对象
            // 安全性保障：协程内的操作是同步顺序执行，不存在并发修改风险
            if (!m_session) {
                // 第一次请求时创建 session
                auto& socket_ref = m_ssl_stream ? m_ssl_stream->next_layer() : m_socket;
                m_session = std::make_shared<BeastContext>(socket_ref, m_io_ctx);

                // ========== MAX_BUFFER_SIZE 限制 ==========
                // 设置缓冲区最大大小，防止内存耗尽攻击
                m_session->buffer.max_size(HttpConfig::MAX_BUFFER_SIZE);
                HKU_DEBUG("Connection buffer max_size set to {} bytes",
                          HttpConfig::MAX_BUFFER_SIZE);
            }

            // 复用已有的 session 对象
            auto session = m_session;
            session->client_ip = m_client_ip;
            session->client_port = m_client_port;

            // ========== 关键：完整重置 session 状态 ==========
            // 注意：必须在每次循环开始时重置，确保上一次请求的状态不会影响新请求
            // 1. 重置 request/response 对象（使用 move 语义）
            session->req = http::request<http::string_body>();
            session->res = http::response<http::string_body>();

            // 2. 清空 buffer，但保留容量避免重新分配
            session->buffer.consume(session->buffer.size());

            // 3. 重置 parser 状态（重要！）
            if (!session->parser.has_value()) {
                // 第一次请求时创建并配置 parser
                session->parser.emplace();
                session->parser->body_limit(HttpConfig::MAX_BODY_SIZE);
                session->parser->header_limit(HttpConfig::MAX_HEADER_SIZE);
            } else {
                // 复用已有 parser，重新构造一个空对象
                session->parser.emplace();
            }

            // 4. 重置其他状态字段
            session->keep_alive = true;

            // ========== P99 延迟优化：简化定时器处理 ==========
            // 直接设置新的超时时间，不需要先 cancel（Boost.Asio 会自动处理）
            session->timer.expires_after(HttpConfig::HEADER_TIMEOUT);

            // 启动超时定时器，到期时主动取消异步操作
            // 使用 weak_ptr 防止定时器回调访问已销毁的 session
            auto weak_session = std::weak_ptr<BeastContext>(session);
            session->timer.async_wait([weak_sess = std::move(weak_session)](beast::error_code ec) {
                if (!ec || ec == boost::asio::error::operation_aborted) {
                    // 尝试锁定 shared_ptr
                    if (auto sess = weak_sess.lock()) {
                        // 主动取消正在进行的异步操作
                        sess->cancel_signal.emit(net::cancellation_type::all);
                    }
                }
            });

            // 异步读取 HTTP 请求（根据 m_ssl_stream 是否为 nullptr 选择不同流）
            // 使用复用的 parser 成员变量而不是创建新的 parser
            try {
                if (m_ssl_stream) {
                    co_await http::async_read(*m_ssl_stream, session->buffer, *session->parser,
                                              net::use_awaitable);
                } else {
                    co_await http::async_read(session->socket, session->buffer, *session->parser,
                                              net::use_awaitable);
                }
            } catch (const beast::system_error& e) {
                // 读取失败时也要取消定时器，防止定时器回调访问已销毁的对象
                session->timer.cancel();

                // 判断是否为正常断开
                if (e.code() == http::error::end_of_stream || e.code() == net::error::eof ||
                    e.code() == beast::errc::connection_reset) {
                    HKU_DEBUG("Client disconnected during read: {}", e.code().message());
                } else {
                    HKU_ERROR("Read error: {}", e.what());
                }
                throw;  // 重新抛出异常，让外层处理
            }

            // 取消定时器（读取成功）
            session->timer.cancel();

            // 将解析后的请求移动到 session 中
            session->req = session->parser->release();

            // ========== P99 延迟优化：ENABLE_FAST_PATH 快速路径 ==========
            // 如果启用快速路径，对简单 GET 请求跳过部分安全检查
            if (HttpConfig::ENABLE_FAST_PATH && session->req.method() == http::verb::get &&
                session->req.target().size() < 256 &&  // 简单请求：URL 较短
                session->req.body().empty()) {         // 无请求体

                HKU_DEBUG("Fast path enabled for simple GET request: {}", session->req.target());
                // 跳过额外的安全验证，直接进入处理流程
            }

            // ========== 增强的 HTTP/2 DoS 防护 ==========
            // 检查是否为 HTTP/2 连接尝试（HTTP/2 Connection Preface）
            // HTTP/2 客户端会先发送 "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
            // 增强检测：不仅检查方法，还要验证目标和版本号
            if (session->req.method_string() == "PRI") {
                HKU_DEBUG("HTTP/2 connection attempt detected on HTTP/1.1 server");

                // 进一步验证：检查目标是否为 "*" 且版本是否为 "HTTP/2.0"
                bool is_http2_preamble =
                  session->req.target() == "*" &&
                  session->req.version() == 2000;  // HTTP/2.0 的版本号为 2000

                if (!is_http2_preamble) {
                    // 可能是畸形的 HTTP/2 预检或其他异常请求
                    HKU_WARN("Malformed HTTP/2 preamble or suspicious request from {}:{}",
                             m_client_ip, m_client_port);
                }

                // 拒绝 HTTP/2 连接，返回 400 Bad Request
                session->res.result(http::status::bad_request);
                session->res.set(http::field::content_type, "text/plain");
                session->res.body() = "This server only supports HTTP/1.1";
                session->res.prepare_payload();
                co_await writeResponse(session);
                break;
            }

            // ========== WebSocket 协议检测 ==========
            // 检查是否为 WebSocket 升级请求
            // 根据 RFC 6455，必须同时满足以下条件：
            // 1. HttpServer 已启用 WebSocket 支持
            // 2. GET 方法
            // 3. Upgrade: websocket
            // 4. Connection: Upgrade
            // 5. Sec-WebSocket-Key (握手密钥)
            // 6. Sec-WebSocket-Version: 13
            bool is_websocket =
              HttpServer::ms_websocket_enabled &&  // 检查 WebSocket 功能是否已启用
              session->req.method() == http::verb::get &&
              session->req.find(http::field::upgrade) != session->req.end() &&
              beast::iequals(session->req[http::field::upgrade], "websocket") &&
              session->req.find(http::field::connection) != session->req.end() &&
              beast::iequals(session->req[http::field::connection], "Upgrade") &&
              !session->req["Sec-WebSocket-Key"].empty() &&
              session->req["Sec-WebSocket-Version"] == "13";

            if (is_websocket) {
                HKU_DEBUG("WebSocket upgrade request detected from {}:{}", m_client_ip,
                          m_client_port);

                // P99 优化：重新获取 socket 引用用于 WebSocket 升级
                auto& socket_ref = m_ssl_stream ? m_ssl_stream->next_layer() : m_socket;

                // 创建 WebSocket 连接处理器，并传递已读取的 HTTP 请求
                auto ws_connection = WebSocketConnection::create(
                  std::move(socket_ref), &HttpServer::ms_ws_router, m_io_ctx,
                  m_ssl_stream ? HttpServer::ms_ssl_context : nullptr,
                  &session->req);  // 传递已读取的请求

                ws_connection->start();

                // WebSocket 连接已接管，退出当前 HTTP 循环
                // 注意：不需要手动减少连接计数，Connection 析构时会处理
                break;
            }

            // 请求读取完成后，处理该请求（调用 Handle）
            co_await processHandle(session);

            // 写入响应（如果响应还未发送）
            if (!session->response_sent) {
                co_await writeResponse(session);
            } else {
                HKU_ERROR("Skipping writeResponse: response already sent");
            }

            // ========== 检查是否需要保持连接 ==========
            // 递增请求计数器
            ++m_request_count;

            // 检查 Keep-Alive 请求数限制（服务器端策略限制）
            if (HttpConfig::MAX_KEEPALIVE_REQUESTS > 0 &&
                m_request_count >= HttpConfig::MAX_KEEPALIVE_REQUESTS) {
                HKU_INFO(
                  "Keep-Alive limit reached ({} requests, age={}s), closing connection from {}:{}",
                  m_request_count,
                  std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - m_connection_start)
                    .count(),
                  m_client_ip, m_client_port);
                break;
            }

            // 直接使用 Beast 的 keep_alive() 判断
            // 该方法会自动检查：
            // 1. HTTP/1.0 且无 Connection: keep-alive 头 -> false
            // 2. HTTP/1.1 且无 Connection: close 头 -> true
            // 3. 任何版本有 Connection: close 头 -> false
            if (!session->req.keep_alive()) {
                HKU_DEBUG(
                  "Closing connection (client requested close or HTTP version={}, method={})",
                  session->req.version(), session->req.method_string());
                break;
            }

            HKU_DEBUG("Keeping connection alive for next request (request #{}, client={}:{})",
                      m_request_count, m_client_ip, m_client_port);

            // ========== P99 延迟优化：缓冲区容量管理 ==========
            // 在 Keep-Alive 连接中，保留基础容量避免频繁分配
            // 但清空内容，释放多余内存
            if (session->buffer.capacity() > HttpConfig::BUFFER_MIN_CAPACITY) {
                session->buffer.consume(session->buffer.size());
                // 注意：boost::beast::flat_buffer 没有 shrink_to 方法
                // 这里仅清空内容，容量会在后续操作中自然调整
            }
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

    // 安全检查 router 是否为空
    if (!m_router) {
        HKU_ERROR("Router is null, cannot process request");
        context->res.result(http::status::internal_server_error);
        context->res.set(http::field::content_type, "application/json");
        context->res.body() = R"({"ret":500,"errmsg":"Internal Server Error"})";
        context->res.prepare_payload();

        // 设置响应头 (安全头 + CORS)
        setResponseHeaders(context->res, HttpServer::getCorsConfig());

        co_return;
    }

    // findHandler 内部会自动处理查询参数
    auto handler = m_router->findHandler(method, target);

    // 处理 CORS 预检请求 (OPTIONS)
    if (method == "OPTIONS" && HttpServer::getCorsConfig() &&
        HttpServer::getCorsConfig()->enabled) {
        HKU_DEBUG("Handling CORS preflight request for: {}", target);
        context->res.result(http::status::no_content);

        // 设置 CORS 响应头
        setResponseHeaders(context->res, HttpServer::getCorsConfig());

        context->res.prepare_payload();
        co_return;
    }

    if (!handler) {
        // 未找到路由，返回 404
        HKU_TRACE("not found: {}", target);
        context->res.result(http::status::not_found);
        context->res.set(http::field::content_type, "application/json");
        context->res.body() = R"({"ret":404, "errcode":404, "errmsg":"Not Found"})";
        context->res.prepare_payload();

        // 设置响应头 (安全头 + CORS)
        setResponseHeaders(context->res, HttpServer::getCorsConfig());

        co_return;
    }

    try {
        // ========== P99 延迟优化：简化定时器处理 ==========
        // 直接设置新的超时时间，不需要先 cancel（Boost.Asio 会自动处理）
        context->timer.expires_after(HttpConfig::READ_TIMEOUT);

        // 启动超时定时器，到期时主动取消异步操作
        // 使用 weak_ptr 防止定时器回调访问已销毁的 session
        auto weak_context = std::weak_ptr<BeastContext>(context);
        context->timer.async_wait([weak_ctx = std::move(weak_context)](beast::error_code ec) {
            if (!ec || ec == boost::asio::error::operation_aborted) {
                // 尝试锁定 shared_ptr
                if (auto ctx = weak_ctx.lock()) {
                    // 主动取消正在进行的异步操作
                    ctx->cancel_signal.emit(net::cancellation_type::all);
                }
            }
        });

        // 统一添加响应头 (安全头 + CORS，在 handler 执行前设置，确保所有响应都包含)
        setResponseHeaders(context->res, HttpServer::getCorsConfig());

        // 执行业务处理（绑定取消令牌到协程）
        // 注意：handler 内部需要通过 net::bind_cancellation 或检查 cancellation_state 来响应取消
        co_await handler(context.get());

        // 如果响应已经手动发送（如分块传输），则直接返回，不再执行后续操作
        if (context->response_sent) {
            co_return;
        }

        // 设置响应版本和 Keep-Alive
        context->res.version(context->req.version());
        context->res.keep_alive(true);  // 默认保持连接

    } catch (std::exception& e) {
        // 异常时也要取消定时器，防止定时器回调访问已销毁的对象
        context->timer.cancel();

        // 记录详细错误到日志（内部可见）
        HKU_ERROR("Handler exception: {}", e.what());

        // 返回通用错误消息给客户端（不泄露内部细节）
        context->res.result(http::status::internal_server_error);
        context->res.set(http::field::content_type, "application/json");
        context->res.body() = R"({"ret":500,"errmsg":"Internal Server Error"})";
        context->res.prepare_payload();

        // 异常时也要设置响应头（安全头 + CORS）
        setResponseHeaders(context->res, HttpServer::getCorsConfig());
    }
}

net::awaitable<void> Connection::writeResponse(std::shared_ptr<BeastContext> context) {
    try {
        // 如果响应已经手动发送（如分块传输），则跳过
        if (context->response_sent) {
            co_return;
        }

        // ========== P99 延迟优化：简化定时器处理 ==========
        // 复用已有的定时器，仅更新超时时间（Boost.Asio 会自动处理）
        context->timer.expires_after(HttpConfig::WRITE_TIMEOUT);

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
        // 写入失败时也要取消定时器，防止定时器回调访问已销毁的对象
        context->timer.cancel();
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

HttpServer::HttpServer(const char* host, uint16_t port) : m_host(host), m_port(port) {
    HKU_CHECK(ms_server == nullptr, "Can only one server!");
    ms_server = this;

    m_root_url = fmt::format("{}:{}", m_host, m_port);
}

HttpServer::~HttpServer() {}

void HttpServer::set_io_thread_count(size_t thread_count) {
    m_io_thread_count = thread_count;
}

void HttpServer::bind_io_context(net::io_context& io_ctx) {
    if (ms_io_context) {
        CLS_WARN("io_context already bound, ignoring bind_io_context call");
        return;
    }
    ms_io_context = &io_ctx;
    ms_use_external_io = true;
    CLS_INFO("Bound to external io_context at {}", static_cast<void*>(&io_ctx));
}

net::io_context* HttpServer::get_io_context() {
    return ms_io_context;
}

void HttpServer::setCors(const CorsConfig& config) {
    m_cors_config = config;
    HKU_INFO("CORS configured: enabled={}, origin={}, methods={}", config.enabled,
             config.allow_origin, config.allow_methods);
}

void HttpServer::configureSsl() {
    HKU_CHECK(!m_ssl_config.ca_key_file.empty(), "SSL CA file not specified");
    HKU_CHECK(existFile(m_ssl_config.ca_key_file), "Not exist ca file: {}",
              m_ssl_config.ca_key_file);

    // 检查证书文件权限（仅类 Unix 系统）
#if !HKU_OS_WINDOWS
    struct stat st;
    if (stat(m_ssl_config.ca_key_file.c_str(), &st) == 0) {
        // 检查文件权限是否为 600（-rw-------）或更严格
        mode_t perms = st.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);
        if (perms & (S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH)) {
            HKU_WARN(
              "Certificate file '{}' has insecure permissions: {:o}. "
              "Recommended: 600 (-rw-------)",
              m_ssl_config.ca_key_file, perms);
        }

        // 如果是私钥文件，检查应该更严格
        // 使用更严格的检查：检查文件内容是否为私钥
        bool is_private_key = false;
        FILE* fp = fopen(m_ssl_config.ca_key_file.c_str(), "r");
        if (fp) {
            char buffer[256];
            if (fgets(buffer, sizeof(buffer), fp)) {
                // 检查是否为私钥文件（PEM格式）
                std::string line(buffer);
                if (line.find("-----BEGIN PRIVATE KEY-----") != std::string::npos ||
                    line.find("-----BEGIN RSA PRIVATE KEY-----") != std::string::npos ||
                    line.find("-----BEGIN EC PRIVATE KEY-----") != std::string::npos) {
                    is_private_key = true;
                }
            }
            fclose(fp);
        }

        // 如果文件扩展名包含.key/.pem或文件内容确认为私钥，则执行严格权限检查
        if (is_private_key || m_ssl_config.ca_key_file.find(".key") != std::string::npos ||
            m_ssl_config.ca_key_file.find(".pem") != std::string::npos) {
            if (perms & (S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH)) {
                HKU_ERROR(
                  "Private key file '{}' has world/group readable permissions! "
                  "This is a security risk. Please set permissions to 600",
                  m_ssl_config.ca_key_file);
                throw std::runtime_error("Insecure private key file permissions");
            }
        }
    } else {
        HKU_WARN("Failed to stat certificate file: {}", m_ssl_config.ca_key_file);
    }
#else
    // Windows 系统：检查文件是否存在即可，Windows 使用 ACL 而非 POSIX 权限
    HKU_DEBUG("Running on Windows, skipping POSIX permission check for '{}'",
              m_ssl_config.ca_key_file);
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

    // 启用椭圆曲线密钥交换 (ECDH) - 自动选择最佳曲线
    // 现代 OpenSSL 版本会自动选择安全的椭圆曲线参数
#if OPENSSL_VERSION_NUMBER >= 0x10002000L
    // ECDH auto is deprecated in newer OpenSSL versions, but we keep it for compatibility
    // Return value is intentionally ignored as it's always successful on modern systems
    (void)SSL_CTX_set_ecdh_auto(ms_ssl_context->native_handle(), 1);
#endif

    // 加载证书和私钥
    ms_ssl_context->use_certificate_chain_file(m_ssl_config.ca_key_file);
    ms_ssl_context->use_private_key_file(m_ssl_config.ca_key_file, ssl::context::pem);

    // 如果有密码
    if (!m_ssl_config.password.empty()) {
        ms_ssl_context->set_password_callback(
          [pwd = m_ssl_config.password](
            std::size_t max_length, ssl::context_base::password_purpose purpose) { return pwd; });
    }

    // 设置客户端验证模式
    switch (m_ssl_config.verify_mode) {
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

    // ========== 增强的 ALPN 扩展校验（HTTP/2 DoS 防护） ==========
    // 禁用 HTTP/2 (ALPN)，强制使用 HTTP/1.1
    // 当前实现仅支持 HTTP/1.1，待后续集成 nghttp2 支持 HTTP/2
    SSL_CTX_set_alpn_select_cb(
      ms_ssl_context->native_handle(),
      [](SSL* ssl, const unsigned char** out, unsigned char* outlen, const unsigned char* in,
         unsigned int inlen, void* /*arg*/) -> int {
          // 遍历客户端提供的协议列表
          unsigned int i = 0;

          while (i < inlen) {
              unsigned char protocol_len = in[i];

              // ========== 增强的畸形 ALPN 检测 ==========
              // 安全检查：防止越界访问和畸形数据
              if (protocol_len == 0 || i + 1 + protocol_len > inlen) {
                  // 检测到畸形 ALPN 扩展，可能是 DoS 攻击
                  HKU_WARN("Malformed ALPN extension detected (length={}, position={}/{})",
                           protocol_len, i, inlen);

                  // 安全处理：记录并拒绝连接
                  // 返回 SSL_TLSEXT_ERR_ALERT_FATAL 让 OpenSSL 中止握手
                  return SSL_TLSEXT_ERR_ALERT_FATAL;
              }

              // 检查是否为 http/1.1
              if (protocol_len == 8 && memcmp(&in[i + 1], "http/1.1", 8) == 0) {
                  *out = &in[i + 1];
                  *outlen = protocol_len;
                  break;  // 找到目标协议，退出循环
              }

              // ========== 增强的 HTTP/2 检测 ==========
              // 如果是 h2 或 h2c，记录但继续遍历
              if ((protocol_len == 2 && memcmp(&in[i + 1], "h2", 2) == 0) ||
                  (protocol_len == 3 && memcmp(&in[i + 1], "h2c", 3) == 0)) {
                  // 尝试获取远程端点信息用于日志记录
                  try {
                      auto endpoint = SSL_get_fd(ssl);
                      if (endpoint >= 0) {
                          // 简单标记为 HTTP/2 尝试
                          HKU_DEBUG("Rejected HTTP/2 negotiation attempt");
                      } else {
                          HKU_DEBUG("Rejected HTTP/2 negotiation attempt (no fd)");
                      }
                  } catch (...) {
                      HKU_DEBUG("Rejected HTTP/2 negotiation attempt (unknown endpoint)");
                  }
              }

              i += protocol_len + 1;
          }

          // ========== 安全降级策略 ==========
          // 始终返回 http/1.1 作为默认协议
          // 这样可以确保即使客户端提供畸形 ALPN 扩展，握手仍能完成
          // 避免在协议协商阶段就阻断连接导致资源浪费
          static const unsigned char default_http11[] = {0x08, 'h', 't', 't', 'p',
                                                         '/',  '1', '.', '1'};
          *out = default_http11 + 1;  // 跳过长度字节
          *outlen = sizeof(default_http11) - 1;
          return SSL_TLSEXT_ERR_OK;
      },
      nullptr);

    CLS_INFO("SSL configured with CA file: {} (HTTP/2 disabled, using HTTP/1.1)",
             m_ssl_config.ca_key_file);
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
          Connection::create(std::move(socket), &m_router, *ms_io_context, ms_ssl_context);
        connection->start();
    }

    co_return;
}

void HttpServer::start() {
    if (ms_running.load()) {
        CLS_WARN("Server is already running");
        return;
    }

    CLS_INFO("HttpServer::start() begin");

    try {
        // ========== 初始化连接管理器（如果未配置） ==========
        // 如果用户未显式配置 ConnectionManager，创建默认实例
        if (!ms_connection_manager) {
            size_t max_conns = HttpConfig::MAX_CONNECTIONS;
            // 使用 READ_TIMEOUT 作为 acquire 超时（60 秒 = 60000 毫秒）
            size_t timeout_ms = 60000;  // 默认 60 秒超时
            ms_connection_manager = std::make_shared<ConnectionManager>(max_conns, timeout_ms);
            CLS_INFO("Default ConnectionManager created: max={}, timeout={}s", max_conns,
                     timeout_ms / 1000);
        } else {
            CLS_INFO("Using pre-configured ConnectionManager");
        }

        // 如果用户未显式配置 WebSocketConnectionManager，创建默认实例
        if (!ms_ws_connection_manager && ms_websocket_enabled) {
            size_t max_ws_conns = WebSocketConfig::MAX_CONNECTIONS;
            // 使用 READ_TIMEOUT 作为 acquire 超时（60 秒 = 60000 毫秒）
            size_t timeout_ms = 60000;  // 默认 60 秒超时
            ms_ws_connection_manager =
              std::make_shared<WebSocketConnectionManager>(max_ws_conns, timeout_ms);
            CLS_INFO("Default WebSocketConnectionManager created: max={}, timeout={}s",
                     max_ws_conns, timeout_ms / 1000);
        } else if (ms_websocket_enabled) {
            CLS_INFO("Using pre-configured WebSocketConnectionManager");
        }

        // 如果未绑定外部 io_context，则自行创建
        if (!ms_use_external_io) {
            CLS_INFO("Creating io_context...");
            ms_io_context = new net::io_context();
            CLS_INFO("io_context created: {}", (void*)ms_io_context);
        } else {
            CLS_INFO("Using externally bound io_context");
        }

        // 创建 acceptor
        CLS_INFO("Creating acceptor on {}:{}", m_host, m_port);
        auto addr = net::ip::make_address(m_host.c_str());
        ms_acceptor = new tcp::acceptor(*ms_io_context, {addr, m_port});
        CLS_INFO("Acceptor created: {}", (void*)ms_acceptor);

        // 配置 SSL（如果启用）
        if (m_ssl_config.enabled) {
            CLS_INFO("Configuring SSL...");
            configureSsl();

            CLS_INFO("HTTPS Server started on {}:{}", m_host, m_port);

            // 使用协程开始接受 SSL 连接
            CLS_INFO("Spawning doAcceptSsl coroutine...");
            net::co_spawn(*ms_io_context, doAcceptSsl(), net::detached);
        } else {
            CLS_INFO("HTTP Server started on {}:{}", m_host, m_port);

            // 使用协程开始接受连接
            CLS_INFO("Spawning doAccept coroutine...");
            net::co_spawn(*ms_io_context, doAccept(), net::detached);
        }

        CLS_INFO("Setting ms_running = true");
        ms_running.store(true);
        CLS_INFO("Server start completed successfully");

#if defined(_WIN32)
        // Windows 下设置控制台程序输出代码页为 UTF8
        g_old_cp = GetConsoleOutputCP();
        SetConsoleOutputCP(CP_UTF8);
#endif

    } catch (std::exception& e) {
        CLS_FATAL("Failed to start server: {}", e.what());
        CLS_FATAL("Exception type: {}", typeid(e).name());
        stop();
    } catch (...) {
        CLS_FATAL("Unknown exception occurred during server start");
        stop();
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

        // 为新连接创建处理器并启动协程（非 SSL 模式）
        // 注意：连接限流由 ConnectionManager 统一管理，这里不再重复检查
        auto connection = Connection::create(std::move(socket), &m_router, *ms_io_context, nullptr);
        connection->start();
    }

    co_return;
}

void HttpServer::loop() {
    HKU_ASSERT(ms_server);
    CLS_INFO("HttpServer::loop() called, ms_running={}, ms_io_context={}", ms_running.load(),
             (void*)ms_io_context);

    if (ms_io_context && ms_running.load()) {
        // 确定线程数：如果未设置或设置为 0，使用硬件并发数
        size_t thread_count = ms_server->m_io_thread_count;
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

    } else {
        CLS_WARN("HttpServer::loop() skipped: ms_io_context={}, ms_running={}",
                 (void*)ms_io_context, ms_running.load());
    }
}

void HttpServer::stop() {
#if defined(_WIN32)
    SetConsoleOutputCP(g_old_cp);
#endif

    if (ms_running.load()) {
        ms_running.store(false);

        // 1. 先关闭 acceptor，停止接受新连接
        if (ms_acceptor) {
            ms_acceptor->cancel();
            ms_acceptor->close();
            delete ms_acceptor;
            ms_acceptor = nullptr;
        }

        // 2. 【关键】通知 ConnectionManager 停止，唤醒所有等待的连接
        if (ms_connection_manager) {
            ms_connection_manager->shutdown();
        } else {
            HKU_WARN("ConnectionManager is null!");
        }

        // 3 关闭 WebSocket 连接管理器
        if (ms_ws_connection_manager) {
            ms_ws_connection_manager->shutdown();
        } else {
            HKU_DEBUG("WebSocketConnectionManager not configured");
        }

        // 4. 停止 io_context，取消所有待处理操作
        if (ms_io_context) {
            ms_io_context->stop();
        } else {
            HKU_WARN("io_context is null!");
        }

        // 5. 清理 SSL 上下文
        if (ms_ssl_context) {
            delete ms_ssl_context;
            ms_ssl_context = nullptr;
        }

        // 6. 最后清理 io_context 对象（如果使用的是外部 io_context 则不清理）
        if (ms_io_context && !ms_use_external_io) {
            // 重要：等待一小段时间确保所有协程完成
            // 因为调用 stop() 后，异步操作可能仍在完成中
            // 直接删除 io_context 可能导致未定义行为
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            delete ms_io_context;
            ms_io_context = nullptr;
        }

        // 6. 清理 server 指针
        if (ms_server) {
            CLS_INFO("Quit Http server");
            ms_server = nullptr;
        }
    }
}

void HttpServer::registerHttpHandle(const std::string& method, const std::string& path,
                                    HttpHandleFactory handler) {
    m_router.registerHandler(method, path, handler);
}

void HttpServer::registerHttpHandle(const char* method, const char* path,
                                    HttpHandleFactory handler) {
    registerHttpHandle(std::string(method), std::string(path), std::move(handler));
}

void HttpServer::registerWsHandle(const std::string& path, WsHandleFactory handler) {
    ms_ws_router.registerHandler(path, std::move(handler));
}

void HttpServer::registerWsHandle(const char* path, WsHandleFactory handler) {
    registerWsHandle(std::string(path), std::move(handler));
}

void HttpServer::setTls(const char* ca_key_file, const char* password, int mode) {
    m_ssl_config.ca_key_file = ca_key_file;
    m_ssl_config.password = password ? password : "";
    m_ssl_config.verify_mode = mode;
    m_ssl_config.enabled = true;
}
}  // namespace hku