# HTTP + WebSocket Unified Server Example

## 概述

本示例展示如何使用 **统一的 HttpServer** 同时提供 HTTP REST API 和 WebSocket 服务。

## 特性

- ✅ **统一架构**: 单个 `HttpServer` 实例同时处理 HTTP 和 WebSocket
- ✅ **资源共享**: 共享 IO 线程池、SSL 配置和连接管理
- ✅ **协议检测**: 自动识别并路由 HTTP 和 WebSocket请求
- ✅ **零迁移成本**: API 与旧版 WebSocketServer 完全兼容
- ✅ **灵活部署**: 支持单端口模式或双端口独立部署

## 端口配置说明

### 方案 A: 单端口模式（推荐）⭐

**HTTP 和 WebSocket 共用同一个端口**，通过路径前缀区分：

```cpp
auto server = std::make_unique<HttpServer>("0.0.0.0", 8765);

// HTTP API (路径前缀：/api/*)
server->GET<HelloHandle>("/api/hello");
server->registerHttpHandle("GET", "/api/download", download_handler);

// WebSocket (路径：/echo, /quotes 等)
server->WS<EchoWsHandle>("/echo");
server->WS<QuotePushHandle>("/quotes");
```

**访问方式：**
- HTTP: `http://localhost:8765/api/hello`
- WebSocket: `ws://localhost:8765/echo`

**优点：**
- ✅ 配置简单，只需管理一个端口
- ✅ 资源共享，减少系统开销
- ✅ 现代服务架构的最佳实践（类似 FastAPI、gRPC）

### 方案 B: 双端口模式

**HTTP 和 WebSocket 使用不同的端口**，完全独立：

#### 当前架构限制 ⚠️

**注意**：当前版本的 `HttpServer` 使用静态成员变量管理 IO 上下文和监听器，**不支持在单个进程中创建多个服务器实例**。

如需双端口部署，推荐以下两种方案：

#### 方案 B1: 多进程部署（推荐用于生产环境）⭐

使用进程管理器分别运行 HTTP 和 WebSocket 服务：

**进程 1: HTTP 服务器 (配置文件 `http_server.conf`)**
```python
# /etc/supervisor/conf.d/http_server.conf
[program:http_server]
command=/path/to/websocket_server --port 8765 --mode http
autostart=true
autorestart=true
```

**进程 2: WebSocket 服务器 (配置文件 `ws_server.conf`)**
```python
# /etc/supervisor/conf.d/ws_server.conf
[program:ws_server]
command=/path/to/websocket_server --port 8766 --mode ws
autostart=true
autorestart=true
```

**启动命令：**
```bash
sudo supervisorctl reread
sudo supervisorctl update
sudo supervisorctl start http_server
sudo supervisorctl start ws_server
```

**优点：**
- ✅ 完全隔离，互不影响
- ✅ 可独立扩展和重启
- ✅ 故障域分离
- ✅ 易于监控和管理

#### 方案 B2: 修改架构支持多实例（需要代码改造）

需要重构 `HttpServer`，将静态成员改为实例成员：

```cpp
// 修改后的 HttpServer 设计（伪代码）
class HttpServer {
private:
    net::io_context m_io_context;      // 每个实例独立的 IO 上下文
    tcp::acceptor m_acceptor;          // 每个实例独立的监听器
    std::thread m_io_thread;           // 每个实例独立的 IO 线程
    
public:
    void start() {
        // 启动独立的 IO 线程
        m_io_thread = std::thread([this]() {
            m_io_context.run();
        });
    }
    
    void stop() {
        m_io_context.stop();
        m_io_thread.join();
    }
};

// 使用示例
auto http_server = std::make_unique<HttpServer>("0.0.0.0", 8765);
auto ws_server = std::make_unique<HttpServer>("0.0.0.0", 8766);

http_server->start();
ws_server->start();

// 主线程等待或处理其他任务
while (running) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

http_server->stop();
ws_server->stop();
```

**访问方式：**
- HTTP: `http://localhost:8765/api/hello`
- WebSocket: `ws://localhost:8766/echo`

**适用场景：**
- 🔧 需要独立控制 HTTP 和 WebSocket 的访问策略
- 🔧 需要分别扩展某个协议的服务实例
- 🔧 安全要求严格隔离的场景

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
```
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

# WebSocket Server 示例

## 功能特性

本示例展示了基于 Boost.Beast 的 WebSocket 服务器实现，支持以下特性：

- ✅ HTTP/HTTPS/WebSocket统一架构
- ✅ 协议自动检测（HTTP → WebSocket 升级）
- ✅ SSL/TLS加密支持
- ✅ 跨域资源共享（CORS）
- ✅ **流式分批推送**（适用于行情数据推送）⭐

## WebSocket 端点

### 1. Echo 服务（基础测试）

**地址：** `ws://localhost:8765/echo`

**功能：** 简单的消息回显测试

**测试方式：**
```python
import websockets

async with websockets.connect("ws://localhost:8765/echo") as ws:
    await ws.send("Hello")
    response = await ws.recv()  # 返回 "Hello"
```

### 2. 行情推送服务（新功能）⭐

**地址：** `ws://localhost:8765/quotes`

**功能：** 演示如何推送 10000 条股票行情数据

#### 模式一：订阅模式（预生成列表）

适合内存充足场景，预先准备好所有数据：

```python
import websockets
import json

async with websockets.connect("ws://localhost:8765/quotes") as ws:
    # 发送订阅请求
    request = {
        "action": "subscribe_quotes",
        "symbols": ["SH600000", "SH600001", ...]  # 可选，默认 10000 只
    }
    await ws.send(json.dumps(request))
    
    # 接收推送
    while True:
        response = await ws.recv()
        data = json.loads(response)
        if data.get("type") == "quote_finish":
            print(f"推送完成：{data}")
            break
```

**性能指标：**
- 批次大小：500 条/批
- 批次间隔：50ms
- 10000 条数据预计耗时：~1 秒（20 批次 × 50ms）

#### 模式二：流式模式（动态生成器）

适合内存敏感场景，边生成边发送：

```python
import websockets
import json

async with websockets.connect("ws://localhost:8765/quotes") as ws:
    # 发送流式推送请求
    request = {
        "action": "stream_quotes",
        "count": 10000  # 推送数量
    }
    await ws.send(json.dumps(request))
    
    # 接收推送
    quote_count = 0
    while True:
        response = await ws.recv()
        data = json.loads(response)
        
        if data.get("type") == "stream_finish":
            print(f"流式推送完成：{data}")
            break
        
        # 统计行情数据
        if "symbol" in data:
            quote_count += 1
            if quote_count % 100 == 0:
                print(f"已接收 {quote_count} 条行情")
```

## 运行示例

### 1. 启动服务器

```bash
cd /Users/fasiondog/workspace/hku_rest
xmake r websocket_server
```

### 2. 安装 Python 依赖

```bash
pip3 install --user websockets
```

### 3. 运行测试脚本

#### 基础 Echo 测试

```bash
python example/websocket_server/simple_ws_test.py
```

#### 综合心跳测试

```bash
python example/websocket_server/comprehensive_heartbeat_test.py
```

#### 流式推送功能测试（新增）⭐

```bash
python example/websocket_server/test_quote_push.py
```

## 技术实现细节

### 配置参数（WebSocketConfig）

```cpp
// hikyuu/httpd/HttpWebSocketConfig.h
struct WebSocketConfig {
    // 流式分批推送配置
    static constexpr bool ENABLE_STREAMING_BATCH = true;  // 启用流式分批
    static constexpr std::size_t BATCH_SIZE = 500;        // 500 条/批
    static constexpr std::chrono::milliseconds BATCH_INTERVAL{50}; // 50ms 间隔
    
    // 消息大小限制
    static constexpr std::size_t MAX_MESSAGE_SIZE = 15 * 1024 * 1024;  // 15MB
    static constexpr std::size_t MAX_FRAME_SIZE = 15 * 1024 * 1024;    // 15MB
    static constexpr std::size_t MAX_READ_BUFFER_SIZE = 32 * 1024 * 1024; // 32MB
    
    // 心跳机制
    static constexpr std::chrono::seconds PING_INTERVAL{30};  // 30 秒
    static constexpr std::chrono::seconds PING_TIMEOUT{15};   // 15 秒
};
```

### API 方法

#### sendBatch - 预生成列表模式

```cpp
net::awaitable<bool> sendBatch(
    const std::vector<std::string>& messages,  // 消息列表
    bool is_text = true,                        // 消息类型
    std::size_t batchSize = 500,               // 每批数量
    std::chrono::milliseconds batchInterval = std::chrono::milliseconds(50)
);
```

#### sendBatch - 动态生成器模式

```cpp
net::awaitable<bool> sendBatch(
    std::function<std::optional<std::string>()> generator,  // 生成器函数
    bool is_text = true,                                     // 消息类型
    std::size_t batchSize = 500,                            // 每批数量
    std::chrono::milliseconds batchInterval = std::chrono::milliseconds(50)
);
```

### 优势

1. **避免网络拥塞**：将大数据集分割成小批次，避免一次性推送造成的网络阻塞
2. **内存友好**：生成器版本支持边生成边发送，无需预先加载全部数据到内存
3. **可调节速率**：通过调整 batchSize 和 batchInterval 控制推送速率
4. **自动日志记录**：自动记录每批发送进度和总耗时
5. **错误处理**：中途失败立即返回 false，便于及时重试或降级

### 注意事项

1. 所有批次共享同一个连接，如果连接中断会立即返回失败
2. 批次间隔时间应结合网络延迟和客户端处理能力调整
3. 建议配合心跳机制检测连接状态
4. 对于实时性要求极高的场景，可减少批次间隔或增大批次大小

## 更多资源

- [Boost.Beast 官方文档](https://www.boost.org/doc/libs/release/libs/beast/)
- [Boost.Asio 异步编程指南](https://www.boost.org/doc/libs/release/libs/asio/)
- [WebSocket RFC 6455](https://datatracker.ietf.org/doc/html/rfc6455)
