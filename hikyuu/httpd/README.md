# hkyuu HTTPD - 基于 Boost.Beast、C++20 协程和 TLS/SSL 的 HTTP 服务器

## 概述

httpd 是一个使用 Boost.Beast、Boost.Asio、**C++20 协程**和 **TLS/SSL**实现的高性能、安全的 HTTP/RESTful 服务器。

**核心特性**：
- ✅ **端到端协程支持** - 从网络 IO 到业务逻辑全链路协程化
- ✅ **非阻塞异步** - 所有操作都是异步的，不阻塞工作线程
- ✅ **TLS/SSL 加密** - 完整的 HTTPS 支持，基于 boost::asio::ssl
- ✅ **简洁的同步式代码** - 使用 co_await 编写异步代码，像同步一样简单
- ✅ **完全兼容原 httpd 接口** - 类名和接口名称保持一致

## 技术栈

- **C++20** - 使用现代 C++ 特性（协程）
- **Boost.Beast** - HTTP 协议处理
- **Boost.Asio** - 异步网络 IO + 协程支持
- **OpenSSL** - TLS/SSL 加密
- **nlohmann/json** - JSON 数据处理
- **fmt** - 格式化输出

## 编译要求

- **编译器**: GCC 10+ / Clang 13+ / MSVC 2022+ (必须支持 C++20 协程)
- **Boost**: 1.75+ (需要包含协程支持)
- **OpenSSL**: 1.1.1+ (用于 TLS/SSL)
- **构建系统**: xmake 或 CMake

## 快速开始

### 1. 创建 Handle（支持协程）

```cpp
#include <hikyuu/httpd/all.h>

using namespace hku;

class AsyncHandle : public HttpHandle {
    CLASS_LOGGER_IMP(AsyncHandle)
    
public:
    HTTP_HANDLE_IMP(AsyncHandle)
    
    net::awaitable<void> run() override {
        // 异步延迟 1 秒（非阻塞！）
        auto* ctx = static_cast<BeastContext*>(m_beast_context);
        ctx->timer.expires_after(std::chrono::seconds(1));
        co_await ctx->timer.async_wait(boost::asio::use_awaitable);
        
        res["message"] = "Async response after 1 second";
        setResStatus(200);
        co_return;
    }
};
```

### 2. 配置 HTTPS（可选）

```cpp
// 生成自签名证书
./generate_ssl_cert.sh . server

// 创建 HTTPS 服务器
HttpServer https_server("0.0.0.0", 8443);

// 配置 SSL/TLS
https_server.set_tls("server.pem", "", 0);
```

### 3. 注册路由

```cpp
class ApiService : public HttpService {
    HTTP_SERVICE_IMP(ApiService)
    
public:
    virtual void regHandle() override {
        GET<AsyncHandle>("/async");
    }
};
```

### 4. 启动服务器

```cpp
int main() {
    // HTTP 服务器
    HttpServer http_server("0.0.0.0", 8080);
    
    // HTTPS 服务器（可选）
    HttpServer https_server("0.0.0.0", 8443);
    https_server.set_tls("server.pem");
    
    // 注册服务
    ApiService service("/api");
    service.bind(&http_server);
    service.bind(&https_server);
    
    // 启动
    http_server.start();
    https_server.start();
    
    std::cout << "HTTP:  http://0.0.0.0:8080" << std::endl;
    std::cout << "HTTPS: https://0.0.0.0:8443" << std::endl;
    
    http_server.loop();
    https_server.loop();
    
    return 0;
}
```

### 4. 测试

```bash
curl http://localhost:8080/api/async
```

## 协程架构

### 完整的协程调用链

```
客户端请求
    ↓
Connection::readRequest() [协程]
    ↓ co_await http::async_read
    ↓
Connection::handleRequest() [协程]
    ↓ co_await handler(ctx)
    ↓
HttpHandle::operator()() [协程]
    ↓ co_await run()
    ↓
YourHandle::run() [协程] ← 你的业务逻辑
    ↓
co_return
```

### 关键点

1. **所有 IO 操作都是异步的** - 使用 `co_await` 等待
2. **不阻塞工作线程** - 等待时协程挂起，线程可以处理其他请求
3. **代码简洁易读** - 没有回调地狱，代码像同步一样直观

## 常见场景示例

### 1. 异步延迟响应

```cpp
net::awaitable<void> run() override {
    auto* ctx = static_cast<BeastContext*>(m_beast_context);
    
    // 延迟 2 秒响应
    ctx->timer.expires_after(std::chrono::seconds(2));
    co_await ctx->timer.async_wait(net::use_awaitable);
    
    res["data"] = "Delayed response";
    co_return;
}
```

### 2. 异步数据库查询（伪代码）

``cpp
net::awaitable<void> run() override {
    std::string user_id = req.value("user_id", "");
    
    // 假设有一个异步数据库驱动
    auto result = co_await db->query("SELECT * FROM users WHERE id = ?", user_id);
    
    res["user"] = result;
    co_return;
}
```

### 3. 并行执行多个任务

``cpp
net::awaitable<void> run() override {
    auto* ctx = static_cast<BeastContext*>(m_beast_context);
    
    // 创建多个异步任务
    auto task1 = some_async_operation1();
    auto task2 = some_async_operation2();
    auto task3 = some_async_operation3();
    
    // 并行等待所有任务完成
    co_await task1;
    co_await task2;
    co_await task3;
    
    res["results"] = {task1.get(), task2.get(), task3.get()};
    co_return;
}
```

### 4. 超时处理

``cpp
net::awaitable<void> run() override {
    auto* ctx = static_cast<BeastContext*>(m_beast_context);
    
    // 设置超时定时器
    const int timeout_ms = 5000;
    ctx->timer.expires_after(std::chrono::milliseconds(timeout_ms));
    auto timeout = ctx->timer.async_wait(net::use_awaitable);
    
    // 创建业务操作的 future
    auto operation = do_something_async();
    
    // 等待操作完成或超时
    if (timeout.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        throw HttpError("Timeout", 504, "Operation timed out");
    }
    
    auto result = co_await operation;
    res["data"] = result;
    co_return;
}
```

### 5. 错误处理

``cpp
net::awaitable<void> run() override {
    try {
        // 业务逻辑
        auto data = co_await fetch_data();
        res["data"] = data;
        
    } catch (HttpError& e) {
        // HTTP 错误会被框架自动捕获
        throw;  // 重新抛出
        
    } catch (std::exception& e) {
        // 其他异常
        CLS_ERROR("Error in handler: {}", e.what());
        throw HttpError("InternalError", 500, e.what());
    }
    
    co_return;
}
```

## API 参考

### HttpHandle

基础 HTTP 处理器，所有 Handle 的基类。

#### 构造函数
```cpp
HTTP_HANDLE_IMP(YourClass)  // 展开为：explicit YourClass(void* beast_context)
```

#### 核心方法
- `virtual net::awaitable<void> run()` - **必须实现**的请求处理方法（协程）
- `virtual void before_run()` - 前处理（可选，同步）
- `virtual void after_run()` - 后处理（可选，同步）

#### 请求访问
- `std::string getReqUrl()` - 获取请求 URL
- `std::string getReqHeader(const char* name)` - 获取请求头
- `std::string getReqData()` - 获取请求体
- `json getReqJson()` - 获取 JSON 格式请求体
- `bool haveQueryParams()` - 判断是否有查询参数
- `bool getQueryParams(QueryParams& params)` - 获取查询参数

#### 响应设置
- `void setResStatus(uint16_t status)` - 设置状态码
- `void setResHeader(const char* key, const char* val)` - 设置响应头
- `void setResData(const std::string& content)` - 设置响应体
- `void setResData(const json& data)` - 设置 JSON 响应

### BeastContext

协程上下文，包含定时器等工具。

```cpp
struct BeastContext {
    http::request<http::string_body> req;
    http::response<http::string_body> res;
    tcp::socket socket;
    net::steady_timer timer;      // 定时器，用于延迟和超时
    std::string client_ip;
    uint16_t client_port = 0;
};
```

### RestHandle

继承自 HttpHandle，专为 RESTful API 设计。

#### 特性
- 自动设置 Content-Type 为 application/json
- 自动包装响应数据格式：`{"ret": 0, "data": {...}}`
- 提供参数检查辅助方法

#### 示例
```cpp
class MyRestHandle : public RestHandle {
    REST_HANDLE_IMP(MyRestHandle)
    
    net::awaitable<void> run() override {
        // 检查必填参数
        check_missing_param("user_id");
        
        // 异步业务逻辑
        auto result = co_await some_async_operation();
        
        res["result"] = result;
        co_return;
    }
};
```

## 与原版 httpd 的对比

| 特性 | httpd (nng) | httpd (beast + coroutines) |
|------|-------------|---------------------------|
| 底层库 | nng | Boost.Beast/Asio |
| 并发模型 | nng aio | C++20 协程 |
| 接口参数 | nng_aio* | void* (BeastContext*) |
| run 方法签名 | `void run(nng_aio*)` | `net::awaitable<void> run()` |
| 异步方式 | 回调函数 | co_await |
| 代码可读性 | 一般（回调嵌套） | 优秀（同步式代码） |
| 性能 | 高 | 高 |
| 依赖 | nng | boost 1.75+ |
| 编译器 | C++11 | **C++20** |

## 注意事项

### 1. 协程要求

- **必须使用 C++20 协程** - 编译器需要支持协程特性
- **run 方法必须返回 `net::awaitable<void>`** - 不能使用 `void`
- **使用 `co_return` 而不是 `return`** - 协程语法

### 2. BeastContext 使用

```cpp
// 正确用法
auto* ctx = static_cast<BeastContext*>(m_beast_context);
ctx->timer.expires_after(...);
co_await ctx->timer.async_wait(...);

// 错误用法
// BeastContext* ctx = (BeastContext*)m_beast_context;  // 不要使用 C 风格转换
```

### 3. 异常安全

```cpp
// 正确：协程中异常会被框架捕获
net::awaitable<void> run() override {
    try {
        co_await some_operation();
    } catch (...) {
        // 处理异常
    }
    co_return;
}
```

### 4. 生命周期管理

- Handle 对象在协程执行期间必须保持有效
- 不要在协程中保存 this 指针的长期引用
- 使用 shared_ptr 管理资源

## 性能优化建议

### 1. 启用编译器优化

```bash
xmake f -m release --cxxflags="-O3 -DNDEBUG"
```

### 2. 使用多线程池

```cpp
int main() {
    HttpServer server("0.0.0.0", 8080);
    
    // 启动服务器
    server.start();
    
    // 使用多线程运行 io_context
    std::vector<std::thread> threads;
    auto thread_count = std::thread::hardware_concurrency();
    
    for (size_t i = 0; i < thread_count; ++i) {
        threads.emplace_back([&server]() {
            server.get_io_context()->run();
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    return 0;
}
```

### 3. 避免不必要的拷贝

```cpp
// 好：使用移动语义
res["data"] = std::move(large_data);

// 不好：产生拷贝
res["data"] = large_data;
```

## TLS/SSL 支持

### 启用 HTTPS

``cpp
// 创建 HTTPS 服务器
HttpServer https_server("0.0.0.0", 8443);

// 配置 SSL/TLS
// 参数 1: CA 证书文件路径（PEM 格式）
// 参数 2: 私钥密码（可为空）
// 参数 3: 客户端验证模式 (0=无需认证)
https_server.set_tls("server.pem", "", 0);
```

### 生成自签名证书

```bash
# 使用提供的脚本
chmod +x generate_ssl_cert.sh
./generate_ssl_cert.sh . server

# 生成的文件:
# - server.key  : RSA 私钥
# - server.crt  : 自签名证书
# - server.pem  : 合并的 PEM 文件（供 httpd 使用）
```

### Windows 平台支持

✅ **完全支持 Windows**！详细配置请参考 [WINDOWS_SSL_GUIDE.md](WINDOWS_SSL_GUIDE.md)

#### Windows 快速开始

``powershell
# 1. 安装 OpenSSL（使用 vcpkg）
.\vcpkg install openssl:x64-windows

# 2. 生成证书
openssl genrsa -out server.key 2048
openssl req -x509 -key server.key -out server.crt -days 365 -subj "/CN=localhost"
cat server.crt server.key > server.pem

# 3. 编译项目
xmake f -p windows -a x64 -m release
xmake

# 4. 运行 HTTPS 示例
xmake run httpd_ssl_example

# 5. 测试
curl -k https://localhost:8443/api/hello
```

### HTTPS 最佳实践

1. **生产环境使用受信任的证书**
   ```bash
   # Let's Encrypt 免费证书
   certbot certonly --standalone -d yourdomain.com
   ```

2. **强制 HTTPS 重定向**
   ```cpp
   class HttpsRedirectHandle : public HttpHandle {
       HTTP_HANDLE_IMP(HttpsRedirectHandle)
       
       net::awaitable<void> run() override {
           if (getReqHeader("X-Forwarded-Proto") != "https") {
               setResStatus(301);
               setResHeader("Location", "https://" + ...);
               co_return;
           }
           co_await handle_request();
       }
   };
   ```

3. **启用 HSTS**
   ```cpp
   setResHeader("Strict-Transport-Security", 
                "max-age=31536000; includeSubDomains");
   ```

详细配置请参考 [SSL_TLS_GUIDE.md](SSL_TLS_GUIDE.md) 和 [WINDOWS_SSL_GUIDE.md](WINDOWS_SSL_GUIDE.md)

## 构建和运行

```bash
cd hikyuu/httpd

# 配置项目
xmake f -m release

# 编译
xmake

# 运行 HTTP 示例
xmake run httpd_example

# 运行 HTTPS 示例
xmake run httpd_ssl_example

# 测试 HTTPS
./test_https.sh
```

## 完整文档

- **[README.md](README.md)** - 本文档
- **[QUICKSTART.md](QUICKSTART.md)** - 5 分钟快速入门
- **[COROUTINE_GUIDE.md](COROUTINE_GUIDE.md)** - 协程编程指南
- **[SSL_TLS_GUIDE.md](SSL_TLS_GUIDE.md)** - TLS/SSL 配置指南
- **[BUILD_CONFIG.md](BUILD_CONFIG.md)** - 编译配置详解
- **[PROJECT_SUMMARY.md](PROJECT_SUMMARY.md)** - 项目总结

## 示例代码

| 文件 | 说明 |
|------|------|
| `main.cpp` | 基础协程示例 |
| `ssl_example.cpp` | HTTPS/SSL 示例 |
| `examples.cpp` | 高级用法示例 |
| `advanced_coroutine_examples.cpp` | 协程工具示例 |

## 许可证

Copyright(C) 2021 hikyuu.org

## 联系方式

- 官网：https://www.hikyuu.org
- GitHub: https://github.com/fasiondog/hku_rest

# HTTP + WebSocket 统一架构说明

## 架构概述

本项目已实现 **HTTP 与 WebSocket 的统一服务器架构**,通过单个 `HttpServer` 类同时支持两种协议。

## 核心设计理念

### 1. 统一而非分离
- ✅ **单一 HttpServer 类**: 同时处理 HTTP/HTTPS 和 WebSocket
- ✅ **资源共享**: IO 线程池、SSL 上下文、连接管理完全共享
- ✅ **协议检测**: 自动识别并路由到对应处理器
- ❌ **移除冗余**: 删除了独立的 WebSocketServer 类

### 2. 三层架构
```
┌─────────────────────────────────────┐
│      HttpServer (统一服务器)        │
├─────────────────────────────────────┤
│  Router (HTTP)   │  Router (WS)     │
├─────────────────────────────────────┤
│    Connection (连接处理器)          │
│    ├─ SSL/non-SSL 分支处理          │
│    └─ 协议检测与路由                │
├─────────────────────────────────────┤
│  Handle 层 (业务逻辑)               │
│  ├─ HttpHandle / RestHandle         │
│  └─ WebSocketHandle                 │
└─────────────────────────────────────┘
```

## 文件结构

### 核心文件
```
hikyuu/httpd/
├── HttpServer.h          # 统一服务器定义 (包含 HTTP+WebSocket)
├── HttpServer.cpp        # 统一服务器实现
├── HttpHandle.h          # HTTP Handle 基类
├── HttpHandle.cpp        # HTTP Handle 实现
├── WebSocketHandle.h     # WebSocket Handle 基类 (保留)
├── WebSocketHandle.cpp   # WebSocket Handle 实现 (保留)
├── all.h                 # 统一导出头文件
└── pod/                  # POD (Plain Old Data) 序列化支持
```

### 已删除的文件
- ❌ ~~WebSocketServer.h~~ - 已被 HttpServer 替代
- ❌ ~~WebSocketServer.cpp~~ - 已被 HttpServer 替代

### 保留的底层组件
- ✅ WebSocketHandle.h/cpp - WebSocket 业务 Handle 基类
- ✅ WebSocketConnection - WebSocket 连接处理器 (在 HttpServer.cpp 中调用)

## 使用方法

### 创建服务器
```cpp
#include <hikyuu/httpd/HttpServer.h>

auto server = std::make_unique<HttpServer>("0.0.0.0", 8765);
```

### 注册 HTTP REST API
```cpp
// 方式 1: Lambda 方式
server->registerHttpHandle("GET", "/api/hello", 
    [](void* ctx) -> net::awaitable<void> {
        // HTTP 处理逻辑
        co_return;
    });

// 方式 2: 模板方式
server->GET<MyHttpHandle>("/api/data");
server->POST<MyHttpPostHandle>("/api/submit");
```

### 注册 WebSocket Handle
```cpp
// 方式 1: Lambda 方式
server->registerWsHandle("/ws/echo",
    [](void* ctx) -> net::awaitable<void> {
        // WebSocket 处理逻辑
        co_return;
    });

// 方式 2: 模板方式 (推荐)
server->WS<EchoWsHandle>("/echo");
```

### 配置 SSL/TLS
```cpp
// 同时作用于 HTTP 和 WebSocket
server->setTls("server.pem", "password", 0);
```

### 启动服务器
```cpp
server->start();
HttpServer::loop();
```

## 完整示例

``cpp
#include <hikyuu/httpd/HttpServer.h>
#include "EchoWsHandle.h"

using namespace hku;

int main() {
    auto server = std::make_unique<HttpServer>("0.0.0.0", 8765);
    
    // HTTP REST API
    server->GET<HelloHandle>("/api/hello");
    server->POST<SubmitHandle>("/api/submit");
    
    // WebSocket
    server->WS<EchoWsHandle>("/ws/chat");
    
    // SSL 配置 (可选)
    server->setTls("server.pem", "", 0);
    
    // 启动
    server->start();
    HttpServer::loop();
    
    return 0;
}
```

## 架构优势对比

| 方面 | 旧架构 (已废弃) | 新架构 (当前) |
|------|----------------|---------------|
| 服务器类 | HttpServer + WebSocketServer | **HttpServer(单个)** ✓ |
| IO 线程池 | 2 套独立 | **1 套共享** ✓ |
| SSL 上下文 | 2 套独立 | **1 套共享** ✓ |
| 端口占用 | 2 个端口 | **1 个端口** ✓ |
| 协议升级 | ❌ 不支持 | **✅ 支持 HTTP→WS** ✓ |
| 代码复用 | 低 | **高** ✓ |
| API 统一性 | 分散 | **统一** ✓ |
| 维护成本 | 高 | **低** ✓ |

## 安全特性

### SSL/TLS 加固
- ✅ TLS 1.2+ only (禁用 SSLv2/v3, TLS 1.0/1.1)
- ✅ 强密码套件 (GCM 优先，ECDHE 密钥交换)
- ✅ 证书文件权限检查 (600)
- ✅ DH 参数每次握手更新 (防 Logjam)

### 请求大小限制
- MAX_BUFFER_SIZE: 1MB
- MAX_BODY_SIZE: 10MB
- MAX_HEADER_SIZE: 8KB

### 超时保护
- HEADER_TIMEOUT: 10 秒
- READ_TIMEOUT: 30 秒
- WRITE_TIMEOUT: 30 秒
- TOTAL_TIMEOUT: 60 秒

### 连接池管理
- MAX_CONNECTIONS: 1000 (可配置)
- MAX_KEEPALIVE_REQUESTS: 10000
- MAX_CONNECTION_AGE: 5 分钟

## 协议检测机制

```cpp
// 在 Connection::handleConnection 中
bool is_websocket = 
    req.method() == http::verb::get &&
    req.find(http::field::upgrade) != req.end() &&
    beast::iequals(req[http::field::upgrade], "websocket");

if (is_websocket) {
    // WebSocket 处理
    auto handler = ms_ws_router.findHandler("WS", path);
    // ...
} else {
    // HTTP 处理
    auto conn = Connection::create(...);
    conn->start();
}
```

## 向后兼容性

### 保留的组件 (仍可使用)
- ✅ `WebSocketHandle` - WebSocket 业务 Handle 基类
- ✅ `WebSocketContext` - WebSocket 上下文
- ✅ `ws::close_code` - WebSocket 关闭码
- ✅ Ping/Pong 心跳机制

### 已移除的组件
- ❌ `WebSocketServer` - 服务器类 (已被 HttpServer 替代)
- ❌ `WebSocketServer::set_tls()` - 静态方法 (改为 HttpServer::setTls())
- ❌ `WebSocketServer::loop()` - 静态方法 (改为 HttpServer::loop())

## 迁移指南

### 从旧版 WebSocketServer 迁移

**旧代码:**
```cpp
#include <hikyuu/httpd/WebSocketServer.h>

WebSocketServer server("0.0.0.0", 8765);
server.WS<EchoWsHandle>("/echo");
server.start();
WebSocketServer::loop();
```

**新代码:**
```cpp
#include <hikyuu/httpd/HttpServer.h>

auto server = std::make_unique<HttpServer>("0.0.0.0", 8765);
server->WS<EchoWsHandle>("/echo");  // ← API 相同!
server->start();
HttpServer::loop();
```

**迁移成本：零!** 只需修改头文件和类名。

## 示例程序

参考 `example/websocket_server/`:
- ✅ HTTP REST API: `GET /api/hello`
- ✅ WebSocket Echo: `ws://localhost:8765/echo`
- ✅ SSL/TLS 支持 (可选)

### 编译示例
```bash
xmake
xmake run websocket_server
```

### 测试
```bash
# HTTP
curl http://localhost:8765/api/hello

# WebSocket
wscat -c ws://localhost:8765/echo
```

## 技术栈

- **C++20**: 协程异步编程
- **Boost.Beast**: HTTP/WebSocket 协议实现
- **Boost.Asio**: 网络 IO
- **OpenSSL 3.x**: TLS/SSL 加密
- **nlohmann/json**: JSON 序列化
- **fmt/spdlog**: 日志系统

## 参考资料

- [HttpServer 实现](hikyuu/httpd/HttpServer.cpp)
- [WebSocketHandle API](hikyuu/httpd/WebSocketHandle.h)
- [示例代码](example/websocket_server/)
- [Boost.Beast 文档](https://www.boost.org/doc/libs/release/libs/beast/)
