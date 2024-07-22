/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-23
 *      Author: fasiondog
 */

#pragma once
#include "hikyuu/httpd/HttpService.h"
#include "HelloHandle.h"

namespace hku {
class HelloService : public HttpService {
    CLASS_LOGGER_IMP(HelloService)

public:
    HelloService() = delete;
    HelloService(const char* url) : HttpService(url) {}

    virtual void regHandle() override {
        GET<HelloHandle>("hello");
    }
};

}  // namespace hku