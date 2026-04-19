#!/usr/bin/env python3
"""
MCP Server SSE (Server-Sent Events) Test Client
测试 SSE 进度推送功能
"""

import requests
import json
import uuid
import time
import threading

class MCPsseTestClient:
    def __init__(self, base_url="http://localhost:8080"):
        self.base_url = base_url
        self.mcp_url = f"{base_url}/mcp"
        self.sse_url = f"{base_url}/sse"
        self.session_id = str(uuid.uuid4())
        self.request_id = 0
        self.session = requests.Session()
        self.received_messages = []
        
    def send_request(self, method, params=None):
        """发送 JSON-RPC 请求"""
        self.request_id += 1
        
        request = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params or {},
            "id": self.request_id
        }
        
        headers = {
            "Content-Type": "application/json",
            "X-Session-ID": self.session_id
        }
        
        response = self.session.post(self.mcp_url, json=request, headers=headers)
        return response.json()
    
    def listen_sse(self, timeout=30):
        """监听 SSE 事件（在后台线程中运行）"""
        print(f"\n📡 Starting SSE listener for session: {self.session_id}")
        
        headers = {
            "X-Session-ID": self.session_id,
            "Accept": "text/event-stream"
        }
        
        try:
            response = requests.get(self.sse_url, headers=headers, stream=True, timeout=timeout)
            
            for line in response.iter_lines():
                if line:
                    line_str = line.decode('utf-8')
                    
                    # 解析 SSE 消息
                    if line_str.startswith('event:'):
                        event_type = line_str[6:].strip()
                    elif line_str.startswith('data:'):
                        data_str = line_str[5:].strip()
                        try:
                            data = json.loads(data_str)
                            message = {
                                'event': event_type,
                                'data': data,
                                'timestamp': time.time()
                            }
                            self.received_messages.append(message)
                            
                            # 实时显示进度
                            if event_type == 'progress':
                                progress = data.get('progress', 0)
                                msg = data.get('message', '')
                                print(f"\r⏳ Progress: {progress}% - {msg}", end='', flush=True)
                            elif event_type == 'connected':
                                print(f"✅ SSE connection established")
                            elif event_type == 'disconnected':
                                print(f"\n🔌 SSE connection closed")
                                
                        except json.JSONDecodeError:
                            pass
                            
        except requests.exceptions.Timeout:
            print("\n⏰ SSE listener timeout")
        except Exception as e:
            print(f"\n❌ SSE listener error: {e}")
    
    def start_listening(self):
        """启动后台 SSE 监听"""
        self.sse_thread = threading.Thread(target=self.listen_sse, daemon=True)
        self.sse_thread.start()
        time.sleep(1)  # 等待连接建立
    
    def stop_listening(self):
        """停止 SSE 监听"""
        time.sleep(2)  # 给一些时间接收最后的消息


def main():
    print("=" * 60)
    print("MCP Server SSE Test")
    print("=" * 60)
    
    client = MCPsseTestClient()
    
    # 1. 初始化
    print("\n=== Step 1: Initialize ===")
    response = client.send_request("initialize", {
        "protocolVersion": "2024-11-05",
        "capabilities": {},
        "clientInfo": {
            "name": "sse-test-client",
            "version": "1.0.0"
        }
    })
    print(f"Initialized: {json.dumps(response['result']['serverInfo'], indent=2)}")
    
    # 2. 启动 SSE 监听
    print("\n=== Step 2: Start SSE Listener ===")
    client.start_listening()
    
    # 3. 调用长时间运行任务
    print("\n=== Step 3: Call Long Running Task ===")
    response = client.send_request("tools/call", {
        "name": "long_running_task",
        "arguments": {
            "task_name": "data_processing",
            "duration_seconds": 5  # 5秒用于快速测试
        }
    })
    
    print(f"\nTask Response: {json.dumps(response['result'], indent=2)}")
    
    # 4. 等待进度更新
    print("\n\n=== Step 4: Waiting for Progress Updates ===")
    time.sleep(8)  # 等待任务完成
    
    # 5. 停止监听并显示结果
    print("\n\n=== Step 5: Summary ===")
    client.stop_listening()
    
    print(f"\nTotal messages received: {len(client.received_messages)}")
    print("\nReceived messages:")
    for i, msg in enumerate(client.received_messages, 1):
        print(f"{i}. [{msg['event']}] {json.dumps(msg['data'], indent=2)}")
    
    print("\n" + "=" * 60)
    print("✅ SSE test completed!")
    print("=" * 60)


if __name__ == "__main__":
    main()
