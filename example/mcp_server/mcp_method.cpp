/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-04-25
 *      Author: fasiondog
 */

#include "hikyuu/utilities/base64.h"
#include "hikyuu/utilities/datetime/Datetime.h"
#include "mcp_method.h"

namespace hku {

std::vector<json> prompts_list = {
  // 示例提示词 1: 代码审查
  {{"name", "code_review"},
   {"description", "Review code for best practices and potential issues"},
   {"arguments",
    nlohmann::json::array(
      {{{"name", "language"}, {"description", "Programming language"}, {"required", true}},
       {{"name", "code"}, {"description", "Code to review"}, {"required", true}}})}},

  // 示例提示词 2: 文档生成
  {{"name", "generate_docs"},
   {"description", "Generate documentation for code"},
   {"arguments", nlohmann::json::array(
                   {{{"name", "code"}, {"description", "Code to document"}, {"required", true}},
                    {{"name", "style"},
                     {"description", "Documentation style (e.g., doxygen, javadoc)"},
                     {"required", false}}})}},
};

std::vector<json> resources_list = {
  // 示例资源 1: 文档
  {{"uri", "doc://getting-started"},
   {"name", "Getting Started Guide"},
   {"description", "Introduction to using this MCP server"},
   {"mimeType", "text/markdown"}},

  // 示例资源 2: API 文档
  {{"uri", "doc://api-reference"},
   {"name", "API Reference"},
   {"description", "Complete API documentation"},
   {"mimeType", "text/markdown"}},
};

/**
 * 获取代码审查提示词
 */
static nlohmann::json getCodeReviewPrompt(const nlohmann::json& arguments,
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
static nlohmann::json getGenerateDocsPrompt(const nlohmann::json& arguments,
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

net::awaitable<JsonResult> promptGetMethod(McpHandle* handle, const nlohmann::json& params,
                                           const std::string& session_id) {
    std::string prompt_name = params.value("name", "");
    nlohmann::json arguments = params.value("arguments", nlohmann::json::object());

    HKU_INFO("MCP prompts/get request: prompt={} (session: {})", prompt_name, session_id);

    if (prompt_name == "code_review") {
        co_return getCodeReviewPrompt(arguments, session_id);
    } else if (prompt_name == "generate_docs") {
        co_return getGenerateDocsPrompt(arguments, session_id);
    }

    co_return BIZ_MCP_PROMPT_NOT_FOUND;
}

/**
 * 读取入门指南
 */
static nlohmann::json readGettingStartedGuide() {
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
static nlohmann::json readApiReference() {
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

net::awaitable<JsonResult> resourceReadMethod(McpHandle* handle, const nlohmann::json& params,
                                              const std::string& session_id) {
    std::string uri = params.value("uri", "");

    HKU_INFO("MCP resources/read request: uri={} (session: {})", uri, session_id);

    if (uri == "doc://getting-started") {
        co_return readGettingStartedGuide();
    } else if (uri == "doc://api-reference") {
        co_return readApiReference();
    }
    co_return BIZ_MCP_RESOURCE_NOT_FOUND;
}

std::vector<json> tools_list = {
  // 示例工具 1: 计算器
  {{"name", "calculator"},
   {"description", "Perform basic arithmetic calculations"},
   {"inputSchema",
    {{"type", "object"},
     {"properties",
      {{"expression",
        {{"type", "string"},
         {"description", "Mathematical expression to evaluate (e.g., '2 + 2')"}}}}},
     {"required", {"expression"}}}}},

  // 示例工具 2: 获取当前时间
  {{"name", "get_current_time"},
   {"description", "Get the current date and time"},
   {"inputSchema",
    {{"type", "object"},
     {"properties",
      {{"format",
        {{"type", "string"},
         {"description", "Optional datetime format string"},
         {"default", "%Y-%m-%d %H:%M:%S"}}}}}}}},

  // 示例工具 3: 获取天气信息
  {{"name", "get_weather"},
   {"description", "Get weather information for a location"},
   {"inputSchema",
    {{"type", "object"},
     {"properties",
      {{"location", {{"type", "string"}, {"description", "City name or coordinates"}}}}},
     {"required", {"location"}}}}},

  // 示例工具 4: 会话历史（需要 Session）
  {{"name", "get_session_history"},
   {"description", "Get the interaction history for current session"},
   {"inputSchema",
    {{"type", "object"},
     {"properties",
      {{"limit",
        {{"type", "integer"},
         {"description", "Maximum number of history items to return"},
         {"default", 10}}}}}}}},

  // 示例工具 5: 长时间运行任务（演示进度推送）
  {{"name", "long_running_task"},
   {"description", "Simulate a long-running task with progress updates via SSE"},
   {"inputSchema",
    {{"type", "object"},
     {"properties",
      {{"duration_seconds",
        {{"type", "integer"}, {"description", "Task duration in seconds"}, {"default", 10}}},
       {"task_name",
        {{"type", "string"},
         {"description", "Name of the task"},
         {"default", "example_task"}}}}}}}},

  // 示例工具 6: 分页数据查询（演示 MCP 分页机制）
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
         {"description", "Opaque cursor string for pagination (omit for first page)"}}}}}}}},
};

/**
 * 执行计算器工具
 */
net::awaitable<JsonResult> executeCalculator(McpHandle* handle, const nlohmann::json& arguments,
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
        handle->recordToolUsage(session_id, "calculator",
                                {{"expression", expression}, {"result", result}});

        nlohmann::json response;
        response["content"] =
          nlohmann::json::array({{{"type", "text"}, {"text", fmt::format("Result: {}", result)}}});

        co_return response;
    } catch (const std::exception& e) {
        co_return BIZ_MCP_TOOL_EXECUTION_ERROR;
    }
    co_return BIZ_MCP_TOOL_EXECUTION_ERROR;
}

/**
 * 执行获取当前时间工具
 */
net::awaitable<JsonResult> executeGetCurrentTime(McpHandle* handle, const nlohmann::json& arguments,
                                                 const std::string& session_id) {
    std::string format = arguments.value("format", "%Y-%m-%d %H:%M:%S");

    auto now = Datetime::now();
    // 记录到会话历史
    handle->recordToolUsage(session_id, "get_current_time",
                            {{"format", format}, {"time", now.str()}});

    nlohmann::json response;
    response["content"] = nlohmann::json::array({{{"type", "text"}, {"text", now.str()}}});

    co_return response;
}

/**
 * 执行天气查询工具（模拟）
 */
net::awaitable<JsonResult> executeGetWeather(McpHandle* handle, const nlohmann::json& arguments,
                                             const std::string& session_id) {
    std::string location = arguments.value("location", "Unknown");

    // 模拟天气数据
    nlohmann::json response;
    response["content"] = nlohmann::json::array(
      {{{"type", "text"},
        {"text", fmt::format("Weather in {}: Temperature 22°C, Condition: Sunny, Humidity: 65%",
                             location)}}});

    // 记录到会话历史
    handle->recordToolUsage(session_id, "get_weather", {{"location", location}});

    co_return response;
}

/**
 * 执行获取会话历史工具
 */
net::awaitable<JsonResult> executeGetSessionHistory(McpHandle* handle,
                                                    const nlohmann::json& arguments,
                                                    const std::string& session_id) {
    int limit = arguments.value("limit", 10);

    // 从 Session 元数据中获取历史记录
    auto history_json = handle->getSessionManager().getSessionMetadata(session_id, "history");
    if (history_json.is_null()) {
        co_return nlohmann::json::array();
    }

    // 限制返回的历史记录数量
    nlohmann::json history = history_json;
    if (history.size() > limit) {
        history.erase(history.begin(), history.begin() + (history.size() - limit));
    }

    co_return history;
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
net::awaitable<JsonResult> executeQueryPaginatedData(McpHandle* handle,
                                                     const nlohmann::json& arguments,
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
    handle->recordToolUsage(session_id, "query_paginated_data",
                            {{"page_size", page_size},
                             {"cursor", cursor.empty() ? "first_page" : cursor},
                             {"returned_count", actual_count},
                             {"has_more", !next_cursor.empty()}});

    co_return response;
}

/**
 * 执行长时间运行任务（演示进度推送）
 */
net::awaitable<JsonResult> executeLongRunningTask(McpHandle* handle,
                                                  const nlohmann::json& arguments,
                                                  const std::string& session_id) {
    int duration = arguments.value("duration_seconds", 10);
    std::string task_name = arguments.value("task_name", "example_task");

    // 生成任务 ID
    auto task_id = fmt::format("{}_{}", task_name, Datetime::now().timestamp());

    HKU_INFO("Starting long running task: {} (session: {}, duration: {}s)", task_id, session_id,
             duration);

    // 推送开始消息
    co_await handle->pushProgress(session_id, task_id, 0, "Task started",
                                  {{"task_name", task_name}, {"estimated_duration", duration}});

    // 模拟长时间运行的任务，定期推送进度
    int steps = 10;
    int step_duration = duration / steps;

    for (int i = 1; i <= steps; i++) {
        // 模拟工作 - 使用异步定时器
        co_await handle->sleep_for(std::chrono::seconds(step_duration));

        int progress = (i * 100) / steps;
        std::string message = fmt::format("Processing... {}% complete", progress);

        // 推送进度更新
        co_await handle->pushProgress(session_id, task_id, progress, message,
                                      {{"current_step", i}, {"total_steps", steps}});

        HKU_DEBUG("Task {} progress: {}%", task_id, progress);
    }

    // 推送完成消息
    co_await handle->pushProgress(session_id, task_id, 100, "Task completed successfully",
                                  {{"result", "success"}, {"output", "Task finished"}});

    // 记录到会话历史
    handle->recordToolUsage(session_id, "long_running_task",
                            {{"task_id", task_id},
                             {"task_name", task_name},
                             {"duration", duration},
                             {"status", "completed"}});

    // 返回任务 ID，客户端可通过 SSE 监听进度
    nlohmann::json response;
    response["content"] =
      nlohmann::json::array({{{"type", "text"},
                              {"text", fmt::format("Task '{}' completed with ID: {}\n"
                                                   "Total duration: {} seconds\n"
                                                   "Received {} progress updates via SSE stream",
                                                   task_name, task_id, duration, steps)}}});
    response["task_id"] = task_id;

    co_return response;
}

}  // namespace hku