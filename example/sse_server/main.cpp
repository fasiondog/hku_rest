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
#include <chrono>
#include "hikyuu/httpd/HttpServer.h"
#include "hikyuu/httpd/pod/all.h"
#include "SseService.h"

using namespace hku;

static std::atomic<bool> g_shutdown_flag{false};

void signal_handle(int signal) {
    if (g_shutdown_flag.exchange(true)) {
        return;
    }

    HKU_INFO("Shutdown now ... (signal={})", signal);
    hku::pod::quit();
    HKU_INFO("Quit");
    exit(0);
}

int main(int argc, char* argv[]) {
    initLogger();

    std::signal(SIGINT, signal_handle);
    std::signal(SIGTERM, signal_handle);
    std::signal(SIGABRT, signal_handle);
    std::signal(SIGSEGV, signal_handle);

    // HTTP 服务器
    HttpServer server("0.0.0.0", 8081);
    
    try {
        pod::init("sse_server.ini");

        // 注册 SSE 服务
        SseService sse_service("");
        sse_service.bind(&server);

        HKU_INFO("SSE Server configured");

        // 设置 IO 线程数
        size_t recommended_threads = std::thread::hardware_concurrency();
        HKU_INFO("Recommended IO threads: {}", recommended_threads);
        server.set_io_thread_count(std::max<size_t>(recommended_threads / 2, 1));

        HKU_INFO("===========================================");
        HKU_INFO("SSE Server started on http://0.0.0.0:8081");
        HKU_INFO("===========================================");
        HKU_INFO("Test endpoints:");
        HKU_INFO("  1. Full SSE stream: curl -N http://localhost:8081/sse/stream");
        HKU_INFO("  2. Simple SSE:      curl -N http://localhost:8081/sse/simple");
        HKU_INFO("  3. Python test:     python3 test_sse.py");
        HKU_INFO("===========================================");
        HKU_INFO("Press Ctrl-C to stop");

        server.start();
        server.loop();

        HKU_INFO("Server stopped");

    } catch (std::exception& e) {
        HKU_FATAL(e.what());
    } catch (...) {
        HKU_FATAL("Unknown error!");
    }

    hku::pod::quit();
    server.stop();
    return 0;
}
