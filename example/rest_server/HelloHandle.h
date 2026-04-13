/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-23
 *      Author: fasiondog
 */

#pragma once

#include "hikyuu/httpd/RestHandle.h"

namespace hku {

class HelloHandle : public RestHandle {
    REST_HANDLE_IMP(HelloHandle)
    virtual net::awaitable<stdx::expected<Ok, Error>> run() override {
        res["msg"] = "hello word!";
        co_return Ok{};
    }
};

}  // namespace hku