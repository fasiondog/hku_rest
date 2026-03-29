#!/bin/bash
# 智能连接管理器压力测试脚本
# 演示连接等待队列和限流机制

echo "========================================"
echo "智能连接管理器压力测试"
echo "========================================"
echo ""
echo "配置说明："
echo "- 最大并发连接数：1000"
echo "- 等待超时时间：30 秒"
echo "- Keep-Alive 限制：10000 请求/连接"
echo ""
echo "测试场景："
echo "1. 低负载（10 并发） - 应该无等待"
echo "2. 中负载（100 并发） - 应该无等待"
echo "3. 高负载（2000 并发） - 观察等待队列"
echo ""

# 检查服务器是否运行
if ! curl -s http://localhost:8080/hello > /dev/null; then
    echo "❌ 错误：服务器未运行！"
    echo "请先启动服务器：xmake r rest_server"
    exit 1
fi

echo "✅ 服务器正在运行"
echo ""

# 测试 1：低负载
echo "========================================"
echo "测试 1: 低负载 (10 并发，5 秒)"
echo "========================================"
wrk -t2 -c10 -d5s http://localhost:8080/hello
echo ""
echo "📊 预期结果：Active < 10, Waiting = 0"
read -p "按回车继续..."
echo ""

# 测试 2：中负载
echo "========================================"
echo "测试 2: 中负载 (100 并发，10 秒)"
echo "========================================"
wrk -t4 -c100 -d10s http://localhost:8080/hello
echo ""
echo "📊 预期结果：Active ~ 100, Waiting = 0"
read -p "按回车继续..."
echo ""

# 测试 3：高负载（触发等待队列）
echo "========================================"
echo "测试 3: 高负载 (2000 并发，15 秒)"
echo "========================================"
echo "⚠️  注意：此测试会触发连接等待队列"
echo "⚠️  观察服务器日志中的 'Waiting Connections' 指标"
echo ""
wrk -t8 -c2000 -d15s http://localhost:8080/hello 2>&1 | tee /tmp/wrk_result.txt
echo ""
echo "📊 查看测试结果："
echo "   - Requests/sec: $(grep 'Requests/sec' /tmp/wrk_result.txt | awk '{print $2}')"
echo "   - Socket errors: $(grep 'Socket errors' /tmp/wrk_result.txt)"
echo ""
echo "🔍 请检查服务器日志："
echo "   - Active Connections 是否接近 1000？"
echo "   - Waiting Connections 是否大于 0？"
echo "   - Total Permits Issued 是否持续增长？"
echo ""
echo "========================================"
echo "测试完成！"
echo "========================================"
