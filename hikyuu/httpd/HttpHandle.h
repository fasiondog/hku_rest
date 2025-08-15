/*
 *  Copyright(C) 2021 hikyuu.org
 *
 *  Create on: 2021-02-28
 *     Author: fasiondog
 */

#pragma once

#include <string_view>
#include <vector>
#include <functional>

#include <nlohmann/json.hpp>
#include "HttpError.h"
#include "hikyuu/utilities/Log.h"
#include "hikyuu/httpd/pod/MOHelper.h"

#ifndef HKU_HTTPD_API
#define HKU_HTTPD_API
#endif

using json = nlohmann::json;                  // 不保持插入排序
using ordered_json = nlohmann::ordered_json;  // 保持插入排序

namespace hku {

// 仅内部使用
#define NNG_CHECK(rv)                                      \
    {                                                      \
        if (rv != 0) {                                     \
            HKU_THROW("[NNG_ERROR] {}", nng_strerror(rv)); \
        }                                                  \
    }

// 仅内部使用
#define NNG_CHECK_M(rv, msg)                                             \
    {                                                                    \
        if (rv != 0) {                                                   \
            HKU_THROW("[HTTP_ERROR] {} err: {}", msg, nng_strerror(rv)); \
        }                                                                \
    }

class HKU_HTTPD_API HttpHandle {
    CLASS_LOGGER_IMP(HttpHandle)

public:
    HttpHandle() = delete;
    HttpHandle(nng_aio *aio);
    virtual ~HttpHandle() {}

    /** 前处理 */
    virtual void before_run() {}

    /** 响应处理 */
    virtual void run() = 0;

    /** 后处理 */
    virtual void after_run() {}

    void addFilter(std::function<void(HttpHandle *)> filter) {
        m_filters.push_back(filter);
    }

    /** 获取请求的 url */
    std::string getReqUrl() const noexcept;

    /**
     * 获取请求头部信息
     * @param name 头部信息名称
     * @return 如果获取不到将返回""
     */
    std::string getReqHeader(const char *name) const noexcept;

    /**
     * 获取请求头部信息
     * @param name 头部信息名称
     * @return 如果获取不到将返回""
     */
    std::string getReqHeader(const std::string &name) const noexcept {
        return getReqHeader(name.c_str());
    }

    /** 根据 Content-Encoding 进行解码，返回解码后的请求数据 */
    std::string getReqData();

    /**
     * 尝试获取请求数据，如果无法获取到数据，则返回空字符串
     * @return 请求数据
     */
    std::string tryGetReqData() noexcept;

    /** 返回请求的 json 数据，如无法解析为json，将抛出异常*/
    json getReqJson();

    /** 判断请求的 ulr 中是否包含 query 参数 */
    bool haveQueryParams();

    typedef std::unordered_map<std::string, std::string> QueryParams;

    /**
     * 获取 query 参数
     * @param query_params [out] 输出 query 参数
     * @return true | false 获取或解析失败
     */
    bool getQueryParams(QueryParams &query_params);

    void setResStatus(uint16_t status) {
        NNG_CHECK(nng_http_res_set_status(m_nng_res, status));
    }

    void setResHeader(const char *key, const char *val) {
        NNG_CHECK(nng_http_res_set_header(m_nng_res, key, val));
    }

    /** 设置响应数据，并根据 Content-encoding 进行 gzip 压缩 */
    void setResData(const char *content);

    void setResData(const std::string &content) {
        setResData(content.c_str());
    }

    void setResData(const json &data) {
        setResData(data.dump());
    }

    void setResData(const ordered_json &data) {
        setResData(data.dump());
    }

    /** 获取当前的相应数据 */
    std::string getResData() const;

    /**
     * 从 Accept-Language 获取第一个语言类型
     * @note 非严格 html 协议，仅返回排在最前面的语言类型
     */
    std::string getLanguage() const;

    /**
     * 多语言翻译
     * @param msgid 待翻译的字符串
     */
    std::string _tr(const char *msgid) const {
        return pod::MOHelper::translate(getLanguage(), msgid);
    }

    /**
     * 多语言翻译
     * @param ctx 翻译上下文
     * @param msgid 待翻译的字符串
     */
    std::string _ctr(const char *ctx, const char *msgid) {
        return pod::MOHelper::translate(getLanguage(), ctx, msgid);
    }

    void operator()();

protected:
    struct ClientAddress {
        std::string ip;
        uint16_t port = 0;

        ClientAddress() = default;
        ClientAddress(const ClientAddress &) = default;
        ClientAddress(ClientAddress &&rhs) : ip(std::move(rhs.ip)), port(rhs.port) {
            rhs.port = 0;
        }

        ClientAddress &operator=(const ClientAddress &) = default;
        ClientAddress &operator=(ClientAddress &&rhs) {
            if (this != &rhs) {
                ip = std::move(rhs.ip);
                port = rhs.port;
                rhs.port = 0;
            }
            return *this;
        }
    };

    /**
     * 获取客户端地址
     * @param tryFromHeader 优先尝试从请求头中获取真实客户ip，否则为直连对端ip
     * @return ClientAddress
     * @note port 始终为直连对端的port（即可能是代理的port)。
     */
    ClientAddress getClientAddress(bool tryFromHeader = true) noexcept;

private:
    void processException(int http_status, int errcode, std::string_view err_msg);

protected:
    nng_aio *m_http_aio{nullptr};
    nng_http_res *m_nng_res{nullptr};
    nng_http_req *m_nng_req{nullptr};
    std::vector<std::function<void(HttpHandle *)>> m_filters;

public:
    static void enableTrace(bool enable, bool only_traceid = false) {
        ms_enable_trace = enable;
        ms_enable_only_traceid = only_traceid;
    }

protected:
    void printTraceInfo() noexcept;

private:
    // 是否跟踪请求打印
    static bool ms_enable_trace;
    static bool ms_enable_only_traceid;
};

#define HTTP_HANDLE_IMP(cls) \
public:                      \
    cls(nng_aio *aio) : HttpHandle(aio) {}

}  // namespace hku
