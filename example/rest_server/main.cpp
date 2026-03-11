/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-23
 *      Author: fasiondog
 */

#include <locale>
#include <csignal>
#include "hikyuu/httpd/HttpServer.h"
#include "hikyuu/httpd/pod/all.h"
#include "HelloService.h"

using namespace hku;

void signal_handle(int signal) {
    HKU_INFO("Shutdown now ...");
    hku::pod::quit();
    HttpServer::stop();
    exit(0);
}

int main(int argc, char* argv[]) {
    initLogger();

    std::signal(SIGINT, signal_handle);
    std::signal(SIGTERM, signal_handle);
    std::signal(SIGABRT, signal_handle);
    std::signal(SIGSEGV, signal_handle);

    // HTTP 服务器
    HttpServer server("0.0.0.0", 8080);
    HttpHandle::enableTrace(true, false);

    // HTTPS 服务器示例
    // HttpServer server("0.0.0.0", 8443);
    // HttpHandle::enableTrace(true, false);
    // server.set_tls("server.pem", "", 0);

    try {
        pod::init("rest_server.ini");

        HelloService hello_service("hku");
        hello_service.bind(&server);

        // =====================================================================
        // 设置协程 IO 执行线程数（可选）
        // =====================================================================
        // 方法 1: 不设置，默认使用 CPU 核心数（推荐）
        // server.set_io_thread_count(0);  // 0 = hardware_concurrency()

        // 方法 2: 指定固定线程数
        // server.set_io_thread_count(4);  // 使用 4 个 IO 线程

        // 方法 3: 单线程模式（适合调试或低负载）
        // server.set_io_thread_count(1);  // 单线程运行

        // 获取推荐的线程数
        size_t recommended_threads = std::thread::hardware_concurrency();
        HKU_INFO("Recommended IO threads: {}", recommended_threads);

        // 实际设置（如果不设置，loop() 会自动使用 hardware_concurrency())
        server.set_io_thread_count(0);  // 使用默认值（硬件并发数）

        HKU_INFO("start server ... You can press Ctrl-C stop");
        HKU_INFO("HTTP Server started on http://0.0.0.0:8080");
        HKU_INFO("Test with: curl http://localhost:8080/api/hello");

        server.start();
        server.loop();

    } catch (std::exception& e) {
        HKU_FATAL(e.what());
    } catch (...) {
        HKU_FATAL("Unknown error!");
    }

    hku::pod::quit();
    server.stop();
    return 0;
}
