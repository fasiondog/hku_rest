#!/bin/bash

# MQTT Broker 自动化测试脚本
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="/Users/fasiondog/workspace/hku_rest"

echo "=========================================="
echo "MQTT Broker Automated Test"
echo "=========================================="
echo ""

# 检查是否已安装 paho-mqtt
echo "[1/4] Checking dependencies..."
if ! python3 -c "import paho.mqtt.client" 2>/dev/null; then
    echo "Installing paho-mqtt..."
    pip install paho-mqtt
fi
echo "✓ Dependencies OK"
echo ""

# 构建项目
echo "[2/4] Building project..."
cd "$PROJECT_ROOT"
xmake build mqtt_broker > /dev/null 2>&1 || {
    echo "✗ Build failed"
    exit 1
}
echo "✓ Build successful"
echo ""

# 启动 MQTT Broker
echo "[3/4] Starting MQTT Broker..."
cd "$SCRIPT_DIR"

# 查找可执行文件
BROKER_BIN=$(find "$PROJECT_ROOT/build" -name "mqtt_broker" -type f 2>/dev/null | head -n 1)

if [ -z "$BROKER_BIN" ]; then
    echo "✗ Broker binary not found"
    exit 1
fi

echo "Broker binary: $BROKER_BIN"

# 在后台启动 Broker
"$BROKER_BIN" &
BROKER_PID=$!

echo "Broker PID: $BROKER_PID"

# 等待 Broker 启动
echo "Waiting for broker to start (5 seconds)..."
sleep 5

# 检查 Broker 是否在运行
if ! kill -0 $BROKER_PID 2>/dev/null; then
    echo "✗ Failed to start broker"
    exit 1
fi

echo "✓ Broker is running on port 1883"
echo ""

# 运行测试
echo "[4/4] Running tests..."
python3 test_mqtt_broker.py
TEST_RESULT=$?

echo ""
echo "Stopping broker (PID: $BROKER_PID)..."
kill $BROKER_PID 2>/dev/null || true
wait $BROKER_PID 2>/dev/null || true

echo ""
if [ $TEST_RESULT -eq 0 ]; then
    echo "=========================================="
    echo "✓ All tests passed!"
    echo "=========================================="
    exit 0
else
    echo "=========================================="
    echo "✗ Some tests failed"
    echo "=========================================="
    exit 1
fi
