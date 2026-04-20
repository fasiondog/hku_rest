/*
 *  Copyright(C) 2021 hikyuu.org
 *
 *  Create on: 2021-03-07
 *     Author: fasiondog
 */

#pragma once

#include <nlohmann/json.hpp>
#include <stdlib.h>
#include <string_view>
#include <string>
#include <vector>
#include "hikyuu/utilities/thread/algorithm.h"
#include "HttpHandle.h"
#include "pod/all.h"

#ifndef HKU_HTTPD_API
#define HKU_HTTPD_API
#endif

namespace hku {

using json = nlohmann::json;                  // 不保持插入排序
using ordered_json = nlohmann::ordered_json;  // 保持插入排序

/**
 * @brief RESTful API 请求处理器基类（同步模式）
 *
 * RestHandle 用于处理标准的 RESTful API 请求，采用**同步执行模型**。
 * 子类只需重写业务逻辑方法，框架会自动处理 JSON 解析、响应封装和 GZIP 压缩。
 *
 * ## 核心特性
 * - **同步执行**: run() 方法在调用线程中直接执行，适合 CPU 密集型或简单 IO 操作
 * - **自动 JSON 处理**: before_run() 自动解析请求体 JSON 到 req 成员变量
 * - **统一响应格式**: after_run() 自动将 res 封装为 {"ret": 0, "data": ...} 格式
 * - **GZIP 压缩**: 响应数据超过 1KB 且客户端支持时自动启用 GZIP 压缩
 * - **参数校验工具**: 提供 check_missing_param() 模板方法快速校验必填参数
 *
 * ## 适用场景
 * - 简单的 CRUD 操作
 * - CPU 密集型计算任务
 * - 不涉及复杂异步 IO 的业务逻辑
 * - 需要快速响应的轻量级接口
 *
 * ## 不适用场景
 * - 需要调用外部异步服务（如数据库异步查询、远程 RPC）
 * - 长时间运行的 IO 操作会阻塞线程池
 * - 需要精细控制协程生命周期的场景
 *
 * ## 使用示例
 * ```cpp
 * class UserQueryHandle : public RestHandle {
 *     REST_HANDLE_IMP(UserQueryHandle)
 *
 *     virtual VoidBizResult run() override {
 *         // 1. 校验参数
 *         auto ret = check_missing_param("user_id");
 *         if (!ret) return ret;
 *
 *         // 2. 业务逻辑（同步执行）
 *         std::string user_id = req["user_id"];
 *         res["name"] = "张三";
 *         res["age"] = 25;
 *
 *         return BIZ_OK;
 *     }
 * };
 * ```
 *
 * ## 生命周期方法
 * 1. **before_run()**: 解析 JSON → 设置 Content-Type
 * 2. **run()**: 子类实现具体业务逻辑（同步）
 * 3. **after_run()**: 封装响应 → GZIP 压缩 → 返回结果
 *
 * @note 如果业务涉及异步操作，请使用 BizHandle 替代
 * @see BizHandle 异步业务处理器
 * @see HttpHandle 底层 HTTP 协议处理器
 */
class HKU_HTTPD_API RestHandle : public HttpHandle {
    CLASS_LOGGER_IMP(RestHandle)

public:
    explicit RestHandle(void* beast_context) : HttpHandle(beast_context) {
        // addFilter(AuthorizeFilter);
    }

    virtual ~RestHandle() override = default;

    virtual VoidBizResult before_run() noexcept override;
    virtual VoidBizResult after_run() noexcept override;

protected:
    VoidBizResult check_missing_param() {
        return BIZ_OK;
    }

    VoidBizResult check_missing_param(std::string_view param) {
        if (!req.contains(param)) {
            return BIZ_BASE_MISS_PARAMETER;
        }
        return BIZ_OK;
    }

    template <typename First, typename... Rest>
    VoidBizResult check_missing_param(const First& first, const Rest&... rest) {
        auto ret = check_missing_param(first);
        if (!ret) {
            return ret;  // 短路！立即返回
        }
        return check_missing_param(rest...);
    }

    /**
     * @brief 将同步函数投递到业务线程池中异步执行（自动使用 CommonPod::executor）
     *
     * 在协程环境中，此方法可以将阻塞型或 CPU 密集型任务投递到 CommonPod 的业务线程池执行器中，
     * 避免阻塞当前的 HTTP 工作线程。通过 co_await 等待任务完成并获取返回值。
     *
     * ## 使用场景
     * - 在 BizHandle 的 biz_run() 中调用外部同步 API
     * - 执行 CPU 密集型计算任务
     * - 调用不支持异步的第三方库
     * - 文件 IO 等阻塞操作
     *
     * ## 使用示例
     * ```cpp
     * virtual net::awaitable<VoidBizResult> biz_run() override {
     *     // 将耗时的同步操作投递到业务线程池执行
     *     std::string ip = req["ip"];
     *     auto info = co_await biz_run([ip]() { return getIPInfoFromTianXing(ip); });
     *
     *     res["ip_info"] = info;
     *     co_return BIZ_OK;
     * }
     * ```
     *
     * ## 参数说明
     * @param func 要执行的 callable 对象（lambda、函数指针、std::function 等）
     * @return 返回 func 的执行结果，类型为 decltype(func())
     *
     * @note 此函数必须在协程上下文中使用（需要 co_await）
     * @note func 应该是可拷贝或可移动的，因为会被传递到另一个线程执行
     * @note 如果 func 抛出异常，异常会被传播到当前协程
     * @note 内部自动使用 pod::CommonPod::executor() 作为执行器，无需手动传入
     *
     * @see pod::CommonPod::executor() 获取业务线程池执行器
     */
    template <typename Func>
    auto biz_run(Func&& func) -> boost::asio::awaitable<std::invoke_result_t<Func>> {
        return hku::co_run(pod::CommonPod::executor(), std::forward<Func>(func));
    }

protected:
    json req;  // 子类在 run 方法中，直接使用此 req
    json res;
};

#define REST_HANDLE_IMP(cls)                                         \
public:                                                              \
    explicit cls(void* beast_context) : RestHandle(beast_context) {} \
    virtual ~cls() = default;

/**
 * @brief 业务逻辑请求处理器基类（异步协程模式）
 *
 * BizHandle 用于处理复杂的业务逻辑请求，采用**异步协程执行模型**。
 * 整个 biz_run() 方法会被投递到 CommonPod::executor() 线程池中执行，避免阻塞 HTTP 工作线程。
 * 适用于内部服务，基本无参数缺失、错误等情况。否则建议使用 RestHandle，通过 ResHande 的 biz_run
 * 控制阻塞或计算任务。
 *
 * ## 核心特性
 * - **整体投递**: run() 方法将整个 biz_run() 投递到业务线程池执行
 * - **前置校验**: 支持 before_biz_run() 在主线程进行快速参数校验
 * - **协程支持**: 基于 Boost.Asio co_await 机制，支持真正的异步 IO
 * - **非阻塞 IO**: 适合数据库查询、远程调用等耗时操作
 * - **自动 JSON 处理**: 与 RestHandle 相同的 JSON 解析和响应封装机制
 * - **GZIP 压缩**: 与 RestHandle 相同的响应压缩策略
 * - **参数校验工具**: 提供 check_missing_param() 模板方法
 *
 * ## 适用场景
 * - 需要调用数据库异步查询（MySQL/SQLite）
 * - 需要发起远程 RPC 或 HTTP 请求
 * - 文件 IO 或其他阻塞型操作
 * - 需要并发执行多个子任务
 * - 长时间运行的业务流程
 *
 * ## 不适用场景
 * - 简单的内存计算（使用 RestHandle 更轻量）
 * - 对延迟极度敏感的微操作（协程调度有额外开销）
 *
 * ## 使用示例
 * ```cpp
 * class OrderCreateHandle : public BizHandle {
 *     BIZ_HANDLE_IMP(OrderCreateHandle)
 *
 *     // 在主线程执行参数校验
 *     virtual VoidBizResult before_biz_run() noexcept override {
 *         auto ret = check_missing_param("product_id", "quantity");
 *         if (!ret) return ret;  // 快速失败
 *         return BIZ_OK;
 *     }
 *
 *     // 在线程池执行业务逻辑
 *     virtual net::awaitable<VoidBizResult> biz_run() override {
 *         // 异步数据库操作（不会阻塞 HTTP 线程）
 *         auto db = co_await pod::MySQLPod::getInstance()->getConnection();
 *         auto result = co_await db->executeAsync(
 *             "INSERT INTO orders ...",
 *             req["product_id"],
 *             req["quantity"]
 *         );
 *
 *         res["order_id"] = result.insertId();
 *         co_return BIZ_OK;
 *     }
 * };
 * ```
 *
 * ## 生命周期方法
 * 1. **before_run()**: 解析 JSON → 设置 Content-Type（在主线程执行）
 * 2. **run()**: 先执行 before_biz_run()，再将 biz_run() 整体投递到线程池（final 方法，不可重写）
 * 3. **before_biz_run()**: 子类可重写此方法进行参数校验（在主线程执行，可选）
 * 4. **biz_run()**: 子类实现具体业务逻辑（在线程池中异步执行）
 * 5. **after_run()**: 封装响应 → GZIP 压缩 → 返回结果（在主线程执行）
 *
 * ## 执行流程对比
 *
 * | 阶段 | RestHandle | BizHandle |
 * |------|-----------|-----------|
 * | before_run | 主线程 | 主线程 |
 * | 前置校验 | - | 主线程（before_biz_run，可选） |
 * | 业务逻辑 | 主线程（同步） | 线程池（整体投递，异步协程） |
 * | after_run | 主线程 | 主线程 |
 *
 * ## 与 RestHandle 的对比
 *
 * **RestHandle 更灵活的场景**：
 * - 可在 run() 中自由控制哪些部分在主线程执行，哪些部分投递到线程池
 * - 使用 `co_await biz_run(func)` 细粒度控制子任务
 * - 适合混合场景：部分轻量级逻辑 + 部分耗时操作
 *
 * **BizHandle 更适合的场景**：
 * - 整个业务逻辑都是耗时操作，需要完全脱离 HTTP 线程
 * - 不需要精细控制执行位置
 * - 代码结构更简单直观
 *
 * @note biz_run() 必须声明为 `virtual net::awaitable<VoidBizResult> biz_run() override`
 * @note 在 biz_run() 中可以使用 co_await 进行异步操作
 * @see RestHandle 同步 REST 处理器（更灵活的控制方式）
 */
class HKU_HTTPD_API BizHandle : public HttpHandle {
    CLASS_LOGGER_IMP(BizHandle)

public:
    explicit BizHandle(void* beast_context) : HttpHandle(beast_context) {
        // addFilter(AuthorizeFilter);
    }

    virtual ~BizHandle() override = default;

    virtual VoidBizResult before_run() noexcept override;
    virtual VoidBizResult after_run() noexcept override;
    virtual net::awaitable<VoidBizResult> run() final override;

    // 前置逻辑，通常进行参数检查，在协程中执行
    virtual VoidBizResult before_biz_run() noexcept {
        return BIZ_OK;
    }

    // 业务逻辑，在业务线程池中执行
    virtual VoidBizResult biz_run() = 0;

protected:
    VoidBizResult check_missing_param() {
        return BIZ_OK;
    }

    VoidBizResult check_missing_param(std::string_view param) {
        if (!req.contains(param)) {
            return BIZ_BASE_MISS_PARAMETER;
        }
        return BIZ_OK;
    }

    template <typename First, typename... Rest>
    VoidBizResult check_missing_param(const First& first, const Rest&... rest) {
        auto ret = check_missing_param(first);  // 调用单参数版本
        if (!ret) {
            return ret;
        }
        return check_missing_param(rest...);
    }

protected:
    json req;  // 子类在 run 方法中，直接使用此 req
    json res;
};

#define BIZ_HANDLE_IMP(cls)                                         \
public:                                                             \
    explicit cls(void* beast_context) : BizHandle(beast_context) {} \
    virtual ~cls() = default;

}  // namespace hku
