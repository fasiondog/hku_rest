/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-23
 *      Author: fasiondog
 */

#include <locale>
#include <csignal>
#include <atomic>
#include <thread>
#include "hikyuu/httpd/HttpServer.h"
#include "hikyuu/httpd/pod/all.h"
#include "McpService.h"
#include "SessionManager.h"

using namespace hku;

static std::atomic<bool> g_shutdown_flag{false};

void signal_handle(int signal) {
    if (g_shutdown_flag.exchange(true)) {
        return;  // 已经处理过退出信号，忽略后续信号
    }

    HKU_INFO("Shutdown now ... (signal={})", signal);

    // 停止 Pod
    pod::quit();

    exit(0);
}

/**
 * Session 清理后台线程
 * 定期清理过期的 Session
 */
void session_cleanup_thread() {
    HKU_INFO("Session cleanup thread started");

    while (!g_shutdown_flag.load()) {
        // 每 60 秒清理一次过期会话
        std::this_thread::sleep_for(std::chrono::seconds(60));

        if (g_shutdown_flag.load()) {
            break;
        }

        try {
            int cleaned = McpHandle::getSessionManager().cleanupExpiredSessions();
            if (cleaned > 0) {
                HKU_INFO("Cleaned up {} expired sessions", cleaned);
            }
        } catch (const std::exception& e) {
            HKU_ERROR("Session cleanup error: {}", e.what());
        }
    }

    HKU_INFO("Session cleanup thread stopped");
}

int main(int argc, char* argv[]) {
    // 设置 locale
    std::locale::global(std::locale(""));

    // 注册信号处理
    std::signal(SIGINT, signal_handle);
    std::signal(SIGTERM, signal_handle);

    try {
        // 1. 初始化 Pod（加载配置）
        pod::init("mcp_server.ini");

        // 2. 创建 HTTP 服务器
        HttpServer server("0.0.0.0", 8080);

        // 3. SSE 心跳已弃用（Streamable HTTP 不需要）
        // McpHandle::configureSSEHeartbeat(false, 0);

        // 4. 注册 MCP 服务
        McpService mcp_service("");
        mcp_service.bind(&server);

        // 5. 启动 Session 清理后台线程
        std::thread cleanup_thread(session_cleanup_thread);
        cleanup_thread.detach();  // 分离线程，让它在后台运行

        // 6. 启动服务器
        HKU_INFO("Starting MCP Server...");
        HKU_INFO("Listen on 0.0.0.0:8080");
        HKU_INFO("MCP endpoint: http://localhost:8080/mcp");
        HKU_INFO("Health check: http://localhost:8080/health");

        server.start();

        HKU_INFO("Server started. Press Ctrl-C to stop.");
        server.loop();

        // 7. 停止服务器（正常情况下不会执行到这里）
        HKU_INFO("Stopping MCP Server...");
        server.stop();

    } catch (const std::exception& e) {
        HKU_ERROR("Fatal error: {}", e.what());
        return -1;
    }

    return 0;
}
