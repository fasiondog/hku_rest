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
#include "HttpHandle.h"
#include "pod/all.h"

#ifndef HKU_HTTPD_API
#define HKU_HTTPD_API
#endif

namespace hku {

using json = nlohmann::json;                  // 不保持插入排序
using ordered_json = nlohmann::ordered_json;  // 保持插入排序

class HKU_HTTPD_API RestHandle : public HttpHandle {
    CLASS_LOGGER_IMP(RestHandle)

public:
    explicit RestHandle(void* beast_context) : HttpHandle(beast_context) {
        // addFilter(AuthorizeFilter);
    }

    virtual ~RestHandle() override = default;

    virtual net::awaitable<Result> before_run() noexcept override;
    virtual net::awaitable<Result> after_run() override;

protected:
    Result check_missing_param(std::string_view param) {
        if (!req.contains(param)) {
            return stdx::unexpected(Error::custom(BadRequestErrorCode::MISS_PARAMETER,
                                                  fmt::format("Missing param: {}", param)));
        }
        return Ok{};
    }

    Result check_missing_param(const std::vector<std::string>& params) {
        for (auto& param : params) {
            auto ret = check_missing_param(param);
            if (!ret) {
                return ret;
            }
        }
        return Ok{};
    }

protected:
    json req;  // 子类在 run 方法中，直接使用此 req
    json res;
};

#define REST_HANDLE_IMP(cls) \
public:                      \
    explicit cls(void* beast_context) : RestHandle(beast_context) {}

}  // namespace hku
