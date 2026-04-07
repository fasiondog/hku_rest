# MQTT Broker 示例

基于 async_mqtt 实现的 MQTT Broker 服务器示例。

## 功能特性

- ✅ MQTT over TCP (端口 1883)
- ⚠️  MQTT over WebSocket (可选，需编译时启用)
- ⚠️  MQTT over TLS (可选，需编译时启用)
- ⚠️  MQTT over WebSocket Secure (可选，需编译时启用)
- ✅ 多 io_context 线程池
- ✅ Round-Robin 连接分发
- ✅ 认证授权支持
- ✅ 优雅关闭

## 构建

### 基本构建（仅 MQTT over TCP）

```bash
cd /Users/fasiondog/workspace/hku_rest/example/mqtt_broker
xmake
```

### 启用 WebSocket 支持

修改 `xmake.lua`，取消注释：
```lua
add_defines("ASYNC_MQTT_USE_WS")
```

然后重新构建：
```bash
xmake clean
xmake
```

### 启用 TLS 支持

修改 `xmake.lua`，取消注释：
```lua
add_defines("ASYNC_MQTT_USE_TLS")
```

然后重新构建：
```bash
xmake clean
xmake
```

## 运行

### 使用默认配置

```bash
xmake run mqtt_broker
```

### 自定义配置

编辑 `mqtt_broker.ini` 文件，然后修改 `main.cpp` 从文件加载配置：

```cpp
#include <hikyuu/utilities/ini_parser/IniParser.h>

// 加载配置文件
IniParser parser;
parser.load("mqtt_broker.ini");

Parameter param;
param.set<uint16_t>("mqtt_port", parser.get<uint16_t>("mqtt_port", 1883));
param.set<std::size_t>("iocs", parser.get<std::size_t>("iocs", 0));
// ... 其他配置
```

## 配置参数说明

### 必需参数

- `mqtt_port`: MQTT TCP 监听端口（默认 1883）

### 可选参数

**网络配置：**
- `ws_port`: WebSocket 端口
- `tls_port`: TLS 端口
- `wss_port`: WebSocket Secure 端口

**性能配置：**
- `iocs`: io_context 数量（0 = CPU 核心数，默认 0）
- `threads_per_ioc`: 每个 io_context 的线程数（默认 1）
- `read_buf_size`: 读取缓冲区大小（默认 65536）
- `bulk_write`: 批量写入（默认 true）
- `tcp_no_delay`: 禁用 Nagle 算法（默认 true）
- `recv_buf_size`: 接收缓冲区大小
- `send_buf_size`: 发送缓冲区大小

**安全配置：**
- `auth_file`: 认证文件路径（JSON 格式）
- `certificate`: TLS 证书文件路径
- `private_key`: TLS 私钥文件路径
- `verify_file`: CA 证书文件
- `verify_field`: 证书验证字段（默认 "CN"）

**其他：**
- `recycling_allocator`: 回收分配器（默认 false）

## 测试

### 使用 mosquitto 客户端测试

#### 订阅主题

```bash
mosquitto_sub -t "test/topic" -v
```

#### 发布消息

```bash
mosquitto_pub -t "test/topic" -m "Hello MQTT!"
```

### 使用 Python 测试

```python
import paho.mqtt.client as mqtt

def on_connect(client, userdata, flags, rc):
    print(f"Connected with result code {rc}")
    client.subscribe("test/topic")

def on_message(client, userdata, msg):
    print(f"{msg.topic}: {msg.payload.decode()}")

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

client.connect("localhost", 1883, 60)
client.loop_forever()
```

## 认证配置

创建 `auth.json` 文件：

```json
{
  "users": [
    {
      "username": "user1",
      "password": "password1"
    },
    {
      "username": "user2",
      "password": "password2"
    }
  ],
  "acls": [
    {
      "username": "user1",
      "topic": "user1/#",
      "access": "readwrite"
    },
    {
      "username": "user2",
      "topic": "user2/#",
      "access": "readwrite"
    }
  ]
}
```

然后在配置中指定：
```cpp
param.set<std::string>("auth_file", "auth.json");
```

## 注意事项

1. **端口权限**: Linux/macOS 上使用 1024 以下端口需要 root 权限
2. **防火墙**: 确保防火墙允许相应端口的连接
3. **TLS 证书**: 启用 TLS 时需要有效的证书和私钥文件
4. **并发连接**: 根据实际需求调整 `iocs` 和 `threads_per_ioc` 参数

# MqttBroker 实现说明

## 概述

MqttBroker 是基于 async_mqtt 库实现的 MQTT Broker 服务器，已简化实现，移除了 Impl 结构体，直接使用类成员变量。

## 核心特性

- **多协议支持**: 
  - MQTT over TCP (必需)
  - MQTT over WebSocket (可选，编译时启用 ASYNC_MQTT_USE_WS)
  - MQTT over TLS (可选，编译时启用 ASYNC_MQTT_USE_TLS)
  - MQTT over WebSocket Secure (可选，需同时启用 WS 和 TLS)
- **多 io_context 架构**: 支持多个 io_context 线程池，Round-Robin 分发连接
- **配置驱动**: 从 hikyuu/utilities/Parameter 获取所有配置参数

## 配置参数

**必需参数：**
- `mqtt_port`: MQTT TCP 监听端口（如 1883）

**可选参数：**
- `ws_port`: WebSocket 端口
- `tls_port`: TLS 端口
- `wss_port`: WebSocket Secure 端口
- `iocs`: io_context 数量（0 = CPU 核心数，默认 0）
- `threads_per_ioc`: 每个 io_context 的线程数（默认 1）
- `read_buf_size`: 读取缓冲区大小（默认 65536）
- `bulk_write`: 批量写入（默认 true）
- `tcp_no_delay`: 禁用 Nagle 算法（默认 true）
- `recv_buf_size`: 接收缓冲区大小
- `send_buf_size`: 发送缓冲区大小
- `auth_file`: 认证文件路径（JSON 格式）

**TLS 必需参数（当启用 TLS 时）：**
- `certificate`: 证书文件路径
- `private_key`: 私钥文件路径
- `verify_file`: CA 证书文件（可选）

## 使用方法

```
#include <hikyuu/mqtt/MqttBroker.h>
#include <hikyuu/utilities/Parameter.h>

using namespace hku;

// 创建配置
Parameter param;
param.set<uint16_t>("mqtt_port", 1883);
param.set<std::size_t>("iocs", 0);  // 自动检测 CPU 核心数
param.set<std::size_t>("threads_per_ioc", 1);
param.set<bool>("tcp_no_delay", true);

// 创建并启动 Broker
MqttBroker broker(param);
broker.start();  // 阻塞直到 stop() 被调用
```

## 架构特点

- **协程化**: 所有 accept 操作使用 C++20 协程
- **优雅关闭**: 通过原子标志 m_stop_requested 控制循环退出
- **线程安全**: Round-Robin 分发使用互斥锁保护
- **无 Impl 模式**: 直接使用类成员变量，简化代码结构

## 构建配置

在 xmake.lua 中已添加：
```
add_requires("async_mqtt")
add_packages("async_mqtt")
add_files("hikyuu/mqtt/*.cpp")
```

需要定义 ASYNC_MQTT_USE_WS 和 ASYNC_MQTT_USE_TLS 宏来启用相应功能。
