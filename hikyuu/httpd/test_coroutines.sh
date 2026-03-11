#!/bin/bash

# httpd 协程功能测试脚本
# 用于验证各种协程场景的正确性

set -e

BASE_URL="http://localhost:8080/api"

echo "=========================================="
echo "httpd 协程功能测试"
echo "=========================================="
echo ""

# 颜色定义
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 测试函数
test_endpoint() {
    local name=$1
    local method=$2
    local endpoint=$3
    local data=$4
    
    echo -n "Testing $name... "
    
    if [ "$method" == "GET" ]; then
        response=$(curl -s -w "\n%{http_code}" "${BASE_URL}${endpoint}")
    else
        response=$(curl -s -w "\n%{http_code}" -X POST \
            -H "Content-Type: application/json" \
            -d "$data" \
            "${BASE_URL}${endpoint}")
    fi
    
    http_code=$(echo "$response" | tail -n1)
    body=$(echo "$response" | head -n-1)
    
    if [ "$http_code" == "200" ]; then
        echo -e "${GREEN}✓ PASS${NC}"
        echo "  Response: $body" | head -c 200
        echo ""
    else
        echo -e "${RED}✗ FAIL (HTTP $http_code)${NC}"
        echo "  Response: $body"
        return 1
    fi
}

# 等待服务器启动
wait_for_server() {
    echo "Waiting for server to start..."
    for i in {1..30}; do
        if curl -s "${BASE_URL}/sync" > /dev/null 2>&1; then
            echo "Server is ready!"
            return 0
        fi
        sleep 1
    done
    echo -e "${RED}Server failed to start${NC}"
    exit 1
}

# 主测试流程
main() {
    wait_for_server
    
    echo ""
    echo "=========================================="
    echo "基础协程测试"
    echo "=========================================="
    
    # 1. 同步 Handle（向后兼容）
    test_endpoint "Sync Handle" "GET" "/sync"
    
    echo ""
    echo "=========================================="
    echo "异步延迟测试"
    echo "=========================================="
    
    # 2. 异步延迟响应
    test_endpoint "Async Delay (1s)" "GET" "/async/delay?delay=1000"
    
    # 3. 自定义延迟时间
    test_endpoint "Custom Delay (500ms)" "GET" "/async/delay?delay=500"
    
    echo ""
    echo "=========================================="
    echo "REST 风格异步测试"
    echo "=========================================="
    
    # 4. REST 异步处理
    test_endpoint "Async User Query" "POST" "/async/user" '{"user_id": "123"}'
    
    echo ""
    echo "=========================================="
    echo "并行任务测试"
    echo "=========================================="
    
    # 5. 并行任务
    test_endpoint "Parallel Tasks" "GET" "/async/parallel"
    
    echo ""
    echo "=========================================="
    echo "错误处理测试"
    echo "=========================================="
    
    # 6. 错误处理（正常）
    test_endpoint "Error Handling (Success)" "GET" "/async/error?fail=false"
    
    # 7. 错误处理（失败）
    test_endpoint "Error Handling (Failure)" "GET" "/async/error?fail=true"
    
    echo ""
    echo "=========================================="
    echo "超时处理测试"
    echo "=========================================="
    
    # 8. 超时处理
    test_endpoint "Timeout Handling" "GET" "/async/timeout"
    
    echo ""
    echo "=========================================="
    echo "高级协程工具测试"
    echo "=========================================="
    
    # 9. 延迟示例
    test_endpoint "Coroutine Delay" "GET" "/coroutine/delay"
    
    # 10. 并行示例
    test_endpoint "Coroutine Parallel" "GET" "/coroutine/parallel"
    
    # 11. 重试示例
    test_endpoint "Coroutine Retry" "GET" "/coroutine/retry"
    
    # 12. 超时示例
    test_endpoint "Coroutine Timeout" "GET" "/coroutine/timeout"
    
    # 13. 错误处理示例
    test_endpoint "Coroutine Error Handling" "GET" "/coroutine/error-handling"
    
    # 14. 锁示例
    test_endpoint "Coroutine Lock" "GET" "/coroutine/lock"
    
    echo ""
    echo "=========================================="
    echo "性能测试"
    echo "=========================================="
    
    # 15. 并发请求测试
    echo "Running concurrent requests..."
    start_time=$(date +%s.%N)
    
    for i in {1..10}; do
        curl -s "${BASE_URL}/async/parallel" > /dev/null &
    done
    wait
    
    end_time=$(date +%s.%N)
    duration=$(echo "$end_time - $start_time" | bc)
    echo -e "${GREEN}✓ 10 concurrent requests completed in ${duration}s${NC}"
    
    echo ""
    echo "=========================================="
    echo "测试完成"
    echo "=========================================="
    echo ""
    echo "Summary:"
    echo "  All basic tests passed!"
    echo "  Coroutine features working correctly"
    echo ""
}

# 执行测试
main
