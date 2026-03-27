/**
 * 智能连接管理器 - 使用示例
 * 
 * 演示如何在 HttpServer 中配置和使用 ConnectionManager
 */

#include "HttpServer.h"
#include "ConnectionManager.h"

using namespace hku;

int main() {
    try {
        // 1. 创建 HTTP 服务器实例
        auto server = std::make_shared<HttpServer>("0.0.0.0", 8080);
        
        // 2. 配置连接管理器（在 start() 之前调用）
        // 参数 1: 最大并发连接数 = 1000
        // 参数 2: 等待超时时间 = 30 秒（0 表示无限等待）
        server->set_max_concurrent_connections(1000, 30000);
        
        // 3. 注册 HTTP Handle（使用正确的 API）
        server->registerHttpHandle("GET", "/hello", [](void* ctx) -> net::awaitable<void> {
            HKU_INFO("Handling /hello request");
            co_return;
        });

        // 4. 启动服务器
        server->start();
        
        // 5. 监控连接状态（可选）
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            auto mgr = HttpServer::get_connection_manager();
            if (mgr) {
                HKU_INFO("=== Connection Stats ===");
                HKU_INFO("Active: {}", mgr->getCurrentCount());
                HKU_INFO("Waiting: {}", mgr->getWaitingCount());
                HKU_INFO("Max: {}", mgr->getMaxConcurrent());
                HKU_INFO("Total Issued: {}", mgr->getTotalIssued());
            }
        }
        
    } catch (const std::exception& e) {
        HKU_ERROR("Server error: {}", e.what());
        return 1;
    }
    
    return 0;
}

/*
 * 运行效果示例：
 * 
 * [DEBUG] Connection 0 acquired: active=1/1000, waiting=0
 * [DEBUG] Connection 1 acquired: active=2/1000, waiting=0
 * ...
 * [DEBUG] Connection 999 acquired: active=1000/1000, waiting=0
 * 
 * // 第 1000 个连接之后，新连接开始等待
 * [DEBUG] Connection waiting: current=1000, waiting=1
 * [DEBUG] Connection waiting: current=1000, waiting=2
 * 
 * // 某个连接断开，唤醒等待者
 * [DEBUG] Connection 50 released
 * [DEBUG] Connection 1000 acquired from queue: active=1000/1000, waiting=1
 * 
 * // 如果等待超时
 * [WARN] Connection acquire timeout after 30000ms
 */
