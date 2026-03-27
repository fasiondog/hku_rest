# Prometheus 监控集成指南

## 📋 概述

本指南介绍如何将 **hku_rest HTTP 服务器** 集成到 Prometheus 监控系统中，实现实时监控连接管理器的各项指标。

---

## 🏗️ 架构设计

### 组件关系

```
┌─────────────────┐
│ ConnectionMgr   │
│ - active: 100   │
│ - waiting: 5    │
│ - total: 1234   │
└────────┬────────┘
         │ (1) 采集指标
         ▼
┌─────────────────┐
│ ConnectionMonitor│◄─── 每秒采样一次
│ - sample()      │
│ - register()    │
└────────┬────────┘
         │ (2) 更新指标
         ▼
┌─────────────────┐
│ MetricsExporter │
│ - gauges        │
│ - counters      │
└────────┬────────┘
         │ (3) 导出格式
         ▼
┌─────────────────┐
│ /metrics 端点   │────► Prometheus Server
│ text/plain      │     (每 15 秒抓取)
└─────────────────┘
```

---

## 🚀 快速开始

### 1. 启用监控（代码示例）

在 `main.cpp` 中：

```cpp
#include "hikyuu/httpd/MetricsExporter.h"
#include "hikyuu/httpd/ConnectionMonitor.h"

int main() {
    // ... 其他配置 ...
    
    // 创建连接监控器
    auto conn_monitor = std::make_shared<ConnectionMonitor>(
        HttpServer::ms_connection_manager, 
        1000  // 采样间隔 1 秒
    );
    
    // 启动后台采样线程
    conn_monitor->startSampling();
    
    // 启动服务器
    server.start();
    server.loop();
    
    // 停止监控器
    conn_monitor->stopSampling();
    
    return 0;
}
```

### 2. 访问监控端点

启动服务器后，访问：

```bash
curl http://localhost:8080/metrics
```

**输出示例：**

```
# HELP hku_http_active_connections 当前活跃连接数
# TYPE hku_http_active_connections gauge
hku_http_active_connections 100

# HELP hku_http_waiting_connections 等待中的连接数
# TYPE hku_http_waiting_connections gauge
hku_http_waiting_connections 0

# HELP hku_http_total_permits_issued 已分配的许可总数
# TYPE hku_http_total_permits_issued counter
hku_http_total_permits_issued 12345
```

---

## 📊 监控指标说明

### Gauge 指标（可增可减）

| 指标名称 | 描述 | 类型 | 示例值 |
|---------|------|------|--------|
| `hku_http_active_connections` | 当前活跃连接数 | Gauge | 100 |
| `hku_http_waiting_connections` | 等待中的连接数 | Gauge | 5 |
| `hku_http_max_concurrent_connections` | 最大并发连接数配置 | Gauge | 1000 |
| `hku_http_acquire_queue_size` | 获取许可的队列长度 | Gauge | 5 |

### Counter 指标（只增不减）

| 指标名称 | 描述 | 类型 | 示例值 |
|---------|------|------|--------|
| `hku_http_total_permits_issued` | 已分配的许可总数 | Counter | 12345 |
| `hku_http_total_releases` | 已释放的许可总数 | Counter | 12200 |
| `hku_http_total_timeouts` | 超时次数 | Counter | 15 |
| `hku_http_total_acquires` | 获取许可请求总数 | Counter | 12500 |

---

## 🔧 Prometheus 配置

### 1. 安装 Prometheus

**Docker 方式（推荐）：**

```bash
docker run -d \
  --name prometheus \
  -p 9090:9090 \
  -v $(pwd)/prometheus.yml:/etc/prometheus/prometheus.yml \
  prom/prometheus
```

**本地安装：**

```bash
# macOS
brew install prometheus

# Linux
sudo apt-get install prometheus
```

### 2. 配置 Prometheus

编辑 `prometheus.yml`：

```yaml
global:
  scrape_interval: 15s

scrape_configs:
  - job_name: 'hku_rest'
    static_configs:
      - targets: ['localhost:8080']
    metrics_path: '/metrics'
```

### 3. 启动 Prometheus

```bash
prometheus --config.file=prometheus.yml
```

访问 Prometheus UI: http://localhost:9090

---

## 📈 Grafana 可视化

### 1. 安装 Grafana

```bash
docker run -d \
  --name grafana \
  -p 3000:3000 \
  grafana/grafana
```

### 2. 配置数据源

1. 登录 Grafana (默认 admin/admin)
2. 进入 Configuration → Data sources
3. 添加 Prometheus 数据源
   - URL: `http://host.docker.internal:9090` (Docker 环境)
   - 或 `http://localhost:9090` (本地环境)

### 3. 创建 Dashboard

#### Dashboard 1: 连接概览

```sql
-- 当前活跃连接
hku_http_active_connections

-- 等待连接数
hku_http_waiting_connections

-- 连接使用率
hku_http_active_connections / hku_http_max_concurrent_connections * 100
```

#### Dashboard 2: 性能指标

```sql
-- 许可分配速率（每秒）
rate(hku_http_total_permits_issued[1m])

-- 超时率（每秒）
rate(hku_http_total_timeouts[1m])

-- 超时百分比
rate(hku_http_total_timeouts[1m]) / rate(hku_http_total_acquires[1m]) * 100
```

#### Dashboard 3: 告警面板

```sql
-- 高负载告警（等待 > 100）
hku_http_waiting_connections > 100

-- 连接饱和告警（使用率 > 90%）
hku_http_active_connections / hku_http_max_concurrent_connections > 0.9

-- 超时率异常告警
rate(hku_http_total_timeouts[5m]) > 10
```

---

## 🛠️ 使用示例

### 基础测试

```bash
# 1. 启动服务器
cd /Users/fasiondog/workspace/hku_rest/example/rest_server
xmake r rest_server

# 2. 测试监控端点
curl http://localhost:8080/metrics

# 3. 运行自动化测试
./test_metrics.sh
```

### 压力测试 + 监控

```bash
# 终端 1: 启动服务器
xmake r rest_server

# 终端 2: 持续监控指标
watch -n 1 'curl -s http://localhost:8080/metrics | grep -E "^(hku_http|# HELP)"'

# 终端 3: 施加负载
wrk -t4 -c100 -d30s http://localhost:8080/hello
```

**预期观察：**
- `active_connections` 上升到 ~100
- `total_permits_issued` 持续增长
- `waiting_connections` 保持为 0（因为 < 1000 限制）

### 触发等待队列

```bash
# 施加大量并发
wrk -t8 -c2000 -d30s http://localhost:8080/hello
```

**预期观察：**
- `active_connections` 达到 1000（上限）
- `waiting_connections` > 0（有连接在等待）
- `total_timeouts` 可能增长（部分连接超时）

---

## 🔍 故障排查

### 问题 1: /metrics 端点返回 404

**原因：**
- 未正确注册 HTTP Handle

**解决方法：**
```cpp
// 确保在 HttpServer 中注册了 /metrics 路由
server.registerHttpHandle("GET", "/metrics", [](void* ctx) -> net::awaitable<void> {
    ConnectionMonitor::handleMetrics(ctx);
    co_return;
});
```

### 问题 2: 指标值为 0

**原因：**
- 监控器未启动采样
- ConnectionManager 指针为空

**解决方法：**
```cpp
// 检查 conn_monitor 是否正确初始化
auto conn_monitor = std::make_shared<ConnectionMonitor>(
    HttpServer::ms_connection_manager,  // 确保不为空
    1000
);
conn_monitor->startSampling();
```

### 问题 3: Prometheus 无法抓取

**原因：**
- 网络不通
- 端口被防火墙阻止

**解决方法：**
```bash
# 测试连通性
curl http://localhost:8080/metrics

# 检查端口监听
netstat -tlnp | grep 8080

# 临时关闭防火墙（开发环境）
sudo ufw disable
```

---

## ⚙️ 高级配置

### 自定义采样间隔

```cpp
// 更快的采样（适合调试）
auto conn_monitor = std::make_shared<ConnectionMonitor>(
    HttpServer::ms_connection_manager, 
    100  // 100ms 采样一次
);

// 更慢的采样（生产环境）
auto conn_monitor = std::make_shared<ConnectionMonitor>(
    HttpServer::ms_connection_manager, 
    5000  // 5 秒采样一次
);
```

### 动态调整并发限制

```cpp
// 根据监控指标自动调整
void autoAdjustConcurrency() {
    auto mgr = HttpServer::get_connection_manager();
    auto& metrics = MetricsExporter::getInstance();
    
    double active = metrics.getGauge("hku_http_active_connections");
    double waiting = metrics.getGauge("hku_http_waiting_connections");
    
    if (waiting > 500 && active < 1000) {
        // 增加并发限制
        server.set_max_concurrent_connections(1500, 30000);
    } else if (active < 100 && waiting == 0) {
        // 降低并发限制以节省资源
        server.set_max_concurrent_connections(500, 30000);
    }
}
```

### 添加自定义指标

```cpp
// 注册业务指标
auto& metrics = MetricsExporter::getInstance();
metrics.registerCounter("myapp_total_orders", "总订单数");
metrics.registerGauge("myapp_inventory_level", "库存水平");

// 更新指标
metrics.incrementCounter("myapp_total_orders");
metrics.setGauge("myapp_inventory_level", 150);
```

---

## 📝 最佳实践

### 1. 生产环境配置

```cpp
// 使用较慢的采样间隔（减少开销）
constexpr size_t PRODUCTION_SAMPLE_INTERVAL = 5000;  // 5 秒

// 禁用日志监控线程（只用 Prometheus）
// 注释掉旧的日志监控代码
// std::thread monitor_thread([]() { ... });

// 启用新的监控器
auto conn_monitor = std::make_shared<ConnectionMonitor>(
    HttpServer::ms_connection_manager,
    PRODUCTION_SAMPLE_INTERVAL
);
conn_monitor->startSampling();
```

### 2. 告警规则

在 Prometheus 中配置告警规则 `alert_rules.yml`：

```yaml
groups:
  - name: hku_rest_alerts
    interval: 30s
    rules:
      - alert: HighConnectionWait
        expr: hku_http_waiting_connections > 100
        for: 2m
        labels:
          severity: warning
        annotations:
          summary: "连接等待队列过长"
          description: "{{ $value }} 个连接正在等待"

      - alert: ConnectionSaturation
        expr: hku_http_active_connections / hku_http_max_concurrent_connections > 0.9
        for: 5m
        labels:
          severity: critical
        annotations:
          summary: "连接池接近饱和"
          description: "连接使用率达到 {{ $value | humanizePercentage }}"

      - alert: HighTimeoutRate
        expr: rate(hku_http_total_timeouts[5m]) > 10
        for: 2m
        labels:
          severity: warning
        annotations:
          summary: "超时率异常"
          description: "每秒 {{ $value }} 次超时"
```

### 3. 性能优化

- **减少采样频率**：生产环境使用 5-15 秒间隔
- **禁用旧监控**：注释掉日志监控线程，只用 Prometheus
- **指标过滤**：只导出必要的指标，减少网络传输
- **异步采样**：确保采样不阻塞主线程

---

## 🔮 未来扩展

### 1. 分布式追踪

集成 Jaeger 或 Zipkin：

```cpp
class DistributedTracer {
public:
    void recordAcquireLatency(std::chrono::microseconds latency) {
        // 上报到 Jaeger
    }
    
    void recordRelease() {
        // 记录释放事件
    }
};
```

### 2. 自适应限流

```cpp
void adaptiveThrottling() {
    auto& metrics = MetricsExporter::getInstance();
    
    double cpu_usage = get_cpu_usage();
    double memory_usage = get_memory_usage();
    double active = metrics.getGauge("hku_http_active_connections");
    
    if (cpu_usage > 0.8 || memory_usage > 0.9) {
        // 动态降低并发限制
        current_max = current_max * 0.8;
    }
}
```

### 3. 多租户配额

```cpp
struct TenantQuota {
    std::string tenant_id;
    int max_connections;
    int used_connections;
};

// 为每个租户单独监控配额
metrics.registerGauge("hku_http_tenant_quota_used", "租户配额使用");
metrics.registerGauge("hku_http_tenant_quota_limit", "租户配额上限");
```

---

## 📚 相关资源

- [Prometheus 官方文档](https://prometheus.io/docs/)
- [Grafana 官方文档](https://grafana.com/docs/)
- [ConnectionManager 源码](../../hikyuu/httpd/ConnectionManager.h)
- [MetricsExporter 源码](../../hikyuu/httpd/MetricsExporter.h)
- [ConnectionMonitor 源码](../../hikyuu/httpd/ConnectionMonitor.h)

---

## ✅ 检查清单

- [ ] 监控代码已集成到 main.cpp
- [ ] /metrics 端点可访问
- [ ] Prometheus 配置正确
- [ ] Grafana Dashboard 已创建
- [ ] 告警规则已配置
- [ ] 压力测试验证通过
- [ ] 生产环境参数已调优

---

**最后更新**: 2026-03-27  
**维护者**: hikyuu.org team
