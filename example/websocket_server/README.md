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

#### 方式一：使用 wscat（推荐）

安装 wscat:
```bash
npm install -g wscat
```

连接并测试:
```bash
wscat -c ws://localhost:8765/echo
```

**发送消息:**
```
> Hello, WebSocket!
```

**接收回显:**
```
< {"type":"echo","message":"Hello, WebSocket!","time":1710409200}
```

#### 方式二：使用 Python websockets 库

安装依赖:
```bash
pip3 install websockets
```

创建测试脚本 `test_ws.py`:
```python
import asyncio
import websockets

async def test_echo():
    uri = "ws://localhost:8765/echo"
    async with websockets.connect(uri) as websocket:
        # 接收欢迎消息
        welcome = await websocket.recv()
        print(f"欢迎消息：{welcome}")
        
        # 发送测试消息
        await websocket.send("Hello, WebSocket!")
        response = await websocket.recv()
        print(f"回显：{response}")
        
        # 保持连接一段时间，观察心跳
        await asyncio.sleep(70)  # 超过 60 秒 Ping 间隔
        print("连接仍然活跃！")

asyncio.run(test_echo())
```

运行测试:
```bash
python3 test_ws.py
```

#### 方式三：使用浏览器控制台

打开浏览器，访问任意网页（如 `about:blank`），按 F12 打开控制台，输入:

```javascript
const ws = new WebSocket('ws://localhost:8765/echo');

ws.onopen = () => {
    console.log('✅ 连接已建立');
};

ws.onmessage = (event) => {
    console.log('收到消息:', event.data);
    
    // 收到欢迎消息后发送测试
    if (JSON.parse(event.data).type === 'welcome') {
        ws.send('Hello from browser!');
    }
};

ws.onerror = (error) => {
    console.error('❌ 错误:', error);
};

ws.onclose = () => {
    console.log('🔴 连接已关闭');
};
```

### 3. 验证心跳机制

连接成功后，等待约 60 秒，查看服务器日志应该能看到类似输出:

```
[DEBUG] Sending WebSocket Ping to 127.0.0.1:xxxxx
[DEBUG] Received WebSocket Ping from 127.0.0.1:xxxxx
```

如果客户端在 10 秒内未响应 Pong，服务器会输出:

```
[WARN] WebSocket Ping timeout from 127.0.0.1:xxxxx, closing connection
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

### WebSocket 心跳保活机制

服务器自动实现了完整的心跳检测机制，无需手动配置：

- **Ping 间隔**: 60 秒（`WS_PING_INTERVAL`）
- **Pong 超时**: 10 秒（`WS_PING_TIMEOUT`）
- **自动清理**: 超时未响应自动关闭连接

**工作流程：**
1. 连接建立后，服务器每 60 秒自动发送 Ping 帧
2. 启动 10 秒超时定时器等待 Pong 响应
3. 收到 Pong 后取消超时定时器
4. 若 10 秒内未收到 Pong，判定为死连接并关闭

**Handle 中的心跳回调：**
```cpp
class MyWebSocketHandle : public WebSocketHandle {
public:
    net::awaitable<void> onPing() override {
        // 收到服务端 Ping 时触发（客户端视角）
        // 服务端会自动回复 Pong，此处可添加日志或监控逻辑
        HKU_DEBUG("Heartbeat check from {}", getClientIp());
        co_return;
    }
    
    net::awaitable<void> onPong() override {
        // 收到服务端 Pong 时触发（客户端视角）
        // 服务端内部已自动处理，此回调可选实现
        co_return;
    }
};
```

**注意：** Boost.Beast 会自动处理 Ping/Pong 帧的收发，应用层通常不需要手动实现心跳逻辑。服务器的心跳机制完全透明运行，仅在检测到死连接时才会关闭连接释放资源。

### 安全限制

- 最大缓冲区：1MB
- 最大请求体：10MB  
- 最大请求头：8KB
- Keep-Alive 限制：10000 次请求
- 读取超时：30 秒
- 写入超时：30 秒
- **WebSocket 消息大小**: 10MB（通过 `read_message_max()` 限制）

## 更多信息

- [WebSocketHandle API](../../hikyuu/httpd/WebSocketHandle.h)
- [HttpServer 实现](../../hikyuu/httpd/HttpServer.cpp)
- [EchoWsHandle 示例](EchoWsHandle.h)