/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-23
 *      Author: fasiondog
 */

#pragma once

#include <random>
#include <nlohmann/json.hpp>
#include "hikyuu/utilities/base64.h"
#include "hikyuu/httpd/HttpHandle.h"
#include "hikyuu/utilities/datetime/Datetime.h"
#include "McpError.h"
#include "SessionManager.h"

namespace hku {

class McpService;

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
    McpHandle(void* beast_context, McpService* service)
    : HttpHandle(beast_context), m_service(service) {}

    virtual net::awaitable<VoidBizResult> run() override;

    /**
     * 获取 Session 管理器引用（用于后台清理线程）
     */
    SessionManager& getSessionManager();

    /**
     * 推送进度更新到 SSE 端点（实时流式推送 + 存储历史）
     * @param session_id 会话 ID
     * @param task_id 任务 ID
     * @param progress 进度百分比 (0-100)
     * @param message 进度消息
     * @param data 附加数据
     */
    net::awaitable<void> pushProgress(const std::string& session_id, const std::string& task_id,
                                      int progress, const std::string& message,
                                      const nlohmann::json& data = nullptr) {
        // 1. 构建 JSON-RPC 进度通知消息
        nlohmann::json progress_notification;
        progress_notification["jsonrpc"] = "2.0";
        progress_notification["method"] = "notifications/progress";

        nlohmann::json params;
        params["progressToken"] = task_id;  // 使用 task_id 作为 progressToken
        params["progress"] = progress;
        params["total"] = 100;
        params["message"] = message;

        if (!data.is_null()) {
            params["data"] = data;
        }

        progress_notification["params"] = params;

        // 2. 如果启用了 chunked transfer，实时推送给客户端
        if (m_beast_context && m_chunked_transfer) {
            // 推送 SSE 格式的消息
            std::string sse_msg = "data: " + progress_notification.dump() + "\n\n";
            co_await writeChunk(sse_msg);

            HKU_DEBUG("Pushed progress notification: {}% - {} (session: {})", progress, message,
                      session_id);
        }

        // 3. 同时存储到 Session 元数据（用于历史记录和查询）
        nlohmann::json progress_data;
        progress_data["task_id"] = task_id;
        progress_data["progress"] = progress;
        progress_data["message"] = message;
        progress_data["timestamp"] = getCurrentTimestamp();

        if (!data.is_null()) {
            progress_data["data"] = data;
        }

        // 使用数组存储多个进度更新（而不是覆盖）
        auto history_json = getSessionManager().getSessionMetadata(session_id, "progress_history");
        nlohmann::json history = history_json.is_array() ? history_json : nlohmann::json::array();

        history.push_back(progress_data);

        // 限制历史记录数量（最多 50 条）
        if (history.size() > 50) {
            history.erase(history.begin());
        }

        // 存储到 Session 元数据
        getSessionManager().setSessionMetadata(session_id, "progress_history", history);

        // 同时设置最新的进度（用于快速访问）
        getSessionManager().setSessionMetadata(session_id, "progress_update", progress_data);
    }

private:
    McpService* m_service{nullptr};

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
        return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    }

    /**
     * 生成符合 MCP 协议规范的 Session ID
     * 使用 UUID v4 格式，仅包含可见 ASCII 字符 (0x21-0x7E)
     */
    static std::string generateSessionId() {
        // 使用 UUID v4 格式: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
        // 其中 x 是随机十六进制数字，y 是 8, 9, A, 或 B
        static thread_local std::random_device rd;
        static thread_local std::mt19937 gen(rd());
        static thread_local std::uniform_int_distribution<> dis(0, 15);

        auto hex_char = [&]() -> char {
            const char hex[] = "0123456789abcdef";
            return hex[dis(gen)];
        };

        std::string uuid;
        uuid.reserve(36);

        for (int i = 0; i < 8; i++)
            uuid += hex_char();
        uuid += '-';
        for (int i = 0; i < 4; i++)
            uuid += hex_char();
        uuid += "-4";  // UUID version 4
        for (int i = 0; i < 3; i++)
            uuid += hex_char();
        uuid += '-';
        // Variant bits: 10xx (8, 9, a, b)
        const char variant[] = "89ab";
        uuid += variant[dis(gen) % 4];
        for (int i = 0; i < 3; i++)
            uuid += hex_char();
        uuid += '-';
        for (int i = 0; i < 12; i++)
            uuid += hex_char();

        return uuid;
    }

    /**
     * 从请求中提取 Session ID
     * 使用标准的 Mcp-Session-Id 头字段
     */
    std::string extractSessionId() {
        return getReqHeader("Mcp-Session-Id");
    }

    /**
     * 处理 MCP 方法调用（Streamable HTTP 模式）
     */
    net::awaitable<void> handleMethod(const std::string& method, const nlohmann::json& params,
                                      const std::string& session_id, const nlohmann::json& id,
                                      bool use_sse) {
        try {
            if (method == "initialize") {
                auto result = handleInitialize(params, session_id);
                co_await sendMcpSuccessResponse(result, id, use_sse);
            } else if (method == "initialized") {
                // initialized 是通知，不需要响应
            } else if (method == "tools/list") {
                auto result = handleToolsList(params, session_id);
                co_await sendMcpSuccessResponse(result, id, use_sse);
            } else if (method == "tools/call") {
                auto result = co_await handleToolsCall(params, session_id);
                co_await sendMcpSuccessResponse(result, id, use_sse);
            } else if (method == "resources/list") {
                auto result = handleResourcesList(params, session_id);
                co_await sendMcpSuccessResponse(result, id, use_sse);
            } else if (method == "resources/read") {
                auto result = co_await handleResourcesRead(params, session_id);
                co_await sendMcpSuccessResponse(result, id, use_sse);
            } else if (method == "prompts/list") {
                auto result = handlePromptsList(params, session_id);
                co_await sendMcpSuccessResponse(result, id, use_sse);
            } else if (method == "prompts/get") {
                auto result = co_await handlePromptsGet(params, session_id);
                co_await sendMcpSuccessResponse(result, id, use_sse);
            } else if (method == "ping") {
                // MCP 协议 ping 方法 - 用于连接健康检查
                auto result = handlePing(params, session_id);
                co_await sendMcpSuccessResponse(result, id, use_sse);
            } else if (method == "session/info") {
                auto result = handleSessionInfo(session_id);
                co_await sendMcpSuccessResponse(result, id, use_sse);
            } else if (method == "session/set_metadata") {
                auto result = handleSetSessionMetadata(params, session_id);
                co_await sendMcpSuccessResponse(result, id, use_sse);
            } else if (method == "session/unregister") {
                auto result = handleUnregisterSession(session_id);
                co_await sendMcpSuccessResponse(result, id, use_sse);
            } else {
                co_await sendMcpErrorResponse(BIZ_JSONRPC_METHOD_NOT_FOUND, id, use_sse);
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
            getSessionManager().setSessionMetadata(session_id, "client_info", params["clientInfo"]);
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

        return result;
    }

    /**
     * 处理 tools/list 方法
     */
    nlohmann::json handleToolsList(const nlohmann::json& params, const std::string& session_id) {
        HKU_INFO("MCP tools/list request (session: {})", session_id);

        nlohmann::json tools = nlohmann::json::array();

        // 示例工具 1: 计算器
        tools.push_back(
          {{"name", "calculator"},
           {"description", "Perform basic arithmetic calculations"},
           {"inputSchema",
            {{"type", "object"},
             {"properties",
              {{"expression",
                {{"type", "string"},
                 {"description", "Mathematical expression to evaluate (e.g., '2 + 2')"}}}}},
             {"required", {"expression"}}}}});

        // 示例工具 2: 时间查询
        tools.push_back({{"name", "get_current_time"},
                         {"description", "Get the current date and time"},
                         {"inputSchema",
                          {{"type", "object"},
                           {"properties",
                            {{"format",
                              {{"type", "string"},
                               {"description", "Optional datetime format string"},
                               {"default", "%Y-%m-%d %H:%M:%S"}}}}}}}});

        // 示例工具 3: 天气查询（模拟）
        tools.push_back(
          {{"name", "get_weather"},
           {"description", "Get weather information for a location"},
           {"inputSchema",
            {{"type", "object"},
             {"properties",
              {{"location", {{"type", "string"}, {"description", "City name or coordinates"}}}}},
             {"required", {"location"}}}}});

        // 示例工具 4: 会话历史（需要 Session）
        tools.push_back({{"name", "get_session_history"},
                         {"description", "Get the interaction history for current session"},
                         {"inputSchema",
                          {{"type", "object"},
                           {"properties",
                            {{"limit",
                              {{"type", "integer"},
                               {"description", "Maximum number of history items to return"},
                               {"default", 10}}}}}}}});

        // 示例工具 5: 长时间运行任务（演示进度推送）
        tools.push_back(
          {{"name", "long_running_task"},
           {"description", "Simulate a long-running task with progress updates via SSE"},
           {"inputSchema",
            {{"type", "object"},
             {"properties",
              {{"duration_seconds",
                {{"type", "integer"},
                 {"description", "Task duration in seconds"},
                 {"default", 10}}},
               {"task_name",
                {{"type", "string"},
                 {"description", "Name of the task"},
                 {"default", "example_task"}}}}}}}});

        // 示例工具 6: 分页数据查询（演示 MCP 分页机制）
        tools.push_back(
          {{"name", "query_paginated_data"},
           {"description",
            "Query a large dataset with pagination support. Returns items in pages "
            "using cursor-based pagination."},
           {"inputSchema",
            {{"type", "object"},
             {"properties",
              {{"page_size",
                {{"type", "integer"},
                 {"description", "Number of items per page (default: 10, max: 100)"},
                 {"default", 10},
                 {"minimum", 1},
                 {"maximum", 100}}},
               {"cursor",
                {{"type", "string"},
                 {"description",
                  "Opaque cursor string for pagination (omit for first page)"}}}}}}}});

        return {{"tools", tools}};
    }

    /**
     * 处理 tools/call 方法
     */
    net::awaitable<nlohmann::json> handleToolsCall(const nlohmann::json& params,
                                                   const std::string& session_id) {
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
        } else if (tool_name == "query_paginated_data") {
            co_return executeQueryPaginatedData(arguments, session_id);
        } else {
            throw std::runtime_error("Unknown tool: " + tool_name);
        }
    }

    /**
     * 执行计算器工具
     */
    net::awaitable<nlohmann::json> executeCalculator(const nlohmann::json& arguments,
                                                     const std::string& session_id) {
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
            recordToolUsage(session_id, "calculator",
                            {{"expression", expression}, {"result", result}});

            nlohmann::json response;
            response["content"] = nlohmann::json::array(
              {{{"type", "text"}, {"text", fmt::format("Result: {}", result)}}});

            co_return response;
        } catch (const std::exception& e) {
            throw std::runtime_error(fmt::format("Calculation error: {}", e.what()));
        }
    }

    /**
     * 执行获取当前时间工具
     */
    nlohmann::json executeGetCurrentTime(const nlohmann::json& arguments,
                                         const std::string& session_id) {
        std::string format = arguments.value("format", "%Y-%m-%d %H:%M:%S");

        auto now = Datetime::now();
        // 记录到会话历史
        recordToolUsage(session_id, "get_current_time", {{"format", format}, {"time", now.str()}});

        nlohmann::json response;
        response["content"] = nlohmann::json::array({{{"type", "text"}, {"text", now.str()}}});

        return response;
    }

    /**
     * 执行天气查询工具（模拟）
     */
    nlohmann::json executeGetWeather(const nlohmann::json& arguments,
                                     const std::string& session_id) {
        std::string location = arguments.value("location", "Unknown");

        // 模拟天气数据
        nlohmann::json response;
        response["content"] = nlohmann::json::array(
          {{{"type", "text"},
            {"text", fmt::format("Weather in {}: Temperature 22°C, Condition: Sunny, Humidity: 65%",
                                 location)}}});

        // 记录到会话历史
        recordToolUsage(session_id, "get_weather", {{"location", location}});

        return response;
    }

    /**
     * 执行获取会话历史工具
     */
    nlohmann::json executeGetSessionHistory(const nlohmann::json& arguments,
                                            const std::string& session_id) {
        int limit = arguments.value("limit", 10);

        // 从 Session 元数据中获取历史记录
        auto history_json = getSessionManager().getSessionMetadata(session_id, "history");
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

        HKU_INFO("Starting long running task: {} (session: {}, duration: {}s)", task_id, session_id,
                 duration);

        // 推送开始消息
        co_await pushProgress(session_id, task_id, 0, "Task started",
                              {{"task_name", task_name}, {"estimated_duration", duration}});

        // 模拟长时间运行的任务，定期推送进度
        int steps = 10;
        int step_duration = duration / steps;

        for (int i = 1; i <= steps; i++) {
            // 模拟工作 - 使用异步定时器
            co_await sleep_for(std::chrono::seconds(step_duration));

            int progress = (i * 100) / steps;
            std::string message = fmt::format("Processing... {}% complete", progress);

            // 推送进度更新
            co_await pushProgress(session_id, task_id, progress, message,
                                  {{"current_step", i}, {"total_steps", steps}});

            HKU_DEBUG("Task {} progress: {}%", task_id, progress);
        }

        // 推送完成消息
        co_await pushProgress(session_id, task_id, 100, "Task completed successfully",
                              {{"result", "success"}, {"output", "Task finished"}});

        // 记录到会话历史
        recordToolUsage(session_id, "long_running_task",
                        {{"task_id", task_id},
                         {"task_name", task_name},
                         {"duration", duration},
                         {"status", "completed"}});

        // 返回任务 ID，客户端可通过 SSE 监听进度
        nlohmann::json response;
        response["content"] = nlohmann::json::array(
          {{{"type", "text"},
            {"text", fmt::format("Task '{}' completed with ID: {}\n"
                                 "Total duration: {} seconds\n"
                                 "Received {} progress updates via SSE stream",
                                 task_name, task_id, duration, steps)}}});
        response["task_id"] = task_id;

        co_return response;
    }

    /**
     * 执行分页数据查询工具（演示 MCP 分页机制）
     *
     * 模拟一个大型数据集，支持基于游标的分页查询
     * 符合 MCP 协议规范：
     * - 使用 cursor 参数进行分页
     * - 返回 nextCursor 指示是否有更多数据
     * - cursor 是不透明的字符串标记
     */
    nlohmann::json executeQueryPaginatedData(const nlohmann::json& arguments,
                                             const std::string& session_id) {
        int page_size = arguments.value("page_size", 10);
        std::string cursor = arguments.value("cursor", "");

        // 限制页面大小
        if (page_size < 1)
            page_size = 1;
        if (page_size > 100)
            page_size = 100;

        HKU_INFO("MCP query_paginated_data: page_size={}, cursor={} (session: {})", page_size,
                 cursor.empty() ? "none" : cursor, session_id);

        // 模拟一个包含 250 条记录的大型数据集
        // 在实际应用中，这里应该是数据库查询或 API 调用
        const int total_items = 250;

        // 解析游标（游标是 base64 编码的偏移量）
        int start_idx = 0;
        if (!cursor.empty()) {
            try {
                // 解码 base64 游标
                std::string decoded_cursor = base64_decode(cursor);
                start_idx = std::stoi(decoded_cursor);

                // 验证游标有效性
                if (start_idx < 0 || start_idx >= total_items) {
                    throw std::runtime_error("Invalid cursor");
                }
            } catch (const std::exception& e) {
                HKU_WARN("Invalid cursor '{}': {}", cursor, e.what());
                // 对于无效游标，返回错误（MCP 规范要求）
                throw std::runtime_error(fmt::format("Invalid cursor: {}", cursor));
            }
        }

        // 计算当前页的数据范围
        int end_idx = std::min(start_idx + page_size, total_items);
        int actual_count = end_idx - start_idx;

        // 生成当前页的数据项
        nlohmann::json items = nlohmann::json::array();
        for (int i = start_idx; i < end_idx; ++i) {
            nlohmann::json item;
            item["id"] = i + 1;  // ID 从 1 开始
            item["name"] = fmt::format("Item_{}", i + 1);
            item["value"] = fmt::format("Value for item {}", i + 1);
            item["index"] = i;
            item["created_at"] = std::chrono::duration_cast<std::chrono::seconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count() -
                                 (total_items - i) * 60;  // 模拟不同的创建时间

            items.push_back(item);
        }

        // 计算下一页的游标
        std::string next_cursor;
        if (end_idx < total_items) {
            // 还有更多数据，生成下一个游标
            int next_start = end_idx;
            next_cursor = base64_encode(std::to_string(next_start));
        }
        // 如果 end_idx >= total_items，next_cursor 保持为空，表示没有更多数据

        // 构建响应
        nlohmann::json response;
        response["content"] =
          nlohmann::json::array({{{"type", "text"},
                                  {"text", fmt::format("Retrieved {} items (total: {}, page "
                                                       "size: {})",
                                                       actual_count, total_items, page_size)}}});

        // 添加分页元数据（符合 MCP 规范）
        response["items"] = items;
        response["pagination"] = {{"total_items", total_items},
                                  {"current_page_start", start_idx},
                                  {"current_page_end", end_idx - 1},
                                  {"returned_count", actual_count},
                                  {"has_more", !next_cursor.empty()}};

        // 如果有更多数据，添加 nextCursor 字段
        if (!next_cursor.empty()) {
            response["nextCursor"] = next_cursor;
        }

        // 记录到会话历史
        recordToolUsage(session_id, "query_paginated_data",
                        {{"page_size", page_size},
                         {"cursor", cursor.empty() ? "first_page" : cursor},
                         {"returned_count", actual_count},
                         {"has_more", !next_cursor.empty()}});

        return response;
    }

    /**
     * 处理 resources/list 方法
     */
    nlohmann::json handleResourcesList(const nlohmann::json& params,
                                       const std::string& session_id) {
        HKU_INFO("MCP resources/list request (session: {})", session_id);

        nlohmann::json resources = nlohmann::json::array();

        // 示例资源 1: 文档
        resources.push_back({{"uri", "doc://getting-started"},
                             {"name", "Getting Started Guide"},
                             {"description", "Introduction to using this MCP server"},
                             {"mimeType", "text/markdown"}});

        // 示例资源 2: API 文档
        resources.push_back({{"uri", "doc://api-reference"},
                             {"name", "API Reference"},
                             {"description", "Complete API documentation"},
                             {"mimeType", "text/markdown"}});

        return {{"resources", resources}};
    }

    /**
     * 处理 resources/read 方法
     */
    net::awaitable<nlohmann::json> handleResourcesRead(const nlohmann::json& params,
                                                       const std::string& session_id) {
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
        response["contents"] = nlohmann::json::array(
          {{{"uri", "doc://getting-started"}, {"mimeType", "text/markdown"}, {"text", content}}});

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
        response["contents"] = nlohmann::json::array(
          {{{"uri", "doc://api-reference"}, {"mimeType", "text/markdown"}, {"text", content}}});

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
        prompts.push_back(
          {{"name", "code_review"},
           {"description", "Review code for best practices and potential issues"},
           {"arguments",
            nlohmann::json::array(
              {{{"name", "language"}, {"description", "Programming language"}, {"required", true}},
               {{"name", "code"}, {"description", "Code to review"}, {"required", true}}})}});

        // 示例提示词 2: 文档生成
        prompts.push_back(
          {{"name", "generate_docs"},
           {"description", "Generate documentation for code"},
           {"arguments",
            nlohmann::json::array(
              {{{"name", "code"}, {"description", "Code to document"}, {"required", true}},
               {{"name", "style"},
                {"description", "Documentation style (e.g., doxygen, javadoc)"},
                {"required", false}}})}});

        return {{"prompts", prompts}};
    }

    /**
     * 处理 prompts/get 方法
     */
    net::awaitable<nlohmann::json> handlePromptsGet(const nlohmann::json& params,
                                                    const std::string& session_id) {
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
    nlohmann::json getCodeReviewPrompt(const nlohmann::json& arguments,
                                       const std::string& session_id) {
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
          language, language, code);

        nlohmann::json response;
        response["description"] = "Code review prompt";
        response["messages"] = nlohmann::json::array(
          {{{"role", "user"}, {"content", {{"type", "text"}, {"text", prompt}}}}});

        return response;
    }

    /**
     * 获取文档生成提示词
     */
    nlohmann::json getGenerateDocsPrompt(const nlohmann::json& arguments,
                                         const std::string& session_id) {
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
          style, code);

        nlohmann::json response;
        response["description"] = "Documentation generation prompt";
        response["messages"] = nlohmann::json::array(
          {{{"role", "user"}, {"content", {{"type", "text"}, {"text", prompt}}}}});

        return response;
    }

    /**
     * 处理 ping 方法（MCP 协议健康检查）
     */
    nlohmann::json handlePing(const nlohmann::json& params, const std::string& session_id) {
        HKU_DEBUG("MCP ping request (session: {})", session_id);

        // 更新会话活动时间
        getSessionManager().touchSession(session_id);

        // 返回空结果（MCP 规范要求）
        return nlohmann::json::object();
    }

    /**
     * 处理 session/info 方法
     */
    nlohmann::json handleSessionInfo(const std::string& session_id) {
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

        bool success = getSessionManager().setSessionMetadata(session_id, key, value);
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

        bool success = getSessionManager().unregisterSession(session_id);
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
    void recordToolUsage(const std::string& session_id, const std::string& tool_name,
                         const nlohmann::json& details) {
        auto now = std::chrono::system_clock::now();
        auto timestamp =
          std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

        nlohmann::json record;
        record["timestamp"] = timestamp;
        record["tool"] = tool_name;
        record["details"] = details;

        // 获取现有历史
        auto history_json = getSessionManager().getSessionMetadata(session_id, "history");
        nlohmann::json history = history_json.is_array() ? history_json : nlohmann::json::array();

        // 添加新记录
        history.push_back(record);

        // 限制历史记录数量（最多 100 条）
        if (history.size() > 100) {
            history.erase(history.begin());
        }

        // 保存回 Session
        getSessionManager().setSessionMetadata(session_id, "history", history);
    }

    /**
     * 发送 JSON-RPC 成功响应（支持 Streamable HTTP）
     */
    net::awaitable<void> sendMcpSuccessResponse(const nlohmann::json& result,
                                                const nlohmann::json& id, bool use_sse = false) {
        nlohmann::json response;
        response["jsonrpc"] = "2.0";
        response["result"] = result;
        response["id"] = id;

        if (use_sse) {
            // SSE 格式：event: message\ndata: {...}\n\n
            std::string sse_msg = "event: message\ndata: " + response.dump() + "\n\n";

            if (m_beast_context) {
                // 注意：enableChunkedTransfer 已在 handleRequest 中调用，这里不需要再次调用
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
    net::awaitable<void> sendMcpErrorResponse(int32_t code, const nlohmann::json& id,
                                              bool use_sse = false) {
        nlohmann::json response;
        response["jsonrpc"] = "2.0";

        nlohmann::json error;
        error["code"] = code;
        error["message"] = biz_err_msg(code);
        response["error"] = error;
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

}  // namespace hku
