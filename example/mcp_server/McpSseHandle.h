#pragma once

#include "hikyuu/httpd/HttpHandle.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include "SessionManager.h"

namespace hku {

/**
 * MCP SSE (Server-Sent Events) 处理器
 * 
 * 用于实时推送任务进度、状态更新等
 * 客户端通过此端点订阅特定任务的进度更新
 */
class McpSseHandle : public HttpHandle {
    HTTP_HANDLE_IMP(McpSseHandle)

public:
    virtual net::awaitable<VoidBizResult> run() override {
        // 1. 获取 Session ID（必需）
        std::string session_id = getReqHeader("X-Session-ID");
        if (session_id.empty()) {
            // 尝试从 URL 参数获取
            if (haveQueryParams()) {
                auto params_result = getQueryParams();
                if (params_result.has_value()) {
                    auto& params = params_result.value();
                    auto it = params.find("sessionId");
                    if (it != params.end()) {
                        session_id = it->second;
                    }
                }
            }
        }
        
        if (session_id.empty()) {
            HKU_WARN("SSE connection rejected: missing session ID");
            co_return BIZ_OK;
        }
        
        // 2. 验证会话
        auto session = McpHandle::getSessionManager().getSession(session_id);
        if (!session) {
            HKU_WARN("SSE connection rejected: invalid session {}", session_id);
            co_return BIZ_OK;
        }
        
        HKU_INFO("SSE connection established for session: {}", session_id);
        
        // 3. 设置 SSE 响应头
        setResHeader("Content-Type", "text/event-stream");
        setResHeader("Cache-Control", "no-cache");
        setResHeader("Connection", "keep-alive");
        setResHeader("Access-Control-Allow-Origin", "*");
        setResHeader("X-Session-ID", session_id.c_str());
        
        // 4. 启用分块传输
        enableChunkedTransfer();
        
        // 5. 发送连接确认消息
        nlohmann::json connect_data;
        connect_data["type"] = "connection_established";
        connect_data["session_id"] = session_id;
        connect_data["timestamp"] = getCurrentTimestamp();
        
        std::string welcome_msg = formatSseMessage("connected", connect_data.dump(), "0");
        if (!co_await writeChunk(welcome_msg)) {
            HKU_WARN("Client disconnected during SSE handshake");
            co_return BIZ_OK;
        }
        
        // 6. 持续监听并推送该会话的进度更新
        bool client_disconnected = false;
        int message_count = 0;
        size_t last_progress_count = 0;
        auto last_heartbeat_time = std::chrono::steady_clock::now();
        
        while (!client_disconnected) {
            try {
                // 检查是否需要发送心跳（符合 MCP 协议规范）
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    now - last_heartbeat_time).count();
                
                if (elapsed >= 15) {
                    // 发送 SSE 注释作为心跳（MCP 协议规范：: ping - timestamp）
                    auto timestamp = std::chrono::system_clock::now();
                    std::time_t time_t_now = std::chrono::system_clock::to_time_t(timestamp);
                    std::tm tm_now;
                    localtime_r(&time_t_now, &tm_now);
                    
                    char time_buffer[100];
                    std::strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &tm_now);
                    
                    // SSE 注释格式：: comment\n\n （不会被解析为事件）
                    std::string heartbeat = fmt::format(": ping - {}\n\n", time_buffer);
                    
                    bool success = co_await writeChunk(heartbeat);
                    if (!success) {
                        HKU_INFO("Client disconnected during heartbeat (session: {})", session_id);
                        client_disconnected = true;
                        break;
                    }
                    
                    last_heartbeat_time = now;
                    HKU_DEBUG("Sent heartbeat to session: {}", session_id);
                }
                
                // 检查是否有新的进度更新
                auto history_json = McpHandle::getSessionManager().getSessionMetadata(
                    session_id, "progress_history");
                
                if (history_json.is_array() && history_json.size() > last_progress_count) {
                    // 推送所有新的进度更新
                    for (size_t i = last_progress_count; i < history_json.size(); i++) {
                        auto& progress_data = history_json[i];
                        
                        message_count++;
                        std::string msg_id = std::to_string(message_count);
                        std::string sse_msg = formatSseMessage("progress", progress_data.dump(), msg_id);
                        
                        bool success = co_await writeChunk(sse_msg);
                        if (!success) {
                            HKU_INFO("Client disconnected after {} messages", message_count);
                            client_disconnected = true;
                            break;
                        }
                        
                        HKU_DEBUG("Pushed progress update {}/{}: {}%", 
                                 i + 1, history_json.size(), 
                                 progress_data.value("progress", 0));
                    }
                    
                    last_progress_count = history_json.size();
                }
                
                // 短暂休眠，避免 busy-wait
                co_await sleep_for(std::chrono::milliseconds(100));
                
            } catch (const std::exception& e) {
                if (std::string(e.what()).find("Broken pipe") != std::string::npos ||
                    std::string(e.what()).find("Connection reset") != std::string::npos) {
                    HKU_INFO("Client disconnected after {} messages", message_count);
                } else {
                    HKU_ERROR("SSE push error: {}", e.what());
                }
                client_disconnected = true;
                break;
            }
        }
        
        // 7. 优雅关闭
        if (!client_disconnected) {
            nlohmann::json close_data;
            close_data["type"] = "connection_closed";
            close_data["reason"] = "server_shutdown";
            close_data["total_messages"] = message_count;
            
            std::string close_msg = formatSseMessage("disconnected", close_data.dump(), 
                                                     std::to_string(message_count + 1));
            
            if (co_await writeChunk(close_msg)) {
                co_await finishChunkedTransfer();
            }
        } else {
            try {
                co_await finishChunkedTransfer();
            } catch (...) {
                // 忽略断开连接时的错误
            }
        }
        
        HKU_INFO("SSE connection closed for session {}, sent {} messages", session_id, message_count);
        co_return BIZ_OK;
    }

private:
    /**
     * 格式化 SSE 消息
     */
    std::string formatSseMessage(const std::string& event, 
                                  const std::string& data,
                                  const std::string& id = "") {
        std::string msg;
        
        if (!event.empty()) {
            msg += "event: " + event + "\n";
        }
        
        if (!id.empty()) {
            msg += "id: " + id + "\n";
        }
        
        msg += "data: " + data + "\n\n";
        
        return msg;
    }
    
    /**
     * 获取当前时间戳（秒）
     */
    long long getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
    }
    
    /**
     * 异步 sleep 辅助方法
     */
    net::awaitable<void> sleep_for(std::chrono::milliseconds duration) {
        auto* ctx = static_cast<BeastContext*>(m_beast_context);
        ctx->timer.expires_after(duration);
        co_await ctx->timer.async_wait(net::use_awaitable);
    }
};

} // namespace hku
