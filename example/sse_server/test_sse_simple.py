#!/usr/bin/env python3
"""
SSE (Server-Sent Events) 客户端测试脚本（使用 urllib，无需额外依赖）

演示如何使用 Python 接收 SSE 推送数据
"""

import urllib.request
import json
import time
import sys


def test_simple_sse():
    """测试简单 SSE 端点"""
    print("=" * 60)
    print("Testing Simple SSE: http://localhost:8081/sse/simple")
    print("=" * 60)
    
    try:
        req = urllib.request.Request('http://localhost:8081/sse/simple')
        response = urllib.request.urlopen(req, timeout=30)
        
        message_count = 0
        for line in response:
            decoded_line = line.decode('utf-8').strip()
            if decoded_line.startswith('data:'):
                data = decoded_line[5:].strip()
                message_count += 1
                print(f"[{message_count}] Received: {data}")
                
        print(f"\n✓ Simple SSE test completed. Total messages: {message_count}")
        
    except Exception as e:
        print(f"✗ Error: {e}")
        return False
    
    return True


def parse_sse_event(raw_data):
    """解析原始 SSE 数据为结构化事件"""
    event = {
        'event': None,
        'id': None,
        'data': None
    }
    
    lines = raw_data.strip().split('\n')
    
    for line in lines:
        if line.startswith('event:'):
            event['event'] = line[6:].strip()
        elif line.startswith('id:'):
            event['id'] = line[3:].strip()
        elif line.startswith('data:'):
            event['data'] = line[5:].strip()
    
    return event


def test_stream_sse():
    """测试完整功能的 SSE 流"""
    print("\n" + "=" * 60)
    print("Testing Full SSE Stream: http://localhost:8081/sse/stream")
    print("=" * 60)
    
    try:
        req = urllib.request.Request('http://localhost:8081/sse/stream')
        response = urllib.request.urlopen(req, timeout=60)
        
        print("Connected to SSE stream. Waiting for events...\n")
        
        message_count = 0
        buffer = []
        
        for chunk in iter(lambda: response.read(1024), b''):
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
                    message_count += 1
                    
                    print(f"--- Message #{message_count} ---")
                    if event['event']:
                        print(f"  Event: {event['event']}")
                    if event['id']:
                        print(f"  ID:    {event['id']}")
                    if event['data']:
                        try:
                            data = json.loads(event['data'])
                            print(f"  Data:  {json.dumps(data, indent=4)}")
                            
                            if event['event'] == 'quote' and isinstance(data, dict):
                                symbol = data.get('symbol', 'N/A')
                                price = data.get('price', 0)
                                change = data.get('change', 0)
                                print(f"  📈 {symbol}: ${price:.2f} ({change:+.2f})")
                        except json.JSONDecodeError:
                            print(f"  Data:  {event['data']}")
                    
                    print()
                    
                    if event['event'] == 'completed':
                        print(f"\n✓ SSE stream completed. Total messages: {message_count}")
                        return True
        
        print(f"\n✓ SSE stream ended. Total messages: {message_count}")
        return True
        
    except Exception as e:
        print(f"\n✗ Error: {e}")
        return False
    except KeyboardInterrupt:
        print("\n\n✗ Interrupted by user")
        return False


if __name__ == '__main__':
    print("SSE Client Test Suite (using urllib)")
    print("=" * 60)
    
    if len(sys.argv) > 1:
        test_type = sys.argv[1]
        
        if test_type == 'simple':
            test_simple_sse()
        elif test_type == 'stream':
            test_stream_sse()
        else:
            print(f"Unknown test type: {test_type}")
            print("Available: simple, stream")
    else:
        # 运行所有测试
        print("\nRunning all tests...\n")
        
        test_simple_sse()
        test_stream_sse()
        
        print("\n" + "=" * 60)
        print("All tests completed!")
        print("=" * 60)
