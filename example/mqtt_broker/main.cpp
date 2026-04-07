/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-04-07
 *      Author: fasiondog
 */

#include <iostream>
#include <signal.h>
#include <thread>
#include <chrono>
#include <hikyuu/mqtt/MqttBroker.h>
#include <hikyuu/utilities/Parameter.h>

using namespace hku;

// 全局变量用于信号处理
static MqttBroker* g_broker = nullptr;
static std::atomic<bool> g_running{false};

void signal_handler(int signum) {
    std::cout << "\n[Signal] Received signal " << signum << ", stopping broker..." << std::endl;
    if (g_broker && g_running.load()) {
        g_broker->stop();
    }
}

void print_banner() {
    std::cout << "========================================" << std::endl;
    std::cout << "   HKU MQTT Broker v1.0" << std::endl;
    std::cout << "   Based on async_mqtt library" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
}

void print_config(const Parameter& param) {
    std::cout << "[Configuration]" << std::endl;

    if (param.have("mqtt_port")) {
        std::cout << "  - MQTT TCP Port: " << param.get<int>("mqtt_port") << std::endl;
    }

    if (param.have("ws_port")) {
        std::cout << "  - WebSocket Port: " << param.get<int>("ws_port") << std::endl;
    }

#if defined(ASYNC_MQTT_USE_TLS)
    if (param.have("tls_port")) {
        std::cout << "  - TLS Port: " << param.get<int>("tls_port") << std::endl;
    }
#endif

#if defined(ASYNC_MQTT_USE_WS) && defined(ASYNC_MQTT_USE_TLS)
    if (param.have("wss_port")) {
        std::cout << "  - WSS Port: " << param.get<int>("wss_port") << std::endl;
    }
#endif

    int num_iocs = param.have("iocs") ? param.get<int>("iocs") : 0;
    if (num_iocs == 0) {
        num_iocs = static_cast<int>(std::thread::hardware_concurrency());
        if (num_iocs == 0)
            num_iocs = 1;
    }
    std::cout << "  - IO Contexts: " << num_iocs << std::endl;

    int threads_per_ioc = param.have("threads_per_ioc") ? param.get<int>("threads_per_ioc") : 1;
    std::cout << "  - Threads per IO Context: " << threads_per_ioc << std::endl;

    std::cout << "  - TCP No Delay: "
              << (param.have("tcp_no_delay") && param.get<bool>("tcp_no_delay") ? "yes" : "no")
              << std::endl;
    std::cout << "  - Bulk Write: "
              << (param.have("bulk_write") && param.get<bool>("bulk_write") ? "yes" : "no")
              << std::endl;

    if (param.have("read_buf_size")) {
        std::cout << "  - Read Buffer Size: " << param.get<int>("read_buf_size") << " bytes"
                  << std::endl;
    }

    if (param.have("auth_file")) {
        std::cout << "  - Auth File: " << param.get<std::string>("auth_file") << std::endl;
    }

    std::cout << std::endl;
}

int main(int argc, char* argv[]) {
    try {
        // 注册信号处理器
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        print_banner();

        // 创建配置参数
        Parameter param;

        // ========== 基础配置 ==========
        // MQTT TCP 端口（必需）
        param.set<int>("mqtt_port", 1883);

        // WebSocket 端口（可选，取消注释以启用）
        // param.set<int>("ws_port", 8083);

        // TLS 端口（可选，需要证书配置）
        // param.set<int>("tls_port", 8883);
        // param.set<std::string>("certificate", "server.crt");
        // param.set<std::string>("private_key", "server.key");
        // param.set<std::string>("verify_file", "ca.crt");  // 客户端证书验证（可选）

        // ========== 性能配置 ==========
        // io_context 数量（0 = 自动检测 CPU 核心数）
        param.set<int>("iocs", 0);

        // 每个 io_context 的线程数
        param.set<int>("threads_per_ioc", 1);

        // TCP 优化
        param.set<bool>("tcp_no_delay", true);  // 禁用 Nagle 算法，降低延迟
        param.set<bool>("bulk_write", true);    // 启用批量写入，提高吞吐量

        // 缓冲区大小
        param.set<int>("read_buf_size", 65536);  // 64KB 读取缓冲

        // 可选：自定义缓冲区大小
        // param.set<int>("recv_buf_size", 1048576);  // 1MB 接收缓冲
        // param.set<int>("send_buf_size", 1048576);  // 1MB 发送缓冲

        // ========== 安全配置 ==========
        // 认证文件（JSON 格式）
        // param.set<std::string>("auth_file", "auth.json");

        // 打印配置信息
        print_config(param);

        // 创建并启动 Broker
        std::cout << "[Starting] Initializing MQTT Broker..." << std::endl;
        MqttBroker broker(param);
        g_broker = &broker;
        g_running.store(true);

        std::cout << "[Running] MQTT Broker is now accepting connections." << std::endl;
        std::cout << "[Info] Press Ctrl+C to stop the broker." << std::endl;
        std::cout << std::endl;

        // start() 会阻塞直到 stop() 被调用
        broker.start();

        g_running.store(false);
        std::cout << "[Stopped] MQTT Broker has been stopped." << std::endl;
        g_broker = nullptr;

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "[Error] " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "[Error] Unknown exception occurred!" << std::endl;
        return 1;
    }
}
