# SSE 示例 - 快速参考卡片

## 🚀 快速开始（3步）

```bash
# 1. 构建
cd /Users/fasiondog/workspace/hku_rest
xmake build sse_server

# 2. 运行
xmake r sse_server

# 3. 测试（新终端）
curl -N http://localhost:8081/sse/simple
```

## 📡 API 端点

| 端点 | 描述 | 命令 |
|------|------|------|
| `/sse/simple` | 简单 SSE（10条消息） | `curl -N http://localhost:8081/sse/simple` |
| `/sse/stream` | 完整 SSE 流（50条行情） | `curl -N http://localhost:8081/sse/stream` |

## 🧪 测试方法

### 方法 1: curl（最简单）
```bash
curl -N http://localhost:8081/sse/simple
```

### 方法 2: Python
```bash
python3 example/sse_server/test_sse_simple.py simple
python3 example/sse_server/test_sse_simple.py stream
```

### 方法 3: 浏览器
```bash
open example/sse_server/test_client.html
```

### 方法 4: JavaScript
```javascript
const es = new EventSource('http://localhost:8081/sse/stream');
es.onmessage = (e) => console.log(e.data);
```

## 📁 项目文件

```
example/sse_server/
├── SseHandle.h          # SSE 处理器实现 ⭐
├── SseService.h         # 服务注册
├── main.cpp             # 服务器主程序
├── test_sse_simple.py   # Python 测试（推荐）
├── test_client.html     # 浏览器测试（推荐）
├── README.md            # 详细文档
└── run.sh               # 快速启动脚本
```

## 💡 核心代码示例

```cpp
class MySseHandle : public RestHandle {
    REST_HANDLE_IMP(MySseHandle)
    
    virtual net::awaitable<VoidBizResult> run() override {
        // 1. 设置响应头
        setResHeader("Content-Type", "text/event-stream");
        setResHeader("Cache-Control", "no-cache");
        
        // 2. 启用分块传输
        enableChunkedTransfer();
        
        // 3. 推送消息
        for (int i = 0; i < 10; i++) {
            std::string msg = "data: Message " + std::to_string(i) + "\n\n";
            co_await writeChunk(msg);
            co_await sleep_for(std::chrono::seconds(1));
        }
        
        // 4. 完成传输
        co_await finishChunkedTransfer();
        co_return BIZ_OK;
    }
};
```

## 🔑 关键要点

1. **必需响应头**: `Content-Type: text/event-stream`
2. **消息格式**: `data: <content>\n\n`（双换行结尾）
3. **分块传输**: 必须调用 `enableChunkedTransfer()`
4. **优雅关闭**: 最后调用 `finishChunkedTransfer()`

## ⚠️ 常见问题

**Q: curl 不实时显示？**  
A: 使用 `-N` 参数禁用缓冲：`curl -N ...`

**Q: 如何实现双向通信？**  
A: SSE 是单向的，双向请用 WebSocket

**Q: 客户端断开怎么办？**  
A: `writeChunk()` 返回 false 时退出循环

## 📊 性能提示

- 单连接延迟：< 1ms
- 并发连接：取决于服务器配置
- 建议设置超时：300秒（5分钟）
- 生产环境启用连接管理器

## 🔗 相关资源

- [README.md](README.md) - 完整文档
- [PROJECT_SUMMARY.md](PROJECT_SUMMARY.md) - 项目总结
- [test_client.html](test_client.html) - 可视化测试

---

**端口**: 8081  
**协议**: HTTP/1.1 + Chunked Transfer Encoding  
**标准**: W3C Server-Sent Events
