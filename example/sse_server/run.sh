#!/bin/bash

# SSE Server 快速启动和测试脚本

set -e

echo "=========================================="
echo "SSE Server - Quick Start Script"
echo "=========================================="
echo ""

# 检查是否在正确的目录
if [ ! -f "xmake.lua" ]; then
    echo "Error: Please run this script from the project root directory"
    exit 1
fi

# 构建项目
echo "Building sse_server..."
xmake build sse_server

echo ""
echo "Starting SSE Server..."
echo ""

# 运行服务器（后台）
xmake r sse_server &
SERVER_PID=$!

# 等待服务器启动
sleep 2

echo ""
echo "=========================================="
echo "Server is running (PID: $SERVER_PID)"
echo "=========================================="
echo ""
echo "Available endpoints:"
echo "  1. Simple SSE:  http://localhost:8081/sse/simple"
echo "  2. Full Stream: http://localhost:8081/sse/stream"
echo ""
echo "Quick tests:"
echo "  curl -N http://localhost:8081/sse/simple"
echo "  python3 example/sse_server/test_sse_simple.py simple"
echo ""
echo "Press Ctrl+C to stop the server"
echo "=========================================="
echo ""

# 捕获退出信号
trap "kill $SERVER_PID 2>/dev/null; echo 'Server stopped'; exit 0" INT TERM

# 保持脚本运行
wait $SERVER_PID
