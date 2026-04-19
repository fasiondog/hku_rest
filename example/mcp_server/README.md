# MCP Server 示例

本示例演示如何使用 hku_rest 框架实现 MCP (Model Context Protocol) Server。

### 协议版本

当前实现遵循 **MCP 2024-11-05** 规范，使用 **Streamable HTTP** 传输层。

### 传输方式：Streamable HTTP

MCP Server 采用 **Streamable HTTP** 作为标准传输层（符合 MCP 2024-11-05 规范）。

**核心特性：**

#### 1. 双向 Chunked Transfer 支持

**请求方向（Client → Server）**
- ✅ **自动处理 Chunked 请求**：框架自动解析 `Transfer-Encoding: chunked` 请求体
- ✅ **大参数支持**：客户端可以流式发送大型 JSON-RPC 请求（如大数据集、长文本）
- ✅ **透明解码**：服务端通过 [getReqData()](file:///Users/fasiondog/workspace/hku_rest/hikyuu/httpd/HttpHandle.h#L154-L154) 获取完整解码后的数据

**响应方向（Server → Client）**
- ✅ **动态选择传输模式**：根据 `Accept` 头自动决定使用 `Content-Length` 或 `chunked`
- ✅ **SSE 强制 Chunked**：流式响应必须使用分块传输编码
- ✅ **实时推送能力**：支持服务端主动推送进度、日志等实时数据

#### 2. 响应模式（根据 Accept 头动态切换）

**模式 A：标准 JSON 响应（短连接）**
```http
POST /mcp
Accept: application/json
```
- **传输编码**：`Content-Length`（小响应）或 `chunked`（大响应）
- **连接行为**：响应完成后立即关闭
- **适用场景**：快速查询、工具调用、配置获取

**模式 B：SSE 流式响应（长连接）**
```http
POST /mcp
Accept: text/event-stream
```
- **传输编码**：强制使用 `Transfer-Encoding: chunked`
- **连接行为**：保持连接打开，支持持续推送
- **心跳机制**：服务端每 15 秒发送 `: ping - <timestamp>`（SSE 注释格式）
- **适用场景**：长时间任务、进度监控、日志流、订阅通知

#### 3. Session 管理

- **Session ID 归属**：由客户端生成和管理（推荐使用 UUID）
- **传递方式**：通过 `X-Session-ID` 请求头传递
- **服务端职责**：验证有效性、维护状态、自动清理过期会话
- **初始化约束**：`initialize` 方法必须提供有效的 Session ID

#### 4. 混合使用场景

同一个 Session 可以在不同请求中使用不同模式：
```python
# 请求 1: 初始化（标准 JSON）
POST /mcp
Accept: application/json
X-Session-ID: abc-123

# 请求 2: 查询工具列表（标准 JSON）
POST /mcp
Accept: application/json
X-Session-ID: abc-123

# 请求 3: 执行长时间任务（SSE 流式）
POST /mcp
Accept: text/event-stream
X-Session-ID: abc-123

# 请求 4: 再次查询（标准 JSON）
POST /mcp
Accept: application/json
X-Session-ID: abc-123
```

### 快速开始

### 1. 构建项目

在项目根目录执行：

```bash
xmake build mcp_server
```

### 2. 运行服务器

**方式一：使用快速启动脚本**

```bash
cd example/mcp_server
./run.sh
```

**方式二：手动运行**

```bash
cd build/release/macosx/arm64/lib
./mcp_server
```

或使用自定义配置文件：

```bash
./mcp_server path/to/config.ini
```

### 3. 测试服务器

服务器启动后会监听 `http://0.0.0.0:8080`，可用端点：
- **健康检查**: `http://localhost:8080/health`
- **MCP 端点**: `http://localhost:8080/mcp`

## 使用示例

### 使用 Python 测试客户端

``bash
# 安装依赖
pip install requests

# 运行完整测试
python test_mcp.py
```

测试内容包括：
- ✅ Initialize 初始化
- ✅ Tools List 列出工具
- ✅ Tools Call 调用工具（计算器、时间、天气）
- ✅ Resources List 列出资源
- ✅ Resources Read 读取资源
- ✅ Prompts List 列出提示词
- ✅ Prompts Get 获取提示词
- ✅ 错误处理测试

### 使用 curl 直接测试

#### 1. 健康检查

```bash
curl -s http://localhost:8080/health | python -m json.tool
```

响应：
```json
{
    "data": {
        "message": "MCP Server is running",
        "status": "ok"
    },
    "ret": 0
}
```

#### 2. 初始化连接

``bash
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "initialize",
    "params": {
      "protocolVersion": "2024-11-05",
      "capabilities": {},
      "clientInfo": {
        "name": "test-client",
        "version": "1.0.0"
      }
    },
    "id": 1
  }' | python -m json.tool
```

#### 3. 列出可用工具

```bash
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/list",
    "params": {},
    "id": 2
  }' | python -m json.tool
```

#### 4. 调用计算器工具

```bash
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "calculator",
      "arguments": {
        "expression": "2 + 2"
      }
    },
    "id": 3
  }' | python -m json.tool
```

响应：
```json
{
    "id": 3,
    "jsonrpc": "2.0",
    "result": {
        "content": [
            {
                "text": "Result: 4",
                "type": "text"
            }
        ]
    }
}
```

#### 5. 获取当前时间

```bash
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "get_current_time",
      "arguments": {}
    },
    "id": 4
  }' | python -m json.tool
```

#### 6. 查询天气（模拟）

```bash
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "get_weather",
      "arguments": {
        "location": "Beijing"
      }
    },
    "id": 5
  }' | python -m json.tool
```

#### 7. 列出资源

```bash
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "resources/list",
    "params": {},
    "id": 6
  }' | python -m json.tool
```

#### 8. 读取资源

```bash
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "resources/read",
    "params": {
      "uri": "doc://getting-started"
    },
    "id": 7
  }' | python -m json.tool
```

#### 9. 列出提示词

```bash
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "prompts/list",
    "params": {},
    "id": 8
  }' | python -m json.tool
```

#### 10. 获取提示词

```bash
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "prompts/get",
    "params": {
      "name": "code_review",
      "arguments": {
        "language": "python",
        "code": "def hello():\n    print('world')"
      }
    },
    "id": 9
  }' | python -m json.tool
```

## Session 支持

MCP Server 支持会话管理，**Session ID 由客户端生成和管理**。

### Session 工作流程

1. **客户端生成 Session ID**：使用 UUID 或其他唯一标识符
2. **在请求中传递 Session ID**：通过 `X-Session-ID` 请求头或 Cookie
3. **服务端验证和存储**：服务端验证会话有效性，存储会话元数据
4. **会话注销**：客户端可主动注销会话

### 使用示例

#### Python 客户端

``python
import uuid
import requests

# 客户端生成 Session ID
session_id = str(uuid.uuid4())

headers = {
    "Content-Type": "application/json",
    "X-Session-ID": session_id  # 在请求头中传递
}

# 初始化时必须提供 Session ID
response = requests.post(
    "http://localhost:8080/mcp",
    json={
        "jsonrpc": "2.0",
        "method": "initialize",
        "params": {...},
        "id": 1
    },
    headers=headers
)
```

#### curl 示例

``bash
# 生成 Session ID（可以使用任何唯一字符串）
SESSION_ID=$(uuidgen)

# 初始化
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -H "X-Session-ID: $SESSION_ID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "initialize",
    "params": {
      "protocolVersion": "2024-11-05",
      "capabilities": {},
      "clientInfo": {"name": "test-client", "version": "1.0.0"}
    },
    "id": 1
  }'

# 后续请求使用相同的 Session ID
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -H "X-Session-ID: $SESSION_ID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/list",
    "params": {},
    "id": 2
  }'
```

### Session 相关方法

#### session/info

获取当前会话信息

**响应**:
``json
{
  "session_id": "...",
  "client_info": "127.0.0.1",
  "created_at": 1234567890,
  "last_active": 1234567890,
  "metadata": {...}
}
```

#### session/set_metadata

设置会话元数据

**参数**:
- `key` (string): 元数据键
- `value` (any): 元数据值

**示例**:
```json
{
  "jsonrpc": "2.0",
  "method": "session/set_metadata",
  "params": {
    "key": "user_preference",
    "value": {"theme": "dark", "language": "zh-CN"}
  },
  "id": 11
}
```

#### session/unregister

注销当前会话

**响应**:
``json
{
  "status": "success",
  "message": "Session unregistered successfully"
}
```

### Session 特性

- ✅ **客户端控制**：Session ID 由客户端生成，服务端仅验证和存储
- ✅ **自动过期**：默认 1 小时无活动自动过期
- ✅ **元数据存储**：可存储任意 JSON 格式的会话数据
- ✅ **历史记录**：自动记录工具调用历史（最多 100 条）
- ✅ **线程安全**：使用读写锁保证并发安全
- ✅ **容量限制**：默认最多 10000 个并发会话

---

## SSE (Server-Sent Events) 支持

MCP Server 支持 SSE 实时推送功能，用于向客户端推送异步事件和进度更新。

### SSE 端点

- **SSE 端点**: `http://localhost:8080/sse`

### 建立 SSE 连接

``bash
curl -N http://localhost:8080/sse \
  -H "X-Session-ID: your-session-id"
```

或使用 Python：

``python
import requests

headers = {
    "X-Session-ID": "your-session-id",
    "Accept": "text/event-stream"
}

response = requests.get(
    "http://localhost:8080/sse",
    headers=headers,
    stream=True
)

for line in response.iter_lines():
    if line:
        print(line.decode('utf-8'))
```

### SSE 事件类型

#### connected

连接建立时触发

**数据格式**:
``json
{
  "type": "connection_established",
  "session_id": "...",
  "timestamp": 1234567890
}
```

#### progress

任务进度更新

**数据格式**:
``json
{
  "task_id": "task_123",
  "progress": 50,
  "message": "Processing... 50% complete",
  "timestamp": 1234567890,
  "data": {
    "current_step": 5,
    "total_steps": 10
  }
}
```

#### disconnected

连接关闭时触发

**数据格式**:
``json
{
  "type": "connection_closed",
  "reason": "server_shutdown",
  "total_messages": 100
}
```

### SSE 与 JSON-RPC 协同工作流程

SSE 用于实时推送，JSON-RPC 用于请求/响应，两者通过 Session ID 关联：

```
客户端 A (SSE)              服务端                  客户端 B (JSON-RPC)
     |                       |                           |
     | GET /sse              |                           |
     | X-Session-ID: abc     |                           |
     |---------------------->|                           |
     | 建立 SSE 连接          |                           |
     |<----------------------|                           |
     | event: connected      |                           |
     |                       |                           |
     |                       | POST /mcp                 |
     |                       | {method: "tools/call"}    |
     |                       |<--------------------------|
     |                       | 执行任务...               |
     |                       | pushProgress(10%)         |
     | event: progress       |                           |
     | data: {...}           |                           |
     |<----------------------|                           |
     |                       | pushProgress(50%)         |
     | event: progress       |                           |
     | data: {...}           |                           |
     |<----------------------|                           |
     |                       | pushProgress(100%)        |
     | event: progress       |                           |
     | data: {...}           |                           |
     |<----------------------|                           |
     |                       | 返回结果                  |
     |                       |-------------------------->|
     |                       | {result: {...}}           |
```

**关键点**：
- SSE 和 JSON-RPC 可以来自不同的客户端连接，只要使用相同的 Session ID
- SSE 是单向推送（服务器 → 客户端），不能发送请求
- JSON-RPC 是双向通信（请求/响应）
- 建议在调用耗时较长的工具前先建立 SSE 连接

### 注意事项

- ✅ SSE 连接是单向的（服务器 → 客户端），不能用于发送请求
- ✅ 每个 Session 可以有多个并发的 SSE 连接
- ✅ 建议在调用耗时较长的工具前先建立 SSE 连接
- ✅ 客户端应处理重连逻辑（网络中断等异常情况）
- ⚠️ 浏览器原生 `EventSource` 不支持自定义请求头，需通过 URL 参数传递 Session ID

### 心跳机制（符合 MCP 协议）

MCP Server 的 SSE 端点实现了符合 MCP 协议规范的心跳机制：

**心跳格式**：
```
: ping - 2026-04-20 01:47:38
```

**特性**：
- 🕐 **间隔**：每 15 秒发送一次（MCP 协议推荐值）
- 📝 **格式**：使用 SSE 注释（以 `:` 开头），不会被解析为事件
- 🔍 **用途**：保持连接活跃，检测客户端是否仍然在线
- ❌ **无需响应**：客户端不需要回复 pong（SSE 单向推送特性）
- 🛡️ **断开检测**：如果写入失败，服务端会自动关闭连接

**测试心跳**：
```bash
python test_sse_heartbeat.py
```

输出示例：
```
[15.0s] ❤️  Heartbeat #1: : ping - 2026-04-20 01:47:38
[30.1s] ❤️  Heartbeat #2: : ping - 2026-04-20 01:47:53
[45.1s] ❤️  Heartbeat #3: : ping - 2026-04-20 01:48:08

✅ Successfully received 3 heartbeats
   Average interval: ~15.0 seconds
```

---

## MCP 协议方法

### 核心方法

| 方法 | 描述 | 参数 |
|------|------|------|
| `initialize` | 初始化连接 | protocolVersion, capabilities, clientInfo |
| `initialized` | 初始化完成通知（无响应） | - |

### Tools 相关

| 方法 | 描述 | 参数 |
|------|------|------|
| `tools/list` | 列出可用工具 | - |
| `tools/call` | 调用工具 | name, arguments |

### Resources 相关

| 方法 | 描述 | 参数 |
|------|------|------|
| `resources/list` | 列出可用资源 | - |
| `resources/read` | 读取资源内容 | uri |

### Prompts 相关

| 方法 | 描述 | 参数 |
|------|------|------|
| `prompts/list` | 列出可用提示词 | - |
| `prompts/get` | 获取提示词模板 | name, arguments |

## 内置工具示例

### 1. calculator

执行基本算术计算

**参数**:
- `expression` (string): 数学表达式

**示例**:
```json
{
  "name": "calculator",
  "arguments": {
    "expression": "2 + 2"
  }
}
```

**响应**:
``json
{
  "content": [
    {
      "type": "text",
      "text": "Result: 4"
    }
  ]
}
```

### 2. get_current_time

获取当前日期和时间

**参数**:
- `format` (string, 可选): 日期时间格式字符串，默认 `%Y-%m-%d %H:%M:%S`

**示例**:
```json
{
  "name": "get_current_time",
  "arguments": {
    "format": "%Y-%m-%d"
  }
}
```

### 3. get_weather

获取天气信息（模拟数据）

**参数**:
- `location` (string): 城市名称或坐标

**示例**:
```json
{
  "name": "get_weather",
  "arguments": {
    "location": "Beijing"
  }
}
```

**响应**:
``json
{
  "content": [
    {
      "type": "text",
      "text": "Weather in Beijing: Temperature 22°C, Condition: Sunny, Humidity: 65%"
    }
  ]
}
```

## 扩展开发

### 添加新工具

在 `McpHandle.h` 中：

1. 在 `handleToolsList()` 中添加工具定义
2. 在 `handleToolsCall()` 中添加路由
3. 实现具体的工具执行函数

``cpp
// 1. 添加工具定义
nlohmann::json handleToolsList(const nlohmann::json& params) {
    nlohmann::json tools = nlohmann::json::array();
    
    tools.push_back({
        {"name", "my_tool"},
        {"description", "My custom tool"},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {...}},
            {"required", {...}}
        }}
    });
    
    return {{"tools", tools}};
}

// 2. 添加路由
net::awaitable<nlohmann::json> handleToolsCall(const nlohmann::json& params) {
    std::string tool_name = params.value("name", "");
    
    if (tool_name == "my_tool") {
        co_return executeMyTool(arguments);
    }
    // ... 其他工具
}

// 3. 实现执行函数
nlohmann::json executeMyTool(const nlohmann::json& arguments) {
    // 实现逻辑
    nlohmann::json response;
    response["content"] = nlohmann::json::array({
        {{"type", "text"}, {"text", "Result"}}
    });
    return response;
}
```

### 添加新资源

``cpp
// 1. 在 handleResourcesList() 中添加资源定义
resources.push_back({
    {"uri", "doc://my-resource"},
    {"name", "My Resource"},
    {"description", "Description"},
    {"mimeType", "text/markdown"}
});

// 2. 在 handleResourcesRead() 中添加路由
if (uri == "doc://my-resource") {
    co_return readMyResource();
}

// 3. 实现读取函数
nlohmann::json readMyResource() {
    std::string content = "Your content here";
    
    nlohmann::json response;
    response["contents"] = nlohmann::json::array({
        {
            {"uri", "doc://my-resource"},
            {"mimeType", "text/markdown"},
            {"text", content}
        }
    });
    
    return response;
}
```

### 添加新提示词

``cpp
// 1. 在 handlePromptsList() 中添加提示词定义
prompts.push_back({
    {"name", "my_prompt"},
    {"description", "My prompt template"},
    {"arguments", nlohmann::json::array({
        {
            {"name", "param1"},
            {"description", "Parameter description"},
            {"required", true}
        }
    })}
});

// 2. 在 handlePromptsGet() 中添加路由
if (prompt_name == "my_prompt") {
    co_return getMyPrompt(arguments);
}

// 3. 实现提示词生成函数
nlohmann::json getMyPrompt(const nlohmann::json& arguments) {
    std::string param1 = arguments.value("param1", "");
    
    std::string prompt = fmt::format("Your prompt with {}", param1);
    
    nlohmann::json response;
    response["description"] = "My prompt";
    response["messages"] = nlohmann::json::array({
        {
            {"role", "user"},
            {"content", {
                {"type", "text"},
                {"text", prompt}
            }}
        }
    });
    
    return response;
}
```

## 配置说明

编辑 `mcp_server.ini` 文件：

``ini
[deploy]
current = local

[local]
sqlite_enable = false
mysql_enable = false
```

**注意**: 当前版本不需要数据库支持，保持 `sqlite_enable` 和 `mysql_enable` 为 `false` 即可。

## 注意事项

1. **JSON-RPC 版本**: 必须使用 `"jsonrpc": "2.0"`
2. **请求 ID**: 每个请求必须有唯一的 `id` 字段
3. **错误处理**: 服务器会返回标准的 JSON-RPC 错误响应
4. **内容类型**: 所有请求必须设置 `Content-Type: application/json`
5. **HTTP 方法**: MCP 端点只接受 POST 请求
6. **响应格式**: 响应严格遵循 JSON-RPC 2.0 规范，不会被 RestHandle 的自动封装影响

## 性能优化建议

1. **连接复用**: 使用 HTTP Keep-Alive 减少连接建立开销
2. **批量操作**: 对于多个工具调用，考虑合并请求
3. **缓存策略**: 对频繁访问的资源实现缓存
4. **异步处理**: 利用协程特性实现非阻塞 I/O

## 故障排查

### 问题 1: 连接拒绝

**症状**: 无法连接到服务器

**解决**:
- 检查服务器是否正在运行: `ps aux | grep mcp_server`
- 确认端口配置正确（默认 8080）
- 检查防火墙设置

### 问题 2: JSON 解析错误

**症状**: 返回 `-32700 Parse error`

**解决**:
- 检查 JSON 格式是否正确
- 确认 Content-Type 设置为 `application/json`
- 验证请求体不为空

### 问题 3: 方法未找到

**症状**: 返回 `-32601 Method not found`

**解决**:
- 检查方法名拼写
- 确认使用的是 MCP 支持的方法
- 查看服务器日志了解详细信息

### 问题 4: 服务器启动失败

**症状**: 服务器无法启动或立即退出

**解决**:
- 检查配置文件格式是否正确
- 确认端口未被占用: `lsof -i :8080`
- 查看日志文件: `/tmp/mcp_server.log`

## 参考资料

- [MCP 官方规范](https://modelcontextprotocol.io/)
- [JSON-RPC 2.0 规范](https://www.jsonrpc.org/specification)
- [hku_rest 项目文档](../../README.md)

## License

Copyright (c) 2024 hikyuu.org
