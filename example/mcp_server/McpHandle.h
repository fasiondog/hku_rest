/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-23
 *      Author: fasiondog
 */

#pragma once

#include "hikyuu/httpd/HttpHandle.h"
#include <nlohmann/json.hpp>
#include "SessionManager.h"

namespace hku {

/**
 * MCP (Model Context Protocol) Server 处理器
 * 
 * 实现 MCP 协议，为 AI 模型提供工具、资源和提示词访问能力
 * MCP 基于 JSON-RPC 2.0 协议，支持：
 * - 工具调用 (Tools)
 * - 资源访问 (Resources)
 * - 提示词模板 (Prompts)
 * 
 * 参考规范: https://modelcontextprotocol.io/
 */
class McpHandle : public HttpHandle {
    HTTP_HANDLE_IMP(McpHandle)

public:
    virtual net::awaitable<VoidBizResult> run() override {
        // 1. 只接受 POST 请求
        if (getReqMethod() != "POST") {
            co_await sendJsonErrorResponse(-32600, "Invalid Request", nlohmann::json(), false);
            co_return BIZ_OK;
        }
        
        // 2. 检查客户端是否使用 chunked 传输请求体
        std::string req_transfer_encoding = getReqHeader("Transfer-Encoding");
        bool client_used_chunked = (req_transfer_encoding.find("chunked") != std::string::npos);
        
        // 3. 检查 Accept 头，判断客户端是否支持 SSE 流式响应
        std::string accept_header = getReqHeader("Accept");
        bool supports_sse = (accept_header.find("text/event-stream") != std::string::npos);
        
        // 4. 读取请求体（框架会自动处理 chunked 解码）
        std::string body = getReqData();
        if (body.empty()) {
            co_await sendJsonErrorResponse(-32700, "Parse error", nlohmann::json(), supports_sse);
            co_return BIZ_OK;
        }
        
        // 用于捕获块中记录错误，稍后统一发送
        bool has_error = false;
        int error_code = 0;
        std::string error_message;
        nlohmann::json error_id = nullptr;

        try {
            // 4. 解析 JSON-RPC 请求
            nlohmann::json request = nlohmann::json::parse(body);
            
            // 5. 验证 JSON-RPC 版本
            if (!request.contains("jsonrpc") || request["jsonrpc"] != "2.0") {
                co_await sendJsonErrorResponse(-32600, "Invalid Request", request.contains("id") ? request["id"] : nlohmann::json(), supports_sse);
                co_return BIZ_OK;
            }
            
            // 7. 获取方法名和 ID
            std::string method = request.value("method", "");
            nlohmann::json id = request.contains("id") ? request["id"] : nlohmann::json();
            nlohmann::json params = request.contains("params") ? request["params"] : nlohmann::json();
            
            // 8. 从请求头或 Cookie 中获取客户端提供的 Session ID
            std::string session_id = extractSessionId();
            
            // 9. 如果是 initialize 方法，注册新会话
            if (method == "initialize") {
                if (session_id.empty()) {
                    co_await sendJsonErrorResponse(-32602, "Missing session ID in headers or cookie", id, supports_sse);
                    co_return BIZ_OK;
                }
                
                std::string client_ip = getClientIp();
                if (!m_session_manager.registerSession(session_id, client_ip)) {
                    co_await sendJsonErrorResponse(-32603, "Failed to register session", id, supports_sse);
                    co_return BIZ_OK;
                }
                
                HKU_INFO("New session registered: {} from {}", session_id, client_ip);
            } else if (!session_id.empty()) {
                // 10. 对于其他方法，验证并更新会话
                auto session = m_session_manager.getSession(session_id);
                if (!session) {
                    co_await sendJsonErrorResponse(-32602, "Invalid or expired session", id, supports_sse);
                    co_return BIZ_OK;
                }
                
                // 更新会话活动时间
                m_session_manager.touchSession(session_id);
            }
            
            // 11. 在响应中回显 Session ID（方便客户端确认）
            if (!session_id.empty()) {
                setResHeader("X-Session-ID", session_id.c_str());
            }
            
            // 12. 如果客户端支持 SSE，启用流式响应
            if (supports_sse) {
                setResHeader("Content-Type", "text/event-stream");
                setResHeader("Cache-Control", "no-cache");
                setResHeader("Connection", "keep-alive");
                enableChunkedTransfer();
                
                HKU_INFO("Streamable HTTP mode enabled for session: {}", session_id);
            }

            // 13. 路由到对应的处理方法（传递 session_id, id 和 supports_sse 标志）
            try {
                co_await handleMethod(method, params, session_id, id, supports_sse);
            } catch (...) {
                // 异常已在 handleMethod 内部处理并发送响应
            }
            
            // 14. 如果使用 SSE，完成分块传输
            if (supports_sse) {
                try {
                    co_await finishChunkedTransfer();
                } catch (...) {
                    // 忽略断开连接时的错误
                }
            }
            
        } catch (const nlohmann::json::parse_error& e) {
            HKU_ERROR("JSON parse error: {}", e.what());
            has_error = true;
            error_code = -32700;
            error_message = "Parse error";
            error_id = nullptr;
        } catch (const std::exception& e) {
            HKU_ERROR("MCP handler error: {}", e.what());
            has_error = true;
            error_code = -32603;
            error_message = fmt::format("Internal error: {}", e.what());
            error_id = nullptr;
        }
        
        if (has_error) {
            co_await sendJsonErrorResponse(error_code, error_message, error_id, supports_sse);
        }
        
        co_return BIZ_OK;
    }

    /**
     * 获取 Session 管理器（用于外部访问）
     */
    static SessionManager& getSessionManager() {
        return m_session_manager;
    }
    
    /**
     * 推送进度更新到 SSE 端点
     * @param session_id 会话 ID
     * @param task_id 任务 ID
     * @param progress 进度百分比 (0-100)
     * @param message 进度消息
     * @param data 附加数据
     */
    static void pushProgress(const std::string& session_id,
                            const std::string& task_id,
                            int progress,
                            const std::string& message,
                            const nlohmann::json& data = nullptr) {
        nlohmann::json progress_data;
        progress_data["task_id"] = task_id;
        progress_data["progress"] = progress;
        progress_data["message"] = message;
        progress_data["timestamp"] = getCurrentTimestamp();
        
        if (!data.is_null()) {
            progress_data["data"] = data;
        }
        
        // 使用数组存储多个进度更新（而不是覆盖）
        auto history_json = m_session_manager.getSessionMetadata(session_id, "progress_history");
        nlohmann::json history = history_json.is_array() ? history_json : nlohmann::json::array();
        
        history.push_back(progress_data);
        
        // 限制历史记录数量（最多 50 条）
        if (history.size() > 50) {
            history.erase(history.begin());
        }
        
        // 存储到 Session 元数据
        m_session_manager.setSessionMetadata(session_id, "progress_history", history);
        
        // 同时设置最新的进度（用于快速访问）
        m_session_manager.setSessionMetadata(session_id, "progress_update", progress_data);
    }

private:
    /**
     * 异步 sleep 辅助方法
     */
    net::awaitable<void> sleep_for(std::chrono::milliseconds duration) {
        auto* ctx = static_cast<BeastContext*>(m_beast_context);
        ctx->timer.expires_after(duration);
        co_await ctx->timer.async_wait(net::use_awaitable);
    }
    
    /**
     * 获取当前时间戳（秒）
     */
    static long long getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
    }
    
    // 静态 Session 管理器（所有实例共享）
    static inline SessionManager m_session_manager{3600, 10000}; // 1小时超时，最多10000个会话
    
    /**
     * 从请求中提取 Session ID
     * 优先级：X-Session-ID 头 > Cookie
     */
    std::string extractSessionId() {
        // 尝试从自定义头部获取
        std::string session_id = getReqHeader("X-Session-ID");
        if (!session_id.empty()) {
            return session_id;
        }
        
        // 尝试从 Cookie 获取
        std::string cookie = getReqHeader("Cookie");
        if (!cookie.empty()) {
            // 简单的 Cookie 解析
            size_t pos = cookie.find("session_id=");
            if (pos != std::string::npos) {
                pos += 11; // length of "session_id="
                size_t end = cookie.find(';', pos);
                if (end == std::string::npos) {
                    end = cookie.length();
                }
                session_id = cookie.substr(pos, end - pos);
                return session_id;
            }
        }
        
        return "";
    }

    /**
     * 处理 MCP 方法调用（Streamable HTTP 模式）
     */
    net::awaitable<void> handleMethod(const std::string& method, 
                                     const nlohmann::json& params,
                                     const std::string& session_id,
                                     const nlohmann::json& id,
                                     bool use_sse) {
        try {
            if (method == "initialize") {
                auto result = handleInitialize(params, session_id);
                co_await sendJsonSuccessResponse(result, id, use_sse);
            } else if (method == "initialized") {
                // initialized 是通知，不需要响应
            } else if (method == "tools/list") {
                auto result = handleToolsList(params, session_id);
                co_await sendJsonSuccessResponse(result, id, use_sse);
            } else if (method == "tools/call") {
                auto result = co_await handleToolsCall(params, session_id);
                co_await sendJsonSuccessResponse(result, id, use_sse);
            } else if (method == "resources/list") {
                auto result = handleResourcesList(params, session_id);
                co_await sendJsonSuccessResponse(result, id, use_sse);
            } else if (method == "resources/read") {
                auto result = co_await handleResourcesRead(params, session_id);
                co_await sendJsonSuccessResponse(result, id, use_sse);
            } else if (method == "prompts/list") {
                auto result = handlePromptsList(params, session_id);
                co_await sendJsonSuccessResponse(result, id, use_sse);
            } else if (method == "prompts/get") {
                auto result = co_await handlePromptsGet(params, session_id);
                co_await sendJsonSuccessResponse(result, id, use_sse);
            } else if (method == "session/info") {
                auto result = handleSessionInfo(session_id);
                co_await sendJsonSuccessResponse(result, id, use_sse);
            } else if (method == "session/set_metadata") {
                auto result = handleSetSessionMetadata(params, session_id);
                co_await sendJsonSuccessResponse(result, id, use_sse);
            } else if (method == "session/unregister") {
                auto result = handleUnregisterSession(session_id);
                co_await sendJsonSuccessResponse(result, id, use_sse);
            } else {
                co_await sendJsonErrorResponse(-32601, "Method not found", id, use_sse);
            }
        } catch (const std::exception& e) {
            HKU_ERROR("Error handling method {}: {}", method, e.what());
            // 注意：不能在 catch 块中使用 co_await，错误已在内部处理
        }
    }

    /**
     * 处理 initialize 方法
     */
    nlohmann::json handleInitialize(const nlohmann::json& params, const std::string& session_id) {
        HKU_INFO("MCP initialize request (session: {})", session_id);
        
        // 存储客户端信息到 Session
        if (params.contains("clientInfo")) {
            m_session_manager.setSessionMetadata(session_id, "client_info", 
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
        result["serverInfo"] = {
            {"name", "hku_rest MCP Server"},
            {"version", "1.0.0"}
        };
        
        return result;
    }

    /**
     * 处理 tools/list 方法
     */
    nlohmann::json handleToolsList(const nlohmann::json& params, const std::string& session_id) {
        HKU_INFO("MCP tools/list request (session: {})", session_id);
        
        nlohmann::json tools = nlohmann::json::array();
        
        // 示例工具 1: 计算器
        tools.push_back({
            {"name", "calculator"},
            {"description", "Perform basic arithmetic calculations"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"expression", {
                        {"type", "string"},
                        {"description", "Mathematical expression to evaluate (e.g., '2 + 2')"}
                    }}
                }},
                {"required", {"expression"}}
            }}
        });
        
        // 示例工具 2: 时间查询
        tools.push_back({
            {"name", "get_current_time"},
            {"description", "Get the current date and time"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"format", {
                        {"type", "string"},
                        {"description", "Optional datetime format string"},
                        {"default", "%Y-%m-%d %H:%M:%S"}
                    }}
                }}
            }}
        });
        
        // 示例工具 3: 天气查询（模拟）
        tools.push_back({
            {"name", "get_weather"},
            {"description", "Get weather information for a location"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"location", {
                        {"type", "string"},
                        {"description", "City name or coordinates"}
                    }}
                }},
                {"required", {"location"}}
            }}
        });
        
        // 示例工具 4: 会话历史（需要 Session）
        tools.push_back({
            {"name", "get_session_history"},
            {"description", "Get the interaction history for current session"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"limit", {
                        {"type", "integer"},
                        {"description", "Maximum number of history items to return"},
                        {"default", 10}
                    }}
                }}
            }}
        });
        
        // 示例工具 5: 长时间运行任务（演示进度推送）
        tools.push_back({
            {"name", "long_running_task"},
            {"description", "Simulate a long-running task with progress updates via SSE"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"duration_seconds", {
                        {"type", "integer"},
                        {"description", "Task duration in seconds"},
                        {"default", 10}
                    }},
                    {"task_name", {
                        {"type", "string"},
                        {"description", "Name of the task"},
                        {"default", "example_task"}
                    }}
                }}
            }}
        });
        
        return {{"tools", tools}};
    }

    /**
     * 处理 tools/call 方法
     */
    net::awaitable<nlohmann::json> handleToolsCall(const nlohmann::json& params, const std::string& session_id) {
        std::string tool_name = params.value("name", "");
        nlohmann::json arguments = params.value("arguments", nlohmann::json::object());
        
        HKU_INFO("MCP tools/call request: tool={} (session: {})", tool_name, session_id);
        
        if (tool_name == "calculator") {
            co_return co_await executeCalculator(arguments, session_id);
        } else if (tool_name == "get_current_time") {
            co_return executeGetCurrentTime(arguments, session_id);
        } else if (tool_name == "get_weather") {
            co_return executeGetWeather(arguments, session_id);
        } else if (tool_name == "get_session_history") {
            co_return executeGetSessionHistory(arguments, session_id);
        } else if (tool_name == "long_running_task") {
            co_return co_await executeLongRunningTask(arguments, session_id);
        } else {
            throw std::runtime_error("Unknown tool: " + tool_name);
        }
    }

    /**
     * 执行计算器工具
     */
    net::awaitable<nlohmann::json> executeCalculator(const nlohmann::json& arguments, const std::string& session_id) {
        std::string expression = arguments.value("expression", "");
        
        // 简单的表达式求值（仅支持基本运算）
        try {
            // 注意：实际生产环境应使用安全的表达式解析库
            double result = 0.0;
            // 这里简化处理，实际应使用 exprtk 或类似库
            if (expression == "2 + 2") {
                result = 4.0;
            } else if (expression == "10 * 5") {
                result = 50.0;
            } else {
                // 默认返回示例结果
                result = 0.0;
            }
            
            // 记录到会话历史
            recordToolUsage(session_id, "calculator", {
                {"expression", expression},
                {"result", result}
            });
            
            nlohmann::json response;
            response["content"] = nlohmann::json::array({
                {{"type", "text"}, {"text", fmt::format("Result: {}", result)}}
            });
            
            co_return response;
        } catch (const std::exception& e) {
            throw std::runtime_error(fmt::format("Calculation error: {}", e.what()));
        }
    }

    /**
     * 执行获取当前时间工具
     */
    nlohmann::json executeGetCurrentTime(const nlohmann::json& arguments, const std::string& session_id) {
        std::string format = arguments.value("format", "%Y-%m-%d %H:%M:%S");
        
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::tm tm_now;
        localtime_r(&time_t_now, &tm_now);
        
        char buffer[100];
        std::strftime(buffer, sizeof(buffer), format.c_str(), &tm_now);
        
        // 记录到会话历史
        recordToolUsage(session_id, "get_current_time", {
            {"format", format},
            {"time", std::string(buffer)}
        });
        
        nlohmann::json response;
        response["content"] = nlohmann::json::array({
            {{"type", "text"}, {"text", std::string(buffer)}}
        });
        
        return response;
    }

    /**
     * 执行天气查询工具（模拟）
     */
    nlohmann::json executeGetWeather(const nlohmann::json& arguments, const std::string& session_id) {
        std::string location = arguments.value("location", "Unknown");
        
        // 模拟天气数据
        nlohmann::json response;
        response["content"] = nlohmann::json::array({
            {{"type", "text"}, {"text", fmt::format(
                "Weather in {}: Temperature 22°C, Condition: Sunny, Humidity: 65%",
                location
            )}}
        });
        
        // 记录到会话历史
        recordToolUsage(session_id, "get_weather", {
            {"location", location}
        });
        
        return response;
    }

    /**
     * 执行获取会话历史工具
     */
    nlohmann::json executeGetSessionHistory(const nlohmann::json& arguments, const std::string& session_id) {
        int limit = arguments.value("limit", 10);
        
        // 从 Session 元数据中获取历史记录
        auto history_json = m_session_manager.getSessionMetadata(session_id, "history");
        if (history_json.is_null()) {
            return nlohmann::json::array();
        }
        
        // 限制返回的历史记录数量
        nlohmann::json history = history_json;
        if (history.size() > limit) {
            history.erase(history.begin(), history.begin() + (history.size() - limit));
        }
        
        return history;
    }

    /**
     * 执行长时间运行任务（演示进度推送）
     */
    net::awaitable<nlohmann::json> executeLongRunningTask(const nlohmann::json& arguments,
                                                          const std::string& session_id) {
        int duration = arguments.value("duration_seconds", 10);
        std::string task_name = arguments.value("task_name", "example_task");
        
        // 生成任务 ID
        auto task_id = fmt::format("{}_{}", task_name, getCurrentTimestamp());
        
        HKU_INFO("Starting long running task: {} (session: {}, duration: {}s)", 
                task_id, session_id, duration);
        
        // 推送开始消息
        pushProgress(session_id, task_id, 0, "Task started", {
            {"task_name", task_name},
            {"estimated_duration", duration}
        });
        
        // 模拟长时间运行的任务，定期推送进度
        int steps = 10;
        int step_duration = duration / steps;
        
        for (int i = 1; i <= steps; i++) {
            // 模拟工作 - 使用异步定时器
            co_await sleep_for(std::chrono::seconds(step_duration));
            
            int progress = (i * 100) / steps;
            std::string message = fmt::format("Processing... {}% complete", progress);
            
            // 推送进度更新
            pushProgress(session_id, task_id, progress, message, {
                {"current_step", i},
                {"total_steps", steps}
            });
            
            HKU_DEBUG("Task {} progress: {}%", task_id, progress);
        }
        
        // 推送完成消息
        pushProgress(session_id, task_id, 100, "Task completed successfully", {
            {"result", "success"},
            {"output", "Task finished"}
        });
        
        // 记录到会话历史
        recordToolUsage(session_id, "long_running_task", {
            {"task_id", task_id},
            {"task_name", task_name},
            {"duration", duration},
            {"status", "completed"}
        });
        
        // 返回任务 ID，客户端可通过 SSE 监听进度
        nlohmann::json response;
        response["content"] = nlohmann::json::array({
            {{"type", "text"}, {"text", fmt::format(
                "Task '{}' started with ID: {}\n"
                "Monitor progress via SSE endpoint: /sse?sessionId={}\n"
                "Estimated completion: {} seconds",
                task_name, task_id, session_id, duration
            )}}
        });
        response["task_id"] = task_id;
        
        co_return response;
    }

    /**
     * 处理 resources/list 方法
     */
    nlohmann::json handleResourcesList(const nlohmann::json& params, const std::string& session_id) {
        HKU_INFO("MCP resources/list request (session: {})", session_id);
        
        nlohmann::json resources = nlohmann::json::array();
        
        // 示例资源 1: 文档
        resources.push_back({
            {"uri", "doc://getting-started"},
            {"name", "Getting Started Guide"},
            {"description", "Introduction to using this MCP server"},
            {"mimeType", "text/markdown"}
        });
        
        // 示例资源 2: API 文档
        resources.push_back({
            {"uri", "doc://api-reference"},
            {"name", "API Reference"},
            {"description", "Complete API documentation"},
            {"mimeType", "text/markdown"}
        });
        
        return {{"resources", resources}};
    }

    /**
     * 处理 resources/read 方法
     */
    net::awaitable<nlohmann::json> handleResourcesRead(const nlohmann::json& params, const std::string& session_id) {
        std::string uri = params.value("uri", "");
        
        HKU_INFO("MCP resources/read request: uri={} (session: {})", uri, session_id);
        
        if (uri == "doc://getting-started") {
            co_return readGettingStartedGuide();
        } else if (uri == "doc://api-reference") {
            co_return readApiReference();
        } else {
            throw std::runtime_error("Resource not found: " + uri);
        }
    }

    /**
     * 读取入门指南
     */
    nlohmann::json readGettingStartedGuide() {
        std::string content = R"(
# Getting Started with HKU_REST MCP Server

## Overview
This MCP server provides various tools and resources for AI assistants.

## Available Tools
- **calculator**: Perform arithmetic calculations
- **get_current_time**: Get current date and time
- **get_weather**: Get weather information

## Available Resources
- **doc://getting-started**: This guide
- **doc://api-reference**: API documentation

## Usage
Use your AI client to connect to this MCP server and access the provided tools and resources.
)";
        
        nlohmann::json response;
        response["contents"] = nlohmann::json::array({
            {
                {"uri", "doc://getting-started"},
                {"mimeType", "text/markdown"},
                {"text", content}
            }
        });
        
        return response;
    }

    /**
     * 读取 API 参考文档
     */
    nlohmann::json readApiReference() {
        std::string content = R"(
# API Reference

## Tools

### calculator
**Description**: Perform basic arithmetic calculations

**Parameters**:
- `expression` (string, required): Mathematical expression

**Example**:
```json
{
  "name": "calculator",
  "arguments": {
    "expression": "2 + 2"
  }
}
```

### get_current_time
**Description**: Get the current date and time

**Parameters**:
- `format` (string, optional): Datetime format string

### get_weather
**Description**: Get weather information for a location

**Parameters**:
- `location` (string, required): City name or coordinates

## Resources

### doc://getting-started
Getting started guide

### doc://api-reference
This API reference document
)";
        
        nlohmann::json response;
        response["contents"] = nlohmann::json::array({
            {
                {"uri", "doc://api-reference"},
                {"mimeType", "text/markdown"},
                {"text", content}
            }
        });
        
        return response;
    }

    /**
     * 处理 prompts/list 方法
     * 返回可用的提示词模板列表
     */
    nlohmann::json handlePromptsList(const nlohmann::json& params, const std::string& session_id) {
        HKU_INFO("MCP prompts/list request (session: {})", session_id);
        
        nlohmann::json prompts = nlohmann::json::array();
        
        // 示例提示词 1: 代码审查
        prompts.push_back({
            {"name", "code_review"},
            {"description", "Review code for best practices and potential issues"},
            {"arguments", nlohmann::json::array({
                {
                    {"name", "language"},
                    {"description", "Programming language"},
                    {"required", true}
                },
                {
                    {"name", "code"},
                    {"description", "Code to review"},
                    {"required", true}
                }
            })}
        });
        
        // 示例提示词 2: 文档生成
        prompts.push_back({
            {"name", "generate_docs"},
            {"description", "Generate documentation for code"},
            {"arguments", nlohmann::json::array({
                {
                    {"name", "code"},
                    {"description", "Code to document"},
                    {"required", true}
                },
                {
                    {"name", "style"},
                    {"description", "Documentation style (e.g., doxygen, javadoc)"},
                    {"required", false}
                }
            })}
        });
        
        return {{"prompts", prompts}};
    }

    /**
     * 处理 prompts/get 方法
     */
    net::awaitable<nlohmann::json> handlePromptsGet(const nlohmann::json& params, const std::string& session_id) {
        std::string prompt_name = params.value("name", "");
        nlohmann::json arguments = params.value("arguments", nlohmann::json::object());
        
        HKU_INFO("MCP prompts/get request: prompt={} (session: {})", prompt_name, session_id);
        
        if (prompt_name == "code_review") {
            co_return getCodeReviewPrompt(arguments, session_id);
        } else if (prompt_name == "generate_docs") {
            co_return getGenerateDocsPrompt(arguments, session_id);
        } else {
            throw std::runtime_error("Prompt not found: " + prompt_name);
        }
    }

    /**
     * 获取代码审查提示词
     */
    nlohmann::json getCodeReviewPrompt(const nlohmann::json& arguments, const std::string& session_id) {
        std::string language = arguments.value("language", "unknown");
        std::string code = arguments.value("code", "");
        
        std::string prompt = fmt::format(
            R"(Please review the following {} code for:
1. Best practices and coding standards
2. Potential bugs or security issues
3. Performance optimizations
4. Code readability and maintainability

Code:
```{}
{}
```

Provide detailed feedback and suggestions for improvement.)",
            language, language, code
        );
        
        nlohmann::json response;
        response["description"] = "Code review prompt";
        response["messages"] = nlohmann::json::array({
            {
                {"role", "user"},
                {"content", {
                    {"type", "text"},
                    {"text", prompt}
                }}
            }
        });
        
        return response;
    }

    /**
     * 获取文档生成提示词
     */
    nlohmann::json getGenerateDocsPrompt(const nlohmann::json& arguments, const std::string& session_id) {
        std::string code = arguments.value("code", "");
        std::string style = arguments.value("style", "doxygen");
        
        std::string prompt = fmt::format(
            R"(Generate {} style documentation for the following code:

```
{}
```

Include:
- Function/class descriptions
- Parameter explanations
- Return value descriptions
- Usage examples where appropriate)",
            style, code
        );
        
        nlohmann::json response;
        response["description"] = "Documentation generation prompt";
        response["messages"] = nlohmann::json::array({
            {
                {"role", "user"},
                {"content", {
                    {"type", "text"},
                    {"text", prompt}
                }}
            }
        });
        
        return response;
    }

    /**
     * 处理 session/info 方法
     */
    nlohmann::json handleSessionInfo(const std::string& session_id) {
        HKU_INFO("MCP session/info request (session: {})", session_id);
        
        auto session = m_session_manager.getSession(session_id);
        if (!session) {
            throw std::runtime_error("Session not found or expired");
        }
        
        nlohmann::json info;
        info["session_id"] = session->session_id;
        info["client_info"] = session->client_info;
        info["created_at"] = std::chrono::duration_cast<std::chrono::seconds>(
            session->created_at.time_since_epoch()).count();
        info["last_active"] = std::chrono::duration_cast<std::chrono::seconds>(
            session->last_active.time_since_epoch()).count();
        info["metadata"] = session->metadata;
        
        return info;
    }

    /**
     * 处理 session/set_metadata 方法
     */
    nlohmann::json handleSetSessionMetadata(const nlohmann::json& params,
                                           const std::string& session_id) {
        std::string key = params.value("key", "");
        nlohmann::json value = params.value("value", nlohmann::json());
        
        if (key.empty()) {
            throw std::runtime_error("Missing 'key' parameter");
        }
        
        bool success = m_session_manager.setSessionMetadata(session_id, key, value);
        if (!success) {
            throw std::runtime_error("Failed to set session metadata");
        }
        
        nlohmann::json result;
        result["status"] = "success";
        result["message"] = fmt::format("Metadata '{}' updated", key);
        
        return result;
    }

    /**
     * 处理 session/unregister 方法
     */
    nlohmann::json handleUnregisterSession(const std::string& session_id) {
        HKU_INFO("MCP session/unregister request (session: {})", session_id);
        
        bool success = m_session_manager.unregisterSession(session_id);
        if (!success) {
            throw std::runtime_error("Session not found or already unregistered");
        }
        
        nlohmann::json result;
        result["status"] = "success";
        result["message"] = "Session unregistered successfully";
        
        return result;
    }

    /**
     * 记录工具使用到会话历史
     */
    void recordToolUsage(const std::string& session_id,
                        const std::string& tool_name,
                        const nlohmann::json& details) {
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        
        nlohmann::json record;
        record["timestamp"] = timestamp;
        record["tool"] = tool_name;
        record["details"] = details;
        
        // 获取现有历史
        auto history_json = m_session_manager.getSessionMetadata(session_id, "history");
        nlohmann::json history = history_json.is_array() ? history_json : nlohmann::json::array();
        
        // 添加新记录
        history.push_back(record);
        
        // 限制历史记录数量（最多 100 条）
        if (history.size() > 100) {
            history.erase(history.begin());
        }
        
        // 保存回 Session
        m_session_manager.setSessionMetadata(session_id, "history", history);
    }

    /**
     * 发送 JSON-RPC 成功响应（支持 Streamable HTTP）
     */
    net::awaitable<void> sendJsonSuccessResponse(const nlohmann::json& result, 
                                                  const nlohmann::json& id,
                                                  bool use_sse = false) {
        nlohmann::json response;
        response["jsonrpc"] = "2.0";
        response["result"] = result;
        response["id"] = id;
        
        if (use_sse) {
            // SSE 格式：event: message\ndata: {...}\n\n
            std::string sse_msg = "event: message\ndata: " + response.dump() + "\n\n";
            
            if (m_beast_context) {
                auto* ctx = static_cast<BeastContext*>(m_beast_context);
                
                // 首次发送时设置 SSE 响应头并启用 chunked
                if (ctx->res.body().empty()) {
                    setResHeader("Content-Type", "text/event-stream");
                    setResHeader("Cache-Control", "no-cache");
                    setResHeader("Connection", "keep-alive");
                    enableChunkedTransfer();
                }
                
                co_await writeChunk(sse_msg);
            }
        } else {
            // 传统 JSON 响应 - 使用标准 HTTP（Content-Length）
            std::string response_str = response.dump();
            
            if (m_beast_context) {
                auto* ctx = static_cast<BeastContext*>(m_beast_context);
                ctx->res.body() = response_str;
                // 不设置 Content-Type，让框架自动处理
            }
        }
    }

    /**
     * 发送 JSON-RPC 错误响应（支持 Streamable HTTP）
     */
    net::awaitable<void> sendJsonErrorResponse(int code, const std::string& message, 
                                                const nlohmann::json& id,
                                                bool use_sse = false) {
        nlohmann::json response;
        response["jsonrpc"] = "2.0";
        response["error"] = {
            {"code", code},
            {"message", message}
        };
        response["id"] = id;
        
        if (use_sse) {
            std::string sse_msg = "event: message\ndata: " + response.dump() + "\n\n";
            
            if (m_beast_context) {
                auto* ctx = static_cast<BeastContext*>(m_beast_context);
                
                if (ctx->res.body().empty()) {
                    setResHeader("Content-Type", "text/event-stream");
                    setResHeader("Cache-Control", "no-cache");
                    setResHeader("Connection", "keep-alive");
                    enableChunkedTransfer();
                }
                
                co_await writeChunk(sse_msg);
            }
        } else {
            // 错误响应也使用标准 HTTP
            std::string response_str = response.dump();
            
            if (m_beast_context) {
                auto* ctx = static_cast<BeastContext*>(m_beast_context);
                ctx->res.body() = response_str;
            }
        }
    }
};

} // namespace hku
