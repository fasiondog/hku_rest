#!/usr/bin/env python3
"""
WebSocket 心跳机制测试脚本

使用方法:
1. 确保 WebSocket 服务器已启动：xmake r websocket_server
2. 运行此脚本：python3 test_ws_heartbeat.py

预期行为:
- 连接建立后收到欢迎消息
- 发送测试消息并收到回显
- 等待 70 秒（超过 60 秒 Ping 间隔），验证连接仍然活跃
- 观察服务器日志中的 Ping/Pong 记录
"""

import asyncio
import sys

try:
    import websockets
except ImportError:
    print("❌ 未安装 websockets 库")
    print("请运行：pip3 install --user websockets")
    sys.exit(1)


async def test_echo_and_heartbeat():
    """测试 Echo 功能和心跳机制"""
    uri = "ws://localhost:8765/echo"

    try:
        async with websockets.connect(uri) as ws:
            print(f"✅ 成功连接到 {uri}")

            # 1. 接收欢迎消息
            welcome = await asyncio.wait_for(ws.recv(), timeout=5)
            print(f"📨 欢迎消息：{welcome}")

            # 2. 发送测试消息
            test_message = "Hello, WebSocket! Testing heartbeat mechanism."
            await ws.send(test_message)
            response = await ws.recv()
            print(f"📤 发送：{test_message}")
            print(f"📥 回显：{response}")

            # 3. 再次发送消息
            await ws.send("Second message")
            response2 = await ws.recv()
            print(f"📥 第二次回显：{response2}")

            # 4. 等待并观察心跳 (70 秒，超过 60 秒的 Ping 间隔)
            print("\n⏳ 等待 70 秒，观察心跳机制...")
            print("   服务器应该在 ~60 秒时发送 Ping 帧")
            print("   如果一切正常，连接将保持活跃")

            for i in range(70, 0, -10):
                await asyncio.sleep(10)
                print(f"   剩余时间：{i} 秒")

            # 5. 心跳后发送消息验证连接
            final_message = "Still alive after heartbeat!"
            await ws.send(final_message)
            final_response = await ws.recv()
            print(f"\n✅ 心跳后连接仍然活跃!")
            print(f"📤 发送：{final_message}")
            print(f"📥 回显：{final_response}")

            print("\n🎉 WebSocket 心跳测试完成!")

    except websockets.exceptions.ConnectionClosed as e:
        print(f"\n❌ 连接意外关闭：code={e.code}, reason={e.reason}")
        print("   可能原因:")
        print("   1. 服务器检测到 Ping 超时（10 秒未响应）")
        print("   2. 网络问题或服务器重启")
        sys.exit(1)
    except ConnectionRefusedError:
        print(f"\n❌ 无法连接到服务器")
        print(f"   请确保 WebSocket 服务器已启动：xmake r websocket_server")
        sys.exit(1)
    except Exception as e:
        print(f"\n❌ 测试失败：{e}")
        sys.exit(1)


if __name__ == "__main__":
    print("=" * 60)
    print("WebSocket 心跳机制测试")
    print("=" * 60)
    print()
    asyncio.run(test_echo_and_heartbeat())
