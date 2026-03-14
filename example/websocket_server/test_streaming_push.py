#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
WebSocket 流式分批推送功能测试脚本
用于测试交易平台行情推送功能（10000 支股票）

测试场景：
1. 订阅模式（预生成列表）- 适合内存充足场景
2. 动态生成器模式 - 适合内存敏感场景
3. 基础 Echo 功能 - 验证连接正常

预期性能：
- 批次大小：500 条/批
- 批次间隔：50ms
- 10000 条数据预计耗时：~1 秒
"""

import asyncio
import json
import sys
import time
from datetime import datetime

try:
    import websockets
except ImportError:
    print("错误：未安装 websockets 库")
    print("请执行：pip3 install --user websockets")
    sys.exit(1)


WS_URL = "ws://localhost:8765"


async def test_echo():
    """测试 0: 基础 Echo 功能"""
    print("\n" + "=" * 70)
    print("测试 0: 基础 Echo 功能 - 验证 WebSocket 连接")
    print("=" * 70)
    
    try:
        async with websockets.connect(f"{WS_URL}/echo") as ws:
            # 接收欢迎消息
            welcome = await ws.recv()
            welcome_data = json.loads(welcome)
            print(f"✓ 收到欢迎消息：{welcome_data['message']}")
            
            # 发送测试消息
            test_msg = "Hello, WebSocket!"
            await ws.send(test_msg)
            response = await ws.recv()
            response_data = json.loads(response)
            
            if response_data.get("message") == test_msg:
                print(f"✓ Echo 测试通过：'{test_msg}' -> '{response_data['message']}'")
                return True
            else:
                print(f"✗ Echo 测试失败：期望 '{test_msg}'，实际 '{response_data.get('message')}'")
                return False
                
    except Exception as e:
        print(f"✗ Echo 测试异常：{e}")
        return False


async def test_subscribe_mode(total_quotes=1000):
    """测试 1: 订阅模式（预生成列表）"""
    print("\n" + "=" * 70)
    print(f"测试 1: 订阅模式（预生成列表）- 推送 {total_quotes} 条行情")
    print("=" * 70)
    
    try:
        async with websockets.connect(f"{WS_URL}/quotes") as ws:
            # 生成股票代码
            symbols = [f"SH6000{i:02d}" for i in range(total_quotes)]
            
            request = {
                "action": "subscribe_quotes",
                "symbols": symbols
            }
            
            start_time = time.time()
            await ws.send(json.dumps(request))
            print(f"✓ 发送订阅请求：{len(symbols)} 条行情")
            
            quote_count = 0
            batch_count = 0
            
            while True:
                response = await ws.recv()
                data = json.loads(response)
                msg_type = data.get("type")
                
                if msg_type == "quote_start":
                    print(f"✓ 开始推送，总计：{data['total']} 条")
                    
                elif msg_type == "quote_finish":
                    elapsed = time.time() - start_time
                    print(f"\n✅ 推送完成统计:")
                    print(f"  - 成功：{data['success']}")
                    print(f"  - 总批次：{data['total_batches']}")
                    print(f"  - 已发送：{data['total_sent']} 条")
                    print(f"  - 实际耗时：{elapsed:.2f}秒")
                    print(f"  - 理论耗时：{data['total_batches'] * 0.05}秒")
                    print(f"  - 平均速度：{total_quotes / elapsed:.0f} 条/秒")
                    break
                    
                elif msg_type == "error":
                    print(f"✗ 错误：{data['message']}")
                    return False
                
                # 统计行情数据
                if "symbol" in data:
                    quote_count += 1
                    if quote_count % 200 == 0:
                        batch_count += 1
                        print(f"  批次 {batch_count}: 已接收 {quote_count} 条")
            
            return True
            
    except Exception as e:
        print(f"✗ 订阅模式测试异常：{e}")
        return False


async def test_streaming_mode(total_quotes=1000):
    """测试 2: 动态生成器模式"""
    print("\n" + "=" * 70)
    print(f"测试 2: 动态生成器模式 - 流式推送 {total_quotes} 条行情")
    print("=" * 70)
    
    try:
        async with websockets.connect(f"{WS_URL}/quotes") as ws:
            request = {
                "action": "stream_quotes",
                "count": total_quotes
            }
            
            start_time = time.time()
            await ws.send(json.dumps(request))
            print(f"✓ 发送流式推送请求：{total_quotes} 条行情")
            
            quote_count = 0
            batch_count = 0
            
            while True:
                response = await ws.recv()
                data = json.loads(response)
                msg_type = data.get("type")
                
                if msg_type == "stream_start":
                    print(f"✓ 开始流式推送，目标：{data['target_count']} 条")
                    
                elif msg_type == "stream_finish":
                    elapsed = time.time() - start_time
                    print(f"\n✅ 流式推送完成统计:")
                    print(f"  - 成功：{data['success']}")
                    print(f"  - 总批次：{data['total_batches']}")
                    print(f"  - 已发送：{data['total_sent']} 条")
                    print(f"  - 实际耗时：{elapsed:.2f}秒")
                    print(f"  - 平均速度：{data['total_sent'] / elapsed:.0f} 条/秒")
                    break
                    
                elif msg_type == "error":
                    print(f"✗ 错误：{data['message']}")
                    return False
                
                # 统计行情数据
                if "symbol" in data:
                    quote_count += 1
                    if quote_count % 200 == 0:
                        batch_count += 1
                        print(f"  批次 {batch_count}: 已接收 {quote_count} 条")
            
            return True
            
    except Exception as e:
        print(f"✗ 流式模式测试异常：{e}")
        return False


async def main():
    """主测试函数"""
    print("\n" + "=" * 70)
    print("WebSocket 流式分批推送功能测试")
    print("=" * 70)
    print(f"\n测试时间：{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"服务器地址：{WS_URL}")
    print("\n配置参数:")
    print("  - 批次大小：500 条/批")
    print("  - 批次间隔：50ms")
    print("  - 10000 条数据预计耗时：~1 秒")
    
    results = []
    
    # 测试 0: Echo 功能
    result0 = await test_echo()
    results.append(("Echo 基础测试", result0))
    
    if not result0:
        print("\n⚠️  Echo 测试失败，跳过后续测试")
        return
    
    # 测试 1: 订阅模式（1000 条）
    result1 = await test_subscribe_mode(1000)
    results.append(("订阅模式 (1000 条)", result1))
    
    # 测试 2: 流式模式（1000 条）
    result2 = await test_streaming_mode(1000)
    results.append(("流式模式 (1000 条)", result2))
    
    # 总结
    print("\n" + "=" * 70)
    print("测试总结")
    print("=" * 70)
    passed = sum(1 for _, r in results if r)
    total = len(results)
    
    for name, result in results:
        status = "✅ 通过" if result else "❌ 失败"
        print(f"  {status} - {name}")
    
    print(f"\n总计：{passed}/{total} 测试通过")
    
    if passed == total:
        print("\n🎉 所有测试通过！流式推送功能运行正常！")
    else:
        print("\n⚠️  部分测试失败，请检查日志")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n\n测试被用户中断")
    except Exception as e:
        print(f"\n程序异常：{e}")
        import traceback
        traceback.print_exc()
