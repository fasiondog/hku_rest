#!/usr/bin/env python3
"""
WebSocket 测试脚本
"""
import asyncio
import websockets


async def test_echo():
    """测试 Echo WebSocket"""
    print("正在连接到 ws://localhost:8765/echo ...")

    try:
        async with websockets.connect("ws://localhost:8765/echo") as websocket:
            print("✓ 连接成功!")

            # 发送测试消息
            test_messages = [
                "Hello WebSocket!",
                "这是一条中文消息",
                "Test message 3"
            ]

            for msg in test_messages:
                print(f"\n发送：{msg}")
                await websocket.send(msg)

                # 接收回显
                response = await websocket.recv()
                print(f"收到：{response}")

                # 等待一小段时间
                await asyncio.sleep(0.1)

            print("\n✓ 所有测试通过!")

    except websockets.exceptions.ConnectionClosed:
        print("✗ 连接已关闭")
    except Exception as e:
        print(f"✗ 错误：{e}")

if __name__ == "__main__":
    asyncio.run(test_echo())
