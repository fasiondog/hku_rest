/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-23
 *      Author: fasiondog
 */

#include <locale>
#include <csignal>
#include <atomic>
#include "hikyuu/httpd/HttpServer.h"
#include "hikyuu/httpd/pod/all.h"
#include "McpService.h"

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
        
        // 3. 注册 MCP 服务
        McpService mcp_service("");
        mcp_service.bind(&server);
        
        // 4. 启动服务器
        HKU_INFO("Starting MCP Server...");
        HKU_INFO("Listen on 0.0.0.0:8080");
        HKU_INFO("MCP endpoint: http://localhost:8080/mcp");
        HKU_INFO("Health check: http://localhost:8080/health");
        
        server.start();
        
        HKU_INFO("Server started. Press Ctrl-C to stop.");
        server.loop();
        
        // 5. 停止服务器（正常情况下不会执行到这里）
        HKU_INFO("Stopping MCP Server...");
        server.stop();
        
    } catch (const std::exception& e) {
        HKU_ERROR("Fatal error: {}", e.what());
        return -1;
    }
    
    return 0;
}
