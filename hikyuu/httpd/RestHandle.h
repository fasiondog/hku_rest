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

    virtual VoidBizResult before_run() noexcept override;
    virtual VoidBizResult after_run() noexcept override;

protected:
    VoidBizResult check_missing_param(std::string_view param) {
        if (!req.contains(param)) {
            return BIZ_BASE_MISS_PARAMETER;
        }
        return BIZ_OK;
    }

    VoidBizResult check_missing_param(const std::vector<std::string>& params) {
        for (auto& param : params) {
            auto ret = check_missing_param(param);
            if (!ret) {
                return ret;
            }
        }
        return BIZ_OK;
    }

protected:
    json req;  // 子类在 run 方法中，直接使用此 req
    json res;
};

#define REST_HANDLE_IMP(cls) \
public:                      \
    explicit cls(void* beast_context) : RestHandle(beast_context) {}

class HKU_HTTPD_API BizHandle : public HttpHandle {
    CLASS_LOGGER_IMP(BizHandle)

public:
    explicit BizHandle(void* beast_context) : HttpHandle(beast_context) {
        // addFilter(AuthorizeFilter);
    }

    virtual ~BizHandle() override = default;

    virtual VoidBizResult before_run() noexcept override;
    virtual VoidBizResult after_run() noexcept override;
    virtual net::awaitable<VoidBizResult> run() override;
    virtual VoidBizResult biz_run() = 0;

protected:
    VoidBizResult check_missing_param(std::string_view param) {
        if (!req.contains(param)) {
            return BIZ_BASE_MISS_PARAMETER;
        }
        return BIZ_OK;
    }

    VoidBizResult check_missing_param(const std::vector<std::string>& params) {
        for (auto& param : params) {
            auto ret = check_missing_param(param);
            if (!ret) {
                return ret;
            }
        }
        return BIZ_OK;
    }

protected:
    json req;  // 子类在 run 方法中，直接使用此 req
    json res;
};

#define BIZ_HANDLE_IMP(cls) \
public:                     \
    explicit cls(void* beast_context) : BizHandle(beast_context) {}

}  // namespace hku
