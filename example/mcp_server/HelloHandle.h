/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-23
 *      Author: fasiondog
 */

#pragma once

#include "hikyuu/httpd/RestHandle.h"

namespace hku {

/**
 * 简单的健康检查处理器
 */
class HelloHandle : public RestHandle {
    REST_HANDLE_IMP(HelloHandle)

public:
    virtual net::awaitable<VoidBizResult> run() override {
        res["status"] = "ok";
        res["message"] = "MCP Server is running";
        co_return BIZ_OK;
    }
};

} // namespace hku
