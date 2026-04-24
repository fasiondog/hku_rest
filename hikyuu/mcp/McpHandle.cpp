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
        try {
            co_await handleMethod(method, params, session_id, id, supports_sse);
        } catch (...) {
            // 异常已在 handleMethod 内部处理并发送响应
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

}  // namespace hku
