/*
 *  Copyright(C) 2021 hikyuu.org
 *
 *  Create on: 2021-03-14
 *     Author: fasiondog
 */

#pragma once

#include <string>
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

using Result = stdx::expected<Ok, Error>;

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

}  // namespace hku
