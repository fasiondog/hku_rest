/*
 *  Copyright(C) 2021 hikyuu.org
 *
 *  Create on: 2021-03-14
 *     Author: fasiondog
 */

#pragma once

#include <string>
#include <cstdint>
#include <fmt/format.h>
#include "expected.h"

#ifndef HKU_HTTPD_API
#define HKU_HTTPD_API
#endif

namespace hku {

/**
 * 公共错误码
 */

struct Error {
    int32_t code() const noexcept {
        return m_code;
    };

    const std::string& message() const noexcept {
        return m_msg;
    };

    // 只通过 code 创建，msg 自动多语言获取
    static Error from_code(int32_t code) {
        return {code, get_error_msg(code)};
    }

    // 也可以手动指定消息（覆盖多语言）
    static Error custom(int32_t code, std::string msg) {
        return {code, std::move(msg)};
    }

    // ==============================
    // 关键：多语言错误信息查询（全局实现一次）
    // ==============================
    static std::string get_error_msg(int32_t code) {
        return std::string{};
    }

    Error() = default;
    Error(int32_t code, std::string msg) : m_code(code), m_msg(std::move(msg)) {}

    Error(const Error&) = default;
    Error& operator=(const Error&) = default;
    Error(Error&& rhs) : m_code(rhs.m_code), m_msg(std::move(rhs.m_msg)) {}
    Error& operator=(Error&& rhs) {
        if (this != &rhs) [[likely]] {
            m_code = rhs.m_code;
            m_msg = std::move(rhs.m_msg);
        }
        return *this;
    }

private:
    int32_t m_code;
    std::string m_msg;
};

using Ok = std::monostate;

// using Result = stdx::expected<Ok, Error>;

namespace AuthorizeErrorCode {
constexpr int32_t MISS_TOKEN = 1000;         // 缺失令牌
constexpr int32_t FAILED_AUTHORIZED = 1001;  // 鉴权失败
constexpr int32_t AUTHORIZE_EXPIRED = 1002;  // 鉴权过期
}  // namespace AuthorizeErrorCode

namespace BadRequestErrorCode {
constexpr int32_t INVALID_JSON_REQUEST = 2001;  // 请求数据无法解析为 JSON
constexpr int32_t MISS_PARAMETER =
  2002;  // 缺失参数，参数不能为空 必填参数不能为空（各个业务接口返回各个接口的参数）
constexpr int32_t WRONG_PARAMETER = 2003;        // 参数值填写错误（各个业务接口返回各个接口的参数）
constexpr int32_t WRONG_PARAMETER_TYPE = 2004;   // 参数类型错误（各个业务接口返回各个接口的参数）
constexpr int32_t TOO_MANY_QUERY_PARAMS = 2005;  //  URL 参数数量过多（防止哈希碰撞 DoS 攻击）
constexpr int32_t TOO_LONG_URL = 2006;           //  URL 长度过长（防止超长 URL 攻击）
}  // namespace BadRequestErrorCode

// 轻量返回值，替代 heavy expected
template <typename T>
struct Result {
    T value;
    int32_t err = 0;

    // 成功
    Result(T v) : value(std::move(v)) {}

    // 失败
    Result(int32_t e) : err(e) {}

    bool ok() const noexcept {
        return err == 0;
    }
    explicit operator bool() const noexcept {
        return ok();
    }

    int32_t error() const noexcept {
        return err;
    }
};

// 无返回值专用
template <>
struct Result<void> {
    int32_t err = 0;

    Result() = default;
    Result(int32_t e) : err(e) {}

    bool ok() const {
        return err == 0;
    }
    explicit operator bool() const {
        return ok();
    }

    int32_t error() const noexcept {
        return err;
    }
};

using VoidResult = Result<void>;

using BizErrCode = int32_t;
constexpr BizErrCode BIZ_OK = 0;

constexpr BizErrCode MAKE_ERR(int32_t biz_mod, int32_t biz_code) {
    return biz_mod + biz_code;
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

// 请求模块错误码
constexpr BizErrCode BIZ_MOD_BASE = 1;
#define BIZ_BASE_ERR(code) MAKE_ERR(BIZ_MOD_BASE, code);
constexpr BizErrCode BIZ_BASE_INVALID_JSON = BIZ_BASE_ERR(0);          // 请求数据无法解析为 JSON
constexpr BizErrCode BIZ_BASE_MISS_PARAMETER = BIZ_BASE_ERR(1);        // 缺失参数，参数不能为空
constexpr BizErrCode BIZ_BASE_WRONG_PARAMETER = BIZ_BASE_ERR(2);       // 参数值填写错误
constexpr BizErrCode BIZ_BASE_WRONG_PARAMETER_TYPE = BIZ_BASE_ERR(3);  // 参数类型错误
constexpr BizErrCode BIZ_BASE_TOO_MANY_QUERY_PARAMS =
  BIZ_BASE_ERR(4);  //  URL 参数数量过多（防止哈希碰撞 DoS 攻击）
constexpr BizErrCode BIZ_BASE_TOO_LONG_URL = BIZ_BASE_ERR(5);  //  URL 长度过长（防止超长 URL 攻击）

// 鉴权模块错误码
constexpr BizErrCode BIZ_MOD_AUTH = 2;
#define BIZ_AUTH_ERR(code) MAKE_ERR(BIZ_MOD_AUTH, code);
constexpr BizErrCode BIZ_AUTH_FAILED = BIZ_AUTH_ERR(0);      // 鉴权失败
constexpr BizErrCode BIZ_AUTH_MISS_TOKEN = BIZ_AUTH_ERR(1);  // 缺失令牌
constexpr BizErrCode BIZ_AUTH_EXPIRED = BIZ_AUTH_ERR(2);     // 鉴权过期

}  // namespace hku
