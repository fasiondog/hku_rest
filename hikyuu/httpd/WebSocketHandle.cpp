/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-03-13
 *      Author: fasiondog
 */

#include "WebSocketHandle.h"
#include <hikyuu/utilities/Log.h>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ws = websocket;

namespace hku {

// ============================================================================
// WebSocketHandle 实现
// ============================================================================

WebSocketHandle::WebSocketHandle(void* ws_context) : m_ws_context(ws_context) {}

WebSocketHandle::~WebSocketHandle() {}

net::awaitable<void> WebSocketHandle::onOpen() {
    // 默认实现为空，派生类可重写
    co_return;
}

net::awaitable<void> WebSocketHandle::onClose(ws::close_code const& code, std::string_view reason) {
    HKU_DEBUG("WebSocket connection closed: code={}, reason={}", static_cast<int>(code), reason);
    co_return;
}

net::awaitable<void> WebSocketHandle::onError(beast::error_code const& ec,
                                              std::string_view message) {
    HKU_ERROR("WebSocket error: {} - {}", message, ec.message());
    co_return;
}

net::awaitable<void> WebSocketHandle::onPing() {
    // 默认实现为空，派生类可重写
    co_return;
}

net::awaitable<bool> WebSocketHandle::send(std::string_view message, bool is_text) {
    // 默认实现返回 false，具体实现在派生类中
    // 实际发送由 WebSocketConnection 通过回调完成
    co_return false;
}

net::awaitable<void> WebSocketHandle::broadcast(std::string_view message, bool is_text) {
    // 默认实现为空，需要服务器维护连接列表
    // 派生类可通过单例或全局管理器实现广播
    co_return;
}

std::string WebSocketHandle::getClientIp() const noexcept {
    if (!m_ws_context) {
        return "unknown";
    }
    auto* ctx = static_cast<WebSocketContext*>(m_ws_context);
    return ctx->client_ip;
}

uint16_t WebSocketHandle::getClientPort() const noexcept {
    if (!m_ws_context) {
        return 0;
    }
    auto* ctx = static_cast<WebSocketContext*>(m_ws_context);
    return ctx->client_port;
}

net::awaitable<void> WebSocketHandle::close(ws::close_code code, std::string_view reason) {
    // 默认实现为空，具体实现在派生类中
    // 实际关闭由 WebSocketConnection 通过回调完成
    co_return;
}

net::awaitable<void> WebSocketHandle::operator()() {
    if (!m_ws_context) {
        HKU_ERROR("WebSocketContext is null");
        co_return;
    }

    try {
        // 调用 onOpen 回调
        co_await onOpen();

        // 主循环由 WebSocketConnection 管理
        // Handle 只负责业务逻辑

    } catch (const beast::system_error& e) {
        // 忽略返回值以避免警告
        (void)onError(e.code(), e.what());
    } catch (const std::exception& e) {
        // 忽略返回值以避免警告
        (void)onError(beast::errc::make_error_code(beast::errc::connection_aborted), e.what());
    }

    co_return;
}

}  // namespace hku
