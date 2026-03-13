/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-03-13
 *      Author: fasiondog
 */

#pragma once

#include <hikyuu/httpd/WebSocketHandle.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace hku {

/**
 * Echo Handle - 简单的回显 WebSocket 处理器
 */
class EchoWsHandle : public WebSocketHandle {
public:
    explicit EchoWsHandle(void* ctx) : WebSocketHandle(ctx) {}

    net::awaitable<void> onOpen() override {
        HKU_INFO("EchoWsHandle: Client connected from {}", getClientIp());

        // 发送欢迎消息
        json welcome;
        welcome["type"] = "welcome";
        welcome["message"] = "Welcome to WebSocket Echo Server!";
        welcome["time"] = std::time(nullptr);

        co_await send(welcome.dump());
    }

    net::awaitable<void> onMessage(std::string_view message, bool is_text) override {
        HKU_DEBUG("EchoWsHandle: Received message (is_text={}): {}", is_text, message);

        // 回显消息
        json response;
        response["type"] = "echo";
        response["message"] = std::string(message);
        response["time"] = std::time(nullptr);

        co_await send(response.dump(), is_text);
    }

    net::awaitable<void> onClose(ws::close_code const& code, std::string_view reason) override {
        HKU_INFO("EchoWsHandle: Client disconnected, code={}, reason={}", static_cast<int>(code),
                 reason);
        co_return;
    }

    net::awaitable<void> onError(beast::error_code const& ec, std::string_view message) override {
        HKU_ERROR("EchoWsHandle: Error occurred - {}: {}", message, ec.message());
        co_return;
    }

    net::awaitable<void> onPing() override {
        HKU_DEBUG("EchoWsHandle: Ping sent to {}", getClientIp());
        // 可以在这里添加心跳响应逻辑
        co_return;
    }
};

}  // namespace hku
