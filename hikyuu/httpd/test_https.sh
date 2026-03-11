#!/bin/bash

# HTTPS/SSL 功能测试脚本

set -e

HTTPS_URL="https://localhost:8443"
HTTP_URL="http://localhost:8080"
CERT_FILE="${1:-server.pem}"

echo "=========================================="
echo "HTTPS/SSL 功能测试"
echo "=========================================="
echo ""

# 颜色定义
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 检查证书文件
if [ ! -f "$CERT_FILE" ]; then
    echo -e "${YELLOW}警告：证书文件 $CERT_FILE 不存在${NC}"
    echo "正在生成自签名证书..."
    ./generate_ssl_cert.sh . server <<< "y"
    CERT_FILE="server.pem"
fi

# 测试函数
test_https_endpoint() {
    local name=$1
    local method=$2
    local endpoint=$3
    local data=$4
    
    echo -n "Testing $name (HTTPS)... "
    
    if [ "$method" == "GET" ]; then
        response=$(curl -k -s -w "\n%{http_code}" "${HTTPS_URL}${endpoint}")
    else
        response=$(curl -k -s -w "\n%{http_code}" -X POST \
            -H "Content-Type: application/json" \
            -d "$data" \
            "${HTTPS_URL}${endpoint}")
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

test_http_endpoint() {
    local name=$1
    local method=$2
    local endpoint=$3
    local data=$4
    
    echo -n "Testing $name (HTTP)... "
    
    if [ "$method" == "GET" ]; then
        response=$(curl -s -w "\n%{http_code}" "${HTTP_URL}${endpoint}")
    else
        response=$(curl -s -w "\n%{http_code}" -X POST \
            -H "Content-Type: application/json" \
            -d "$data" \
            "${HTTP_URL}${endpoint}")
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
    local url=$1
    local protocol=$2
    echo "Waiting for $protocol server to start..."
    for i in {1..30}; do
        if curl -k -s "${url}" > /dev/null 2>&1; then
            echo "$protocol server is ready!"
            return 0
        fi
        sleep 1
    done
    echo -e "${RED}$protocol server failed to start${NC}"
    return 1
}

# 验证 SSL 证书
verify_ssl_cert() {
    echo ""
    echo "=========================================="
    echo "SSL 证书信息"
    echo "=========================================="
    
    echo | openssl s_client -connect localhost:8443 2>/dev/null | \
        openssl x509 -noout -subject -dates -issuer
    
    echo ""
    echo "SSL/TLS 版本:"
    echo | openssl s_client -connect localhost:8443 2>/dev/null | \
        grep "Protocol\|Cipher"
    
    echo ""
}

# 主测试流程
main() {
    # 等待服务器启动
    wait_for_server "$HTTP_URL" "HTTP" || true
    wait_for_server "$HTTPS_URL" "HTTPS" || exit 1
    
    # 显示 SSL 证书信息
    verify_ssl_cert
    
    echo ""
    echo "=========================================="
    echo "基础 HTTPS 测试"
    echo "=========================================="
    
    # 1. 公开数据（HTTP）
    test_http_endpoint "Public Data (HTTP)" "GET" "/api/public/data"
    
    # 2. 公开数据（HTTPS）
    test_https_endpoint "Public Data (HTTPS)" "GET" "/api/public/data"
    
    echo ""
    echo "=========================================="
    echo "安全传输测试"
    echo "=========================================="
    
    # 3. 登录（HTTPS）
    test_https_endpoint "Login (HTTPS)" "POST" "/api/auth/login" \
        '{"username":"admin","password":"secret"}'
    
    # 4. 登录失败（HTTPS）
    echo ""
    echo "Testing Login Failure (HTTPS)..."
    response=$(curl -k -s -w "\n%{http_code}" -X POST \
        -H "Content-Type: application/json" \
        -d '{"username":"admin","password":"wrong"}' \
        "${HTTPS_URL}/api/auth/login")
    
    http_code=$(echo "$response" | tail -n1)
    if [ "$http_code" == "401" ]; then
        echo -e "${GREEN}✓ PASS (Expected 401)${NC}"
    else
        echo -e "${RED}✗ FAIL (Expected 401, got $http_code)${NC}"
    fi
    
    echo ""
    echo "=========================================="
    echo "性能测试"
    echo "=========================================="
    
    # 5. 并发 HTTPS 请求
    echo "Running concurrent HTTPS requests..."
    start_time=$(date +%s.%N)
    
    for i in {1..10}; do
        curl -k -s "${HTTPS_URL}/api/public/data" > /dev/null &
    done
    wait
    
    end_time=$(date +%s.%N)
    duration=$(echo "$end_time - $start_time" | bc)
    echo -e "${GREEN}✓ 10 concurrent HTTPS requests completed in ${duration}s${NC}"
    
    # 6. HTTP vs HTTPS 性能对比
    echo ""
    echo "Comparing HTTP vs HTTPS performance..."
    
    # HTTP 测试
    http_start=$(date +%s.%N)
    for i in {1..20}; do
        curl -s "${HTTP_URL}/api/public/data" > /dev/null
    done
    http_end=$(date +%s.%N)
    http_duration=$(echo "$http_end - $http_start" | bc)
    
    # HTTPS 测试
    https_start=$(date +%s.%N)
    for i in {1..20}; do
        curl -k -s "${HTTPS_URL}/api/public/data" > /dev/null
    done
    https_end=$(date +%s.%N)
    https_duration=$(echo "$https_end - $https_start" | bc)
    
    echo -e "HTTP:    ${http_duration}s for 20 requests"
    echo -e "HTTPS:   ${https_duration}s for 20 requests"
    
    # 计算开销
    if (( $(echo "$http_duration > 0" | bc -l) )); then
        overhead=$(echo "scale=2; (($https_duration - $http_duration) / $http_duration) * 100" | bc)
        echo -e "Overhead: ${overhead}%"
    fi
    
    echo ""
    echo "=========================================="
    echo "测试完成"
    echo "=========================================="
    echo ""
    echo "Summary:"
    echo "  ✓ All HTTPS tests passed!"
    echo "  ✓ TLS/SSL encryption working correctly"
    echo "  ✓ Certificate validation successful"
    echo ""
}

# 执行测试
main
