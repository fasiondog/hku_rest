/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-04-22
 *      Author: fasiondog
 */

#include "McpService.h"
#include "McpHandle.h"

namespace hku {

SessionManager& McpHandle::getSessionManager() {
    return m_service->getSessionManager();
}

net::awaitable<VoidBizResult> McpHandle::run() {
    // 1. 只接受 POST 请求
    // if (getReqMethod() != "POST") {
    //     co_await sendMcpErrorResponse(BIZ_JSONRPC_INVALID_REQUEST, nlohmann::json(), false);
    //     co_return BIZ_OK;
    // }

    // 2. 检查 Accept 头，判断客户端是否支持 SSE 流式响应
    std::string accept_header = getReqHeader("Accept");
    bool supports_sse = (accept_header.find("text/event-stream") != std::string::npos);

    // 3. 读取请求体（框架会自动处理 chunked 解码）
    std::string body = getReqData();
    if (body.empty()) {
        co_await sendMcpErrorResponse(BIZ_JSONRPC_PARSE_ERROR, nlohmann::json(), supports_sse);
        co_return BIZ_OK;
    }

    // 用于捕获块中记录错误，稍后统一发送
    bool has_error = false;
    int error_code = 0;
    nlohmann::json error_id = nullptr;

    try {
        // 4. 解析 JSON-RPC 请求
        nlohmann::json request = nlohmann::json::parse(body);

        // 4. 验证 JSON-RPC 版本
        if (!request.contains("jsonrpc") || request["jsonrpc"] != "2.0") {
            co_await sendMcpErrorResponse(BIZ_JSONRPC_INVALID_REQUEST,
                                          request.contains("id") ? request["id"] : nlohmann::json(),
                                          supports_sse);
            co_return BIZ_OK;
        }

        // 5. 获取方法名和 ID
        std::string method = request.value("method", "");
        nlohmann::json id = request.contains("id") ? request["id"] : nlohmann::json();
        nlohmann::json params = request.contains("params") ? request["params"] : nlohmann::json();

        // 6. 从请求头中获取 Session ID
        std::string session_id = extractSessionId();

        // 7. 处理 initialize 方法：服务器生成并返回 Session ID
        if (method == "initialize") {
            // 如果客户端已提供 Session ID（重连场景），验证其有效性
            if (!session_id.empty()) {
                auto session = getSessionManager().getSession(session_id);
                if (!session) {
                    // Session 已失效，要求客户端重新初始化
                    HKU_WARN("Invalid session ID in initialize request: {}", session_id);
                    co_await sendMcpErrorResponse(BIZ_JSONRPC_INVALID_PARAMS, id, supports_sse);
                    co_return BIZ_OK;
                }
                // 更新会话活动时间
                getSessionManager().touchSession(session_id);
                HKU_INFO("Session reconnected: {}", session_id);
            } else {
                // 为新连接生成 Session ID
                session_id = generateSessionId();
                std::string client_ip = getClientIp();

                if (!getSessionManager().registerSession(session_id, client_ip)) {
                    co_await sendMcpErrorResponse(BIZ_JSONRPC_INTERNAL_ERROR, id, supports_sse);
                    co_return BIZ_OK;
                }

                // 在响应头中返回 Mcp-Session-Id（MCP 协议规范）
                setResHeader("Mcp-Session-Id", session_id);
                HKU_INFO("New session registered: {} from {}", session_id, client_ip);
            }
        } else {
            // 8. 对于非 initialize 方法，必须携带有效的 Session ID
            if (session_id.empty()) {
                // 返回 HTTP 400 Bad Request（通过 JSON-RPC 错误码表示）
                co_await sendMcpErrorResponse(BIZ_MCP_VERSION_MISMATCH, id, supports_sse);
                co_return BIZ_OK;
            }

            // 验证会话有效性
            auto session = getSessionManager().getSession(session_id);
            if (!session) {
                // 返回 HTTP 404 Not Found（通过 JSON-RPC 错误码表示）
                co_await sendMcpErrorResponse(BIZ_MCP_UNAUTHORIZED, id, supports_sse);
                co_return BIZ_OK;
            }

            // 更新会话活动时间
            getSessionManager().touchSession(session_id);
        }

        // 9. 在响应中回显 Session ID（方便客户端确认，使用标准头字段）
        setResHeader("Mcp-Session-Id", session_id);

        // 10. 对于长任务，如果客户端支持 SSE，启用流式响应
        // 注意：不是所有请求都启用 SSE，只有真正需要流式推送的长任务才启用
        bool enable_streaming = false;
        if (supports_sse && method == "tools/call") {
            std::string tool_name = params.value("name", "");
            if (tool_name == "long_running_task") {
                enable_streaming = true;
                setResHeader("Content-Type", "text/event-stream");
                setResHeader("Cache-Control", "no-cache");
                setResHeader("Connection", "keep-alive");
                enableChunkedTransfer();
                HKU_INFO("SSE streaming enabled for long_running_task (session: {})", session_id);
            }
        }

        // 11. 路由到对应的处理方法（传递 session_id, id 和 supports_sse 标志）
        // initialized 是通知，不需要响应
        if (method != "initialized") {
            try {
                // co_await handleMethod(method, params, session_id, id, supports_sse);
                auto result = co_await m_service->dispatchMethod(this, method, params, session_id,
                                                                 id, supports_sse);
                if (result) {
                    co_await sendMcpSuccessResponse(result.value(), id, supports_sse);
                } else {
                    co_await sendMcpErrorResponse(result.error(), id, supports_sse);
                }
            } catch (...) {
                // 异常已在 handleMethod 内部处理并发送响应
            }
        }

        // 12. 如果使用了 SSE，完成分块传输
        if (enable_streaming) {
            try {
                co_await finishChunkedTransfer();
            } catch (...) {
                // 忽略断开连接时的错误
                HKU_DEBUG("SSE connection closed for session: {}", session_id);
            }
        }

        // 注意：Session 不与 HTTP 连接绑定
        // 客户端可以在 Session 有效期内（默认3600秒）随时重连
        // Session 由超时机制或 session/unregister 方法清理

    } catch (const nlohmann::json::parse_error& e) {
        HKU_ERROR("JSON parse error: {}", e.what());
        has_error = true;
        error_code = BIZ_JSONRPC_PARSE_ERROR;
        error_id = nullptr;
    } catch (const std::exception& e) {
        HKU_ERROR("MCP handler error: {}", e.what());
        has_error = true;
        error_code = BIZ_JSONRPC_INTERNAL_ERROR;
        error_id = nullptr;
    }

    if (has_error) {
        co_await sendMcpErrorResponse(error_code, error_id, supports_sse);
    }

    co_return BIZ_OK;
}

/*
 * 发送 JSON-RPC 成功响应（支持 Streamable HTTP）
 */
net::awaitable<void> McpHandle::sendMcpSuccessResponse(const nlohmann::json& result,
                                                       const nlohmann::json& id, bool use_sse) {
    if (result.is_null()) {
        co_return;
    }

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

/*
 * 发送 JSON-RPC 错误响应（支持 Streamable HTTP）
 */
net::awaitable<void> McpHandle::sendMcpErrorResponse(int32_t code, const nlohmann::json& id,
                                                     bool use_sse) {
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

/*
 * 记录工具使用到会话历史
 */
void McpHandle::recordToolUsage(const std::string& session_id, const std::string& tool_name,
                                const nlohmann::json& details) {
    if (session_id.empty()) {
        return;  // 无效会话 ID，无法记录
    }
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

/*
 * 推送进度更新到 SSE 端点（实时流式推送 + 存储历史）
 * @param session_id 会话 ID
 * @param task_id 任务 ID
 * @param progress 进度百分比 (0-100)
 * @param message 进度消息
 * @param data 附加数据
 */
net::awaitable<void> McpHandle::pushProgress(const std::string& session_id,
                                             const std::string& task_id, int progress,
                                             const std::string& message,
                                             const nlohmann::json& data) {
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
    progress_data["timestamp"] = Datetime::now().timestamp();

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

}  // namespace hku
