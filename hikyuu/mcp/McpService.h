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

    using json = nlohmann::json;

public:
    McpService();
    virtual ~McpService() override = default;

    McpService(const McpService&) = delete;
    McpService& operator=(const McpService&) = delete;

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

    using InitializeMethod = std::function<net::awaitable<JsonResult>(
      McpHandle* handle, const nlohmann::json&, const std::string&)>;
    void setInitializeMethod(InitializeMethod method) {
        m_initialize_method = method;
    }

    using ToolMethod = std::function<net::awaitable<JsonResult>(
      McpHandle* handle, const nlohmann::json&, const std::string&)>;
    void addTool(const nlohmann::json& description, ToolMethod&& tool);

    net::awaitable<JsonResult> dispatchMethod(McpHandle* handle, const std::string& method,
                                              const nlohmann::json& params,
                                              const std::string& session_id,
                                              const nlohmann::json& id, bool use_sse);

private:
    // 验证必需字段是否存在且类型正确
    void validate_required_fields(const json& tool);

    // 验证 inputSchema 的内部结构
    void validate_input_schema(const json& tool);

    // 验证数组类型的属性
    void validate_array_property(const std::string& prop_name, const json& prop_schema);

    // 验证嵌套对象类型的属性
    void validate_nested_object_property(const std::string& prop_name, const json& prop_schema);

    net::awaitable<nlohmann::json> toolsCallMethod(McpHandle* handle, const nlohmann::json& params,
                                                   const std::string& session_id);

    json pingMethod(const std::string& session_id);

    json sessionInfoMethod(const std::string& session_id);

private:
    SessionManager m_session_manager{3600, 10000};

    InitializeMethod m_initialize_method;

    std::map<std::string, ToolMethod> m_tools;
    nlohmann::json m_tool_descriptions{json::array()};
};

}  // namespace hku
