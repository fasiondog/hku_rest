/*
 *  Copyright(C) 2021 hikyuu.org
 *
 *  Create on: 2021-03-07
 *     Author: fasiondog
 */

#pragma once

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

class RestHandle : public HttpHandle {
    CLASS_LOGGER_IMP(RestHandle)

public:
    explicit RestHandle(nng_aio *aio) : HttpHandle(aio) {
        // addFilter(AuthorizeFilter);
    }

    virtual ~RestHandle() {}

    virtual void before_run() override {
        setResHeader("Content-Type", "application/json; charset=UTF-8");
        req = getReqJson();
    }

    virtual void after_run() override {
        // 强制关闭连接，即仅有短连接
        // nng_http_res_set_status(m_nng_res, NNG_HTTP_STATUS_OK);
        json new_res;
        new_res["ret"] = 0;
        new_res["data"] = std::move(res);
        setResData(new_res);
    }

protected:
    void check_missing_param(std::string_view param) {
        if (!req.contains(param)) {
            throw HttpBadRequestError(BadRequestErrorCode::MISS_PARAMETER,
                                      fmt::format(R"(Missing param "{}")", param));
        }
    }

    void check_missing_param(const std::vector<std::string> &params) {
        for (auto &param : params) {
            check_missing_param(param);
        }
    }

protected:
    json req;  // 子类在 run 方法中，直接使用次req
    json res;
};

#define REST_HANDLE_IMP(cls) \
public:                      \
    cls(nng_aio *aio) : RestHandle(aio) {}

}  // namespace hku