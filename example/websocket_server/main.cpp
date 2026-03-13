/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-03-13
 *      Author: fasiondog
 */

#include <iostream>
#include <csignal>
#include <hikyuu/httpd/HttpServer.h>
#include "EchoWsHandle.h"

using namespace hku;

/**
 * @brief 统一的 HTTP + WebSocket 服务器示例
 *
 * 功能:
 * 1. REST API: GET /api/hello - 返回欢迎消息
 * 2. WebSocket: ws://localhost:8765/echo - 回显服务
 * 3. 可选 SSL/TLS 支持
 */
int main(int argc, char* argv[]) {
    try {
        // 设置日志级别（可选）
        // setLogLevel(2);

        std::cout << "========================================" << std::endl;
        std::cout << "HTTP + WebSocket Unified Server" << std::endl;
        std::cout << "========================================" << std::endl;

        // 创建统一的 HTTP 服务器实例 (同时支持 WebSocket)
        auto server = std::make_unique<HttpServer>("0.0.0.0", 8765);

        // ========== 注册 HTTP Handle ==========

        // 方式 1: 模板方式 (推荐)
        // server->GET<HelloHandle>("/api/hello");

        // 方式 2: Lambda 方式 - 简单示例
        server->registerHttpHandle("GET", "/api/hello", [](void* ctx) -> net::awaitable<void> {
            auto context = static_cast<BeastContext*>(ctx);

            // 构建 HTTP 响应 (使用 context->res)
            context->res.result(http::status::ok);
            context->res.set(http::field::server, "Hikuuu-HttpServer");
            context->res.set(http::field::content_type, "application/json");
            context->res.body() = R"({"message": "Hello from HTTP Server!"})";
            context->res.prepare_payload();

            // 响应会通过框架统一发送 (自动包含安全响应头)
            co_return;
        });

        // ========== 注册 WebSocket Handle ==========

        // 方式 1: 模板方式 (推荐)
        server->WS<EchoWsHandle>("/echo");

        // 方式 2: Lambda 方式
        // server->registerWsHandle("/ws/echo", [](void* ctx) -> net::awaitable<void> {
        //     // WebSocket 处理逻辑
        //     co_return;
        // });

        // ========== 可选配置 ==========

        // 设置 IO 线程数 (默认使用硬件并发数)
        // server->set_io_thread_count(4);

        // 配置 SSL/TLS (同时作用于 HTTP 和 WebSocket)
        // server->setTls("/path/to/cert.pem", "password", 0);

        // 配置 CORS (跨域资源共享)
        // 示例 1: 允许所有源 (开发环境)
        server->setCors(CorsConfig::allowAll());

        // 示例 2: 允许指定源 (生产环境，取消注释使用)
        // server->setCors(CorsConfig::allowOrigin("https://example.com"));

        std::cout << std::endl;
        std::cout << "Endpoints:" << std::endl;
        std::cout << "  HTTP REST API:  http://0.0.0.0:8765/api/hello" << std::endl;
        std::cout << "  WebSocket:      ws://0.0.0.0:8765/echo" << std::endl;
        std::cout << std::endl;
        std::cout << "Features:" << std::endl;
        std::cout << "  ✓ Unified HTTP + WebSocket server" << std::endl;
        std::cout << "  ✓ Shared IO thread pool" << std::endl;
        std::cout << "  ✓ Shared SSL/TLS configuration" << std::endl;
        std::cout << "  ✓ Auto protocol detection" << std::endl;
        std::cout << "  ✓ Connection pooling (max: " << HttpServer::get_max_connections() << ")"
                  << std::endl;
        std::cout << std::endl;

        // 启动服务器
        server->start();

        std::cout << "Server started. Press Ctrl+C to stop." << std::endl;

        // 运行事件循环
        HttpServer::loop();

    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}