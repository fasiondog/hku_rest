/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-04-07
 *      Author: fasiondog
 */

#include "MqttBroker.h"

#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <thread>

#if defined(ASYNC_MQTT_USE_TLS)
#include <openssl/x509.h>
#endif  // defined(ASYNC_MQTT_USE_TLS)

#include <boost/asio.hpp>
#include <async_mqtt/broker/endpoint_variant.hpp>
#include <async_mqtt/broker/broker.hpp>
#include <async_mqtt/broker/security.hpp>

#if defined(ASYNC_MQTT_USE_WS)
#include <async_mqtt/asio_bind/predefined_layer/ws.hpp>
#endif  // defined(ASYNC_MQTT_USE_WS)

#if defined(ASYNC_MQTT_USE_TLS)
#include <async_mqtt/asio_bind/predefined_layer/mqtts.hpp>
#if defined(ASYNC_MQTT_USE_WS)
#include <async_mqtt/asio_bind/predefined_layer/wss.hpp>
#endif  // defined(ASYNC_MQTT_USE_WS)
#endif  // defined(ASYNC_MQTT_USE_TLS)

namespace am = async_mqtt;
namespace as = boost::asio;
namespace net = boost::asio;

namespace hku {

// ============================================================================
// Impl 类定义 - 所有 async_mqtt 相关类型都在这里
// ============================================================================

class MqttBroker::Impl {
public:
    // Endpoint variant 类型定义
    using epv_type = am::basic_endpoint_variant<am::role::server, 2, am::protocol::mqtt
#if defined(ASYNC_MQTT_USE_WS)
                                                ,
                                                am::protocol::ws
#endif  // defined(ASYNC_MQTT_USE_WS)
#if defined(ASYNC_MQTT_USE_TLS)
                                                ,
                                                am::protocol::mqtts
#if defined(ASYNC_MQTT_USE_WS)
                                                ,
                                                am::protocol::wss
#endif  // defined(ASYNC_MQTT_USE_WS)
#endif  // defined(ASYNC_MQTT_USE_TLS)
                                                >;

    Impl(const Parameter& param);
    ~Impl();

    void start();
    void stop();
    bool isRunning() const {
        return m_running.load();
    }

private:
#if defined(ASYNC_MQTT_USE_TLS)
    static bool verify_certificate(std::string const& verify_field, bool preverified,
                                   as::ssl::verify_context& ctx,
                                   std::shared_ptr<std::optional<std::string>> const& username);
#endif

    net::io_context& con_ioc_getter();
    void apply_socket_opts(auto& lowest_layer);
    void load_auth();
    void start_mqtt_listener();

#if defined(ASYNC_MQTT_USE_WS)
    void start_ws_listener();
#endif

#if defined(ASYNC_MQTT_USE_TLS)
    void start_tls_listener();

#if defined(ASYNC_MQTT_USE_WS)
    void start_wss_listener();
#endif
#endif

    // 配置参数（从 Parameter 中提取的具体值）
    struct Config {
        // MQTT 端口配置（0 表示未启用）
        uint16_t mqtt_port = 0;
        uint16_t ws_port = 0;
        uint16_t tls_port = 0;
        uint16_t wss_port = 0;

        // 性能配置
        std::size_t num_iocs = 0;  // 0 = 自动检测 CPU 核心数
        std::size_t threads_per_ioc = 1;
        bool tcp_no_delay = true;
        bool bulk_write = true;
        std::size_t read_buf_size = 65536;  // 64KB
        int recv_buf_size = 0;              // 0 = 使用系统默认
        int send_buf_size = 0;              // 0 = 使用系统默认

        // 认证配置
        std::string auth_file;

        // TLS 配置
        std::string certificate;
        std::string private_key;
        std::string verify_file;
        std::string verify_field = "CN";
    };

    Config m_config;

    // 运行状态
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stop_requested{false};

    // IO 上下文
    net::io_context m_accept_ioc;
    std::optional<as::executor_work_guard<net::io_context::executor_type>> m_guard_accept_ioc;
    std::vector<std::shared_ptr<net::io_context>> m_con_iocs;
    std::vector<as::executor_work_guard<net::io_context::executor_type>> m_guard_con_iocs;

    // MQTT Broker 实例
    std::unique_ptr<am::broker<epv_type>> m_broker;

    // 线程
    std::vector<std::thread> m_con_threads;

    // Round-Robin 分发
    std::mutex m_mtx_con_iocs;
    typename std::vector<std::shared_ptr<net::io_context>>::iterator m_con_iocs_it;
};

// ============================================================================
// TLS 证书验证回调
// ============================================================================

#if defined(ASYNC_MQTT_USE_TLS)
bool MqttBroker::Impl::verify_certificate(
  std::string const& verify_field, bool preverified, as::ssl::verify_context& ctx,
  std::shared_ptr<std::optional<std::string>> const& username) {
    if (!preverified)
        return false;

    int error = X509_STORE_CTX_get_error(ctx.native_handle());
    if (error != X509_V_OK) {
        int depth = X509_STORE_CTX_get_error_depth(ctx.native_handle());
        CLS_ERROR("Certificate validation failed, depth: {}, message: {}", depth,
                  X509_verify_cert_error_string(error));
        return false;
    }

    X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
    if (!cert)
        return false;

    char subject_name[256];
    X509_NAME_oneline(X509_get_subject_name(cert), subject_name, sizeof(subject_name));

    CLS_INFO("Client certificate subject: {}", subject_name);

    if (username) {
        char common_name[256];
        X509_NAME_get_text_by_NID(X509_get_subject_name(cert), NID_commonName, common_name,
                                  sizeof(common_name));
        *username = std::string(common_name);
    }

    return true;
}
#endif  // defined(ASYNC_MQTT_USE_TLS)

// ============================================================================
// Impl 构造函数和析构函数
// ============================================================================

MqttBroker::Impl::Impl(const Parameter& param) {
    // 从 Parameter 中提取配置值
    if (param.have("mqtt_port")) {
        m_config.mqtt_port = static_cast<uint16_t>(param.get<int>("mqtt_port"));
    }
    if (param.have("ws_port")) {
        m_config.ws_port = static_cast<uint16_t>(param.get<int>("ws_port"));
    }
    if (param.have("tls_port")) {
        m_config.tls_port = static_cast<uint16_t>(param.get<int>("tls_port"));
    }
    if (param.have("wss_port")) {
        m_config.wss_port = static_cast<uint16_t>(param.get<int>("wss_port"));
    }

    if (param.have("iocs")) {
        m_config.num_iocs = static_cast<std::size_t>(param.get<int>("iocs"));
    }
    if (m_config.num_iocs == 0) {
        m_config.num_iocs = std::thread::hardware_concurrency();
        if (m_config.num_iocs == 0)
            m_config.num_iocs = 1;
    }

    if (param.have("threads_per_ioc")) {
        m_config.threads_per_ioc = static_cast<std::size_t>(param.get<int>("threads_per_ioc"));
    }

    if (param.have("tcp_no_delay")) {
        m_config.tcp_no_delay = param.get<bool>("tcp_no_delay");
    }

    if (param.have("bulk_write")) {
        m_config.bulk_write = param.get<bool>("bulk_write");
    }

    if (param.have("read_buf_size")) {
        m_config.read_buf_size = static_cast<std::size_t>(param.get<int>("read_buf_size"));
    }

    if (param.have("recv_buf_size")) {
        m_config.recv_buf_size = param.get<int>("recv_buf_size");
    }

    if (param.have("send_buf_size")) {
        m_config.send_buf_size = param.get<int>("send_buf_size");
    }

    if (param.have("auth_file")) {
        m_config.auth_file = param.get<std::string>("auth_file");
    }

    if (param.have("certificate")) {
        m_config.certificate = param.get<std::string>("certificate");
    }

    if (param.have("private_key")) {
        m_config.private_key = param.get<std::string>("private_key");
    }

    if (param.have("verify_file")) {
        m_config.verify_file = param.get<std::string>("verify_file");
    }

    if (param.have("verify_field")) {
        m_config.verify_field = param.get<std::string>("verify_field");
    }

    CLS_INFO("Initializing MQTT Broker with {} io_context(s)", m_config.num_iocs);

    // 初始化 accept io_context
    m_guard_accept_ioc.emplace(as::make_work_guard(m_accept_ioc.get_executor()));

    // 初始化 io_context 池
    for (std::size_t i = 0; i < m_config.num_iocs; ++i) {
        auto ioc = std::make_shared<net::io_context>();
        m_con_iocs.push_back(ioc);
        m_guard_con_iocs.push_back(as::make_work_guard(ioc->get_executor()));
    }

    m_con_iocs_it = m_con_iocs.begin();

    // 创建 broker
    m_broker = std::make_unique<am::broker<epv_type>>(m_con_iocs[0]->get_executor());

    // 加载认证文件
    load_auth();

    CLS_INFO("MQTT Broker initialized successfully");
}

MqttBroker::Impl::~Impl() {
    stop();
}

// ============================================================================
// Impl 公共方法
// ============================================================================

void MqttBroker::Impl::start() {
    if (m_running.exchange(true)) {
        CLS_WARN("MQTT Broker is already running");
        return;
    }

    m_stop_requested.store(false);

    try {
        // 启动各种监听器
        start_mqtt_listener();

#if defined(ASYNC_MQTT_USE_WS)
        start_ws_listener();
#endif

#if defined(ASYNC_MQTT_USE_TLS)
        start_tls_listener();
#if defined(ASYNC_MQTT_USE_WS)
        start_wss_listener();
#endif
#endif

        CLS_INFO("MQTT Broker started successfully");

        // 启动连接处理线程
        std::size_t threads_per_ioc = m_config.threads_per_ioc;
        for (std::size_t i = 0; i < m_con_iocs.size(); ++i) {
            for (std::size_t t = 0; t < threads_per_ioc; ++t) {
                m_con_threads.emplace_back([this, i]() {
                    try {
                        m_con_iocs[i]->run();
                    } catch (const std::exception& e) {
                        CLS_ERROR("Connection thread error: {}", e.what());
                    }
                });
            }
        }

        // 运行 accept io_context（阻塞）
        try {
            m_accept_ioc.run();
        } catch (const std::exception& e) {
            CLS_ERROR("Accept thread error: {}", e.what());
        }

        // 等待所有线程结束
        for (auto& t : m_con_threads) {
            if (t.joinable()) {
                t.join();
            }
        }

        CLS_INFO("MQTT Broker stopped");
    } catch (const std::exception& e) {
        CLS_ERROR("MQTT Broker error: {}", e.what());
        m_running.store(false);
        throw;
    }
}

void MqttBroker::Impl::stop() {
    if (!m_running.exchange(false)) {
        return;
    }

    CLS_INFO("Stopping MQTT Broker...");
    m_stop_requested.store(true);

    // 1. 清除 work guards（允许 io_context::run() 返回）
    m_guard_accept_ioc.reset();
    m_guard_con_iocs.clear();

    // 2. 停止所有 io_context（中断 run() 调用）
    m_accept_ioc.stop();
    for (auto& ioc : m_con_iocs) {
        ioc->stop();
    }

    // 3. 等待所有线程结束
    for (auto& t : m_con_threads) {
        if (t.joinable()) {
            t.join();
        }
    }
    m_con_threads.clear();

    CLS_INFO("MQTT Broker stopped");
}

// ============================================================================
// Impl 私有方法
// ============================================================================

net::io_context& MqttBroker::Impl::con_ioc_getter() {
    std::lock_guard<std::mutex> g{m_mtx_con_iocs};
    auto& ret = **m_con_iocs_it;
    ++m_con_iocs_it;
    if (m_con_iocs_it == m_con_iocs.end()) {
        m_con_iocs_it = m_con_iocs.begin();
    }
    return ret;
}

void MqttBroker::Impl::apply_socket_opts(auto& lowest_layer) {
    lowest_layer.set_option(net::ip::tcp::no_delay(m_config.tcp_no_delay));

    if (m_config.recv_buf_size != 0) {
        lowest_layer.set_option(net::socket_base::receive_buffer_size(m_config.recv_buf_size));
    }
    if (m_config.send_buf_size != 0) {
        lowest_layer.set_option(net::socket_base::send_buffer_size(m_config.send_buf_size));
    }
}

void MqttBroker::Impl::load_auth() {
    if (!m_config.auth_file.empty()) {
        CLS_INFO("Loading auth file: {}", m_config.auth_file);

        std::ifstream input(m_config.auth_file);
        if (input) {
            am::security security;
            security.load_json(input);
            m_broker->set_security(am::force_move(security));
            CLS_INFO("Auth file loaded successfully");
        } else {
            CLS_WARN("Authorization file '{}' not found", m_config.auth_file);
        }
    }
}

void MqttBroker::Impl::start_mqtt_listener() {
    if (m_config.mqtt_port == 0) {
        CLS_INFO("MQTT TCP listener not configured (mqtt.port not set)");
        return;
    }

    uint16_t port = m_config.mqtt_port;
    net::ip::tcp::endpoint endpoint(net::ip::tcp::v4(), port);
    net::ip::tcp::acceptor acceptor(m_accept_ioc, endpoint);

    CLS_INFO("Starting MQTT TCP listener on port {}", port);

    net::co_spawn(
      m_accept_ioc.get_executor(),
      [this, acceptor = std::move(acceptor)]() mutable -> net::awaitable<void> {
          while (!m_stop_requested.load()) {
              auto epsp =
                std::make_shared<am::basic_endpoint<am::role::server, 2, am::protocol::mqtt>>(
                  am::protocol_version::undetermined,
                  net::make_strand(con_ioc_getter().get_executor()));

              epsp->set_bulk_write(m_config.bulk_write);
              epsp->set_read_buffer_size(m_config.read_buf_size);

              auto& lowest_layer = epsp->lowest_layer();
              auto [ec] =
                co_await acceptor.async_accept(lowest_layer, net::as_tuple(net::deferred));

              if (ec) {
                  if (!m_stop_requested.load()) {
                      CLS_ERROR("TCP accept error: {}", ec.message());
                      // 如果是资源耗尽错误，等待一段时间后重试
                      if (ec == boost::system::errc::too_many_files_open ||
                          ec == boost::system::errc::no_buffer_space ||
                          ec == boost::system::errc::not_enough_memory) {
                          CLS_WARN("System resource exhausted, waiting 1s before retry...");
                          net::steady_timer timer(m_accept_ioc.get_executor());
                          timer.expires_after(std::chrono::seconds(1));
                          co_await timer.async_wait(net::deferred);
                      }
                  }
                  continue;
              }

              apply_socket_opts(lowest_layer);
              epsp->underlying_accepted();

              // 直接调用 handle_accept，它会启动异步读取
              m_broker->handle_accept(epv_type{std::move(epsp)});
          }
      },
      net::detached);
}

#if defined(ASYNC_MQTT_USE_WS)
void MqttBroker::Impl::start_ws_listener() {
    if (m_config.ws_port == 0) {
        CLS_INFO("WebSocket listener not configured (ws.port not set)");
        return;
    }

    uint16_t port = m_config.ws_port;
    net::ip::tcp::endpoint endpoint(net::ip::tcp::v4(), port);
    net::ip::tcp::acceptor acceptor(m_accept_ioc, endpoint);

    CLS_INFO("Starting WebSocket listener on port {}", port);

    net::co_spawn(
      m_accept_ioc.get_executor(),
      [this, acceptor = std::move(acceptor)]() mutable -> net::awaitable<void> {
          while (!m_stop_requested.load()) {
              auto epsp =
                std::make_shared<am::basic_endpoint<am::role::server, 2, am::protocol::ws>>(
                  am::protocol_version::undetermined,
                  net::make_strand(con_ioc_getter().get_executor()));

              epsp->set_bulk_write(m_config.bulk_write);
              epsp->set_read_buffer_size(m_config.read_buf_size);

              auto& lowest_layer = epsp->lowest_layer();
              auto [ec] =
                co_await acceptor.async_accept(lowest_layer, net::as_tuple(net::deferred));

              if (ec) {
                  if (!m_stop_requested.load()) {
                      CLS_ERROR("WebSocket accept error: {}", ec.message());
                      // 如果是资源耗尽错误，等待一段时间后重试
                      if (ec == boost::system::errc::too_many_files_open ||
                          ec == boost::system::errc::no_buffer_space ||
                          ec == boost::system::errc::not_enough_memory) {
                          CLS_WARN("System resource exhausted, waiting 1s before retry...");
                          net::steady_timer timer(m_accept_ioc.get_executor());
                          timer.expires_after(std::chrono::seconds(1));
                          co_await timer.async_wait(net::deferred);
                      }
                  }
                  continue;
              }

              apply_socket_opts(lowest_layer);
              epsp->underlying_accepted();

              // 直接调用 handle_accept
              m_broker->handle_accept(epv_type{std::move(epsp)});
          }
      },
      net::detached);
}
#endif  // defined(ASYNC_MQTT_USE_WS)

#if defined(ASYNC_MQTT_USE_TLS)
void MqttBroker::Impl::start_tls_listener() {
    if (m_config.tls_port == 0) {
        CLS_INFO("TLS listener not configured (tls.port not set)");
        return;
    }

    uint16_t port = m_config.tls_port;

    if (m_config.certificate.empty() || m_config.private_key.empty()) {
        CLS_ERROR("TLS requires 'certificate' and 'private_key' parameters");
        return;
    }

    try {
        auto ctx = std::make_shared<net::ssl::context>(net::ssl::context::tlsv12_server);
        ctx->set_options(net::ssl::context::default_workarounds | net::ssl::context::no_sslv2 |
                         net::ssl::context::no_sslv3 | net::ssl::context::no_tlsv1 |
                         net::ssl::context::no_tlsv1_1);

        ctx->use_certificate_chain_file(m_config.certificate);
        ctx->use_private_key_file(m_config.private_key, net::ssl::context::pem);

        if (!m_config.verify_file.empty()) {
            ctx->load_verify_file(m_config.verify_file);
            ctx->set_verify_mode(net::ssl::verify_peer | net::ssl::verify_fail_if_no_peer_cert);
        }

        net::ip::tcp::endpoint endpoint(net::ip::tcp::v4(), port);
        net::ip::tcp::acceptor acceptor(m_accept_ioc, endpoint);

        CLS_INFO("Starting TLS listener on port {}", port);

        net::co_spawn(
          m_accept_ioc.get_executor(),
          [this, acceptor = std::move(acceptor), ctx]() mutable -> net::awaitable<void> {
              while (!m_stop_requested.load()) {
                  auto epsp =
                    std::make_shared<am::basic_endpoint<am::role::server, 2, am::protocol::mqtts>>(
                      am::protocol_version::undetermined,
                      net::make_strand(con_ioc_getter().get_executor()), *ctx);

                  epsp->set_bulk_write(m_config.bulk_write);
                  epsp->set_read_buffer_size(m_config.read_buf_size);

                  auto& ssl_layer = epsp->next_layer();
                  ssl_layer.set_verify_callback(
                    [this, username = std::make_shared<std::optional<std::string>>()](
                      bool preverified, net::ssl::verify_context& ctx) {
                        return verify_certificate(m_config.verify_field, preverified, ctx,
                                                  username);
                    });

                  auto& lowest_layer = epsp->lowest_layer();
                  auto [ec] =
                    co_await acceptor.async_accept(lowest_layer, net::as_tuple(net::deferred));

                  if (ec) {
                      if (!m_stop_requested.load()) {
                          CLS_ERROR("TLS accept error: {}", ec.message());
                          // 如果是资源耗尽错误，等待一段时间后重试
                          if (ec == boost::system::errc::too_many_files_open ||
                              ec == boost::system::errc::no_buffer_space ||
                              ec == boost::system::errc::not_enough_memory) {
                              CLS_WARN("System resource exhausted, waiting 1s before retry...");
                              net::steady_timer timer(m_accept_ioc.get_executor());
                              timer.expires_after(std::chrono::seconds(1));
                              co_await timer.async_wait(net::deferred);
                          }
                      }
                      continue;
                  }

                  apply_socket_opts(lowest_layer);

                  co_await ssl_layer.async_handshake(net::ssl::stream_base::server, net::deferred);

                  epsp->underlying_accepted();

                  // 直接调用 handle_accept
                  m_broker->handle_accept(epv_type{std::move(epsp)});
              }
          },
          net::detached);
    } catch (const std::exception& e) {
        CLS_ERROR("Failed to start TLS listener: {}", e.what());
    }
}

#if defined(ASYNC_MQTT_USE_WS)
void MqttBroker::Impl::start_wss_listener() {
    if (m_config.wss_port == 0) {
        CLS_INFO("WebSocket Secure listener not configured (wss.port not set)");
        return;
    }

    uint16_t port = m_config.wss_port;

    if (m_config.certificate.empty() || m_config.private_key.empty()) {
        CLS_ERROR("WSS requires 'certificate' and 'private_key' parameters");
        return;
    }

    try {
        auto ctx = std::make_shared<net::ssl::context>(net::ssl::context::tlsv12_server);
        ctx->set_options(net::ssl::context::default_workarounds | net::ssl::context::no_sslv2 |
                         net::ssl::context::no_sslv3 | net::ssl::context::no_tlsv1 |
                         net::ssl::context::no_tlsv1_1);

        ctx->use_certificate_chain_file(m_config.certificate);
        ctx->use_private_key_file(m_config.private_key, net::ssl::context::pem);

        if (!m_config.verify_file.empty()) {
            ctx->load_verify_file(m_config.verify_file);
            ctx->set_verify_mode(net::ssl::verify_peer | net::ssl::verify_fail_if_no_peer_cert);
        }

        net::ip::tcp::endpoint endpoint(net::ip::tcp::v4(), port);
        net::ip::tcp::acceptor acceptor(m_accept_ioc, endpoint);

        CLS_INFO("Starting WebSocket Secure listener on port {}", port);

        net::co_spawn(
          m_accept_ioc.get_executor(),
          [this, acceptor = std::move(acceptor), ctx]() mutable -> net::awaitable<void> {
              while (!m_stop_requested.load()) {
                  auto epsp =
                    std::make_shared<am::basic_endpoint<am::role::server, 2, am::protocol::wss>>(
                      am::protocol_version::undetermined,
                      net::make_strand(con_ioc_getter().get_executor()), *ctx);

                  epsp->set_bulk_write(m_config.bulk_write);
                  epsp->set_read_buffer_size(m_config.read_buf_size);

                  auto& ssl_layer = epsp->next_layer().next_layer();
                  ssl_layer.set_verify_callback(
                    [this, username = std::make_shared<std::optional<std::string>>()](
                      bool preverified, net::ssl::verify_context& ctx) {
                        return verify_certificate(m_config.verify_field, preverified, ctx,
                                                  username);
                    });

                  auto& lowest_layer = epsp->lowest_layer();
                  auto [ec] =
                    co_await acceptor.async_accept(lowest_layer, net::as_tuple(net::deferred));

                  if (ec) {
                      if (!m_stop_requested.load()) {
                          CLS_ERROR("WSS accept error: {}", ec.message());
                          // 如果是资源耗尽错误，等待一段时间后重试
                          if (ec == boost::system::errc::too_many_files_open ||
                              ec == boost::system::errc::no_buffer_space ||
                              ec == boost::system::errc::not_enough_memory) {
                              CLS_WARN("System resource exhausted, waiting 1s before retry...");
                              net::steady_timer timer(m_accept_ioc.get_executor());
                              timer.expires_after(std::chrono::seconds(1));
                              co_await timer.async_wait(net::deferred);
                          }
                      }
                      continue;
                  }

                  apply_socket_opts(lowest_layer);

                  co_await ssl_layer.async_handshake(net::ssl::stream_base::server, net::deferred);

                  epsp->underlying_accepted();

                  // 直接调用 handle_accept
                  m_broker->handle_accept(epv_type{std::move(epsp)});
              }
          },
          net::detached);
    } catch (const std::exception& e) {
        CLS_ERROR("Failed to start WSS listener: {}", e.what());
    }
}
#endif  // defined(ASYNC_MQTT_USE_WS)
#endif  // defined(ASYNC_MQTT_USE_TLS)

// ============================================================================
// MqttBroker 公共接口实现 - 简单转发到 Impl
// ============================================================================

MqttBroker::MqttBroker(const Parameter& param) : m_impl(std::make_unique<Impl>(param)) {}

MqttBroker::~MqttBroker() = default;

void MqttBroker::start() {
    m_impl->start();
}

void MqttBroker::stop() {
    m_impl->stop();
}

bool MqttBroker::isRunning() const {
    return m_impl->isRunning();
}

}  // namespace hku
