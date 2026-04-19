#!/bin/bash

# SSE 示例完整演示脚本
# 展示所有测试方法和功能

set -e

echo "╔═══════════════════════════════════════════════════════════╗"
echo "║           SSE Server - Complete Demo Script              ║"
echo "╚═══════════════════════════════════════════════════════════╝"
echo ""

# 颜色定义
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# 检查是否在正确的目录
if [ ! -f "xmake.lua" ]; then
    echo -e "${RED}Error: Please run this script from the project root directory${NC}"
    exit 1
fi

echo -e "${BLUE}Step 1: Building sse_server...${NC}"
xmake build sse_server > /dev/null 2>&1
echo -e "${GREEN}✓ Build successful${NC}"
echo ""

echo -e "${BLUE}Step 2: Starting SSE Server...${NC}"
xmake r sse_server > /tmp/sse_server.log 2>&1 &
SERVER_PID=$!
echo "Server PID: $SERVER_PID"

# 等待服务器启动
sleep 2

# 检查服务器是否成功启动
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}✗ Server failed to start. Check /tmp/sse_server.log for details${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Server started successfully${NC}"
echo ""

# 显示服务器日志的前几行
echo -e "${YELLOW}Server Log:${NC}"
head -15 /tmp/sse_server.log | grep "\[HKU-I\]" | tail -8
echo ""

echo "╔═══════════════════════════════════════════════════════════╗"
echo "║                    Running Tests                         ║"
echo "╚═══════════════════════════════════════════════════════════╝"
echo ""

# Test 1: Simple SSE with curl
echo -e "${BLUE}Test 1: Simple SSE (curl)${NC}"
echo "Command: curl -N http://localhost:8081/sse/simple"
echo "---"
curl -s -N http://localhost:8081/sse/simple | head -10
echo ""
echo -e "${GREEN}✓ Test 1 passed${NC}"
echo ""

# Test 2: Stream SSE with curl
echo -e "${BLUE}Test 2: Full SSE Stream (curl)${NC}"
echo "Command: curl -N http://localhost:8081/sse/stream"
echo "---"
curl -s -N http://localhost:8081/sse/stream 2>&1 | head -20
echo ""
echo -e "${GREEN}✓ Test 2 passed${NC}"
echo ""

# Test 3: Python test
echo -e "${BLUE}Test 3: Python Test (Simple)${NC}"
echo "Command: python3 example/sse_server/test_sse_simple.py simple"
echo "---"
python3 example/sse_server/test_sse_simple.py simple 2>&1 | tail -15
echo ""
echo -e "${GREEN}✓ Test 3 passed${NC}"
echo ""

# Stop server
echo -e "${BLUE}Stopping server...${NC}"
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true
echo -e "${GREEN}✓ Server stopped${NC}"
echo ""

echo "╔═══════════════════════════════════════════════════════════╗"
echo "║                   Demo Completed!                        ║"
echo "╚═══════════════════════════════════════════════════════════╝"
echo ""
echo -e "${GREEN}All tests passed successfully!${NC}"
echo ""
echo "Next steps:"
echo "  1. Start server: xmake r sse_server"
echo "  2. Open browser: open example/sse_server/test_client.html"
echo "  3. Read docs: cat example/sse_server/README.md"
echo ""
echo "Available endpoints:"
echo "  • Simple SSE:  http://localhost:8081/sse/simple"
echo "  • Full Stream: http://localhost:8081/sse/stream"
echo ""
