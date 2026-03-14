/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-03-15
 *      Author: fasiondog
 */

#pragma once

#include <hikyuu/httpd/WebSocketHandle.h>
#include <nlohmann/json.hpp>
#include <vector>
#include <chrono>
#include <ctime>

using namespace hku;
using json = nlohmann::json;

/**
 * WebSocket 行情推送处理器 - 演示流式分批推送功能
 * 
 * 功能演示：
 * 1. 推送 10000 只股票的行情数据
 * 2. 自动分批次发送（500 只/批）
 * 3. 批次间隔 50ms，总耗时约 1 秒
 * 4. 支持两种推送模式：预生成列表 & 动态生成器
 * 
 * 使用方法：
 * 1. 订阅模式（预生成列表）: {"action": "subscribe_quotes", "symbols": [...]}
 * 2. 流式模式（动态生成器）: {"action": "stream_quotes", "count": 10000}
 */
class QuotePushHandle : public WebSocketHandle {
    WS_HANDLE_IMP(QuotePushHandle)

public:
    net::awaitable<void> onMessage(std::string_view message, bool is_text) override {
        // 解析客户端请求
        auto request = json::parse(std::string(message));
        std::string action = request.value("action", "");
        
        if (action == "subscribe_quotes") {
            // 订阅行情推送（预生成列表模式）
            co_await handleSubscribeQuotes(request);
            
        } else if (action == "stream_quotes") {
            // 流式行情推送（动态生成器模式）
            co_await handleStreamQuotes(request);
            
        } else {
            // 未知命令，返回错误
            json error_resp;
            error_resp["type"] = "error";
            error_resp["message"] = "Unknown action: " + action;
            co_await send(error_resp.dump(), true);
        }
    }

private:
    /**
     * 处理行情订阅请求 - 使用预生成列表方式
     */
    net::awaitable<void> handleSubscribeQuotes(const json& request) {
        // 获取订阅的股票代码列表（可选）
        std::vector<std::string> symbols;
        if (request.contains("symbols")) {
            symbols = request["symbols"].get<std::vector<std::string>>();
        }
        
        // 如果没有指定股票，默认推送 10000 只
        if (symbols.empty()) {
            symbols.reserve(10000);
            for (int i = 0; i < 10000; ++i) {
                symbols.push_back("SH60000" + std::to_string(i));
            }
        }
        
        // 构建响应消息
        json resp;
        resp["type"] = "quote_start";
        resp["total"] = symbols.size();
        resp["batch_size"] = 500;
        co_await send(resp.dump(), true);
        
        // 生成所有行情数据
        std::vector<std::string> messages;
        messages.reserve(symbols.size());
        
        for (std::size_t i = 0; i < symbols.size(); ++i) {
            json quote;
            quote["symbol"] = symbols[i];
            quote["price"] = 10.0 + (i % 100) * 0.1;
            quote["change"] = (i % 10) * 0.01;
            quote["volume"] = 1000000 + i * 1000;
            quote["timestamp"] = std::time(nullptr);
            messages.push_back(quote.dump());
        }
        
        // 使用流式分批推送
        bool success = co_await sendBatch(
            messages,           // 消息列表
            true,               // 文本消息
            500,                // 每批 500 条
            std::chrono::milliseconds(50)  // 批次间隔 50ms
        );
        
        // 推送完成通知
        json finish_resp;
        finish_resp["type"] = "quote_finish";
        finish_resp["success"] = success;
        finish_resp["total_sent"] = messages.size();
        finish_resp["total_batches"] = (messages.size() + 499) / 500;  // 向上取整
        co_await send(finish_resp.dump(), true);
    }
    
    /**
     * 处理流式行情推送请求 - 使用生成器方式
     * 适合内存敏感场景，边生成边发送
     */
    net::awaitable<void> handleStreamQuotes(const json& request) {
        std::size_t total_count = request.value("count", 10000);
        
        // 构建响应消息
        json resp;
        resp["type"] = "stream_start";
        resp["target_count"] = total_count;
        co_await send(resp.dump(), true);
        
        // 创建生成器函数
        std::size_t current_index = 0;
        auto generator = [&current_index, total_count]() mutable 
            -> std::optional<std::string> {
            
            if (current_index >= total_count) {
                return std::nullopt;  // 数据结束
            }
            
            // 动态生成一条行情数据
            json quote;
            quote["symbol"] = "SH60000" + std::to_string(current_index % 10000);
            quote["price"] = 10.0 + (current_index % 100) * 0.1;
            quote["change"] = (current_index % 10) * 0.01;
            quote["volume"] = 1000000 + current_index * 1000;
            quote["timestamp"] = std::time(nullptr);
            
            ++current_index;
            return quote.dump();
        };
        
        // 使用生成器方式的流式分批推送
        bool success = co_await sendBatch(
            generator,          // 消息生成器
            true,               // 文本消息
            500,                // 每批 500 条
            std::chrono::milliseconds(50)  // 批次间隔 50ms
        );
        
        // 推送完成通知
        json finish_resp;
        finish_resp["type"] = "stream_finish";
        finish_resp["success"] = success;
        finish_resp["total_sent"] = current_index;
        finish_resp["total_batches"] = (current_index + 499) / 500;  // 向上取整
        co_await send(finish_resp.dump(), true);
    }
};
