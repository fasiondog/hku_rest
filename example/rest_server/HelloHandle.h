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
    virtual net::awaitable<VoidBizResult> run() override {
        // std::this_thread::sleep_for(std::chrono::milliseconds(100));
        res["msg"] = "hello word!";
        co_return BIZ_OK;
    }
};

class BizHelloHandle : public BizHandle {
    BIZ_HANDLE_IMP(BizHelloHandle)
    virtual VoidBizResult biz_run() override {
        // std::this_thread::sleep_for(std::chrono::milliseconds(100));
        res["msg"] = "hello word!";
        return BIZ_OK;
    }
};

}  // namespace hku