/*
 *  Copyright(C) 2021 hikyuu.org
 *
 *  Create on: 2021-02-28
 *     Author: fasiondog
 */

#pragma once

#include "config.h"
#include "HttpError.h"
#include "hikyuu/utilities/Log.h"
#include "hikyuu/httpd/pod/MOHelper.h"
#include "HttpConfig.h"
#include "expected.h"

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

namespace hku {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

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
    net::io_context* io_ctx_ptr{nullptr};

    // 新增：取消令牌源，用于主动中断超时操作
    net::cancellation_signal cancel_signal;

    // P99 优化：Keep-Alive 状态标记
    bool keep_alive = true;

    // P99 优化：复用 request_parser 对象，避免每次请求都创建
    std::optional<http::request_parser<http::string_body>> parser;

    // 流式传输支持：标记响应是否已手动发送
    bool response_sent = false;

    BeastContext(tcp::socket& sock, net::io_context& io_context)
    : socket(sock),
      timer(io_context),
      buffer(HttpConfig::BUFFER_MIN_CAPACITY),
      io_ctx_ptr(&io_context) {}
};

class HKU_HTTPD_API HttpHandle {
    CLASS_LOGGER_IMP(HttpHandle)

public:
    HttpHandle() = delete;
    explicit HttpHandle(void* beast_context);
    virtual ~HttpHandle() {}

    /** 前处理 */
    virtual net::awaitable<stdx::expected<Ok, Error>> before_run() noexcept {
        co_return Ok{};
    }

    /**
     * 响应处理 (支持协程)
     *
     * @note 此方法会被 Connection::processHandle 统一包裹在超时保护中
     * @note 框架会在 processHandle 中为整个 Handle 执行设置总处理超时 (默认 60 秒)
     * @note 超时会通过 BeastContext::cancel_signal 主动取消协程执行
     *
     * 超时与中断机制说明:
     * - 取消信号通过 BeastContext::cancel_signal 发送，能够中断以下场景:
     *   1. [可中断] 异步 IO 操作：如 async_read、async_write、async_timer 等 Boost.Asio 原生操作
     *   2. [可中断] 支持取消令牌的协程：使用 net::bind_cancellation 或检查 cancellation_state
     * 的协程
     *   3. [可中断] 定期检测取消状态的循环：在循环中检查
     * net::this_coro::cancellation_state().cancelled()
     *   4. [可中断] 链式异步调用：所有子协程都传递取消令牌的复合异步操作
     *
     * [无法中断] 以下场景无法被自动中断，需手动实现取消逻辑:
     *   1. 纯 CPU 密集型计算 (无挂起点的循环)
     *   2. 同步阻塞调用 (如 std::this_thread::sleep_for)
     *   3. 未检查取消状态的第三方库调用
     *   4. 死循环或逻辑错误导致的无限等待
     *
     * 最佳实践:
     *   1. 避免长时间同步阻塞，优先使用异步操作
     *   2. 长循环中定期检查取消状态并优雅退出
     *   3. 外部 IO 操作 (数据库、HTTP 请求) 应设置独立超时
     *   4. 使用 with_timeout 工具包裹可能慢速的子操作
     *
     * @note 如果 Handle 执行时间超过 TOTAL_TIMEOUT(默认 60 秒),框架会返回 504 Gateway Timeout
     * @note 超时后协程会被强制取消，但已执行的副作用 (如数据库写入) 不会回滚
     */
    virtual net::awaitable<stdx::expected<Ok, Error>> run() = 0;

    /** 后处理 */
    virtual net::awaitable<stdx::expected<Ok, Error>> after_run() {
        co_return Ok{};
    }

    void addFilter(std::function<net::awaitable<stdx::expected<Ok, Error>>(HttpHandle*)> filter) {
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

    /** 判断请求的 ulr 中是否包含 query 参数 */
    bool haveQueryParams() const noexcept;

    typedef std::unordered_map<std::string, std::string> QueryParams;

    /**
     * 获取 query 参数
     * @param query_params [out] 输出 query 参数
     * @return true | false 获取或解析失败
     * @throws HttpBadRequestError 当参数数量超过最大限制时抛出异常
     */
    bool getQueryParams(QueryParams& query_params) const;

    void setResHeader(const char* key, const char* val) {
        // 直接写入 BeastContext，避免中间存储
        if (m_beast_context) {
            auto* ctx = static_cast<BeastContext*>(m_beast_context);
            ctx->res.set(key, val);
        }
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

    // ========== 流式分批传输支持（新增）==========

    /**
     * 启用分块传输编码（Chunked Transfer Encoding）
     * 用于大文件下载、SSE、大数据量导出等场景
     *
     * @note 必须在设置响应体之前调用
     */
    void enableChunkedTransfer() {
        m_chunked_transfer = true;
        m_headers_sent = false;  // 重置标志
        if (m_beast_context) {
            auto* ctx = static_cast<BeastContext*>(m_beast_context);
            ctx->res.set(http::field::transfer_encoding, "chunked");
        }
    }

    /**
     * 检查是否启用了分块传输编码
     */
    bool isChunkedTransferEnabled() const {
        return m_chunked_transfer;
    }

    /**
     * 写入一个数据块（异步协程版本）
     *
     * @param data 数据块内容
     * @return 是否写入成功
     *
     * @note 必须先调用 enableChunkedTransfer()
     */
    net::awaitable<bool> writeChunk(const std::string& data);

    /**
     * 写入一个数据块（同步版本）
     *
     * @param data 数据块内容
     * @return 是否写入成功
     */
    bool writeChunkSync(const std::string& data);

    /**
     * 完成分块传输
     *
     * @return 是否成功完成
     *
     * @note 必须调用此方法以正确结束分块传输
     */
    net::awaitable<bool> finishChunkedTransfer();

    /**
     * 完成分块传输（同步版本）
     */
    bool finishChunkedTransferSync();

    /**
     * 获取 io_context
     * @return net::io_context* io_context 指针
     */
    net::io_context* get_io_context() const noexcept {
        if (!m_beast_context) {
            return nullptr;
        }
        auto* ctx = static_cast<BeastContext*>(m_beast_context);
        return ctx->io_ctx_ptr;
    }

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
    void processException(int http_status, int errcode, std::string_view err_msg) noexcept;
    void processError(int http_status, const Error& err) noexcept;

protected:
    void* m_beast_context{nullptr};  // boost::beast 上下文
    std::vector<std::function<net::awaitable<stdx::expected<Ok, Error>>(HttpHandle*)>> m_filters;

    // 流式分批传输支持
    bool m_chunked_transfer{false};  // 是否启用分块传输
    bool m_headers_sent{false};      // 响应头是否已发送（用于分块传输）

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
