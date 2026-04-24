/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-23
 *      Author: fasiondog
 */

#pragma once

#include "hikyuu/httpd/HttpService.h"
#include "hikyuu/httpd/pod/CommonPod.h"
#include "SessionManager.h"
#include "McpHandle.h"

namespace hku {

/**
 * MCP Server 服务注册类
 *
 * 注册 MCP 协议相关的 HTTP 路由
 */
class McpService : public HttpService {
    CLASS_LOGGER_IMP(McpService)

public:
    McpService() : HttpService() {
        HKU_INFO("Registering MCP error module: {}", mcp_mod_reg);
        pod::CommonPod::getScheduler()->addDurationFunc(
          std::numeric_limits<int>::max(), Minutes(1), [this]() {
              try {
                  int cleaned = getSessionManager().cleanupExpiredSessions();
                  if (cleaned > 0) {
                      HKU_INFO("Cleaned up {} expired sessions ", cleaned);
                  }
              } catch (const std::exception& e) {
                  HKU_ERROR("Session cleanup error: {}", e.what());
              }
          });
    }

    virtual void regHandle() override {
        // 注册 MCP 主端点（Streamable HTTP - 统一端点）
        m_server->registerHttpHandle("POST", "/mcp", [this](void* ctx) -> net::awaitable<void> {
            McpHandle x(ctx, this);
            co_await x();
        });
    }

    SessionManager& getSessionManager() {
        return m_session_manager;
    }

private:
    SessionManager m_session_manager{3600, 10000};
};

}  // namespace hku
