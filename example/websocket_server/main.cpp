/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-03-13
 *      Author: fasiondog
 */

#include <iostream>
#include <csignal>
#include <hikyuu/httpd/HttpServer.h>
#include "hikyuu/httpd/pod/all.h"
#include "EchoWsHandle.h"
#include "QuotePushHandle.h"

using namespace hku;

static std::atomic<bool> g_shutdown_flag{false};  // 防止重复退出

void signal_handle(int signal) {
    if (g_shutdown_flag.exchange(true)) {
        return;  // 已经处理过退出信号，忽略后续信号
    }

    HKU_INFO("Shutdown now ...");
    hku::pod::quit();
    HttpServer::stop();
    exit(0);  // 直接退出，不再返回 main 函数
}

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
        std::signal(SIGINT, signal_handle);
        std::signal(SIGTERM, signal_handle);
        std::signal(SIGABRT, signal_handle);
        std::signal(SIGSEGV, signal_handle);

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

        // 方式 1: 模板方式（推荐）- Echo 测试服务
        server->WS<EchoWsHandle>("/echo");
        
        // 方式 2: 行情推送示例（演示流式分批推送功能）
        server->WS<QuotePushHandle>("/quotes");

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
        std::cout << "  HTTP REST API:     http://0.0.0.0:8765/api/hello" << std::endl;
        std::cout << "  WebSocket Echo:    ws://0.0.0.0:8765/echo (基础测试)" << std::endl;
        std::cout << "  WebSocket Quotes:  ws://0.0.0.0:8765/quotes (行情推送)" << std::endl;
        std::cout << std::endl;
        std::cout << "Quote Push Examples:" << std::endl;
        std::cout << "  1. Subscribe mode (pre-generated list):" << std::endl;
        std::cout << "     {\"action\": \"subscribe_quotes\", \"symbols\": [\"SH600000\", ...]}" << std::endl;
        std::cout << std::endl;
        std::cout << "  2. Streaming mode (dynamic generator):" << std::endl;
        std::cout << "     {\"action\": \"stream_quotes\", \"count\": 10000}" << std::endl;
        std::cout << std::endl;
        std::cout << "Features:" << std::endl;
        std::cout << "  ✓ Unified HTTP + WebSocket server" << std::endl;
        std::cout << "  ✓ Shared IO thread pool" << std::endl;
        std::cout << "  ✓ Shared SSL/TLS configuration" << std::endl;
        std::cout << "  ✓ Auto protocol detection" << std::endl;
        std::cout << "  ✓ Streaming batch push (500 msg/batch, 50ms interval)" << std::endl;
        std::cout << "  ✓ Push 10000 quotes in ~1 second" << std::endl;
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