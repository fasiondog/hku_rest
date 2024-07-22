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
    virtual void run() override {
        res["data"] = "hello word!";
    }
};

}