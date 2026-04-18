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
#include "hikyuu/httpd/ConnectionManager.h"  // 智能连接管理器
#include "hikyuu/httpd/MetricsExporter.h"    // Prometheus 指标导出
#include "hikyuu/httpd/ConnectionMonitor.h"  // 连接监控器
#include "HelloService.h"

using namespace hku;

static std::atomic<bool> g_shutdown_flag{false};  // 防止重复退出
static std::thread g_monitor_thread;              // 全局监控线程

void signal_handle(int signal) {
    if (g_shutdown_flag.exchange(true)) {
        return;  // 已经处理过退出信号，忽略后续信号
    }

    HKU_INFO("Shutdown now ... (signal={})", signal);

    // 1. 通知监控线程退出并等待其结束
    HKU_INFO("Step 1: Stopping monitor thread...");
    g_shutdown_flag.store(true);
    if (g_monitor_thread.joinable()) {
        g_monitor_thread.join();
        HKU_INFO("Monitor thread stopped");
    } else {
        HKU_WARN("Monitor thread is not joinable!");
    }

    // 2. 停止 Pod（最多 1 秒）
    HKU_INFO("Step 2: Stopping Pod...");
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
    HttpServer server("0.0.0.0", 8080);
    // HttpHandle::enableTrace(true, false);

    // HTTPS 服务器示例
    // HttpServer server("0.0.0.0", 8443);
    // HttpHandle::enableTrace(true, false);
    // server.set_tls("server.pem", "", 0);

    try {
        pod::init("rest_server.ini");

        HelloService hello_service("");
        hello_service.bind(&server);

        // =====================================================================
        // IP 访问控制配置
        // =====================================================================
        // 仅允许本地私有网络访问（生产环境推荐）
        // server.allowSubnets({
        //   "192.168.0.0/16",  // 192.168.x.x 网段
        //   "10.0.0.0/8",      // 10.x.x.x 网段
        //   "172.16.0.0/12"    // 172.16.x.x - 172.31.x.x 网段
        // });

        HKU_INFO("IP access control configured: only local private networks allowed");

        // =====================================================================
        // 智能连接管理器配置
        // =====================================================================
        // 配置连接管理器（在 start() 之前调用）
        // 参数 1: 最大并发连接数 = 1000
        // 参数 2: 等待超时时间 = 30 秒（0 表示无限等待）
        //
        // 效果：
        // - 同时最多允许 1000 个连接并行处理
        // - 超过 1000 时，新连接进入 FIFO 等待队列
        // - 等待超过 30 秒则自动拒绝
        // - 连接断开时自动释放许可，唤醒等待者
        server.set_max_concurrent_connections(1000, 30000);

        HKU_INFO("Connection manager configured: max={}, timeout={}ms", 1000, 30000);

        // =====================================================================
        // Prometheus 监控系统集成
        // =====================================================================
        // 创建连接监控器（自动注册所有指标）
        auto conn_monitor = std::make_shared<ConnectionMonitor>(server.get_connection_manager(),
                                                                1000  // 采样间隔 1 秒
        );

        // 启动后台采样线程
        conn_monitor->startSampling();

        HKU_INFO("Prometheus metrics exporter enabled");
        HKU_INFO("Metrics endpoint: http://0.0.0.0:8080/metrics");

        // =====================================================================
        // WebSocket 功能开关（默认禁用）
        // =====================================================================
        // 如果需要使用 WebSocket，必须显式启用
        server.enableWebSocket(true);

        // 注册 WebSocket Handle（必须在 enableWebSocket(true) 之后）
        // server.WS<EchoWsHandle>("/echo");
        // server.WS<StreamHandle>("/stream");
        // server.WS<QuotePushHandle>("/quote");

        // =====================================================================
        // HTTP RESTful API
        // =====================================================================

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
        server.set_io_thread_count(1);
        // server.set_io_thread_count(
        //   std::max<size_t>(recommended_threads / 2, 1));  // 使用默认值（硬件并发数一半）

        HKU_INFO("start server ... You can press Ctrl-C stop");
        HKU_INFO("HTTP Server started on http://0.0.0.0:8080");
        HKU_INFO("Test with: curl http://localhost:8080/hello");
        HKU_INFO("Prometheus metrics: curl http://localhost:8080/metrics");

        // 【关键】先启动监控线程，再进入 loop
        g_monitor_thread = std::thread([&]() {
            while (!g_shutdown_flag.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(5));

                auto mgr = server.get_connection_manager();
                if (mgr) {
                    HKU_INFO("=== Connection Manager Stats ===");
                    HKU_INFO("Active Connections: {}/{}", mgr->getCurrentCount(),
                             mgr->getMaxConcurrent());
                    HKU_INFO("Waiting Connections: {}", mgr->getWaitingCount());
                    HKU_INFO("Total Permits Issued: {}", mgr->getTotalIssued());

                    // 警告：如果等待队列过长
                    if (mgr->getWaitingCount() > 100) {
                        HKU_WARN("High load detected! {} connections waiting",
                                 mgr->getWaitingCount());
                    }
                }
            }
        });

        server.start();
        server.loop();

        // 【关键】正常情况下不会执行到这里，因为 loop() 会一直阻塞
        // 只有当 io_context 被 stop() 后才会继续
        HKU_INFO("Server loop finished, cleaning up...");

        // 停止监控器
        conn_monitor->stopSampling();

        // 设置退出标志并等待监控线程结束
        g_shutdown_flag.store(true);
        if (g_monitor_thread.joinable()) {
            g_monitor_thread.join();
            HKU_INFO("Monitor thread joined");
        }

        HKU_INFO("Cleanup completed");

    } catch (std::exception& e) {
        HKU_FATAL(e.what());
    } catch (...) {
        HKU_FATAL("Unknown error!");
    }

    hku::pod::quit();
    server.stop();
    return 0;
}
