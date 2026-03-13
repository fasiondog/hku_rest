/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-03-13
 *      Author: fasiondog
 */

#include <iostream>
#include <csignal>
#include <hikyuu/httpd/WebSocketServer.h>
#include "EchoWsHandle.h"

using namespace hku;

int main(int argc, char* argv[]) {
    try {
        // 设置日志级别（可选）
        // setLogLevel(2);
        
        std::cout << "Starting WebSocket Server..." << std::endl;
        
        // 创建 WebSocket 服务器实例
        WebSocketServer server("0.0.0.0", 8765);
        
        // 注册 WebSocket Handle
        server.ws<EchoWsHandle>("/echo");
        
        // 可选：设置 IO 线程数
        // server.set_io_thread_count(4);
        
        // 可选：配置 SSL/TLS
        // WebSocketServer::set_tls("/path/to/cert.pem", "password");
        
        std::cout << "WebSocket Server listening on ws://0.0.0.0:8765/echo" << std::endl;
        
        // 启动服务器
        server.start();
        
        // 运行事件循环
        WebSocketServer::loop();
        
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
