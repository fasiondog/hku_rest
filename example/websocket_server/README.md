# HTTP + WebSocket Unified Server Example

## 概述

本示例展示如何使用 **统一的 HttpServer** 同时提供 HTTP REST API 和 WebSocket 服务。

## 特性

- ✅ **统一架构**: 单个 `HttpServer` 实例同时处理 HTTP 和 WebSocket
- ✅ **资源共享**: 共享 IO 线程池、SSL 配置和连接管理
- ✅ **协议检测**: 自动识别并路由 HTTP 和 WebSocket请求
- ✅ **零迁移成本**: API 与旧版 WebSocketServer 完全兼容

## 编译

```bash
cd /Users/fasiondog/workspace/hku_rest
xmake
```

## 运行

```bash
xmake run websocket_server
```

## 测试

### 1. 测试 HTTP REST API

```bash
curl http://localhost:8765/api/hello
```

**响应:**
```json
{"message": "Hello from HTTP Server!"}
```

### 2. 测试 WebSocket

使用 [wscat](https://github.com/websockets/wscat) 或其他 WebSocket 客户端:

```bash
wscat -c ws://localhost:8765/echo
```

**发送消息:**
```
> Hello, WebSocket!
```

**接收回显:**
```
< Hello, WebSocket!
```

## 代码说明

### 创建服务器

```cpp
#include <hikyuu/httpd/HttpServer.h>

auto server = std::make_unique<HttpServer>("0.0.0.0", 8765);
```

### 注册 HTTP Handle

**方式 1: Lambda 方式**
```cpp
server->registerHttpHandle("GET", "/api/hello", 
    [](void* ctx) -> net::awaitable<void> {
        // HTTP 处理逻辑
        co_return;
    });
```

**方式 2: 模板方式**
```cpp
server->GET<MyHttpHandle>("/api/data");
```

### 注册 WebSocket Handle

**方式 1: Lambda 方式**
```cpp
server->registerWsHandle("/ws/echo",
    [](void* ctx) -> net::awaitable<void> {
        // WebSocket 处理逻辑
        co_return;
    });
```

**方式 2: 模板方式 (推荐)**
```cpp
server->WS<EchoWsHandle>("/echo");
```

### 配置 SSL/TLS

```cpp
// 同时作用于 HTTP 和 WebSocket
server->setTls("server.pem", "password", 0);
```

## 架构优势

| 方面 | 旧架构 | 新架构 |
|------|--------|--------|
| 服务器类 | HttpServer + WebSocketServer | HttpServer(单个) |
| IO 线程池 | 2 套 | 1 套共享 ✓ |
| SSL 上下文 | 2 套 | 1 套共享 ✓ |
| 端口占用 | 2 个 | 1 个 ✓ |
| API 统一性 | 分散 | 统一 ✓ |

## 迁移指南

### 旧代码 (WebSocketServer)
```cpp
#include <hikyuu/httpd/WebSocketServer.h>

WebSocketServer server("0.0.0.0", 8765);
server.WS<EchoWsHandle>("/echo");
server.start();
```

### 新代码 (HttpServer)
```cpp
#include <hikyuu/httpd/HttpServer.h>

auto server = std::make_unique<HttpServer>("0.0.0.0", 8765);
server.WS<EchoWsHandle>("/echo");  // ← API 完全相同!
server.start();
```

**零迁移成本!** 只需修改头文件和类名。

## 高级特性

### 连接池管理

```cpp
// 获取当前活跃连接数
int active = HttpServer::get_active_connections();

// 最大连接数限制 (默认 1000)
int max_conn = HttpServer::get_max_connections();
```

### 安全限制

- 最大缓冲区：1MB
- 最大请求体：10MB  
- 最大请求头：8KB
- Keep-Alive 限制：10000 次请求
- 读取超时：30 秒
- 写入超时：30 秒

## 更多信息

- [WebSocketHandle API](../../hikyuu/httpd/WebSocketHandle.h)
- [HttpServer 实现](../../hikyuu/httpd/HttpServer.cpp)
- [EchoWsHandle 示例](EchoWsHandle.h)