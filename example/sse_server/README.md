# SSE (Server-Sent Events) 服务器示例

本示例演示如何使用 hku_rest HTTP Server 实现 SSE (Server-Sent Events) 实时数据推送功能。

## 什么是 SSE？

SSE 是一种基于 HTTP 的服务器推送技术，允许服务器向客户端单向推送实时更新。与 WebSocket 不同，SSE 是单向的（仅服务器到客户端），但具有以下优势：

- ✅ 基于标准 HTTP，无需特殊协议
- ✅ 自动重连支持
- ✅ 简单易用，浏览器原生支持
- ✅ 适合日志流、行情推送、进度通知等场景

## 项目结构

```
sse_server/
├── SseHandle.h          # SSE 推送处理器实现
├── SseService.h         # SSE 服务注册
├── main.cpp             # 服务器主程序
├── sse_server.ini       # 配置文件
├── xmake.lua            # 构建配置
├── test_sse.py          # Python 测试脚本
└── README.md            # 本文档
```

## 快速开始

### 1. 构建项目

```bash
cd /Users/fasiondog/workspace/hku_rest
xmake build sse_server
```

### 2. 运行服务器

```bash
xmake r sse_server
```

服务器将在 `http://0.0.0.0:8081` 启动。

### 3. 测试 SSE 端点

#### 方法 1: 使用 curl

```bash
# 测试简单 SSE
curl -N http://localhost:8081/sse/simple

# 测试完整 SSE 流（带事件类型）
curl -N http://localhost:8081/sse/stream
```

> **注意**: `-N` 参数禁用 curl 的输出缓冲，确保实时显示推送数据。

#### 方法 2: 使用 Python 测试脚本

```bash
# 安装依赖
pip install requests

# 运行所有测试
python3 test_sse.py

# 或单独测试某个端点
python3 test_sse.py simple    # 简单 SSE
python3 test_sse.py stream    # 完整 SSE 流
python3 test_sse.py client    # SSEClient 类测试（支持断线重连）
```

#### 方法 3: 浏览器测试

在浏览器中打开 `test_client.html` 文件：

```bash
# macOS
open test_client.html

# Linux
xdg-open test_client.html

# Windows
start test_client.html
```

或者直接在浏览器控制台执行：

```javascript
// 连接 SSE
const eventSource = new EventSource('http://localhost:8081/sse/stream');

// 监听特定事件
eventSource.addEventListener('quote', (event) => {
    const data = JSON.parse(event.data);
    console.log('Quote:', data);
});

// 监听连接建立
eventSource.onopen = () => {
    console.log('SSE connection established');
};

// 监听错误
eventSource.onerror = (error) => {
    console.error('SSE error:', error);
};

// 关闭连接
// eventSource.close();
```

## API 端点

### 1. `/sse/simple` - 简单 SSE 推送

**描述**: 基础的 SSE 推送示例，每秒推送一条简单消息。

**请求方式**: GET

**响应头**:
```
Content-Type: text/event-stream
Cache-Control: no-cache
Connection: keep-alive
```

**消息格式**:
```
data: Message 1

data: Message 2

...
```

**示例输出**:
```
data: Message 1

data: Message 2

data: Message 3
```

---

### 2. `/sse/stream` - 完整 SSE 流

**描述**: 完整的 SSE 推送示例，模拟实时行情数据推送，包含事件类型和消息 ID。

**请求方式**: GET

**响应头**:
```
Content-Type: text/event-stream
Cache-Control: no-cache
Connection: keep-alive
Access-Control-Allow-Origin: *
```

**消息格式**:
```
event: connected
id: 0
data: SSE connection established

event: quote
id: 1
data: {"timestamp": 1234567890, "price": 150.25, "change": 2.35, "symbol": "AAPL"}

event: completed
id: 49
data: Push completed, total messages: 50
```

**事件类型**:
- `connected`: 连接建立
- `quote`: 行情数据推送
- `completed`: 推送完成

**数据字段**:
- `timestamp`: Unix 时间戳
- `price`: 价格
- `change`: 涨跌幅
- `symbol`: 股票代码

**示例输出**:
```
--- Message #1 ---
  Event: connected
  ID:    0
  Data:  SSE connection established

--- Message #2 ---
  Event: quote
  ID:    1
  Data:  {
      "timestamp": 1719123456,
      "price": 152.35,
      "change": 2.35,
      "symbol": "AAPL"
  }
  📈 AAPL: $152.35 (+2.35)

...

--- Message #51 ---
  Event: completed
  ID:    49
  Data:  Push completed, total messages: 50
```

## 技术实现

### 核心原理

SSE 基于 HTTP 分块传输编码（Chunked Transfer Encoding）实现：

1. **设置响应头**: `Content-Type: text/event-stream`
2. **启用分块传输**: 调用 `enableChunkedTransfer()`
3. **推送数据块**: 使用 `writeChunk()` 发送 SSE 格式消息
4. **结束传输**: 调用 `finishChunkedTransfer()`

### SSE 消息格式

每条 SSE 消息由以下字段组成（以空行结尾）：

```
event: <事件名称>     # 可选
id: <消息ID>          # 可选，用于断线重连
data: <数据内容>      # 必需

```

> **重要**: 每条消息必须以 `\n\n`（双换行符）结尾。

### 代码示例

``cpp
class SseHandle : public RestHandle {
    REST_HANDLE_IMP(SseHandle)
    
    virtual net::awaitable<VoidBizResult> run() override {
        // 1. 设置 SSE 响应头
        setResHeader("Content-Type", "text/event-stream");
        setResHeader("Cache-Control", "no-cache");
        setResHeader("Connection", "keep-alive");
        
        // 2. 启用分块传输
        enableChunkedTransfer();
        
        // 3. 推送消息
        for (int i = 0; i < 10; i++) {
            std::string msg = "data: Message " + std::to_string(i) + "\n\n";
            co_await writeChunk(msg);
            
            // 延迟 1 秒
            co_await sleep_for(std::chrono::seconds(1));
        }
        
        // 4. 完成传输
        co_await finishChunkedTransfer();
        
        co_return BIZ_OK;
    }
};
```

## 生产环境建议

### 1. 超时控制

SSE 连接可能长时间保持，建议设置合理的超时时间：

```cpp
// 在 HttpServer 配置中设置
server.set_connection_timeout(300000);  // 5 分钟
```

### 2. 心跳机制

定期发送注释消息保持连接活跃：

```cpp
// SSE 注释消息（以冒号开头）
std::string heartbeat = ": heartbeat\n\n";
co_await writeChunk(heartbeat);
```

### 3. 断线重连

客户端应实现自动重连，并支持 `Last-Event-ID` 头部：

```python
headers = {'Last-Event-ID': last_event_id}
response = requests.get(url, headers=headers, stream=True)
```

### 4. 并发控制

对于大量 SSE 连接，使用连接管理器限制并发数：

```cpp
server.set_max_concurrent_connections(1000, 30000);
```

### 5. 监控指标

集成 Prometheus 监控 SSE 连接数和消息推送速率：

```cpp
auto conn_monitor = std::make_shared<ConnectionMonitor>(
    server.get_connection_manager(), 1000
);
conn_monitor->startSampling();
```

## 性能测试

### 单连接测试

```bash
time curl -N http://localhost:8081/sse/stream > /dev/null
```

### 多连接并发测试

```bash
# 同时开启 10 个 SSE 连接
for i in {1..10}; do
    curl -N http://localhost:8081/sse/stream > /dev/null &
done
wait
```

### 压力测试

使用 [wrk](https://github.com/wg/wrk) 进行 HTTP 基准测试：

```bash
wrk -t4 -c100 -d30s http://localhost:8081/sse/stream
```

## 常见问题

### Q1: 为什么 curl 不实时显示数据？

**A**: curl 默认会缓冲输出。使用 `-N` 参数禁用缓冲：

```bash
curl -N http://localhost:8081/sse/stream
```

### Q2: 如何实现双向通信？

**A**: SSE 是单向的（服务器→客户端）。如需双向通信，请使用 WebSocket：

```cpp
// 参考 example/websocket_server 示例
server.enableWebSocket(true);
server.WS<EchoWsHandle>("/ws");
```

### Q3: 如何处理客户端断开？

**A**: `writeChunk()` 会在客户端断开时返回 `false` 或抛出异常：

```cpp
bool success = co_await writeChunk(msg);
if (!success) {
    HKU_WARN("Client disconnected");
    break;
}
```

### Q4: SSE vs WebSocket 如何选择？

| 特性 | SSE | WebSocket |
|------|-----|-----------|
| 通信方向 | 单向（服务器→客户端） | 双向 |
| 协议 | HTTP | WebSocket |
| 浏览器支持 | 原生 EventSource API | WebSocket API |
| 自动重连 | ✅ 内置支持 | ❌ 需手动实现 |
| 适用场景 | 日志、行情、通知 | 聊天、游戏、实时协作 |

## 参考资料

- [MDN: Using Server-Sent Events](https://developer.mozilla.org/en-US/docs/Web/API/Server-sent_events/Using_server-sent_events)
- [HTML5 SSE Specification](https://html.spec.whatwg.org/multipage/server-sent-events.html)
- [Boost.Beast Documentation](https://www.boost.org/doc/libs/release/libs/beast/)

## 许可证

Copyright (c) 2024 hikyuu.org
