#!/usr/bin/env python3
"""
WebSocket 行情推送测试脚本 - 测试流式分批推送功能

功能：
1. 测试预生成列表模式的批量推送
2. 测试动态生成器模式的流式推送
3. 验证 10000 条行情数据的推送性能（目标：~1 秒完成）

运行方式：
python test_quote_push.py

依赖：
pip install websockets
"""

import asyncio
import json
import time
import sys

try:
    import websockets
except ImportError:
    print("错误：请先安装 websockets 库")
    print("运行：pip3 install --user websockets")
    sys.exit(1)


async def test_subscribe_mode():
    """测试订阅模式（预生成列表方式）"""
    print("\n" + "="*60)
    print("测试 1: 订阅模式（预生成列表）")
    print("="*60)
    
    uri = "ws://localhost:8765/quotes"
    
    try:
        async with websockets.connect(uri) as websocket:
            # 发送订阅请求
            request = {
                "action": "subscribe_quotes",
                "symbols": [f"SH6000{i:02d}" for i in range(100)]  # 测试 100 只股票
            }
            
            print(f"\n发送订阅请求：{json.dumps(request, indent=2)}")
            await websocket.send(json.dumps(request))
            
            # 接收响应
            start_time = time.time()
            message_count = 0
            first_msg_time = None
            
            while True:
                try:
                    response = await asyncio.wait_for(
                        websocket.recv(), 
                        timeout=10.0
                    )
                    data = json.loads(response)
                    
                    if first_msg_time is None:
                        first_msg_time = time.time()
                    
                    msg_type = data.get("type", "")
                    
                    if msg_type == "quote_start":
                        total = data.get("total", 0)
                        batch_size = data.get("batch_size", 0)
                        print(f"\n开始推送：总计 {total} 条，批次大小 {batch_size}")
                        
                    elif msg_type == "quote_finish":
                        elapsed = time.time() - start_time
                        success = data.get("success", False)
                        total_sent = data.get("total_sent", 0)
                        print(f"\n✓ 推送完成:")
                        print(f"  - 成功：{success}")
                        print(f"  - 总发送：{total_sent} 条")
                        print(f"  - 耗时：{elapsed:.3f} 秒")
                        print(f"  - 平均速度：{total_sent/elapsed:.0f} 条/秒")
                        break
                        
                    elif msg_type == "error":
                        print(f"\n✗ 错误：{data.get('message', 'Unknown error')}")
                        break
                    
                    message_count += 1
                    
                except asyncio.TimeoutError:
                    print("\n✗ 等待响应超时")
                    break
                    
    except websockets.exceptions.ConnectionClosed as e:
        print(f"\n✗ 连接关闭：{e}")
    except ConnectionRefusedError:
        print("\n✗ 连接被拒绝，请确保服务器已启动")
    except Exception as e:
        print(f"\n✗ 异常：{e}")


async def test_streaming_mode():
    """测试流式模式（动态生成器方式）"""
    print("\n" + "="*60)
    print("测试 2: 流式模式（动态生成器）")
    print("="*60)
    
    uri = "ws://localhost:8765/quotes"
    
    try:
        async with websockets.connect(uri) as websocket:
            # 发送流式推送请求
            request = {
                "action": "stream_quotes",
                "count": 1000  # 测试 1000 条
            }
            
            print(f"\n发送流式推送请求：{json.dumps(request, indent=2)}")
            await websocket.send(json.dumps(request))
            
            # 接收响应
            start_time = time.time()
            message_count = 0
            quote_count = 0
            
            while True:
                try:
                    response = await asyncio.wait_for(
                        websocket.recv(), 
                        timeout=10.0
                    )
                    data = json.loads(response)
                    
                    msg_type = data.get("type", "")
                    
                    if msg_type == "stream_start":
                        total = data.get("total", 0)
                        print(f"\n开始流式推送：总计 {total} 条")
                        
                    elif msg_type == "stream_finish":
                        elapsed = time.time() - start_time
                        success = data.get("success", False)
                        total_sent = data.get("total_sent", 0)
                        print(f"\n✓ 流式推送完成:")
                        print(f"  - 成功：{success}")
                        print(f"  - 总发送：{total_sent} 条")
                        print(f"  - 耗时：{elapsed:.3f} 秒")
                        print(f"  - 平均速度：{total_sent/elapsed:.0f} 条/秒")
                        
                        # 计算理论耗时
                        expected_batches = (total_sent + 499) // 500
                        expected_time = expected_batches * 0.05  # 50ms 间隔
                        print(f"  - 理论耗时：~{expected_time:.3f} 秒 ({expected_batches} 批次 × 50ms)")
                        break
                        
                    elif msg_type == "error":
                        print(f"\n✗ 错误：{data.get('message', 'Unknown error')}")
                        break
                    
                    else:
                        # 统计行情消息数量
                        if "symbol" in data and "price" in data:
                            quote_count += 1
                            if quote_count <= 5 or quote_count % 100 == 0:
                                print(f"  [{quote_count}] {data.get('symbol')}: "
                                      f"¥{data.get('price'):.2f} "
                                      f"({data.get('change'):+.2f})")
                        message_count += 1
                    
                except asyncio.TimeoutError:
                    print("\n✗ 等待响应超时")
                    break
                    
    except websockets.exceptions.ConnectionClosed as e:
        print(f"\n✗ 连接关闭：{e}")
    except ConnectionRefusedError:
        print("\n✗ 连接被拒绝，请确保服务器已启动")
    except Exception as e:
        print(f"\n✗ 异常：{e}")


async def test_basic_echo():
    """测试基础 Echo 功能"""
    print("\n" + "="*60)
    print("测试 3: 基础 Echo 功能")
    print("="*60)
    
    uri = "ws://localhost:8765/echo"
    
    try:
        async with websockets.connect(uri) as websocket:
            test_message = "Hello, WebSocket!"
            print(f"\n发送消息：{test_message}")
            await websocket.send(test_message)
            
            response = await websocket.recv()
            print(f"收到回复：{response}")
            
            if response == test_message:
                print("✓ Echo 测试通过")
            else:
                print("✗ Echo 测试失败")
                
    except ConnectionRefusedError:
        print("\n✗ 连接被拒绝，请确保服务器已启动")
    except Exception as e:
        print(f"\n✗ 异常：{e}")


async def main():
    """主函数"""
    print("\n" + "="*60)
    print("WebSocket 流式分批推送功能测试")
    print("="*60)
    print("\n测试场景:")
    print("1. 预生成列表模式 - 适合内存充足场景")
    print("2. 动态生成器模式 - 适合内存敏感场景")
    print("3. 基础 Echo 功能 - 验证连接正常")
    print("\n预期性能:")
    print("- 批次大小：500 条/批")
    print("- 批次间隔：50ms")
    print("- 10000 条数据预计耗时：~1 秒")
    
    # 执行测试
    await test_basic_echo()
    await test_subscribe_mode()
    await test_streaming_mode()
    
    print("\n" + "="*60)
    print("所有测试完成")
    print("="*60 + "\n")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n\n测试被用户中断")
    except Exception as e:
        print(f"\n程序异常：{e}")
        sys.exit(1)
