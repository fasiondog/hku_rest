/*
 *  Copyright(C) 2021 hikyuu.org
 *
 *  Create on: 2021-02-28
 *     Author: fasiondog
 */

#include <csignal>
#include <nng/nng.h>
#include <nng/supplemental/tls/tls.h>
#include <hikyuu/utilities/os.h>
#include "HttpServer.h"

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace hku {

#define HTTP_FATAL_CHECK(rv, msg)                                        \
    {                                                                    \
        if (rv != 0) {                                                   \
            CLS_FATAL("[HTTP_FATAL] {} err: {}", msg, nng_strerror(rv)); \
            http_exit();                                                 \
        }                                                                \
    }

nng_http_server* HttpServer::ms_server = nullptr;
ThreadPool HttpServer::ms_tg(std::thread::hardware_concurrency(), false);

#if defined(_WIN32)
static UINT g_old_cp;
#endif

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
    m_root_url = fmt::format("{}:{}", m_host, m_port);
    nng_url* url = nullptr;
    HTTP_FATAL_CHECK(nng_url_parse(&url, m_root_url.c_str()), "Failed nng_url_parse!");
    HTTP_FATAL_CHECK(nng_http_server_hold(&ms_server, url), "Failed nng_http_server_hold!");
    nng_url_free(url);
}

HttpServer::~HttpServer() {}

void HttpServer::start() {
    // std::signal(SIGINT, &HttpServer::signal_handler);
    // std::signal(SIGTERM, &HttpServer::signal_handler);

    HTTP_FATAL_CHECK(nng_http_server_start(ms_server), "Failed nng_http_server_start!");

#if defined(_WIN32)
    // Windows 下设置控制台程序输出代码页为 UTF8
    g_old_cp = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);
#endif
}

void HttpServer::loop() {
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

void HttpServer::stop() {
#if defined(_WIN32)
    SetConsoleOutputCP(g_old_cp);
#endif
    ms_tg.stop();
    if (ms_server) {
        CLS_INFO("Quit Http server");
        nng_http_server_stop(ms_server);
        nng_http_server_release(ms_server);
        nng_fini();
        ms_server = nullptr;
    }
}

void HttpServer::set_error_msg(int16_t http_status, const std::string& body) {
    HTTP_FATAL_CHECK(nng_http_server_set_error_page(ms_server, http_status, body.c_str()),
                     "Failed nng_http_server_set_error_page");
}

void HttpServer::set_tls(const char* ca_key_file, const char* password) {
    HKU_CHECK(existFile(ca_key_file), "Not exist ca file: {}", ca_key_file);

    nng_tls_config* cfg;
    int rv;

    // 创建一个新的 TLS 配置
    HTTP_FATAL_CHECK(nng_tls_config_alloc(&cfg, NNG_TLS_MODE_SERVER),
                     "nng_tls_config_alloc failed!");

    // 设置证书和私钥文件
    if ((rv = nng_tls_config_cert_key_file(cfg, ca_key_file, password)) != 0) {
        nng_tls_config_free(cfg);
        HKU_THROW("nng_tls_config_cert_key_file falied! err: {}", nng_strerror(rv));
    }

    if ((rv = nng_http_server_set_tls(ms_server, cfg)) != 0) {
        nng_tls_config_free(cfg);
        HKU_THROW("nng_http_server_set_tls falied! err: {}", nng_strerror(rv));
    }
    nng_tls_config_free(cfg);
}

void HttpServer::regHandle(const char* method, const char* path, void (*rest_handle)(nng_aio*)) {
    try {
        HKU_CHECK(strlen(path) > 1, "Invalid api path!");
        HKU_CHECK(path[0] == '/', "The api path must start with '/', but current is '{}'", path[0]);
    } catch (std::exception& e) {
        CLS_FATAL(e.what());
        http_exit();
    }
    nng_url* url = nullptr;
    nng_http_handler* handler = nullptr;
    HTTP_FATAL_CHECK(nng_url_parse(&url, fmt::format("{}/{}", m_root_url, path).c_str()),
                     "Failed nng_url_parse!");
    HTTP_FATAL_CHECK(nng_http_handler_alloc(&handler, url->u_path, rest_handle),
                     "Failed nng_http_handler_alloc!");
    HTTP_FATAL_CHECK(nng_http_handler_set_method(handler, method),
                     "Failed nng_http_handler_set_method!");
    HTTP_FATAL_CHECK(nng_http_server_add_handler(ms_server, handler),
                     "Failed nng_http_server_add_handler!");
}

}  // namespace hku