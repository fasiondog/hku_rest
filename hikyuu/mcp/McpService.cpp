/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-04-25
 *      Author: fasiondog
 */

#include "McpService.h"
#include <set>

namespace hku {

/**
 * 默认处理 initialize 方法
 */
static net::awaitable<JsonResult> defaultInitializeMethod(McpHandle* handle,
                                                          const nlohmann::json& params,
                                                          const std::string& session_id) {
    HKU_INFO("MCP initialize request (session: {})", session_id);

    // 存储客户端信息到 Session
    if (params.contains("clientInfo")) {
        handle->getSessionManager().setSessionMetadata(session_id, "client_info",
                                                       params["clientInfo"]);
    }

    nlohmann::json result;
    result["protocolVersion"] = "2024-11-05";
    result["capabilities"] = {
      {"tools", {{"listChanged", false}}},
      {"resources", {{"subscribe", false}, {"listChanged", false}}},
      {"prompts", {{"listChanged", false}}},
      {"session", {{"supported", true}}}  // 声明支持 Session
    };
    result["serverInfo"] = {{"name", "hku_rest MCP Server"}, {"version", "1.0.0"}};

    co_return result;
}

McpService::McpService() : HttpService(), m_initialize_method(defaultInitializeMethod) {
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

net::awaitable<JsonResult> McpService::dispatchMethod(McpHandle* handle, const std::string& method,
                                                      const nlohmann::json& params,
                                                      const std::string& session_id,
                                                      const nlohmann::json& id, bool use_sse) {
    try {
        if (method == "initialize") {
            co_return co_await m_initialize_method(handle, params, session_id);
            // } else if (method == "initialized") {
            //     // initialized 是通知，不需要响应
        } else if (method == "ping") {
            // MCP 协议 ping 方法 - 用于连接健康检查
            co_return pingMethod(session_id);
        } else if (method == "tools/list") {
            co_return JsonResult{{"tools", m_tool_descriptions}};
        } else if (method == "tools/call") {
            co_return co_await toolsCallMethod(handle, params, session_id);
            // } else if (method == "resources/list") {
            //     auto result = handleResourcesList(params, session_id);
            //     co_await sendMcpSuccessResponse(result, id, use_sse);
            // } else if (method == "resources/read") {
            //     auto result = co_await handleResourcesRead(params, session_id);
            //     co_await sendMcpSuccessResponse(result, id, use_sse);
            // } else if (method == "prompts/list") {
            //     auto result = handlePromptsList(params, session_id);
            //     co_await sendMcpSuccessResponse(result, id, use_sse);
            // } else if (method == "prompts/get") {
            //     auto result = co_await handlePromptsGet(params, session_id);
            //     co_await sendMcpSuccessResponse(result, id, use_sse);
        } else if (method == "session/info") {
            co_return sessionInfoMethod(session_id);
            // } else if (method == "session/set_metadata") {
            //     auto result = handleSetSessionMetadata(params, session_id);
            //     co_await sendMcpSuccessResponse(result, id, use_sse);
            // } else if (method == "session/unregister") {
            //     auto result = handleUnregisterSession(session_id);
            //     co_await sendMcpSuccessResponse(result, id, use_sse);
        } else {
            co_return BIZ_JSONRPC_METHOD_NOT_FOUND;
        }

        co_return BIZ_OK;
    } catch (const std::exception& e) {
        HKU_ERROR("Error in dispatchMethod: {}", e.what());
        co_return BIZ_JSONRPC_INTERNAL_ERROR;
    }
}

nlohmann::json McpService::pingMethod(const std::string& session_id) {
    HKU_DEBUG("MCP ping request (session: {})", session_id);

    // 更新会话活动时间
    if (!session_id.empty()) {
        getSessionManager().touchSession(session_id);
    }

    // 返回空结果（MCP 规范要求）
    return nlohmann::json::object();
}

nlohmann::json McpService::sessionInfoMethod(const std::string& session_id) {
    HKU_INFO("MCP session/info request (session: {})", session_id);

    auto session = getSessionManager().getSession(session_id);
    if (!session) {
        throw std::runtime_error("Session not found or expired");
    }

    nlohmann::json info;
    info["session_id"] = session->session_id;
    info["client_info"] = session->client_info;
    info["created_at"] =
      std::chrono::duration_cast<std::chrono::seconds>(session->created_at.time_since_epoch())
        .count();
    info["last_active"] =
      std::chrono::duration_cast<std::chrono::seconds>(session->last_active.time_since_epoch())
        .count();
    info["metadata"] = session->metadata;

    return info;
}

void McpService::addTool(const nlohmann::json& description, ToolMethod&& tool) {
    HKU_ASSERT(tool);
    validate_required_fields(description);
    validate_input_schema(description);
    m_tools[description["method"]] = std::move(tool);
    m_tool_descriptions.push_back(description);
}

net::awaitable<nlohmann::json> McpService::toolsCallMethod(McpHandle* handle,
                                                           const nlohmann::json& params,
                                                           const std::string& session_id) {
    try {
        std::string tool_name = params.value("name", "");
        nlohmann::json arguments = params.value("arguments", nlohmann::json::object());

        HKU_TRACE("MCP tools/call request: tool={} (session: {})", tool_name, session_id);

        auto it = m_tools.find(tool_name);
        if (it == m_tools.end()) {
            co_return BIZ_MCP_TOOL_NOT_FOUND;
        }

        auto& tool_method = it->second;
        JsonResult result = co_await tool_method(handle, arguments, session_id);

        // 检查结果是否成功
        if (!result.has_value()) {
            HKU_ERROR("Tool execution failed! {}, {}", result.error(), result.message());
            co_return BIZ_MCP_TOOL_EXECUTION_ERROR;
        }

        co_return result.value();
    } catch (const std::exception& e) {
        HKU_ERROR("Error in toolsCall: {}", e.what());
        co_return BIZ_MCP_TOOL_EXECUTION_ERROR;
    } catch (...) {
        HKU_ERROR("Unknown error in toolsCall");
        co_return BIZ_MCP_TOOL_EXECUTION_ERROR;
    }
}

void McpService::validate_required_fields(const json& tool) {
    // 检查 name
    if (!tool.contains("name") || !tool["name"].is_string() ||
        tool["name"].get<std::string>().empty()) {
        throw std::logic_error("Field 'name' is required and must be a non-empty string.");
    }

    // 检查 description
    if (!tool.contains("description") || !tool["description"].is_string() ||
        tool["description"].get<std::string>().empty()) {
        throw std::logic_error("Field 'description' is required and must be a non-empty string.");
    }

    // 检查 inputSchema
    if (!tool.contains("inputSchema")) {
        throw std::logic_error("Field 'inputSchema' is required.");
    }
    if (!tool["inputSchema"].is_object()) {
        throw std::logic_error("Field 'inputSchema' must be a JSON object.");
    }
}

// 验证数组类型的属性
void McpService::validate_array_property(const std::string& prop_name, const json& prop_schema) {
    // 数组类型应该有 items 字段定义元素类型
    if (!prop_schema.contains("items")) {
        throw std::logic_error(fmt::format(
          "Array property '{}' must have an 'items' field defining element type.", prop_name));
    }

    const json& items = prop_schema["items"];
    if (!items.is_object()) {
        throw std::logic_error(
          fmt::format("Property '{}' items must be a JSON Schema object.", prop_name));
    }

    // 验证 items 的 type
    if (!items.contains("type") || !items["type"].is_string()) {
        throw std::logic_error(
          fmt::format("Property '{}' items must have a 'type' field.", prop_name));
    }

    std::string item_type = items["type"].get<std::string>();
    // 如果数组元素是复杂类型，递归验证
    if (item_type == "object") {
        validate_nested_object_property(prop_name + "[]", items);
    } else if (item_type == "array") {
        validate_array_property(prop_name + "[]", items);
    }
}

// 验证嵌套对象类型的属性
void McpService::validate_nested_object_property(const std::string& prop_name,
                                                 const json& prop_schema) {
    // 嵌套对象可以有 properties 定义子属性
    if (prop_schema.contains("properties")) {
        const json& nested_properties = prop_schema["properties"];
        if (!nested_properties.is_object()) {
            throw std::logic_error(
              fmt::format("Property '{}' properties must be an object.", prop_name));
        }

        // 递归验证每个嵌套属性
        for (auto it = nested_properties.begin(); it != nested_properties.end(); ++it) {
            const std::string& nested_prop_name = it.key();
            const json& nested_prop_schema = it.value();

            if (!nested_prop_schema.is_object()) {
                throw std::logic_error(fmt::format("Nested property '{}.{}' must be a JSON object.",
                                                   prop_name, nested_prop_name));
            }

            if (!nested_prop_schema.contains("type") || !nested_prop_schema["type"].is_string()) {
                throw std::logic_error(
                  fmt::format("Nested property '{}.{}' must have a 'type' field.", prop_name,
                              nested_prop_name));
            }

            std::string nested_type = nested_prop_schema["type"].get<std::string>();
            if (nested_type == "object") {
                validate_nested_object_property(prop_name + "." + nested_prop_name,
                                                nested_prop_schema);
            } else if (nested_type == "array") {
                validate_array_property(prop_name + "." + nested_prop_name, nested_prop_schema);
            }
        }
    }
}

// 验证 inputSchema 的内部结构（符合 JSON Schema Draft-07 规范）
void McpService::validate_input_schema(const json& tool) {
    const json& schema = tool["inputSchema"];

    // inputSchema 的顶层 type 必须是 "object"
    if (!schema.contains("type") || !schema["type"].is_string() ||
        schema["type"].get<std::string>() != "object") {
        throw std::logic_error("inputSchema 'type' must be 'object'.");
    }

    // 检查 properties 是否存在且为对象
    if (!schema.contains("properties") || !schema["properties"].is_object()) {
        throw std::logic_error("inputSchema must contain a 'properties' object.");
    }

    const json& properties = schema["properties"];

    // 如果 properties 为空对象，这是允许的（表示无参数）
    if (properties.empty()) {
        return;
    }

    // 验证 properties 中每个属性的结构
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        const std::string& prop_name = it.key();
        const json& prop_schema = it.value();

        // 每个属性必须是对象
        if (!prop_schema.is_object()) {
            throw std::logic_error(
              fmt::format("Property '{}' in inputSchema must be a JSON object.", prop_name));
        }

        // 每个属性必须有 type 字段
        if (!prop_schema.contains("type") || !prop_schema["type"].is_string()) {
            throw std::logic_error(
              fmt::format("Property '{}' must have a 'type' field.", prop_name));
        }

        // 验证 type 值的有效性
        std::string type_value = prop_schema["type"].get<std::string>();
        if (type_value != "string" && type_value != "number" && type_value != "integer" &&
            type_value != "boolean" && type_value != "array" && type_value != "object" &&
            type_value != "null") {
            throw std::logic_error(
              fmt::format("Property '{}' has invalid type '{}'. Valid types are: "
                          "string, number, integer, boolean, array, object, null.",
                          prop_name, type_value));
        }

        // 对于复杂类型（array, object），需要进一步验证
        if (type_value == "array") {
            validate_array_property(prop_name, prop_schema);
        } else if (type_value == "object") {
            // 递归验证嵌套对象
            validate_nested_object_property(prop_name, prop_schema);
        }

        // 可选：验证 description 字段（推荐但不强制）
        if (prop_schema.contains("description") && !prop_schema["description"].is_string()) {
            throw std::logic_error(
              fmt::format("Property '{}' description must be a string.", prop_name));
        }
    }

    // 检查 required 字段（如果存在）
    if (schema.contains("required")) {
        if (!schema["required"].is_array()) {
            throw std::logic_error("inputSchema 'required' field must be an array.");
        }

        const json& required = schema["required"];
        std::set<std::string> property_names;
        for (auto it = properties.begin(); it != properties.end(); ++it) {
            property_names.insert(it.key());
        }

        // 验证 required 数组中的每个元素
        for (const auto& req : required) {
            if (!req.is_string()) {
                throw std::logic_error("Items in 'required' array must be strings.");
            }

            std::string req_field = req.get<std::string>();

            // 验证 required 字段是否在 properties 中存在
            if (property_names.find(req_field) == property_names.end()) {
                throw std::logic_error(
                  fmt::format("Required field '{}' is not defined in properties.", req_field));
            }
        }
    }

    // 验证 additionalProperties（如果存在）
    if (schema.contains("additionalProperties")) {
        const auto& additional_props = schema["additionalProperties"];
        // additionalProperties 可以是布尔值或 JSON Schema 对象
        if (!additional_props.is_boolean() && !additional_props.is_object()) {
            throw std::logic_error(
              "inputSchema 'additionalProperties' must be a boolean or JSON Schema object.");
        }
    }
}

}  // namespace hku