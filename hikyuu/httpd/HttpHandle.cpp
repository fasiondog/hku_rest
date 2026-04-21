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
// #include <WS2tcpnip.h>
// #include <Ws2ipdef.h>
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
            auto result = co_await filter(this);
            if (!result) {
                processError(result.error());
                CLS_INFO("{}! {} from: {}:{}", getReqUrl(), biz_err_msg(result.error()),
                         getClientIp(), getClientPort());
                co_return;
            }
        }

        auto before_ret = before_run();
        if (!before_ret) {
            processError(before_ret.error());
            CLS_INFO("{}! {} from: {}:{}", getReqUrl(), biz_err_msg(before_ret.error()),
                     getClientIp(), getClientPort());
            co_return;
        }

        auto result = co_await run();
        if (!result) {
            processError(result.error());
            CLS_INFO("{}! {} from: {}:{}", getReqUrl(), biz_err_msg(result.error()), getClientIp(),
                     getClientPort());
            co_return;
        }

        auto after_ret = after_run();
        if (!after_ret) {
            processError(after_ret.error());
            CLS_INFO("{}! {} from: {}:{}", getReqUrl(), biz_err_msg(after_ret.error()),
                     getClientIp(), getClientPort());
            co_return;
        }

        // 如果是分块传输，响应已经在 run() 内部手动发送完成，直接返回
        if (m_chunked_transfer) {
            co_return;
        }

        // 默认响应状态码为 200，无需显式设置
        auto* ctx = static_cast<BeastContext*>(m_beast_context);
        ctx->res.prepare_payload();

    } catch (const BizException& e) {
        processBizException(e);

    } catch (const std::exception& e) {
        processException(e.what());

    } catch (...) {
        processException("Unknown error");
    }

    if (ms_enable_trace) {
        printTraceInfo();
    }

    co_return;
}

void HttpHandle::processError(int32_t err) noexcept {
    try {
        // 直接设置错误响应的状态码和数据
        auto* ctx = static_cast<BeastContext*>(m_beast_context);
        ctx->res.result(http::status::ok);  // 传输成功，统一返回200
        ctx->res.set(http::field::content_type, "application/json; charset=UTF-8");
        ctx->res.body() = fmt::format(R"({{"ret":{},"errmsg":"{}"}})", err, biz_err_msg(err));
        ctx->res.prepare_payload();
    } catch (std::exception& e) {
        CLS_ERROR("Exception in processError: {}", e.what());
    } catch (...) {
        CLS_FATAL("Unknown error in processError!");
    }

    if (ms_enable_trace) {
        printTraceInfo();
    }
}

void HttpHandle::processBizException(const BizException& e) noexcept {
    try {
        CLS_ERROR("{}", e.what());
        auto* ctx = static_cast<BeastContext*>(m_beast_context);
        ctx->res.result(http::status::ok);  // 传输成功，统一返回200
        ctx->res.set(http::field::content_type, "application/json; charset=UTF-8");
        ctx->res.body() = fmt::format(R"({{"ret":{},"errmsg":"{}"}})", e.errcode(), e.what());
        ctx->res.prepare_payload();
    } catch (std::exception& e) {
        CLS_ERROR("Exception in processException: {}", e.what());
    } catch (...) {
        CLS_FATAL("Unknown error in processException!");
    }
}

void HttpHandle::processException(const char* err_msg) noexcept {
    CLS_ERROR("{}", err_msg);
    try {
        // 直接设置错误响应的状态码和数据
        auto* ctx = static_cast<BeastContext*>(m_beast_context);
        ctx->res.result(http::status::internal_server_error);
        ctx->res.set(http::field::content_type, "application/json; charset=UTF-8");
        ctx->res.body() = fmt::format(R"({{"ret":{},"errmsg":"{}"}})", BIZ_BASE_FAILED, err_msg);
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
    auto* ctx = static_cast<BeastContext*>(m_beast_context);
    ret = ctx->req.method_string();
    return ret;
}

std::string HttpHandle::getReqUrl() const noexcept {
    std::string ret;
    auto* ctx = static_cast<BeastContext*>(m_beast_context);
    ret = ctx->req.target();
    return ret;
}

std::string HttpHandle::getReqHeader(const char* name) const noexcept {
    std::string ret;
    auto* ctx = static_cast<BeastContext*>(m_beast_context);
    auto& req = ctx->req;

    // 直接遍历请求头查找
    for (const auto& field : req) {
        if (field.name_string() == name) {
            ret = field.value();
            break;
        }
    }

    return ret;
}

std::string HttpHandle::getReqData() const noexcept {
    std::string result;
    auto* ctx = static_cast<BeastContext*>(m_beast_context);

    try {
        std::string encoding = getReqHeader("Content-Encoding");
        if (encoding == "gzip") {
            gzip::Decompressor decomp;
            decomp.decompress(result, ctx->req.body().data(), ctx->req.body().size());

        } else {
            result = ctx->req.body();
        }
    } catch (std::exception& e) {
        CLS_ERROR("{}", e.what());
    }

    return result;
}

std::string HttpHandle::getResData() const {
    std::string result;
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
    auto* ctx = static_cast<BeastContext*>(m_beast_context);
    std::string_view target = ctx->req.target();
    return target.find('?') != std::string_view::npos;
}

BizResult<HttpHandle::QueryParams> HttpHandle::getQueryParams() const noexcept {
    QueryParams query_params;
    auto* ctx = static_cast<BeastContext*>(m_beast_context);
    std::string_view target = ctx->req.target();

    // 长度限制，防止超长 URL 攻击
    constexpr std::size_t MAX_URL_LENGTH = 8192;
    if (target.size() > MAX_URL_LENGTH) {
        CLS_WARN("URL length exceeds limit (max={}, actual={}, client={}:{})", MAX_URL_LENGTH,
                 target.size(), getClientIp(), getClientPort());
        return BIZ_BASE_TOO_LONG_URL;
    }

    // 使用 string_view 安全地找到 query string，避免依赖 \0 终止
    size_t qpos = target.find('?');
    if (qpos == std::string_view::npos) {
        return query_params;
    }

    // query string 从 '?' 后面开始
    std::string_view query = target.substr(qpos + 1);
    const char* p = query.data();
    size_t remaining = query.size();  // 剩余字符数，用于安全遍历

    if (!p || remaining == 0) {
        return query_params;
    }

    enum {
        s_key,
        s_value,
    } state = s_key;

    const char* key = p;
    const char* value = NULL;
    size_t key_len = 0;
    size_t value_len = 0;

    // URL 参数数量限制，防止哈希碰撞 DoS 攻击
    constexpr std::size_t MAX_QUERY_PARAMS = 100;  // 最大允许 100 个查询参数
    std::size_t param_count = 0;

    size_t idx = 0;
    while (idx < remaining) {
        char c = p[idx];
        if (c == '&') {
            if (key_len && value_len) {
                // 检查参数数量是否超过限制
                if (++param_count > MAX_QUERY_PARAMS) {
                    CLS_WARN("Query parameters exceed limit (max={}, client={}:{})",
                             MAX_QUERY_PARAMS, getClientIp(), getClientPort());
                    return BIZ_BASE_TOO_MANY_QUERY_PARAMS;
                }

                std::string strkey = std::string(key, key_len);
                std::string strvalue = std::string(value, value_len);
                // 先做 unescape，确保临时字符串在 unescape 完成前有效
                std::string unescaped_key = url_unescape(strkey.c_str());
                std::string unescaped_value = url_unescape(strvalue.c_str());
                // 显式保留 unescape 结果后再插入 map
                query_params[unescaped_key] = std::move(unescaped_value);
            } else if (key_len && !value_len) {
                // 只有 key 没有 value，视为非法请求
                CLS_WARN("Invalid query parameter format: missing value for key '{}', client={}:{}",
                         std::string(key, key_len), getClientIp(), getClientPort());
                return BIZ_BASE_INVALID_URL;
            }
            key_len = value_len = 0;
            state = s_key;
            key = p + idx + 1;
        } else if (c == '=') {
            state = s_value;
            value = p + idx + 1;
        } else {
            if (state == s_key) {
                ++key_len;
            } else {
                ++value_len;
            }
        }
        ++idx;
    }

    // 处理最后一个参数
    if (key_len && value_len) {
        if (++param_count > MAX_QUERY_PARAMS) {
            CLS_WARN("Query parameters exceed limit (max={}, client={}:{})", MAX_QUERY_PARAMS,
                     getClientIp(), getClientPort());
            return BIZ_BASE_TOO_MANY_QUERY_PARAMS;
        }

        std::string strkey = std::string(key, key_len);
        std::string strvalue = std::string(value, value_len);
        // 先做 unescape，确保临时字符串在 unescape 完成前有效
        std::string unescaped_key = url_unescape(strkey.c_str());
        std::string unescaped_value = url_unescape(strvalue.c_str());
        // 显式保留 unescape 结果后再插入 map
        query_params[unescaped_key] = std::move(unescaped_value);
    } else if (key_len && !value_len) {
        // 最后一个参数只有 key 没有 value，视为非法请求
        CLS_WARN("Invalid query parameter format: missing value for key '{}', client={}:{}",
                 std::string(key, key_len), getClientIp(), getClientPort());
        return BIZ_BASE_INVALID_URL;
    }

    return query_params;
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
    auto* ctx = static_cast<BeastContext*>(m_beast_context);
    return ctx->client_port;
}

// ========== 流式分批传输实现（新增）==========

net::awaitable<bool> HttpHandle::writeChunk(const std::string& data) noexcept {
    if (!m_chunked_transfer) {
        CLS_ERROR("Chunked transfer not enabled! Call enableChunkedTransfer() first.");
        co_return false;
    }

    try {
        auto* ctx = static_cast<BeastContext*>(m_beast_context);

        // 如果是第一次写入，需要先发送响应头
        if (!m_headers_sent) {
            m_headers_sent = true;

            // 设置 Transfer-Encoding: chunked
            ctx->res.set(http::field::transfer_encoding, "chunked");

            // 准备空的响应体（重要：这会移除 Content-Length）
            ctx->res.body() = "";

            // 不要调用 prepare_payload()！它会设置 Content-Length，而分块传输不需要
            // ctx->res.prepare_payload();

            // 手动构造 HTTP 响应头字符串
            std::string header_str;
            header_str.reserve(512);

            // 状态行
            header_str +=
              fmt::format("HTTP/1.1 {} {}\r\n", ctx->res.result_int(), ctx->res.reason());

            // 所有响应头
            for (const auto& field : ctx->res) {
                header_str += fmt::format("{}: {}\r\n", field.name_string(), field.value());
            }

            // 空行表示头结束
            header_str += "\r\n";

            // 同步写入响应头
            beast::error_code ec;
            net::write(ctx->socket, net::buffer(header_str), ec);

            if (ec.failed()) {
                CLS_ERROR("Failed to send response headers: {}", ec.message());
                co_return false;
            }
        }

        // 构造 HTTP 分块
        // 格式：chunk-size\r\nchunk-data\r\n
        std::string chunk = fmt::format("{:x}\r\n", data.size());
        chunk += data;
        chunk += "\r\n";

        // 写入 socket
        beast::error_code ec;
        co_await net::async_write(ctx->socket, net::buffer(chunk), net::use_awaitable);

        co_return !ec.failed();

    } catch (const std::exception& e) {
        CLS_ERROR("writeChunk error: {}", e.what());
        co_return false;
    } catch (...) {
        CLS_ERROR("writeChunk unknown error");
        co_return false;
    }
}

bool HttpHandle::writeChunkSync(const std::string& data) noexcept {
    if (!m_chunked_transfer) {
        CLS_ERROR("Chunked transfer not enabled!");
        return false;
    }

    try {
        auto* ctx = static_cast<BeastContext*>(m_beast_context);

        // 如果是第一次写入，需要先发送响应头
        if (!m_headers_sent) {
            m_headers_sent = true;

            // 设置 Transfer-Encoding: chunked
            ctx->res.set(http::field::transfer_encoding, "chunked");

            // 准备空的响应体（重要：这会移除 Content-Length）
            ctx->res.body() = "";
            // 不要调用 prepare_payload()！它会设置 Content-Length，而分块传输不需要

            // 手动构造 HTTP 响应头字符串
            std::string header_str;
            header_str.reserve(512);

            // 状态行
            header_str +=
              fmt::format("HTTP/1.1 {} {}\r\n", ctx->res.result_int(), ctx->res.reason());

            // 所有响应头
            for (const auto& field : ctx->res) {
                header_str += fmt::format("{}: {}\r\n", field.name_string(), field.value());
            }

            // 空行表示头结束
            header_str += "\r\n";

            // 同步写入响应头
            beast::error_code ec;
            net::write(ctx->socket, net::buffer(header_str), ec);

            if (ec.failed()) {
                CLS_ERROR("Failed to send response headers: {}", ec.message());
                return false;
            }
        }

        // 构造 HTTP 分块
        std::string chunk = fmt::format("{:x}\r\n", data.size());
        chunk += data;
        chunk += "\r\n";

        // 同步写入数据块
        beast::error_code ec;
        net::write(ctx->socket, net::buffer(chunk), ec);

        return !ec.failed();

    } catch (const std::exception& e) {
        CLS_ERROR("writeChunkSync error: {}", e.what());
        return false;
    } catch (...) {
        return false;
    }
}

net::awaitable<bool> HttpHandle::finishChunkedTransfer() noexcept {
    if (!m_chunked_transfer) {
        CLS_ERROR("Chunked transfer not enabled!");
        co_return false;
    }

    try {
        auto* ctx = static_cast<BeastContext*>(m_beast_context);

        // 发送最后一个空块表示结束：0\r\n\r\n
        std::string final_chunk = "0\r\n\r\n";

        beast::error_code ec;
        co_await net::async_write(ctx->socket, net::buffer(final_chunk), net::use_awaitable);

        // 重置状态并标记响应已发送
        m_chunked_transfer = false;
        m_headers_sent = false;
        ctx->response_sent = true;

        co_return !ec.failed();

    } catch (const std::exception& e) {
        CLS_ERROR("finishChunkedTransfer error: {}", e.what());
        // 即使出错也要标记响应已发送，防止框架再次发送响应
        if (m_beast_context) {
            auto* ctx = static_cast<BeastContext*>(m_beast_context);
            ctx->response_sent = true;
        }
        co_return false;
    } catch (...) {
        CLS_ERROR("finishChunkedTransfer unknown error");
        // 即使出错也要标记响应已发送，防止框架再次发送响应
        if (m_beast_context) {
            auto* ctx = static_cast<BeastContext*>(m_beast_context);
            ctx->response_sent = true;
        }
        co_return false;
    }
}

bool HttpHandle::finishChunkedTransferSync() noexcept {
    if (!m_chunked_transfer) {
        CLS_ERROR("Chunked transfer not enabled!");
        return false;
    }

    try {
        auto* ctx = static_cast<BeastContext*>(m_beast_context);

        // 发送最后一个空块
        std::string final_chunk = "0\r\n\r\n";

        beast::error_code ec;
        net::write(ctx->socket, net::buffer(final_chunk), ec);

        // 重置状态并标记响应已发送
        m_chunked_transfer = false;
        m_headers_sent = false;
        ctx->response_sent = true;

        return !ec.failed();

    } catch (const std::exception& e) {
        CLS_ERROR("finishChunkedTransferSync error: {}", e.what());
        // 即使出错也要标记响应已发送，防止框架再次发送响应
        if (m_beast_context) {
            auto* ctx = static_cast<BeastContext*>(m_beast_context);
            ctx->response_sent = true;
        }
        return false;
    } catch (...) {
        // 即使出错也要标记响应已发送，防止框架再次发送响应
        if (m_beast_context) {
            auto* ctx = static_cast<BeastContext*>(m_beast_context);
            ctx->response_sent = true;
        }
        return false;
    }
}

}  // namespace hku
