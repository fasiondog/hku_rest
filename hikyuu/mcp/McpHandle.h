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
#include "hikyuu/httpd/HttpHandle.h"
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
                         const nlohmann::json& details);

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
                                      const nlohmann::json& data = nullptr);

private:
    /**
     * 发送 JSON-RPC 成功响应（支持 Streamable HTTP）
     */
    net::awaitable<void> sendMcpSuccessResponse(const nlohmann::json& result,
                                                const nlohmann::json& id, bool use_sse = false);

    /**
     * 发送 JSON-RPC 错误响应（支持 Streamable HTTP）
     */
    net::awaitable<void> sendMcpErrorResponse(int32_t code, const nlohmann::json& id,
                                              bool use_sse = false);

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
