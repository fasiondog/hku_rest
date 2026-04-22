# MCP Server Example

这是一个基于 HKU_REST 框架的 Model Context Protocol (MCP) 服务器示例实现。

## 功能特性

- ✅ 完整的 MCP 协议支持（基于 JSON-RPC 2.0）
- ✅ Session 管理（会话生命周期、超时清理）
- ✅ SSE（Server-Sent Events）流式响应
- ✅ SSE 心跳机制（保持长连接活跃）
- ✅ 工具调用（Tools）
- ✅ 资源访问（Resources）
- ✅ 提示词模板（Prompts）
- ✅ **分页查询（Pagination）** - 基于游标的分页机制

## 快速开始

### 构建

```
cd /path/to/hku_rest
xmake build mcp_server
```

### 运行

```
xmake r mcp_server
```

服务器将在 `http://localhost:8080` 启动，MCP 端点为 `/mcp`。

## 配置

编辑 `mcp_server.ini` 文件可以自定义服务器行为：

```
[mcp]
sse_heartbeat_enabled = true
sse_heartbeat_interval = 30
```

## API 端点

- `POST /mcp` - MCP 协议端点（JSON-RPC 2.0）
- `GET /health` - 健康检查端点

## 支持的 MCP 方法

### 核心方法

- `initialize` - 初始化 MCP 连接
- `initialized` - 客户端初始化完成通知
- `ping` - 连接健康检查

### 工具（Tools）

- `tools/list` - 列出所有可用工具
- `tools/call` - 调用指定工具

**内置工具：**
1. `calculator` - 执行基本算术计算
2. `get_current_time` - 获取当前日期和时间
3. `get_weather` - 查询天气信息（模拟）
4. `get_session_history` - 获取会话历史记录
5. `long_running_task` - 长时间运行任务（演示进度推送）
6. **`query_paginated_data`** - **分页数据查询（演示 MCP 分页机制）**

### 资源（Resources）

- `resources/list` - 列出可用资源
- `resources/read` - 读取指定资源

### 提示词（Prompts）

- `prompts/list` - 列出可用提示词模板
- `prompts/get` - 获取指定提示词模板

### 会话管理

- `session/info` - 获取会话信息
- `session/set_metadata` - 设置会话元数据
- `session/unregister` - 注销会话

## 分页功能详解

### 概述

MCP 协议支持基于游标（cursor）的分页机制，用于处理大型数据集。本示例通过 `query_paginated_data` 工具演示了如何实现符合 MCP 规范的分页功能。

### 分页模型

- **游标机制**：使用不透明的字符串标记表示结果集中的位置
- **页面大小**：由客户端指定（1-100），默认 10
- **下一页指示**：通过 `nextCursor` 字段告知是否有更多数据
- **无效游标处理**：返回错误码 -32602（Invalid params）

### 请求格式

首次请求（无游标）：
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "query_paginated_data",
    "arguments": {
      "page_size": 10
    }
  },
  "id": 1
}
```

后续请求（携带游标）：
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "query_paginated_data",
    "arguments": {
      "page_size": 10,
      "cursor": "MTA="
    }
  },
  "id": 2
}
```

### 响应格式

有下一页时：
```json
{
  "jsonrpc": "2.0",
  "result": {
    "content": [
      {
        "type": "text",
        "text": "Retrieved 10 items (total: 250, page size: 10)"
      }
    ],
    "items": [...],
    "pagination": {
      "total_items": 250,
      "current_page_start": 0,
      "current_page_end": 9,
      "returned_count": 10,
      "has_more": true
    },
    "nextCursor": "MTA="
  },
  "id": 1
}
```

最后一页（无更多数据）：
```json
{
  "jsonrpc": "2.0",
  "result": {
    "content": [...],
    "items": [...],
    "pagination": {
      "total_items": 250,
      "current_page_start": 240,
      "current_page_end": 249,
      "returned_count": 10,
      "has_more": false
    }
  },
  "id": 5
}
```

### 测试分页功能

运行提供的测试脚本：

```bash
python test_pagination.py
```

该脚本演示了：
1. 多页数据遍历
2. 无效游标处理
3. 自定义页面大小
4. 分页元数据查看

### 实现要点

在 `McpHandle.h` 中，分页功能的关键实现包括：

1. **工具定义**（`handleToolsList` 方法）：
```cpp
tools.push_back({{"name", "query_paginated_data"},
                 {"description", "Query a large dataset with pagination support..."},
                 {"inputSchema", {...}}});
```

2. **工具路由**（`handleToolsCall` 方法）：
```cpp
else if (tool_name == "query_paginated_data") {
    co_return executeQueryPaginatedData(arguments, session_id);
}
```

3. **分页逻辑**（`executeQueryPaginatedData` 方法）：
   - 解析和验证游标
   - 计算数据范围
   - 生成下一页游标
   - 构建包含分页元数据的响应

4. **游标编解码**：
   - `encodeBase64()` - 将偏移量编码为 Base64 字符串
   - `decodeBase64()` - 解码游标获取偏移量

### 最佳实践

1. **游标不透明性**：客户端不应假设游标格式或尝试解析它
2. **稳定性**：相同的游标应始终返回相同的结果
3. **错误处理**：对无效游标返回明确的错误信息
4. **性能优化**：避免一次性加载大量数据到内存
5. **限制页面大小**：设置合理的最大值防止滥用

## Session 管理

### Session 生命周期

- Session 与 HTTP 连接解耦
- 默认超时时间：3600 秒（1 小时）
- 最大 Session 数：10000
- 后台线程定期清理过期 Session（每 60 秒）

### Session ID 传递

使用标准 HTTP 头字段 `Mcp-Session-Id`：

```
Mcp-Session-Id: 53c4c0fc-56e4-4e11-8a38-5d35d50dd1f6
```

### Session 清理

Session 仅在以下情况被清理：
1. 客户端主动调用 `session/unregister`
2. 超过空闲超时时间（默认 3600 秒）

**注意**：HTTP 连接断开不会立即清理 Session，支持客户端重连。

## SSE 流式响应

当客户端在 `Accept` 头中包含 `text/event-stream` 时，服务器启用 SSE 模式：

```
Accept: text/event-stream
```

SSE 特性：
- 实时推送工具执行结果
- 定期发送心跳（默认 30 秒）
- 支持长时间运行任务的进度更新

## 示例代码

### Python 客户端示例

``python
import requests
import json

class MCPClient:
    def __init__(self, base_url="http://localhost:8080"):
        self.base_url = base_url
        self.session_id = None
    
    def send_request(self, method, params=None):
        headers = {"Content-Type": "application/json"}
        if self.session_id:
            headers["Mcp-Session-Id"] = self.session_id
        
        request = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params or {},
            "id": 1
        }
        
        response = requests.post(
            f"{self.base_url}/mcp",
            headers=headers,
            json=request
        )
        
        # 保存 session_id
        if "Mcp-Session-Id" in response.headers:
            self.session_id = response.headers["Mcp-Session-Id"]
        
        return response.json()

# 使用示例
client = MCPClient()

# 初始化
result = client.send_request("initialize", {
    "protocolVersion": "2024-11-05",
    "capabilities": {},
    "clientInfo": {"name": "my-client", "version": "1.0.0"}
})

# 调用分页查询工具
page1 = client.send_request("tools/call", {
    "name": "query_paginated_data",
    "arguments": {"page_size": 10}
})

# 使用游标获取下一页
if "nextCursor" in page1["result"]:
    page2 = client.send_request("tools/call", {
        "name": "query_paginated_data",
        "arguments": {
            "page_size": 10,
            "cursor": page1["result"]["nextCursor"]
        }
    })
```

## 架构设计

```
┌─────────────┐
│  MCP Client │
└──────┬──────┘
       │ HTTP POST /mcp
       │ Mcp-Session-Id: xxx
       ▼
┌──────────────────┐
│   HttpServer     │
│  (Beast/Asio)    │
└──────┬───────────┘
       │
       ▼
┌──────────────────┐
│   McpHandle      │
│  - Session Mgmt  │
│  - Method Router │
│  - SSE Support   │
└──────┬───────────┘
       │
       ├─► Tools (calculator, weather, pagination...)
       ├─► Resources (documents, guides)
       └─► Prompts (code review, docs generation)
```

## 参考资源

- [Model Context Protocol Specification](https://modelcontextprotocol.io/)
- [JSON-RPC 2.0 Specification](https://www.jsonrpc.org/specification)
- [HKU_REST Documentation](../../README.md)

## 许可证

Copyright (c) 2024 hikyuu.org
