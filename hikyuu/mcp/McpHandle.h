/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-23
 *      Author: fasiondog
 */

#pragma once

#include <random>
#include <nlohmann/json.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include "hikyuu/utilities/base64.h"
#include "hikyuu/httpd/HttpHandle.h"
#include "hikyuu/utilities/datetime/Datetime.h"
#include "McpError.h"
#include "SessionManager.h"

namespace hku {

class McpService;

using JsonResult = BizResult<nlohmann::json>;

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
    friend class McpService;

public:
    McpHandle(void* beast_context, McpService* service)
    : HttpHandle(beast_context), m_service(service) {}

    virtual net::awaitable<VoidBizResult> run() override;

    /**
     * 获取 Session 管理器引用（用于后台清理线程）
     */
    SessionManager& getSessionManager();

    /**
     * 记录工具使用到会话历史
     */
    void recordToolUsage(const std::string& session_id, const std::string& tool_name,
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

private: /**
          * 发送 JSON-RPC 成功响应（支持 Streamable HTTP）
          */
    net::awaitable<void> sendMcpSuccessResponse(const nlohmann::json& result,
                                                const nlohmann::json& id, bool use_sse = false) {
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

    /**
     * 生成符合 MCP 协议规范的 Session ID
     * 使用 UUID v4 格式，仅包含可见 ASCII 字符 (0x21-0x7E)
     */
    static std::string generateSessionId() {
        // 使用 Boost.UUID 生成 UUID v4
        static thread_local boost::uuids::random_generator generator;
        boost::uuids::uuid uuid = generator();
        return boost::uuids::to_string(uuid);
    }

    /**
     * 从请求中提取 Session ID
     * 使用标准的 Mcp-Session-Id 头字段
     */
    std::string extractSessionId() {
        return getReqHeader("Mcp-Session-Id");
    }

private:
    McpService* m_service{nullptr};
};

}  // namespace hku
