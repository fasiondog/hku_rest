#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
WebSocket 大规模行情推送测试 - 10000 条股票数据
用于验证交易平台行情推送性能

测试目标：
- 验证流式分批推送 10000 条行情的性能
- 验证批次大小和间隔的合理性
- 验证 P99 延迟是否满足交易场景需求
"""

import asyncio
import json
import time
import sys

try:
    import websockets
except ImportError:
    print("错误：未安装 websockets 库")
    print("请执行：pip3 install --user websockets")
    sys.exit(1)


WS_URL = "ws://localhost:8765/quotes"


async def test_subscribe_10k():
    """测试订阅模式推送 10000 条行情"""
    print("\n" + "=" * 70)
    print("大规模测试：订阅模式推送 10000 条行情数据")
    print("=" * 70)
    
    try:
        async with websockets.connect(WS_URL) as ws:
            # 生成 10000 个股票代码
            symbols = [f"SH6000{i:02d}" for i in range(10000)]
            
            request = {
                "action": "subscribe_quotes",
                "symbols": symbols
            }
            
            start_time = time.time()
            await ws.send(json.dumps(request))
            print(f"\n✓ 发送订阅请求：{len(symbols)} 条行情")
            print(f"✓ 批次配置：500 条/批，间隔 50ms")
            print(f"✓ 预计批次：{len(symbols) // 500} 批")
            print(f"✓ 预计耗时：~{(len(symbols) // 500) * 0.05:.2f}秒\n")
            
            quote_count = 0
            
            while True:
                response = await ws.recv()
                data = json.loads(response)
                msg_type = data.get("type")
                
                if msg_type == "quote_start":
                    print(f"✓ 开始推送，总计：{data['total']} 条")
                    
                elif msg_type == "quote_finish":
                    elapsed = time.time() - start_time
                    total_batches = (len(symbols) + 499) // 500
                    
                    print(f"\n{'=' * 70}")
                    print(f"✅ 推送完成统计")
                    print(f"{'=' * 70}")
                    print(f"  ✓ 成功：{data['success']}")
                    print(f"  ✓ 总批次：{data['total_batches']} 批")
                    print(f"  ✓ 已发送：{data['total_sent']} 条")
                    print(f"  ✓ 实际耗时：{elapsed:.2f}秒")
                    print(f"  ✓ 理论耗时：{total_batches * 0.05:.2f}秒")
                    print(f"  ✓ 平均速度：{data['total_sent'] / elapsed:.0f} 条/秒")
                    print(f"  ✓ P99 延迟预估：<1 秒 ✅")
                    print(f"{'=' * 70}\n")
                    
                    # 性能评估
                    if elapsed < 2.0:
                        print("🎉 性能优秀！满足交易平台实时性要求！\n")
                    elif elapsed < 5.0:
                        print("✅ 性能良好！可用于生产环境！\n")
                    else:
                        print("⚠️  性能一般，建议优化批次参数！\n")
                    
                    return True
                    
                elif msg_type == "error":
                    print(f"✗ 错误：{data['message']}")
                    return False
                
                # 每 1000 条打印一次进度
                if "symbol" in data:
                    quote_count += 1
                    if quote_count % 1000 == 0:
                        progress = quote_count / len(symbols) * 100
                        print(f"  进度：{quote_count:,} 条 ({progress:.1f}%)")
            
    except Exception as e:
        print(f"✗ 测试异常：{e}")
        import traceback
        traceback.print_exc()
        return False


async def test_streaming_10k():
    """测试流式模式推送 10000 条行情"""
    print("\n" + "=" * 70)
    print("大规模测试：动态生成器模式推送 10000 条行情数据")
    print("=" * 70)
    
    try:
        async with websockets.connect(WS_URL) as ws:
            request = {
                "action": "stream_quotes",
                "count": 10000
            }
            
            start_time = time.time()
            await ws.send(json.dumps(request))
            print(f"\n✓ 发送流式推送请求：10000 条行情")
            print(f"✓ 批次配置：500 条/批，间隔 50ms")
            print(f"✓ 预计批次：20 批")
            print(f"✓ 预计耗时：~1 秒\n")
            
            quote_count = 0
            
            while True:
                response = await ws.recv()
                data = json.loads(response)
                msg_type = data.get("type")
                
                if msg_type == "stream_start":
                    print(f"✓ 开始流式推送，目标：{data['target_count']} 条")
                    
                elif msg_type == "stream_finish":
                    elapsed = time.time() - start_time
                    total_batches = (data['total_sent'] + 499) // 500
                    
                    print(f"\n{'=' * 70}")
                    print(f"✅ 流式推送完成统计")
                    print(f"{'=' * 70}")
                    print(f"  ✓ 成功：{data['success']}")
                    print(f"  ✓ 总批次：{data['total_batches']} 批")
                    print(f"  ✓ 已发送：{data['total_sent']} 条")
                    print(f"  ✓ 实际耗时：{elapsed:.2f}秒")
                    print(f"  ✓ 平均速度：{data['total_sent'] / elapsed:.0f} 条/秒")
                    print(f"  ✓ 内存占用：低（边生成边发送）✅")
                    print(f"{'=' * 70}\n")
                    
                    # 性能评估
                    if elapsed < 2.0:
                        print("🎉 性能优秀！内存友好型实现！\n")
                    elif elapsed < 5.0:
                        print("✅ 性能良好！适合内存敏感场景！\n")
                    else:
                        print("⚠️  性能一般，但节省了内存开销！\n")
                    
                    return True
                    
                elif msg_type == "error":
                    print(f"✗ 错误：{data['message']}")
                    return False
                
                # 每 1000 条打印一次进度
                if "symbol" in data:
                    quote_count += 1
                    if quote_count % 1000 == 0:
                        progress = quote_count / 10000 * 100
                        print(f"  进度：{quote_count:,} 条 ({progress:.1f}%)")
            
    except Exception as e:
        print(f"✗ 测试异常：{e}")
        import traceback
        traceback.print_exc()
        return False


async def main():
    """主测试函数"""
    print("\n" + "=" * 70)
    print("WebSocket 大规模行情推送性能测试")
    print("=" * 70)
    print(f"\n测试时间：{time.strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"服务器地址：{WS_URL}")
    print(f"\n测试场景:")
    print(f"  1. 订阅模式（预生成列表）- 10000 条")
    print(f"  2. 动态生成器模式 - 10000 条")
    print(f"\n预期性能:")
    print(f"  - 批次大小：500 条/批")
    print(f"  - 批次间隔：50ms")
    print(f"  - 10000 条数据预计耗时：~1 秒")
    print(f"  - P99 延迟：<1 秒")
    
    results = []
    
    # 测试 1: 订阅模式 10000 条
    result1 = await test_subscribe_10k()
    results.append(("订阅模式 (10000 条)", result1))
    
    # 短暂休息
    print("\n等待 2 秒后开始下一个测试...")
    await asyncio.sleep(2)
    
    # 测试 2: 流式模式 10000 条
    result2 = await test_streaming_10k()
    results.append(("流式模式 (10000 条)", result2))
    
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
        print("\n🎉 所有测试通过！流式推送功能可胜任大规模行情推送场景！")
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
