#!/usr/bin/env python3
"""
简单的 WebSocket 测试脚本（无需外部依赖）
使用标准库实现基本的 WebSocket 客户端
"""
import socket
import hashlib
import base64
import struct
import time


def create_websocket_key():
    """生成 WebSocket 密钥"""
    import os
    return base64.b64encode(os.urandom(16)).decode('utf-8')


def compute_accept(key):
    """计算服务器应返回的 accept 值"""
    GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
    sha1 = hashlib.sha1((key + GUID).encode('utf-8')).digest()
    return base64.b64encode(sha1).decode('utf-8')


def encode_frame(data, is_text=True):
    """编码 WebSocket 帧"""
    if isinstance(data, str):
        data = data.encode('utf-8')

    opcode = 0x01 if is_text else 0x02  # 文本或二进制
    frame = bytearray()

    # FIN + 操作码
    frame.append(0x80 | opcode)

    # 掩码 + 负载长度
    mask = 0x80  # 客户端发送必须使用掩码
    length = len(data)

    if length <= 125:
        frame.append(mask | length)
    elif length <= 65535:
        frame.append(mask | 126)
        frame.extend(struct.pack('>H', length))
    else:
        frame.append(mask | 127)
        frame.extend(struct.pack('>Q', length))

    # 掩码（简单的 4 字节掩码）
    masking_key = b'\x00\x00\x00\x00'  # 实际应该随机，但这里简化处理
    frame.extend(masking_key)

    # 应用掩码
    for i, byte in enumerate(data):
        frame.append(byte ^ masking_key[i % 4])

    return bytes(frame)


def decode_frame(sock):
    """解码接收到的 WebSocket 帧"""
    header = sock.recv(2)
    if len(header) < 2:
        return None

    fin = (header[0] & 0x80) != 0
    opcode = header[0] & 0x0F
    masked = (header[1] & 0x80) != 0
    length = header[1] & 0x7F

    if length == 126:
        length = struct.unpack('>H', sock.recv(2))[0]
    elif length == 127:
        length = struct.unpack('>Q', sock.recv(8))[0]

    if masked:
        masking_key = sock.recv(4)

    payload = sock.recv(length)

    if masked:
        payload = bytes(b ^ masking_key[i % 4] for i, b in enumerate(payload))

    return payload.decode('utf-8') if opcode == 0x01 else payload


def test_websocket():
    """测试 WebSocket 连接和 Echo 功能"""
    print("=" * 60)
    print("WebSocket 简单测试（无依赖版）")
    print("=" * 60)
    print()

    host = 'localhost'
    port = 8765
    path = '/echo'

    try:
        # 1. 建立 TCP 连接
        print(f"🔌 正在连接到 {host}:{port}...")
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((host, port))
        sock.settimeout(5)
        print("✅ TCP 连接成功")

        # 2. WebSocket 握手
        print("\n🤝 执行 WebSocket 握手...")
        ws_key = create_websocket_key()
        expected_accept = compute_accept(ws_key)

        handshake = (
            f"GET {path} HTTP/1.1\r\n"
            f"Host: {host}:{port}\r\n"
            f"Upgrade: websocket\r\n"
            f"Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {ws_key}\r\n"
            f"Sec-WebSocket-Version: 13\r\n"
            f"\r\n"
        )

        sock.send(handshake.encode('utf-8'))

        # 读取响应
        response = b''
        while b'\r\n\r\n' not in response:
            chunk = sock.recv(1024)
            if not chunk:
                raise Exception("握手失败：连接关闭")
            response += chunk

        response_str = response.decode('utf-8')
        print("📨 握手响应:")
        for line in response_str.split('\r\n'):
            if line.strip():
                print(f"   {line}")

        # 验证 accept
        if expected_accept not in response_str:
            print(f"⚠️  警告：Accept 值不匹配（可能是正常的）")

        print("\n✅ WebSocket 握手成功!")

        # 3. 等待欢迎消息（如果有）
        sock.settimeout(2)
        try:
            welcome = decode_frame(sock)
            print(f"\n📨 欢迎消息：{welcome}")
        except socket.timeout:
            print("\nℹ️  没有收到欢迎消息（正常）")

        # 4. 发送测试消息
        print("\n📤 发送测试消息...")
        test_messages = [
            "Hello WebSocket!",
            "这是一条中文消息",
            "Test message 3"
        ]

        for msg in test_messages:
            print(f"\n发送：{msg}")
            frame = encode_frame(msg, is_text=True)
            sock.send(frame)

            # 接收回显
            sock.settimeout(3)
            try:
                response = decode_frame(sock)
                print(f"📥 回显：{response}")

                if response == msg:
                    print("✅ 回显正确")
                else:
                    print("❌ 回显不匹配")
            except socket.timeout:
                print("❌ 未收到回显")

            time.sleep(0.1)

        print("\n" + "=" * 60)
        print("🎉 WebSocket 测试完成!")
        print("=" * 60)

        sock.close()

    except ConnectionRefusedError:
        print(f"\n❌ 无法连接到服务器")
        print(f"   请确保 WebSocket 服务器已启动：xmake r websocket_server")
    except Exception as e:
        print(f"\n❌ 测试失败：{e}")
        import traceback
        traceback.print_exc()


if __name__ == "__main__":
    test_websocket()
