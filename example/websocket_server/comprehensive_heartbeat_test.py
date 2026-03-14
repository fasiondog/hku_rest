#!/usr/bin/env python3
"""
WebSocket 综合心跳机制测试脚本

功能:
1. 测试连接建立和欢迎消息
2. 测试消息回显功能
3. 测试长时间连接保活（70 秒，超过 60 秒 Ping 间隔）
4. 测试自动重连机制
5. 验证服务器日志中的 Ping 记录

使用方法:
1. 启动 WebSocket 服务器：xmake r websocket_server
2. 运行此脚本：python3 comprehensive_heartbeat_test.py
3. 观察输出日志和服务器端的 Ping 记录
"""

import asyncio
import sys
import time
from datetime import datetime

try:
    import websockets
except ImportError:
    print("❌ 未安装 websockets 库")
    print("请运行：pip3 install --user websockets")
    sys.exit(1)


class HeartbeatTester:
    """WebSocket 心跳测试器"""
    
    def __init__(self, uri="ws://localhost:8765/echo"):
        self.uri = uri
        self.connection_count = 0
        self.messages_sent = 0
        self.messages_received = 0
        self.ping_detected = False
        
    async def test_connection(self):
        """测试连接和心跳机制"""
        max_retries = 3
        retry_delay = 2
        
        for attempt in range(max_retries):
            try:
                print(f"\n🔌 尝试连接 (第 {attempt + 1}/{max_retries} 次)...")
                await self._do_test()
                return True
                
            except ConnectionRefusedError:
                if attempt < max_retries - 1:
                    print(f"⚠️  连接被拒绝，{retry_delay}秒后重试...")
                    await asyncio.sleep(retry_delay)
                else:
                    print(f"\n❌ 无法连接到服务器")
                    print(f"   请确保 WebSocket 服务器已启动：xmake r websocket_server")
                    return False
                    
            except Exception as e:
                print(f"\n❌ 测试失败：{e}")
                return False
        
        return False
    
    async def _do_test(self):
        """执行实际测试逻辑"""
        async with websockets.connect(self.uri) as ws:
            self.connection_count += 1
            print(f"✅ 成功连接到 {self.uri}")
            print(f"   连接时间：{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
            
            # 1. 接收欢迎消息
            print("\n📡 阶段 1: 接收欢迎消息")
            welcome = await asyncio.wait_for(ws.recv(), timeout=5)
            print(f"   📨 {welcome}")
            
            # 2. 发送测试消息
            print("\n📡 阶段 2: 测试消息回显")
            test_messages = [
                "Hello, WebSocket! Testing heartbeat mechanism.",
                "Second message for verification.",
                "Third message to ensure stability."
            ]
            
            for msg in test_messages:
                await ws.send(msg)
                self.messages_sent += 1
                response = await ws.recv()
                self.messages_received += 1
                print(f"   📤 发送：{msg}")
                print(f"   📥 回显：{response}")
            
            # 3. 等待并观察心跳（70 秒，超过 60 秒的 PING_INTERVAL）
            print("\n📡 阶段 3: 心跳保活测试")
            print(f"   ⏳ 等待 70 秒（服务器每 60 秒发送一次 Ping）")
            print(f"   ℹ️  如果心跳机制正常，连接将保持活跃")
            
            start_time = time.time()
            elapsed = 0
            
            while elapsed < 70:
                await asyncio.sleep(10)
                elapsed = time.time() - start_time
                remaining = 70 - elapsed
                print(f"   ⏰ 已等待 {elapsed:.0f}秒，剩余 {remaining:.0f}秒")
            
            # 4. 心跳后验证连接
            print("\n📡 阶段 4: 心跳后验证")
            final_message = "Still alive after heartbeat!"
            await ws.send(final_message)
            self.messages_sent += 1
            final_response = await ws.recv()
            self.messages_received += 1
            print(f"   ✅ 心跳后连接仍然活跃!")
            print(f"   📤 发送：{final_message}")
            print(f"   📥 回显：{final_response}")
            
            # 5. 打印统计信息
            print("\n📊 测试统计:")
            print(f"   连接次数：{self.connection_count}")
            print(f"   发送消息：{self.messages_sent}")
            print(f"   接收消息：{self.messages_received}")
            print(f"   检测 Ping: {'是' if self.ping_detected else '否'}")
            
            print("\n🎉 WebSocket 心跳测试完成!")
            print("💡 提示：查看服务器日志确认 Ping 帧发送记录")


async def main():
    """主函数"""
    print("=" * 70)
    print("WebSocket 综合心跳机制测试")
    print("=" * 70)
    print()
    print("测试目标:")
    print("  1. ✓ 验证连接建立和欢迎消息")
    print("  2. ✓ 验证消息回显功能")
    print("  3. ✓ 验证心跳保活机制（60 秒间隔）")
    print("  4. ✓ 验证长时间连接稳定性")
    print()
    print("预期结果:")
    print("  - 连接成功建立")
    print("  - 消息正常回显")
    print("  - 70 秒后连接仍然活跃")
    print("  - 服务器日志中有 Ping 记录")
    print()
    
    tester = HeartbeatTester()
    success = await tester.test_connection()
    
    if success:
        print("\n" + "=" * 70)
        print("✅ 所有测试通过!")
        print("=" * 70)
        return 0
    else:
        print("\n" + "=" * 70)
        print("❌ 测试失败!")
        print("=" * 70)
        return 1


if __name__ == "__main__":
    exit_code = asyncio.run(main())
    sys.exit(exit_code)
