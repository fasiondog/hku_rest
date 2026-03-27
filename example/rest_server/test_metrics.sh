#!/bin/bash
# Prometheus 监控端点测试脚本

echo "========================================"
echo "Prometheus 监控指标测试"
echo "========================================"
echo ""

# 检查服务器是否运行
if ! curl -s http://localhost:8080/hello > /dev/null; then
    echo "❌ 错误：服务器未运行！"
    echo "请先启动服务器：xmake r rest_server"
    exit 1
fi

echo "✅ 服务器正在运行"
echo ""

# 测试基础 HTTP 请求
echo "1️⃣  测试基础 HTTP 请求："
curl -s http://localhost:8080/hello | jq .
echo ""
echo ""

# 获取 Prometheus 格式指标
echo "2️⃣  获取 Prometheus 监控指标："
echo "----------------------------------------"
curl -s http://localhost:8080/metrics
echo ""
echo "----------------------------------------"
echo ""

# 验证指标格式
echo "3️⃣  验证指标格式（检查 HELP 和 TYPE）："
if curl -s http://localhost:8080/metrics | grep -q "# HELP"; then
    echo "✅ 包含 HELP 注释"
else
    echo "❌ 缺少 HELP 注释"
fi

if curl -s http://localhost:8080/metrics | grep -q "# TYPE"; then
    echo "✅ 包含 TYPE 声明"
else
    echo "❌ 缺少 TYPE 声明"
fi

echo ""

# 检查关键指标
echo "4️⃣  检查关键监控指标："
METRICS=$(curl -s http://localhost:8080/metrics)

if echo "$METRICS" | grep -q "hku_http_active_connections"; then
    echo "✅ active_connections 指标存在"
    ACTIVE=$(echo "$METRICS" | grep "^hku_http_active_connections" | awk '{print $2}')
    echo "   当前值：$ACTIVE"
else
    echo "❌ active_connections 指标缺失"
fi

if echo "$METRICS" | grep -q "hku_http_waiting_connections"; then
    echo "✅ waiting_connections 指标存在"
    WAITING=$(echo "$METRICS" | grep "^hku_http_waiting_connections" | awk '{print $2}')
    echo "   当前值：$WAITING"
else
    echo "❌ waiting_connections 指标缺失"
fi

if echo "$METRICS" | grep -q "hku_http_total_permits_issued"; then
    echo "✅ total_permits_issued 指标存在"
    TOTAL=$(echo "$METRICS" | grep "^hku_http_total_permits_issued" | awk '{print $2}')
    echo "   当前值：$TOTAL"
else
    echo "❌ total_permits_issued 指标缺失"
fi

echo ""
echo "========================================"
echo "使用 Grafana 可视化（可选）"
echo "========================================"
echo ""
echo "如果你想用 Grafana 可视化这些指标："
echo ""
echo "1. 安装 Prometheus:"
echo "   docker run -d --name prometheus -p 9090:9090 \\"
echo "     -v \$(pwd)/prometheus.yml:/etc/prometheus/prometheus.yml \\"
echo "     prom/prometheus"
echo ""
echo "2. 安装 Grafana:"
echo "   docker run -d --name grafana -p 3000:3000 \\"
echo "     grafana/grafana"
echo ""
echo "3. 在 Grafana 中添加数据源:"
echo "   - URL: http://localhost:9090"
echo "   - 创建 Dashboard 导入指标"
echo ""
echo "4. 查看示例 Dashboard:"
echo "   - 活跃连接数：hku_http_active_connections"
echo "   - 等待连接数：hku_http_waiting_connections"
echo "   - 许可总数：hku_http_total_permits_issued"
echo ""
echo "========================================"
