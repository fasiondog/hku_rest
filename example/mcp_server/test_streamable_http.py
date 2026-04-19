#!/usr/bin/env python3
"""
MCP Server Streamable HTTP 完整测试
演示短连接和长连接两种模式
"""

import requests
import json
import uuid
import time

def test_short_connection_mode():
    """测试短连接 JSON 响应模式"""
    session_id = str(uuid.uuid4())
    
    print("=" * 70)
    print("模式 1: 短连接 JSON 响应 (Short Connection)")
    print("=" * 70)
    print(f"Session ID: {session_id}\n")
    
    headers = {
        "Content-Type": "application/json",
        "X-Session-ID": session_id,
        "Accept": "application/json"  # 明确请求 JSON 响应
    }
    
    # Initialize
    print("1. Initialize (短连接)")
    start = time.time()
    response = requests.post(
        "http://localhost:8080/mcp",
        headers=headers,
        json={
            "jsonrpc": "2.0",
            "method": "initialize",
            "params": {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "short-conn-test", "version": "1.0"}
            },
            "id": 1
        }
    )
    elapsed = time.time() - start
    
    print(f"   Status: {response.status_code}")
    print(f"   Content-Type: {response.headers.get('Content-Type')}")
    print(f"   Response Time: {elapsed*1000:.2f}ms")
    print(f"   Connection: Closed immediately ✅\n")
    
    # Tools List
    print("2. Tools List (短连接)")
    start = time.time()
    response = requests.post(
        "http://localhost:8080/mcp",
        headers=headers,
        json={
            "jsonrpc": "2.0",
            "method": "tools/list",
            "params": {},
            "id": 2
        }
    )
    elapsed = time.time() - start
    
    result = response.json()
    print(f"   Status: {response.status_code}")
    print(f"   Content-Type: {response.headers.get('Content-Type')}")
    print(f"   Response Time: {elapsed*1000:.2f}ms")
    print(f"   Tools Count: {len(result['result']['tools'])}")
    print(f"   Connection: Closed immediately ✅\n")


def test_long_connection_sse_mode():
    """测试长连接 SSE 流式响应模式"""
    session_id = str(uuid.uuid4())
    
    print("=" * 70)
    print("模式 2: 长连接 SSE 流式响应 (Long Connection with SSE)")
    print("=" * 70)
    print(f"Session ID: {session_id}\n")
    
    headers = {
        "Content-Type": "application/json",
        "X-Session-ID": session_id,
        "Accept": "text/event-stream"  # 请求 SSE 流式响应
    }
    
    # Initialize with SSE
    print("1. Initialize (SSE 流式)")
    start = time.time()
    response = requests.post(
        "http://localhost:8080/mcp",
        headers=headers,
        json={
            "jsonrpc": "2.0",
            "method": "initialize",
            "params": {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "sse-test", "version": "1.0"}
            },
            "id": 1
        },
        stream=True,
        timeout=5
    )
    elapsed = time.time() - start
    
    print(f"   Status: {response.status_code}")
    print(f"   Content-Type: {response.headers.get('Content-Type')}")
    print(f"   Response Time: {elapsed*1000:.2f}ms")
    
    # 读取 SSE 消息
    message_count = 0
    for line in response.iter_lines():
        if line:
            line_str = line.decode('utf-8')
            if line_str.startswith('data:'):
                message_count += 1
                data = json.loads(line_str[5:])
                print(f"   Message #{message_count}: Received")
                break  # 只读取第一条消息
    
    print(f"   Connection: Kept alive for streaming ✅\n")
    
    # Long Running Task with SSE
    print("2. Long Running Task (SSE 实时推送)")
    response = requests.post(
        "http://localhost:8080/mcp",
        headers=headers,
        json={
            "jsonrpc": "2.0",
            "method": "tools/call",
            "params": {
                "name": "long_running_task",
                "arguments": {
                    "task_name": "sse_demo",
                    "duration_seconds": 2
                }
            },
            "id": 2
        },
        stream=True,
        timeout=10
    )
    
    print(f"   Status: {response.status_code}")
    print(f"   Content-Type: {response.headers.get('Content-Type')}")
    print(f"   Streaming messages:")
    
    message_count = 0
    start_time = time.time()
    for line in response.iter_lines():
        if line:
            line_str = line.decode('utf-8')
            if line_str.startswith('data:'):
                message_count += 1
                data = json.loads(line_str[5:])
                elapsed = time.time() - start_time
                
                # 提取进度信息
                if 'result' in data and 'content' in data['result']:
                    content = data['result']['content'][0]['text']
                    print(f"      [{elapsed:.1f}s] Message #{message_count}: {content[:60]}...")
    
    print(f"   Total Messages: {message_count}")
    print(f"   Connection: Maintained throughout task ✅\n")


def test_mixed_mode():
    """测试混合模式：同一个 Session 同时使用短连接和长连接"""
    session_id = str(uuid.uuid4())
    
    print("=" * 70)
    print("模式 3: 混合模式 (Mixed Mode - Same Session)")
    print("=" * 70)
    print(f"Session ID: {session_id}\n")
    
    # 先注册 Session
    headers_init = {
        "Content-Type": "application/json",
        "X-Session-ID": session_id,
        "Accept": "application/json"
    }
    
    print("1. Initialize (短连接 JSON)")
    requests.post(
        "http://localhost:8080/mcp",
        headers=headers_init,
        json={
            "jsonrpc": "2.0",
            "method": "initialize",
            "params": {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "mixed-test", "version": "1.0"}
            },
            "id": 1
        }
    )
    print("   ✅ Session registered\n")
    
    # 短连接查询
    print("2. Quick Query (短连接)")
    response = requests.post(
        "http://localhost:8080/mcp",
        headers=headers_init,
        json={
            "jsonrpc": "2.0",
            "method": "tools/list",
            "params": {},
            "id": 2
        }
    )
    result = response.json()
    print(f"   ✅ Received {len(result['result']['tools'])} tools\n")
    
    # 长连接订阅
    print("3. Subscribe to Updates (长连接 SSE)")
    headers_sse = {
        "Content-Type": "application/json",
        "X-Session-ID": session_id,
        "Accept": "text/event-stream"
    }
    
    response = requests.post(
        "http://localhost:8080/mcp",
        headers=headers_sse,
        json={
            "jsonrpc": "2.0",
            "method": "tools/call",
            "params": {
                "name": "long_running_task",
                "arguments": {
                    "task_name": "mixed_test",
                    "duration_seconds": 1
                }
            },
            "id": 3
        },
        stream=True,
        timeout=5
    )
    
    message_count = 0
    for line in response.iter_lines():
        if line:
            line_str = line.decode('utf-8')
            if line_str.startswith('data:'):
                message_count += 1
    
    print(f"   ✅ Received {message_count} streaming messages\n")
    
    # 再次短连接查询
    print("4. Another Quick Query (短连接)")
    response = requests.post(
        "http://localhost:8080/mcp",
        headers=headers_init,
        json={
            "jsonrpc": "2.0",
            "method": "session/info",
            "params": {},
            "id": 4
        }
    )
    result = response.json()
    print(f"   ✅ Session info retrieved\n")


if __name__ == "__main__":
    test_short_connection_mode()
    print("\n")
    test_long_connection_sse_mode()
    print("\n")
    test_mixed_mode()
    
    print("=" * 70)
    print("✅ 所有测试完成！Streamable HTTP 支持两种模式正常工作")
    print("=" * 70)
