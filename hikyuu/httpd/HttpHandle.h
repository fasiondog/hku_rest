/*
 *  Copyright(C) 2021 hikyuu.org
 *
 *  Create on: 2021-02-28
 *     Author: fasiondog
 */

#pragma once

#include <nlohmann/json.hpp>
#include "config.h"
#include "HttpError.h"
#include "hikyuu/utilities/Log.h"
#include "hikyuu/httpd/pod/MOHelper.h"

#include <string>
#include <memory>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <coroutine>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>

#ifndef HKU_HTTPD_API
#define HKU_HTTPD_API
#endif

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

using json = nlohmann::json;                  // 不保持插入排序
using ordered_json = nlohmann::ordered_json;  // 保持插入排序

namespace hku {

// 前向声明
class HttpHandle;

/**
 * HTTP 路由器 - 负责注册和分发请求到对应的 Handle
 */
class Router {
public:
    using HandlerFunc = std::function<net::awaitable<void>(void*)>;

    struct RouteKey {
        std::string method;
        std::string path;

        bool operator==(const RouteKey& other) const {
            return method == other.method && path == other.path;
        }
    };

    void registerHandler(const std::string& method, const std::string& path, HandlerFunc handler);
    HandlerFunc findHandler(const std::string& method, const std::string& path);

private:
    // 使用 vector 存储路由表，避免 map 的哈希开销和动态分配
    // 路由数量有限（通常 < 100），线性搜索性能足够且缓存友好
    std::vector<std::pair<RouteKey, HandlerFunc>> m_routes;
};

/**
 * Beast 上下文 - 封装 beast 的请求和响应对象
 */
struct BeastContext {
    http::request<http::string_body> req;
    http::response<http::string_body> res;
    tcp::socket& socket;
    net::steady_timer timer;
    std::string client_ip;
    uint16_t client_port = 0;
    beast::flat_buffer buffer;  // 用于读取请求的缓冲区

    BeastContext(tcp::socket& sock, net::io_context& io_ctx) : socket(sock), timer(io_ctx) {}
};

class HKU_HTTPD_API HttpHandle {
    CLASS_LOGGER_IMP(HttpHandle)

public:
    HttpHandle() = delete;
    explicit HttpHandle(void* beast_context);
    virtual ~HttpHandle() {}

    /** 前处理 */
    virtual void before_run() {}

    /** 响应处理（支持协程）*/
    virtual net::awaitable<void> run() = 0;

    /** 后处理 */
    virtual void after_run() {}

    void addFilter(std::function<void(HttpHandle*)> filter) {
        m_filters.push_back(filter);
    }

    /** 获取请求的 url */
    std::string getReqUrl() const noexcept;

    /** 获取请求方法 */
    std::string getReqMethod() const noexcept;

    /**
     * 获取请求头部信息
     * @param name 头部信息名称
     * @return 如果获取不到将返回""
     */
    std::string getReqHeader(const char* name) const noexcept;

    /**
     * 获取请求头部信息
     * @param name 头部信息名称
     * @return 如果获取不到将返回""
     */
    std::string getReqHeader(const std::string& name) const noexcept {
        return getReqHeader(name.c_str());
    }

    /** 根据 Content-Encoding 进行解码，返回解码后的请求数据 */
    std::string getReqData();

    /**
     * 尝试获取请求数据，如果无法获取到数据，则返回空字符串
     * @return 请求数据
     */
    std::string tryGetReqData() noexcept;

    /** 返回请求的 json 数据，如无法解析为 json，将抛出异常*/
    json getReqJson();

    /** 判断请求的 ulr 中是否包含 query 参数 */
    bool haveQueryParams() const noexcept;

    typedef std::unordered_map<std::string, std::string> QueryParams;

    /**
     * 获取 query 参数
     * @param query_params [out] 输出 query 参数
     * @return true | false 获取或解析失败
     */
    bool getQueryParams(QueryParams& query_params) const noexcept;

    void setResStatus(uint16_t status) {
        m_res_status = status;
    }

    void setResHeader(const char* key, const char* val) {
        m_res_headers[key] = val;
    }

    /** 设置响应数据，并根据 Content-encoding 进行 gzip 压缩 */
    void setResData(const char* content);

    void setResData(const std::string& content) {
        setResData(content.c_str());
    }

    void setResData(const json& data) {
        setResData(data.dump());
    }

    void setResData(const ordered_json& data) {
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
    std::string _tr(const char* msgid) const {
        return pod::MOHelper::translate(getLanguage(), msgid);
    }

    /**
     * 多语言翻译
     * @param ctx 翻译上下文
     * @param msgid 待翻译的字符串
     */
    std::string _ctr(const char* ctx, const char* msgid) {
        return pod::MOHelper::translate(getLanguage(), ctx, msgid);
    }

    /** 协程方式的调用入口 */
    net::awaitable<void> operator()();

protected:
    /**
     * 获取客户端 IP 地址
     * @param tryFromHeader 优先尝试从请求头中获取真实客户 ip，否则为直连对端 ip
     * @return 客户端 IP 地址
     * @note 如果 tryFromHeader 为 true，会依次检查 X-Real-IP、X-Forwarded-For 等头部
     */
    std::string getClientIp(bool tryFromHeader = true) const noexcept;

    /**
     * 获取客户端端口
     * @return 客户端端口（直连对端的 port，可能是代理的 port）
     */
    uint16_t getClientPort() const noexcept;

private:
    void processException(int http_status, int errcode, std::string_view err_msg);

protected:
    void* m_beast_context{nullptr};  // boost::beast 上下文
    std::vector<std::function<void(HttpHandle*)>> m_filters;

    // 响应数据
    uint16_t m_res_status{200};
    std::map<std::string, std::string> m_res_headers;
    std::string m_res_body;

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
    explicit cls(void* beast_context) : HttpHandle(beast_context) {}

}  // namespace hku
