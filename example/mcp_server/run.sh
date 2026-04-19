#!/bin/bash

# MCP Server 快速启动脚本

echo "=========================================="
echo "MCP Server - Quick Start"
echo "=========================================="
echo ""

# 检查是否已构建
if [ ! -f "../../build/release/macosx/arm64/lib/mcp_server" ]; then
    echo "Building MCP Server..."
    cd ../..
    xmake build mcp_server
    if [ $? -ne 0 ]; then
        echo "Build failed!"
        exit 1
    fi
    cd example/mcp_server
fi

# 启动服务器
echo "Starting MCP Server..."
cd ../../build/release/macosx/arm64/lib
./mcp_server &
SERVER_PID=$!

echo "Server PID: $SERVER_PID"
echo ""
echo "Waiting for server to start..."
sleep 2

# 检查服务器是否成功启动
if ps -p $SERVER_PID > /dev/null; then
    echo "✅ Server started successfully!"
    echo ""
    echo "Endpoints:"
    echo "  - Health Check: http://localhost:8080/health"
    echo "  - MCP Endpoint: http://localhost:8080/mcp"
    echo ""
    echo "Quick Test:"
    echo "  curl -s http://localhost:8080/health | python3 -m json.tool"
    echo ""
    echo "To stop the server: kill $SERVER_PID"
    echo ""
    
    # 保持脚本运行
    wait $SERVER_PID
else
    echo "❌ Server failed to start!"
    exit 1
fi
