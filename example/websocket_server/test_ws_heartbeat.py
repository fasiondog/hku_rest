#!/usr/bin/env python3
"""
简化的 WebSocket 心跳测试
"""

import asyncio
import sys
import time

try:
    import websockets
except ImportError:
    print("❌ 未安装 websockets 库")
    print("请运行：pip install --user websockets")
    sys.exit(1)


async def test_heartbeat():
    uri = "ws://localhost:8765/echo"

    try:
        async with websockets.connect(uri) as ws:
            print(f"✅ 连接到 {uri}")

            # 接收欢迎消息
            welcome = await asyncio.wait_for(ws.recv(), timeout=5)
            print(f"📨 {welcome}\n")

            # 保持连接 60 秒，观察心跳
            print("⏳ 保持连接 60 秒，观察服务器心跳...")
            start_time = time.time()
            
            while True:
                elapsed = time.time() - start_time
                if elapsed >= 60:
                    print(f"\n✅ 测试完成，连接保持了 {elapsed:.0f} 秒")
                    break
                
                # 每5秒打印一次状态
                if int(elapsed) % 5 == 0:
                    print(f"   ⏱️  {elapsed:.0f} 秒", end='\r')
                    sys.stdout.flush()
                
                await asyncio.sleep(0.5)

    except websockets.exceptions.ConnectionClosed as e:
        elapsed = time.time() - start_time if 'start_time' in locals() else 0
        print(f"\n\n❌ 连接在 {elapsed:.1f} 秒时关闭：code={e.code}, reason={e.reason}")
        
        if e.code == 1006:
            print("⚠️  异常关闭 (code=1006)")
        elif e.code in [1000, 1001]:
            print("✅ 正常关闭")
    except Exception as e:
        print(f"\n❌ 错误：{e}")
        import traceback
        traceback.print_exc()


if __name__ == "__main__":
    asyncio.run(test_heartbeat())
