/*
 *  Copyright(C) 2021 hikyuu.org
 *
 *  Create on: 2021-02-28
 *     Author: fasiondog
 */

#pragma once

#include <string>
#include <unordered_set>
#include <hikyuu/utilities/thread/thread.h>
#include <hikyuu/utilities/thread/FuncWrapper.h>
#include "HttpHandle.h"

#ifndef HKU_HTTPD_API
#define HKU_HTTPD_API
#endif

namespace hku {

class HKU_HTTPD_API HttpServer {
    CLASS_LOGGER_IMP(HttpServer)

public:
    HttpServer(const char *host, uint16_t port);
    virtual ~HttpServer();

    static void start();
    static void loop();
    static void stop();

    /**
     * 设置 handle 无法捕获的错误返回信息，如 404
     * @param http_status http状态码
     * @param body 返回消息
     */
    static void set_error_msg(int16_t http_status, const std::string &body);

    /**
     * 设置 tls 配置，启动前设置
     * @param ca_key_file ca文件路径(同时包含PEM格式的cert和key的文件)
     * @param password ca文件密码,无密码时指定空指针
     * @param mode 0 无需客户端认证 | 1 客户端认证可选 | 2 需客户端认证
     */
    static void set_tls(const char *ca_key_file, const char *password, int mode = 0);

    template <typename Handle>
    void GET(const char *path) {
        regHandle("GET", path, [](nng_aio *aio) {
            Handle x(aio);
            x();
        });
    }

    template <typename Handle>
    void POST(const char *path) {
        regHandle("POST", path, [](nng_aio *aio) {
            Handle x(aio);
            x();
        });
    }

    template <typename Handle>
    void PUT(const char *path) {
        regHandle("PUT", path, [](nng_aio *aio) {
            Handle x(aio);
            x();
        });
    }

    template <typename Handle>
    void DEL(const char *path) {
        regHandle("DELETE", path, [](nng_aio *aio) {
            Handle x(aio);
            x();
        });
    }

    template <typename Handle>
    void PATCH(const char *path) {
        regHandle("PATCH", path, [](nng_aio *aio) {
            Handle x(aio);
            x();
        });
    }

    template <typename Handle>
    void regHandle(const char *method, const char *path) {
        regHandle(method, path, [](nng_aio *aio) {
            Handle x(aio);
            x();
        });
    }

private:
    void regHandle(const char *method, const char *path, void (*rest_handle)(nng_aio *));

private:
    std::string m_root_url;
    std::string m_host;
    uint16_t m_port;

private:
    static void http_exit();
    static void signal_handler(int signal);

private:
    static nng_http_server *ms_server;
};

}  // namespace hku