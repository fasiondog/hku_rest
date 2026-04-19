/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-23
 *      Author: fasiondog
 */

#pragma once

#include "hikyuu/httpd/RestHandle.h"
#include <chrono>
#include <random>

namespace hku {

/**
 * SSE (Server-Sent Events) 推送处理器
 * 
 * 演示如何使用 HTTP Server 实现 SSE 实时数据推送
 * SSE 是一种基于 HTTP 的服务器推送技术，适用于：
 * - 实时行情推送
 * - 日志流式输出
 * - 进度通知
 * - 事件订阅系统
 */
class SseHandle : public RestHandle {
    REST_HANDLE_IMP(SseHandle)

public:
    virtual net::awaitable<VoidBizResult> run() override {
        // 1. 设置 SSE 必需的响应头
        setResHeader("Content-Type", "text/event-stream");
        setResHeader("Cache-Control", "no-cache");
        setResHeader("Connection", "keep-alive");
        setResHeader("Access-Control-Allow-Origin", "*");
        
        // 2. 启用分块传输编码（SSE 的基础）
        enableChunkedTransfer();
        
        // 3. 发送初始连接消息
        std::string welcome_msg = formatSseMessage("connected", "SSE connection established", "");
        if (!co_await writeChunk(welcome_msg)) {
            HKU_WARN("Client disconnected during initial message");
            co_return BIZ_OK;
        }
        
        // 4. 持续推送数据（模拟实时数据流）
        int message_count = 0;
        const int max_messages = 50;  // 最多推送 50 条消息
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> price_dist(100, 200);
        std::uniform_real_distribution<> change_dist(-5.0, 5.0);
        
        bool client_disconnected = false;
        
        while (message_count < max_messages) {
            try {
                // 生成模拟数据
                double price = price_dist(gen) + change_dist(gen);
                double change = change_dist(gen);
                auto now = std::chrono::system_clock::now();
                auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                    now.time_since_epoch()).count();
                
                // 构造 SSE 消息
                std::string event_name = "quote";
                std::string data = fmt::format(
                    "{{\"timestamp\": {}, \"price\": {:.2f}, \"change\": {:.2f}, \"symbol\": \"AAPL\"}}",
                    timestamp, price, change
                );
                
                std::string sse_msg = formatSseMessage(event_name, data, std::to_string(message_count));
                
                // 推送消息
                bool success = co_await writeChunk(sse_msg);
                if (!success) {
                    HKU_INFO("Client disconnected after {} messages (normal behavior)", message_count);
                    client_disconnected = true;
                    break;
                }
                
                message_count++;
                
                // 模拟推送间隔（100ms - 500ms 随机延迟）
                std::uniform_int_distribution<> delay_dist(100, 500);
                int delay_ms = delay_dist(gen);
                co_await sleep_for(std::chrono::milliseconds(delay_ms));
                
            } catch (const std::exception& e) {
                // Broken pipe 等网络错误是正常现象（客户端提前断开）
                if (std::string(e.what()).find("Broken pipe") != std::string::npos ||
                    std::string(e.what()).find("Connection reset") != std::string::npos) {
                    HKU_INFO("Client disconnected after {} messages (connection closed by client)", message_count);
                } else {
                    HKU_ERROR("SSE push error: {}", e.what());
                }
                client_disconnected = true;
                break;
            }
        }
        
        // 5. 仅在客户端仍然连接时发送结束消息和完成分块传输
        if (!client_disconnected) {
            // 发送结束消息
            std::string end_msg = formatSseMessage("completed", 
                fmt::format("Push completed, total messages: {}", message_count), "");
            
            if (co_await writeChunk(end_msg)) {
                // 6. 完成分块传输
                co_await finishChunkedTransfer();
            } else {
                HKU_INFO("Client disconnected before completion message");
            }
        } else {
            // 客户端已断开，尝试完成分块传输（可能会失败，但这是正常的）
            try {
                co_await finishChunkedTransfer();
            } catch (...) {
                // 忽略完成时的错误（客户端已断开）
            }
        }
        
        HKU_INFO("SSE connection closed, sent {} messages", message_count);
        co_return BIZ_OK;
    }

private:
    /**
     * 格式化 SSE 消息
     * 
     * SSE 协议格式：
     * event: <event_name>\n
     * id: <message_id>\n
     * data: <data>\n
     * \n
     * 
     * @param event 事件名称（可选）
     * @param data 数据内容（必需）
     * @param id 消息 ID（可选，用于断线重连）
     * @return 格式化后的 SSE 消息字符串
     */
    std::string formatSseMessage(const std::string& event, 
                                  const std::string& data,
                                  const std::string& id = "") {
        std::string msg;
        
        // 事件名称（可选）
        if (!event.empty()) {
            msg += "event: " + event + "\n";
        }
        
        // 消息 ID（可选）
        if (!id.empty()) {
            msg += "id: " + id + "\n";
        }
        
        // 数据（必需）
        msg += "data: " + data + "\n";
        
        // 空行表示消息结束
        msg += "\n";
        
        return msg;
    }
    
    /**
     * 协程睡眠辅助函数
     */
    net::awaitable<void> sleep_for(std::chrono::milliseconds duration) {
        auto* ctx = static_cast<BeastContext*>(m_beast_context);
        ctx->timer.expires_after(duration);
        co_await ctx->timer.async_wait(net::use_awaitable);
    }
};

/**
 * 简单 SSE 推送示例（不带事件类型和 ID）
 */
class SimpleSseHandle : public RestHandle {
    REST_HANDLE_IMP(SimpleSseHandle)

public:
    virtual net::awaitable<VoidBizResult> run() override {
        // 设置 SSE 响应头
        setResHeader("Content-Type", "text/event-stream");
        setResHeader("Cache-Control", "no-cache");
        setResHeader("Connection", "keep-alive");
        
        // 启用分块传输
        enableChunkedTransfer();
        
        // 推送简单消息
        int message_count = 0;
        bool client_disconnected = false;
        
        for (int i = 1; i <= 10; i++) {
            std::string msg = "data: Message " + std::to_string(i) + "\n\n";
            
            if (!co_await writeChunk(msg)) {
                HKU_INFO("Client disconnected after {} messages", message_count);
                client_disconnected = true;
                break;
            }
            
            message_count++;
            
            // 每秒推送一条
            auto* ctx = static_cast<BeastContext*>(m_beast_context);
            ctx->timer.expires_after(std::chrono::seconds(1));
            co_await ctx->timer.async_wait(net::use_awaitable);
        }
        
        // 仅在客户端仍连接时完成传输
        if (!client_disconnected) {
            co_await finishChunkedTransfer();
        } else {
            try {
                co_await finishChunkedTransfer();
            } catch (...) {
                // 忽略错误
            }
        }
        
        HKU_INFO("Simple SSE connection closed, sent {} messages", message_count);
        co_return BIZ_OK;
    }
};

}  // namespace hku
