/*
 *  Copyright(C) 2021 hikyuu.org
 *
 *  Create on: 2021-02-28
 *     Author: fasiondog
 */

#include <string_view>
#include <hikyuu/utilities/arithmetic.h>
#include <hikyuu/utilities/datetime/Datetime.h>
#include <hikyuu/utilities/http_client/url.h>
#include "gzip/decompress.hpp"
#include "gzip/utils.hpp"
#include "HttpHandle.h"

#include <hikyuu/utilities/osdef.h>
#if HKU_OS_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <WS2tcpnip.h>
#include <Ws2ipdef.h>
#else
#include <arpa/inet.h>
#endif

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace hku {

bool HttpHandle::ms_enable_trace = false;
bool HttpHandle::ms_enable_only_traceid = false;

HttpHandle::HttpHandle(void* beast_context) : m_beast_context(beast_context) {}

net::awaitable<void> HttpHandle::operator()() {
    if (!m_beast_context) {
        CLS_FATAL("beast context is null!");
        co_return;
    }

    try {
        for (const auto& filter : m_filters) {
            co_await filter(this);
        }

        co_await before_run();
        co_await run();
        co_await after_run();

        // 默认响应状态码为 200，无需显式设置
        auto* ctx = static_cast<BeastContext*>(m_beast_context);
        ctx->res.prepare_payload();

    } catch (HttpError& e) {
        processException(e.status(), e.errcode(), e.what());
        CLS_WARN("{}({}): {}, client: {}:{}, url: {}, req: {}", e.name(), e.errcode(), e.what(),
                 getClientIp(), getClientPort(), getReqUrl(), tryGetReqData());

    } catch (std::exception& e) {
        processException(500, 500, e.what());
        CLS_ERROR("HttpError({}): {}", int(500), e.what());

    } catch (...) {
        processException(500, 500, "Unknown error");
        CLS_ERROR("HttpError({}): {}", int(500), "Unknown error");
    }

    if (ms_enable_trace) {
        printTraceInfo();
    }

    co_return;
}

void HttpHandle::processException(int http_status, int errcode, std::string_view err_msg) {
    try {
        // 直接设置错误响应的状态码和数据
        auto* ctx = static_cast<BeastContext*>(m_beast_context);
        ctx->res.result(http_status);
        ctx->res.set(http::field::content_type, "application/json; charset=UTF-8");
        ctx->res.body() = fmt::format(R"({{"ret":{},"errmsg":"{}"}})", errcode, err_msg);
        ctx->res.prepare_payload();
    } catch (std::exception& e) {
        CLS_ERROR("Exception in processException: {}", e.what());
    } catch (...) {
        CLS_FATAL("Unknown error in processException!");
    }
}

void HttpHandle::printTraceInfo() noexcept {
    std::string traceid = getReqHeader("traceid");
    if (ms_enable_only_traceid && traceid.empty()) {
        return;
    }

    std::string url = getReqUrl();
    Datetime now = Datetime::now();
#if FMT_VERSION >= 90000
    std::string str = fmt::format(
      "{:>4d}-{:0>2d}-{:0>2d} {:0>2d}:{:0>2d}:{:0>2d}.{:0<3d} [trace] - ", now.year(), now.month(),
      now.day(), now.hour(), now.minute(), now.second(), now.millisecond());
#else
    std::string str = fmt::format(
      "{:>4d}-{:>02d}-{:>02d} {:>02d}:{:>02d}:{:>02d}.{:<03d} [trace] - ", now.year(), now.month(),
      now.day(), now.hour(), now.minute(), now.second(), now.millisecond());
#endif

    try {
        std::string client_ip = getClientIp();
        uint16_t client_port = getClientPort();
        if (traceid.empty()) {
            HKU_INFO(
              "\n{}╔════════════════════════════════════════════════════════════\n"
              "{}║  url: {}\n"
              "{}║  client: {}:{}\n"
              "{}║  method: {}\n"
              "{}║  request: {}\n"
              "{}║  response: {}\n"
              "{}╚════════════════════════════════════════",
              str, str, url, str, client_ip, client_port, str, getReqMethod(), str, getReqData(),
              str, getResData(), str);
        } else {
            HKU_INFO(
              "\n{}╔════════════════════════════════════════════════════════════\n"
              "{}║  url:{}\n"
              "{}║  client: {}:{}\n"
              "{}║  method: {}\n"
              "{}║  traceid: {}\n"
              "{}║  request: {}\n"
              "{}║  response: {}\n{}╚════════════════════════════════════════",
              str, str, url, str, client_ip, client_port, str, getReqMethod(), str, traceid, str,
              getReqData(), str, getResData(), str);
        }
    } catch (std::exception& e) {
        HKU_ERROR("printTraceInfo error: {}", e.what());
    } catch (...) {
        HKU_ERROR("printTraceInfo error!");
    }
}

std::string HttpHandle::getReqMethod() const noexcept {
    std::string ret;
    HKU_IF_RETURN(!m_beast_context, ret);
    auto* ctx = static_cast<BeastContext*>(m_beast_context);
    ret = ctx->req.method_string();
    return ret;
}

std::string HttpHandle::getReqUrl() const noexcept {
    std::string ret;
    HKU_IF_RETURN(!m_beast_context, ret);
    auto* ctx = static_cast<BeastContext*>(m_beast_context);
    ret = ctx->req.target();
    return ret;
}

std::string HttpHandle::getReqHeader(const char* name) const noexcept {
    std::string ret;
    HKU_IF_RETURN(!m_beast_context, ret);

    auto* ctx = static_cast<BeastContext*>(m_beast_context);
    auto& req = ctx->req;

    // 直接遍历请求头查找
    for (auto& field : req) {
        if (field.name_string() == name) {
            ret = field.value();
            break;
        }
    }

    return ret;
}

std::string HttpHandle::getReqData() {
    std::string result;
    if (!m_beast_context) {
        return result;
    }

    auto* ctx = static_cast<BeastContext*>(m_beast_context);

    std::string encoding = getReqHeader("Content-Encoding");
    if (encoding.empty()) {
        result = ctx->req.body();

    } else if (encoding == "gzip") {
        gzip::Decompressor decomp;
        decomp.decompress(result, ctx->req.body().data(), ctx->req.body().size());

    } else {
        throw HttpNotAcceptableError(
          HttpNotAcceptableError::UNSUPPORT_CONTENT_ENCODING,
          fmt::format("Unsupported Content-Encoding format: {}! only gzip", encoding));
    }

    return result;
}

std::string HttpHandle::tryGetReqData() noexcept {
    try {
        return getReqData();
    } catch (...) {
        return std::string();
    }
}

std::string HttpHandle::getResData() const {
    std::string result;
    HKU_IF_RETURN(!m_beast_context, result);

    auto* ctx = static_cast<BeastContext*>(m_beast_context);
    auto& body = ctx->res.body();

    if (!gzip::is_compressed(body.data(), body.size())) {
        result = body;
        return result;
    }

    gzip::Decompressor decomp;
    decomp.decompress(result, body.data(), body.size());
    return result;
}

bool HttpHandle::haveQueryParams() const noexcept {
    if (!m_beast_context) {
        return false;
    }

    auto* ctx = static_cast<BeastContext*>(m_beast_context);
    std::string_view target = ctx->req.target();
    return target.find('?') != std::string_view::npos;
}

bool HttpHandle::getQueryParams(QueryParams& query_params) const noexcept {
    if (!m_beast_context) {
        return false;
    }

    auto* ctx = static_cast<BeastContext*>(m_beast_context);
    const char* url = ctx->req.target().data();
    CLS_IF_RETURN(!url, false);

    const char* p = strchr(url, '?');
    CLS_IF_RETURN(!p, false);

    p = p + 1;

    enum {
        s_key,
        s_value,
    } state = s_key;

    const char* key = p;
    const char* value = NULL;
    int key_len = 0;
    int value_len = 0;
    while (*p != '\0') {
        if (*p == '&') {
            if (key_len && value_len) {
                std::string strkey = std::string(key, key_len);
                std::string strvalue = std::string(value, value_len);
                query_params[url_unescape(strkey.c_str())] = url_unescape(strvalue.c_str());
                key_len = value_len = 0;
            }
            state = s_key;
            key = p + 1;
        } else if (*p == '=') {
            state = s_value;
            value = p + 1;
        } else {
            state == s_key ? ++key_len : ++value_len;
        }
        ++p;
    }
    if (key_len && value_len) {
        std::string strkey = std::string(key, key_len);
        std::string strvalue = std::string(value, value_len);
        query_params[url_unescape(strkey.c_str())] = url_unescape(strvalue.c_str());
        key_len = value_len = 0;
    }

    return query_params.size() != 0;
}

std::string HttpHandle::getLanguage() const {
    std::string lang = getReqHeader("Accept-Language");
    auto pos = lang.find_first_of(',');
    if (pos != std::string::npos) {
        lang = lang.substr(0, pos);
    }
    if (!lang.empty()) {
        to_lower(lang);
    }
    return lang;
}

std::string HttpHandle::getClientIp(bool tryFromHeader) const noexcept {
    std::string result;
    HKU_IF_RETURN(!m_beast_context, result);

    auto* ctx = static_cast<BeastContext*>(m_beast_context);
    result = ctx->client_ip;

    if (tryFromHeader) {
        std::string ip_hdr;
        std::string unknown("unknown");
        ip_hdr = getReqHeader("X-Real-IP");
        to_lower(ip_hdr);
        if (ip_hdr.empty() || ip_hdr == unknown) {
            ip_hdr = getReqHeader("X-Forwarded-For");
            to_lower(ip_hdr);
        }
        if (ip_hdr.empty() || ip_hdr == unknown) {
            ip_hdr = getReqHeader("Proxy-Client-IP");
            to_lower(ip_hdr);
        }
        if (ip_hdr.empty() || ip_hdr == unknown) {
            ip_hdr = getReqHeader("WL-Proxy-Client-IP");
            to_lower(ip_hdr);
        }

        if (!ip_hdr.empty()) {
            auto ips = split(ip_hdr, ",");
            for (auto&& x : ips) {
                if (!x.empty() && x != unknown) {
                    result = std::move(x);
                    break;
                }
            }
        }
    }

    return result;
}

uint16_t HttpHandle::getClientPort() const noexcept {
    HKU_IF_RETURN(!m_beast_context, 0);
    auto* ctx = static_cast<BeastContext*>(m_beast_context);
    return ctx->client_port;
}
}  // namespace hku
