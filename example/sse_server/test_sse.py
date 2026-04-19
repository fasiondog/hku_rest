#!/usr/bin/env python3
"""
SSE (Server-Sent Events) 客户端测试脚本

演示如何使用 Python 接收 SSE 推送数据
"""

import requests
import json
import time
import sys


def test_simple_sse():
    """测试简单 SSE 端点"""
    print("=" * 60)
    print("Testing Simple SSE: http://localhost:8081/sse/simple")
    print("=" * 60)
    
    try:
        response = requests.get('http://localhost:8081/sse/simple', stream=True, timeout=30)
        response.raise_for_status()
        
        message_count = 0
        for line in response.iter_lines():
            if line:
                # SSE 格式: "data: <content>"
                decoded_line = line.decode('utf-8')
                if decoded_line.startswith('data:'):
                    data = decoded_line[5:].strip()
                    message_count += 1
                    print(f"[{message_count}] Received: {data}")
                    
        print(f"\n✓ Simple SSE test completed. Total messages: {message_count}")
        
    except requests.exceptions.RequestException as e:
        print(f"✗ Error: {e}")
        return False
    
    return True


def parse_sse_event(raw_data):
    """
    解析原始 SSE 数据为结构化事件
    
    SSE 消息格式:
    event: <event_name>
    id: <message_id>
    data: <json_data>
    <empty line>
    """
    event = {
        'event': None,
        'id': None,
        'data': None
    }
    
    lines = raw_data.strip().split('\n')
    current_field = None
    
    for line in lines:
        if line.startswith('event:'):
            event['event'] = line[6:].strip()
        elif line.startswith('id:'):
            event['id'] = line[3:].strip()
        elif line.startswith('data:'):
            event['data'] = line[5:].strip()
    
    return event


def test_stream_sse():
    """测试完整功能的 SSE 流（带事件类型和 ID）"""
    print("\n" + "=" * 60)
    print("Testing Full SSE Stream: http://localhost:8081/sse/stream")
    print("=" * 60)
    
    try:
        response = requests.get('http://localhost:8081/sse/stream', stream=True, timeout=60)
        response.raise_for_status()
        
        print("Connected to SSE stream. Waiting for events...\n")
        
        message_count = 0
        buffer = []
        
        for chunk in response.iter_content(chunk_size=1024):
            if not chunk:
                continue
                
            decoded_chunk = chunk.decode('utf-8')
            buffer.append(decoded_chunk)
            
            # SSE 消息以双换行符分隔
            full_text = ''.join(buffer)
            
            if '\n\n' in full_text:
                # 分割出完整的 SSE 消息
                messages = full_text.split('\n\n')
                
                # 保留最后一个可能不完整的消息
                if not full_text.endswith('\n\n'):
                    buffer = [messages[-1]]
                    messages = messages[:-1]
                else:
                    buffer = []
                
                # 处理每个完整的消息
                for msg in messages:
                    if not msg.strip():
                        continue
                    
                    event = parse_sse_event(msg)
                    message_count += 1
                    
                    # 格式化输出
                    print(f"--- Message #{message_count} ---")
                    if event['event']:
                        print(f"  Event: {event['event']}")
                    if event['id']:
                        print(f"  ID:    {event['id']}")
                    if event['data']:
                        try:
                            # 尝试解析 JSON 数据
                            data = json.loads(event['data'])
                            print(f"  Data:  {json.dumps(data, indent=4)}")
                            
                            # 如果是行情数据，显示关键信息
                            if event['event'] == 'quote' and isinstance(data, dict):
                                symbol = data.get('symbol', 'N/A')
                                price = data.get('price', 0)
                                change = data.get('change', 0)
                                print(f"  📈 {symbol}: ${price:.2f} ({change:+.2f})")
                        except json.JSONDecodeError:
                            print(f"  Data:  {event['data']}")
                    
                    print()
                    
                    # 检查是否完成
                    if event['event'] == 'completed':
                        print(f"\n✓ SSE stream completed. Total messages: {message_count}")
                        return True
        
        print(f"\n✓ SSE stream ended. Total messages: {message_count}")
        return True
        
    except requests.exceptions.Timeout:
        print("\n✗ Connection timed out")
        return False
    except requests.exceptions.RequestException as e:
        print(f"\n✗ Error: {e}")
        return False
    except KeyboardInterrupt:
        print("\n\n✗ Interrupted by user")
        return False


class SSEClient:
    """
    SSE 客户端类 - 支持断线重连
    
    生产环境中应实现：
    1. 自动重连机制
    2. Last-Event-ID 支持（从上次断开处继续）
    3. 心跳检测
    """
    
    def __init__(self, url, reconnect_delay=5, max_retries=10):
        self.url = url
        self.reconnect_delay = reconnect_delay
        self.max_retries = max_retries
        self.last_event_id = None
    
    def connect(self, callback=None):
        """连接 SSE 服务器并处理事件"""
        headers = {}
        if self.last_event_id:
            headers['Last-Event-ID'] = self.last_event_id
        
        retries = 0
        
        while retries < self.max_retries:
            try:
                print(f"Connecting to {self.url}...")
                response = requests.get(self.url, stream=True, headers=headers, timeout=60)
                response.raise_for_status()
                
                print("✓ Connected\n")
                retries = 0  # 重置重试计数
                
                buffer = []
                for chunk in response.iter_content(chunk_size=1024):
                    if not chunk:
                        continue
                    
                    decoded_chunk = chunk.decode('utf-8')
                    buffer.append(decoded_chunk)
                    full_text = ''.join(buffer)
                    
                    if '\n\n' in full_text:
                        messages = full_text.split('\n\n')
                        
                        if not full_text.endswith('\n\n'):
                            buffer = [messages[-1]]
                            messages = messages[:-1]
                        else:
                            buffer = []
                        
                        for msg in messages:
                            if not msg.strip():
                                continue
                            
                            event = parse_sse_event(msg)
                            
                            # 更新最后的事件 ID
                            if event['id']:
                                self.last_event_id = event['id']
                            
                            # 调用回调函数
                            if callback:
                                callback(event)
                
            except requests.exceptions.RequestException as e:
                retries += 1
                print(f"\n✗ Connection error: {e}")
                
                if retries < self.max_retries:
                    print(f"Retrying in {self.reconnect_delay} seconds... (attempt {retries}/{self.max_retries})")
                    time.sleep(self.reconnect_delay)
                else:
                    print(f"✗ Max retries reached ({self.max_retries})")
                    break
    
    def disconnect(self):
        """断开连接"""
        print("Disconnecting...")


def test_with_client_class():
    """使用 SSEClient 类进行测试"""
    print("\n" + "=" * 60)
    print("Testing with SSEClient class (with auto-reconnect)")
    print("=" * 60)
    
    def handle_event(event):
        """事件处理回调"""
        if event['event'] == 'connected':
            print(f"🔗 {event['data']}")
        elif event['event'] == 'quote':
            try:
                data = json.loads(event['data'])
                symbol = data.get('symbol', 'N/A')
                price = data.get('price', 0)
                change = data.get('change', 0)
                print(f"📈 {symbol}: ${price:.2f} ({change:+.2f})")
            except:
                print(f"📊 {event['data']}")
        elif event['event'] == 'completed':
            print(f"✅ {event['data']}")
        else:
            print(f"📨 {event['data']}")
    
    client = SSEClient('http://localhost:8081/sse/stream', reconnect_delay=3, max_retries=3)
    
    try:
        client.connect(callback=handle_event)
    except KeyboardInterrupt:
        print("\n\nInterrupted by user")
    finally:
        client.disconnect()


if __name__ == '__main__':
    print("SSE Client Test Suite")
    print("=" * 60)
    
    if len(sys.argv) > 1:
        test_type = sys.argv[1]
        
        if test_type == 'simple':
            test_simple_sse()
        elif test_type == 'stream':
            test_stream_sse()
        elif test_type == 'client':
            test_with_client_class()
        else:
            print(f"Unknown test type: {test_type}")
            print("Available: simple, stream, client")
    else:
        # 运行所有测试
        print("\nRunning all tests...\n")
        
        # 测试 1: 简单 SSE
        test_simple_sse()
        
        # 测试 2: 完整 SSE 流
        test_stream_sse()
        
        # 测试 3: SSEClient 类
        print("\n" + "=" * 60)
        print("Note: SSEClient test requires manual interruption (Ctrl-C)")
        print("=" * 60)
        input("Press Enter to start SSEClient test or Ctrl-C to skip...")
        test_with_client_class()
        
        print("\n" + "=" * 60)
        print("All tests completed!")
        print("=" * 60)
