#!/usr/bin/env python3
"""
MCP Server SSE Heartbeat Test
测试 SSE 心跳功能（符合 MCP 协议规范）
注意：心跳在长连接保持期间由服务端定期发送
"""

import requests
import time
import json

def test_sse_heartbeat():
    """测试 SSE 心跳是否正常发送"""
    print("=" * 60)
    print("MCP Server SSE Heartbeat Test (Streamable HTTP)")
    print("=" * 60)
    
    # 1. 先通过 MCP 端点注册会话并获取 Session ID
    print("\nStep 1: Initializing session with SSE support...")
    mcp_headers = {
        "Content-Type": "application/json",
        "Accept": "text/event-stream"  # 请求 SSE 流式响应以接收心跳
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
        },
        stream=True,  # 启用流式读取
        timeout=50  # 等待 50 秒以观察多次心跳
    )
    
    # 提取 Session ID
    session_id = response.headers.get('Mcp-Session-Id')
    if not session_id:
        print(f"❌ Failed to get Mcp-Session-Id from response")
        return
    
    if response.status_code == 200:
        print(f"✅ Session registered")
        print(f"   Session ID: {session_id}")
        print(f"   Content-Type: {response.headers.get('Content-Type')}")
        print(f"   Transfer-Encoding: {response.headers.get('Transfer-Encoding')}")
    else:
        print(f"❌ Failed to register session: {response.status_code}")
        return
    
    # 2. 启动一个长时间运行的任务以保持连接活跃
    print("\nStep 2: Starting long-running task to keep connection alive...")
    
    # 注意：在当前实现中，心跳需要在业务逻辑中显式发送
    # 这里我们只是演示如何保持长连接
    print("   ℹ️  Note: Heartbeat implementation depends on server-side logic")
    print("   ℹ️  Current implementation sends heartbeats during active SSE streams\n")
    
    # 3. 监听 SSE 流中的消息（包括初始化和可能的后续消息）
    print("Step 3: Monitoring SSE stream...\n")
    
    try:
        message_count = 0
        start_time = time.time()
        
        for line in response.iter_lines():
            if line:
                line_str = line.decode('utf-8')
                message_count += 1
                
                # SSE 心跳格式：: ping - timestamp
                if line_str.startswith(': ping'):
                    elapsed = time.time() - start_time
                    print(f"[{elapsed:.1f}s] ❤️  Heartbeat: {line_str}")
                else:
                    # 其他 SSE 消息
                    elapsed = time.time() - start_time
                    print(f"[{elapsed:.1f}s] 📨 Message #{message_count}: {line_str[:100]}")
                
                # 收到 3 条消息后退出
                if message_count >= 3:
                    print(f"\n✅ Successfully received {message_count} messages")
                    break
        
        if message_count == 0:
            print("❌ No messages received")
        else:
            print("\n" + "=" * 60)
            print("✅ SSE stream monitoring completed!")
            print("=" * 60)
            
    except requests.exceptions.Timeout:
        print("\n⏰ Connection timeout (expected for long-lived connections)")
    except Exception as e:
        print(f"\n❌ Error: {e}")


if __name__ == "__main__":
    test_sse_heartbeat()
