/*
 *  Copyright(C) 2021 hikyuu.org
 *
 *  Create on: 2021-03-14
 *     Author: fasiondog
 */

#pragma once

#include <string>
#include <cstdint>
#include <exception>
#include <fmt/format.h>
#include "expected.h"

#ifndef HKU_HTTPD_API
#define HKU_HTTPD_API
#endif

namespace hku {

/**
 * 业务错误码
 */

using BizErrCode = int32_t;
constexpr BizErrCode BIZ_OK = 0;

constexpr BizErrCode MAKE_ERR(int32_t biz_mod, int32_t biz_code) {
    return biz_mod * 10000 + biz_code;
}

constexpr int32_t get_biz_mod(BizErrCode e) {
    return e / 10000;
}

constexpr int32_t get_biz_code(BizErrCode e) {
    return e % 10000;
}

// 注册模块错误信息
using BizErrMsgFunc = const char* (*)(BizErrCode);
HKU_HTTPD_API void register_biz_error_module(int32_t biz_mod, BizErrMsgFunc func);

// 统一获取错误信息
HKU_HTTPD_API const char* biz_err_msg(BizErrCode e);

// 轻量返回值
template <typename T>
struct BizResult {
    T value_;
    int32_t err_ = 0;

    // 成功
    BizResult(T v) : value_(std::move(v)) {}

    // 失败
    BizResult(int32_t e) : err_(e) {}

    bool ok() const noexcept {
        return err_ == 0;
    }
    explicit operator bool() const noexcept {
        return ok();
    }

    const T& value() const noexcept {
        return value_;
    }

    T value_or(T v) const noexcept {
        return ok() ? value_ : std::move(v);
    }

    int32_t error() const noexcept {
        return err_;
    }

    const char* message() const noexcept {
        return biz_err_msg(err_);
    }
};

// 无返回值专用
template <>
struct BizResult<void> {
    int32_t err_ = 0;

    BizResult() = default;
    BizResult(int32_t e) : err_(e) {}

    bool ok() const {
        return err_ == 0;
    }
    explicit operator bool() const {
        return ok();
    }

    int32_t error() const noexcept {
        return err_;
    }

    const char* message() const noexcept {
        return biz_err_msg(err_);
    }
};

using VoidBizResult = BizResult<void>;

// 请求模块错误码
constexpr BizErrCode BIZ_MOD_BASE = 1;
#define BIZ_BASE_ERR(code) MAKE_ERR(BIZ_MOD_BASE, code);
constexpr BizErrCode BIZ_BASE_FAILED = BIZ_BASE_ERR(0);
constexpr BizErrCode BIZ_BASE_INVALID_JSON = BIZ_BASE_ERR(1);          // 请求数据无法解析为 JSON
constexpr BizErrCode BIZ_BASE_MISS_PARAMETER = BIZ_BASE_ERR(2);        // 缺失参数，参数不能为空
constexpr BizErrCode BIZ_BASE_WRONG_PARAMETER = BIZ_BASE_ERR(3);       // 参数值填写错误
constexpr BizErrCode BIZ_BASE_WRONG_PARAMETER_TYPE = BIZ_BASE_ERR(4);  // 参数类型错误
constexpr BizErrCode BIZ_BASE_TOO_MANY_QUERY_PARAMS =
  BIZ_BASE_ERR(5);  //  URL 参数数量过多（防止哈希碰撞 DoS 攻击）
constexpr BizErrCode BIZ_BASE_TOO_LONG_URL = BIZ_BASE_ERR(6);  //  URL 长度过长（防止超长 URL 攻击）
constexpr BizErrCode BIZ_BASE_INVALID_URL = BIZ_BASE_ERR(7);   //  URL 无效

// 鉴权模块错误码
constexpr BizErrCode BIZ_MOD_AUTH = 2;
#define BIZ_AUTH_ERR(code) MAKE_ERR(BIZ_MOD_AUTH, code);
constexpr BizErrCode BIZ_AUTH_FAILED = BIZ_AUTH_ERR(0);      // 鉴权失败
constexpr BizErrCode BIZ_AUTH_MISS_TOKEN = BIZ_AUTH_ERR(1);  // 缺失令牌
constexpr BizErrCode BIZ_AUTH_EXPIRED = BIZ_AUTH_ERR(2);     // 鉴权过期

/**
 * 业务异常，兼容以异常方式返回报错的模式，以及需要动态返回自定义错误信息的模式
 */
class HKU_HTTPD_API BizException : public std::exception {
public:
    BizException() : m_msg("Unknown exception!"), m_err(BIZ_BASE_FAILED) {}
    BizException(std::string msg) : m_msg(std::move(msg)), m_err(BIZ_BASE_FAILED) {}
    BizException(BizErrCode err, std::string msg) : m_msg(std::move(msg)), m_err(err) {}
    virtual ~BizException() noexcept {}
    virtual const char* what() const noexcept;

    BizErrCode errcode() const noexcept {
        return m_err;
    }

protected:
    std::string m_msg;
    int32_t m_err;
};

#define REQ_CHECK(expr, errcode, ...)                              \
    {                                                              \
        if (!(expr)) {                                             \
            throw BizException(errcode, fmt::format(__VA_ARGS__)); \
        }                                                          \
    }

#define REQ_THROW(errcode, ...) throw BizException(errcode, fmt::format(__VA_ARGS__))

}  // namespace hku
