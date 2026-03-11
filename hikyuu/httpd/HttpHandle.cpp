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
#include "gzip/compress.hpp"
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

HttpHandle::HttpHandle(void* beast_context) : m_beast_context(beast_context) {
    if (beast_context) {
        auto* ctx = static_cast<BeastContext*>(beast_context);
        
        // 提取请求信息
        m_req_method = std::string(ctx->req.method_string());
        m_req_uri = std::string(ctx->req.target());
        m_req_body = ctx->req.body();
        m_client_ip = ctx->client_ip;
        m_client_port = ctx->client_port;
    }
}

net::awaitable<void> HttpHandle::operator()() {
    if (!m_beast_context) {
        CLS_FATAL("beast context is null!");
        co_return;
    }

    try {
        for (const auto& filter : m_filters) {
            filter(this);
        }

        std::string encodings = getReqHeader("Accept-Encoding");
        size_t pos = encodings.find("gzip");
        if (pos != std::string::npos) {
            setResHeader("Content-Encoding", "gzip");
        }

        before_run();
        
        // 协程方式调用 run 方法
        co_await run();
        
        after_run();
        
        // 将响应写入 BeastContext
        if (m_beast_context) {
            auto* ctx = static_cast<BeastContext*>(m_beast_context);
            ctx->res.result(m_res_status);
            
            // 设置响应头
            for (const auto& [key, val] : m_res_headers) {
                ctx->res.set(key, val);
            }
            
            // 设置响应体
            ctx->res.body() = m_res_body;
            ctx->res.prepare_payload();
        }

    } catch (nlohmann::json::exception& e) {
        processException(400, INVALID_JSON_REQUEST, e.what());
        auto addr = getClientAddress();
        CLS_WARN("HttpBadRequestError({}): {}, client: {}:{}, url: {}, req: {}",
                 int(INVALID_JSON_REQUEST), e.what(), addr.ip, addr.port, getReqUrl(),
                 tryGetReqData());

    } catch (HttpError& e) {
        processException(e.status(), e.errcode(), e.what());
        auto addr = getClientAddress();
        CLS_WARN("{}({}): {}, client: {}:{}, url: {}, req: {}", e.name(), e.errcode(), e.what(),
                 addr.ip, addr.port, getReqUrl(), tryGetReqData());

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

HttpHandle::ClientAddress HttpHandle::getClientAddress(bool tryFromHeader) noexcept {
    ClientAddress ret;
    
    // 从上下文中获取客户端地址
    if (!m_client_ip.empty()) {
        ret.ip = m_client_ip;
        ret.port = m_client_port;
    }

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
                    ret.ip = std::move(x);
                    break;
                }
            }
        }
    }

    return ret;
}

void HttpHandle::printTraceInfo() noexcept {
    std::string url = getReqUrl();
    std::string traceid = getReqHeader("traceid");
    if (ms_enable_only_traceid && traceid.empty()) {
        return;
    }
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
        auto client_addr = getClientAddress();
        if (traceid.empty()) {
            HKU_INFO(
              "\n{}╔════════════════════════════════════════════════════════════\n"
              "{}║  url: {}\n"
              "{}║  client: {}:{}\n"
              "{}║  method: {}\n"
              "{}║  request: {}\n"
              "{}║  response: {}\n"
              "{}╚════════════════════════════════════════",
              str, str, url, str, client_addr.ip, client_addr.port, str,
              m_req_method, str, getReqData(), str, getResData(), str);
        } else {
            HKU_INFO(
              "\n{}╔════════════════════════════════════════════════════════════\n"
              "{}║  url:{}\n"
              "{}║  client: {}:{}\n"
              "{}║  method: {}\n"
              "{}║  traceid: {}\n"
              "{}║  request: {}\n"
              "{}║  response: {}\n{}╚════════════════════════════════════════",
              str, str, url, str, client_addr.ip, client_addr.port, str,
              m_req_method, str, traceid, str, getReqData(), str,
              getResData(), str);
        }
    } catch (std::exception& e) {
        HKU_ERROR("printTraceInfo error: {}", e.what());
    } catch (...) {
        HKU_ERROR("printTraceInfo error!");
    }
}

void HttpHandle::processException(int http_status, int errcode, std::string_view err_msg) {
    try {
        m_res_status = http_status;
        m_res_headers["Content-Type"] = "application/json; charset=UTF-8";
        m_res_body = fmt::format(R"({{"ret":{},"errmsg":"{}"}})", errcode, err_msg);
        
        // 将错误响应写入 BeastContext
        if (m_beast_context) {
            auto* ctx = static_cast<BeastContext*>(m_beast_context);
            ctx->res.result(http_status);
            ctx->res.set(http::field::content_type, "application/json; charset=UTF-8");
            ctx->res.body() = m_res_body;
            ctx->res.prepare_payload();
        }
    } catch (std::exception& e) {
        CLS_ERROR("Exception in processException: {}", e.what());
    } catch (...) {
        CLS_FATAL("Unknown error in processException!");
    }
}

std::string HttpHandle::getReqUrl() const noexcept {
    return m_req_uri;
}

std::string HttpHandle::getReqHeader(const char* name) const noexcept {
    if (!m_beast_context) {
        return "";
    }
    
    auto* ctx = static_cast<BeastContext*>(m_beast_context);
    auto& req = ctx->req;
    
    // 直接遍历请求头查找
    for (auto& field : req) {
        if (field.name_string() == name) {
            return std::string(field.value());
        }
    }
    
    return "";
}

std::string HttpHandle::getReqData() {
    std::string result;
    
    std::string encoding = getReqHeader("Content-Encoding");
    if (encoding.empty()) {
        result = m_req_body;

    } else if (encoding == "gzip") {
        gzip::Decompressor decomp;
        decomp.decompress(result, m_req_body.data(), m_req_body.size());

    } else {
        throw HttpNotAcceptableError(
          HttpNotAcceptableError::UNSUPPORT_CONTENT_ENCODING,
          fmt::format("Unsupported Content-Encoding format: {}", encoding));
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
    
    if (!gzip::is_compressed(m_res_body.data(), m_res_body.size())) {
        result = m_res_body;
        return result;
    }

    gzip::Decompressor decomp;
    decomp.decompress(result, m_res_body.data(), m_res_body.size());
    return result;
}

json HttpHandle::getReqJson() {
    std::string data = getReqData();
    json result;
    try {
        if (!data.empty()) {
            result = json::parse(data);
        }
    } catch (json::exception& e) {
        HKU_ERROR("Failed parse json: {}", data);
        throw HttpBadRequestError(BadRequestErrorCode::INVALID_JSON_REQUEST, e.what());
    }
    return result;
}

bool HttpHandle::haveQueryParams() {
    return strchr(m_req_uri.c_str(), '?') != nullptr;
}

bool HttpHandle::getQueryParams(QueryParams& query_params) {
    const char* url = m_req_uri.c_str();
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

void HttpHandle::setResData(const char* content) {
    const char* encoding = nullptr;
    auto it = m_res_headers.find("Content-Encoding");
    if (it != m_res_headers.end()) {
        encoding = it->second.c_str();
    }
    
    if (!encoding) {
        m_res_body = content;
    } else {
        gzip::Compressor comp(Z_DEFAULT_COMPRESSION);
        comp.compress(m_res_body, content, strlen(content));
    }
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

}  // namespace hku
