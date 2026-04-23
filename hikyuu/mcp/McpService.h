/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-23
 *      Author: fasiondog
 */

#pragma once

#include "hikyuu/httpd/HttpService.h"
#include "hikyuu/httpd/HttpServer.h"
#include "McpHandle.h"

namespace hku {

template <>
inline void HttpServer::POST<McpHandle>(const char* path) {
    registerHttpHandle("POST", path, [](void* ctx) -> net::awaitable<void> {
        McpHandle x(ctx);
        co_await x();
    });
}

/**
 * MCP Server 服务注册类
 *
 * 注册 MCP 协议相关的 HTTP 路由
 */
class McpService : public HttpService {
    CLASS_LOGGER_IMP(McpService)

public:
    McpService() = delete;
    McpService(const char* url) : HttpService(url) {}

    virtual void regHandle() override {
        // 注册 MCP 主端点（Streamable HTTP - 统一端点）
        POST<McpHandle>("mcp");
    }
};

}  // namespace hku
