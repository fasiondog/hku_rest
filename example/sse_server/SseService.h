/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-23
 *      Author: fasiondog
 */

#pragma once

#include "hikyuu/httpd/HttpService.h"
#include "SseHandle.h"

namespace hku {

/**
 * SSE 服务
 * 
 * 注册 SSE 相关的路由端点
 */
class SseService : public HttpService {
    CLASS_LOGGER_IMP(SseService)

public:
    SseService() = delete;
    SseService(const char* url) : HttpService(url) {}

    virtual void regHandle() override {
        // 完整功能的 SSE 推送（带事件类型、ID、模拟行情数据）
        GET<SseHandle>("sse/stream");
        
        // 简单 SSE 推送示例
        GET<SimpleSseHandle>("sse/simple");
    }
};

}  // namespace hku
