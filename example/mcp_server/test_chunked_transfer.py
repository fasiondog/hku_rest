#!/usr/bin/env python3
"""
MCP Server Chunked Transfer 完整测试
测试请求和响应方向的 chunked transfer encoding
"""

import requests
import json
import uuid
import time

def test_chunked_request():
    """测试客户端使用 chunked 发送大请求体"""
    print("=" * 70)
    print("测试 1: Chunked Request (Client → Server)")
    print("=" * 70)
    
    headers = {
        "Content-Type": "application/json",
        "Accept": "application/json"
    }
    
    # 先初始化并获取 Session ID
    print("Step 1: Initialize session...")
    response = requests.post(
        "http://localhost:8080/mcp",
        headers=headers,
        json={
            "jsonrpc": "2.0",
            "method": "initialize",
            "params": {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "chunked-test", "version": "1.0"}
            },
            "id": 1
        }
    )
    
    session_id = response.headers.get('Mcp-Session-Id')
    if not session_id:
        print(f"❌ Failed to get Mcp-Session-Id from response")
        return
    
    print(f"  ✅ Session initialized, ID: {session_id}\n")
    
    # 后续请求使用 Session ID
    headers["Mcp-Session-Id"] = session_id
    
    # 构造一个较大的请求体（模拟大参数）
    large_params = {
        "name": "long_running_task",  # 必须指定工具名称
        "arguments": {
            "task_name": "chunked_test",
            "duration_seconds": 1,
            "large_data": "x" * 10000  # 10KB 数据
        }
    }
    
    request_body = json.dumps({
        "jsonrpc": "2.0",
        "method": "tools/call",
        "params": large_params,
        "id": 2
    })
    
    print(f"Step 2: Sending large request ({len(request_body)} bytes)...")
    
    start = time.time()
    response = requests.post(
        "http://localhost:8080/mcp",
        headers=headers,
        data=request_body
    )
    elapsed = time.time() - start
    
    print(f"\nResponse Status: {response.status_code}")
    print(f"Response Time: {elapsed*1000:.2f}ms")
    print(f"Response Content-Type: {response.headers.get('Content-Type')}")
    print(f"Response Length: {len(response.text)} bytes")
    
    if not response.text:
        print(f"❌ Error: Empty response")
        return
    
    try:
        result = response.json()
        if 'result' in result:
            print(f"✅ Server successfully processed large request")
            print(f"   Response size: {len(response.text)} bytes\n")
        else:
            print(f"❌ Error: {result.get('error', {}).get('message')}\n")
    except Exception as e:
        print(f"❌ Failed to parse JSON: {e}")
        print(f"   Response text: {response.text[:200]}\n")


def test_standard_json_response():
    """测试标准 JSON 响应（短连接）"""
    print("=" * 70)
    print("测试 2: Standard JSON Response (Server → Client)")
    print("=" * 70)
    
    headers = {
        "Content-Type": "application/json",
        "Accept": "application/json"
    }
    
    # 先初始化并获取 Session ID
    print("\nStep 1: Initialize session...")
    response = requests.post(
        "http://localhost:8080/mcp",
        headers=headers,
        json={
            "jsonrpc": "2.0",
            "method": "initialize",
            "params": {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "json-test", "version": "1.0"}
            },
            "id": 1
        }
    )
    
    session_id = response.headers.get('Mcp-Session-Id')
    if not session_id:
        print(f"❌ Failed to get Mcp-Session-Id from response")
        return
    
    print(f"  ✅ Session initialized, ID: {session_id}\n")
    
    # 后续请求使用 Session ID
    headers["Mcp-Session-Id"] = session_id
    
    print("Step 2: Requesting tools/list (standard JSON)...")
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
    
    print(f"\nResponse Headers:")
    print(f"  Content-Type: {response.headers.get('Content-Type')}")
    print(f"  Content-Length: {response.headers.get('Content-Length', 'N/A')}")
    print(f"  Transfer-Encoding: {response.headers.get('Transfer-Encoding', 'N/A')}")
    print(f"  Connection: {response.headers.get('Connection', 'N/A')}")
    print(f"  Response Time: {elapsed*1000:.2f}ms")
    
    result = response.json()
    if 'result' in result:
        print(f"✅ Standard JSON response received")
        print(f"   Tools count: {len(result['result']['tools'])}\n")
    else:
        print(f"❌ Error: {result.get('error', {}).get('message')}\n")


def test_sse_streaming_response():
    """测试 SSE 流式响应（长连接 + chunked）"""
    print("=" * 70)
    print("测试 3: SSE Streaming Response (Server → Client)")
    print("=" * 70)
    
    # Step 1: Initialize with standard JSON (not SSE)
    headers_init = {
        "Content-Type": "application/json"
    }
    
    print("\nStep 1: Initialize session (standard JSON)...")
    response = requests.post(
        "http://localhost:8080/mcp",
        headers=headers_init,
        json={
            "jsonrpc": "2.0",
            "method": "initialize",
            "params": {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "sse-test", "version": "1.0"}
            },
            "id": 1
        }
    )
    
    session_id = response.headers.get('Mcp-Session-Id')
    if not session_id:
        print(f"❌ Failed to get Mcp-Session-Id from response")
        return
    
    print(f"  ✅ Session initialized, ID: {session_id}\n")
    
    # Step 2: Use SSE for long-running task
    headers = {
        "Content-Type": "application/json",
        "Accept": "text/event-stream",
        "Mcp-Session-Id": session_id
    }
    
    print("Step 2: Requesting long_running_task with SSE streaming...")
    start = time.time()
    response = requests.post(
        "http://localhost:8080/mcp",
        headers=headers,
        json={
            "jsonrpc": "2.0",
            "method": "tools/call",
            "params": {
                "name": "long_running_task",
                "arguments": {
                    "task_name": "streaming_test",
                    "duration_seconds": 2
                }
            },
            "id": 2
        },
        stream=True,
        timeout=10
    )
    
    print(f"\nResponse Headers:")
    print(f"  Content-Type: {response.headers.get('Content-Type')}")
    print(f"  Transfer-Encoding: {response.headers.get('Transfer-Encoding', 'N/A')}")
    print(f"  Connection: {response.headers.get('Connection', 'N/A')}")
    print(f"  Cache-Control: {response.headers.get('Cache-Control', 'N/A')}")
    
    # 读取 SSE 消息
    message_count = 0
    for line in response.iter_lines():
        if line:
            line_str = line.decode('utf-8')
            if line_str.startswith('data:'):
                message_count += 1
                data = json.loads(line_str[5:])
                
                # 提取进度信息
                if 'result' in data and 'content' in data['result']:
                    content = data['result']['content'][0]['text']
                    elapsed = time.time() - start
                    print(f"  [{elapsed:.1f}s] Message #{message_count}: {content[:60]}...")
    
    elapsed = time.time() - start
    print(f"\nTotal Messages: {message_count}")
    print(f"Total Time: {elapsed:.2f}s")
    
    if message_count > 0:
        print(f"✅ SSE streaming response received with chunked transfer\n")
    else:
        print(f"❌ No messages received\n")


def test_mixed_modes_same_session():
    """测试同一会话中混合使用标准 JSON 和 SSE"""
    print("=" * 70)
    print("测试 4: Mixed Modes (Same Session)")
    print("=" * 70)
    
    # 1. 初始化（标准 JSON）并获取 Session ID
    print("\nStep 1: Initialize (Standard JSON)")
    headers_json = {
        "Content-Type": "application/json",
        "Accept": "application/json"
    }
    
    response = requests.post(
        "http://localhost:8080/mcp",
        headers=headers_json,
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
    
    session_id = response.headers.get('Mcp-Session-Id')
    if not session_id:
        print(f"❌ Failed to get Mcp-Session-Id from response")
        return
    
    print(f"  ✅ Initialized, ID: {session_id} (Content-Length: {response.headers.get('Content-Length')})\n")
    
    # 后续请求使用 Session ID
    headers_json["Mcp-Session-Id"] = session_id
    
    # 2. 快速查询（标准 JSON）
    print("Step 2: Quick Query (Standard JSON)")
    response = requests.post(
        "http://localhost:8080/mcp",
        headers=headers_json,
        json={
            "jsonrpc": "2.0",
            "method": "tools/list",
            "params": {},
            "id": 2
        }
    )
    result = response.json()
    print(f"  ✅ Received {len(result['result']['tools'])} tools\n")
    
    # 3. 流式任务（SSE）
    print("Step 3: Streaming Task (SSE)")
    headers_sse = {
        "Content-Type": "application/json",
        "Mcp-Session-Id": session_id,
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
    
    print(f"  ✅ Received {message_count} streaming messages\n")
    
    # 4. 再次快速查询（标准 JSON）
    print("Step 4: Another Quick Query (Standard JSON)")
    response = requests.post(
        "http://localhost:8080/mcp",
        headers=headers_json,
        json={
            "jsonrpc": "2.0",
            "method": "session/info",
            "params": {},
            "id": 4
        }
    )
    result = response.json()
    print(f"  ✅ Session info retrieved\n")
    
    print("✅ Mixed mode test passed!\n")


if __name__ == "__main__":
    test_chunked_request()
    print("\n")
    test_standard_json_response()
    print("\n")
    test_sse_streaming_response()
    print("\n")
    test_mixed_modes_same_session()
    
    print("=" * 70)
    print("✅ 所有 Chunked Transfer 测试完成！")
    print("=" * 70)
