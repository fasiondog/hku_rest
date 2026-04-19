#!/usr/bin/env python3
"""
MCP Server SSE Heartbeat Test
测试 SSE 心跳功能（符合 MCP 协议规范）
"""

import requests
import time
import uuid
import json

def test_sse_heartbeat():
    """测试 SSE 心跳是否正常发送"""
    session_id = str(uuid.uuid4())
    
    print("=" * 60)
    print("MCP Server SSE Heartbeat Test")
    print("=" * 60)
    print(f"\nSession ID: {session_id}")
    
    # 1. 先通过 MCP 端点注册会话
    print("\nStep 1: Registering session via /mcp endpoint...")
    mcp_headers = {
        "Content-Type": "application/json",
        "X-Session-ID": session_id
    }
    
    response = requests.post(
        "http://localhost:8080/mcp",
        headers=mcp_headers,
        json={
            "jsonrpc": "2.0",
            "method": "initialize",
            "params": {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {
                    "name": "heartbeat-test-client",
                    "version": "1.0.0"
                }
            },
            "id": 1
        }
    )
    
    if response.status_code == 200:
        result = response.json()
        print(f"✅ Session registered: {result['result']['serverInfo']['name']}")
    else:
        print(f"❌ Failed to register session: {response.status_code}")
        return
    
    # 2. 连接到 SSE 端点
    print("\nStep 2: Connecting to SSE endpoint...")
    sse_headers = {
        "X-Session-ID": session_id,
        "Accept": "text/event-stream"
    }
    
    try:
        response = requests.get(
            "http://localhost:8080/sse",
            headers=sse_headers,
            stream=True,
            timeout=50  # 等待 50 秒以观察多次心跳
        )
        
        heartbeat_count = 0
        start_time = time.time()
        
        print("\nListening for heartbeat messages (waiting ~45 seconds)...\n")
        
        for line in response.iter_lines():
            if line:
                line_str = line.decode('utf-8')
                
                # SSE 心跳格式：: ping - timestamp
                if line_str.startswith(': ping'):
                    heartbeat_count += 1
                    elapsed = time.time() - start_time
                    print(f"[{elapsed:.1f}s] ❤️  Heartbeat #{heartbeat_count}: {line_str}")
                    
                    # 收到 3 次心跳后退出（约 45 秒）
                    if heartbeat_count >= 3:
                        print(f"\n✅ Successfully received {heartbeat_count} heartbeats")
                        print(f"   Average interval: ~{elapsed/heartbeat_count:.1f} seconds")
                        break
        
        if heartbeat_count == 0:
            print("❌ No heartbeat received")
        else:
            print("\n" + "=" * 60)
            print("✅ Heartbeat test passed!")
            print("=" * 60)
            
    except requests.exceptions.Timeout:
        print("\n⏰ Connection timeout")
    except Exception as e:
        print(f"\n❌ Error: {e}")


if __name__ == "__main__":
    test_sse_heartbeat()
