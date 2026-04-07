#!/usr/bin/env python3
"""
MQTT Broker 压力测试脚本
模拟大量客户端并发连接、发布和订阅消息
"""

import paho.mqtt.client as mqtt
import time
import threading
import statistics
import argparse
from datetime import datetime

# 测试结果统计
class TestStats:
    def __init__(self):
        self.connect_times = []
        self.publish_times = []
        self.receive_times = []
        self.success_count = 0
        self.fail_count = 0
        self.lock = threading.Lock()
    
    def add_connect_time(self, duration):
        with self.lock:
            self.connect_times.append(duration)
    
    def add_publish_time(self, duration):
        with self.lock:
            self.publish_times.append(duration)
    
    def add_receive_time(self, duration):
        with self.lock:
            self.receive_times.append(duration)
    
    def record_success(self):
        with self.lock:
            self.success_count += 1
    
    def record_fail(self):
        with self.lock:
            self.fail_count += 1
    
    def print_summary(self):
        print("\n" + "="*70)
        print("   压力测试结果汇总")
        print("="*70)
        
        if self.connect_times:
            print(f"\n连接性能:")
            print(f"  平均连接时间: {statistics.mean(self.connect_times)*1000:.2f} ms")
            print(f"  P50 连接时间: {statistics.median(self.connect_times)*1000:.2f} ms")
            print(f"  P95 连接时间: {sorted(self.connect_times)[int(len(self.connect_times)*0.95)]*1000:.2f} ms")
            print(f"  P99 连接时间: {sorted(self.connect_times)[int(len(self.connect_times)*0.99)]*1000:.2f} ms")
            print(f"  最大连接时间: {max(self.connect_times)*1000:.2f} ms")
        
        if self.publish_times:
            print(f"\n发布性能:")
            print(f"  平均发布时间: {statistics.mean(self.publish_times)*1000:.2f} ms")
            print(f"  P50 发布时间: {statistics.median(self.publish_times)*1000:.2f} ms")
            print(f"  P95 发布时间: {sorted(self.publish_times)[int(len(self.publish_times)*0.95)]*1000:.2f} ms")
            print(f"  P99 发布时间: {sorted(self.publish_times)[int(len(self.publish_times)*0.99)]*1000:.2f} ms")
        
        if self.receive_times:
            print(f"\n接收延迟:")
            print(f"  平均接收延迟: {statistics.mean(self.receive_times)*1000:.2f} ms")
            print(f"  P50 接收延迟: {statistics.median(self.receive_times)*1000:.2f} ms")
            print(f"  P95 接收延迟: {sorted(self.receive_times)[int(len(self.receive_times)*0.95)]*1000:.2f} ms")
            print(f"  P99 接收延迟: {sorted(self.receive_times)[int(len(self.receive_times)*0.99)]*1000:.2f} ms")
        
        print(f"\n成功率: {self.success_count}/{self.success_count + self.fail_count} "
              f"({self.success_count/(self.success_count+self.fail_count)*100:.2f}%)" if (self.success_count + self.fail_count) > 0 else "")
        print("="*70)


def create_client(client_id, host, port, stats, msg_count, ready_event, start_event):
    """创建单个 MQTT 客户端并执行测试"""
    
    connect_start = None
    publish_times = []
    received_msgs = 0
    expected_msgs = msg_count
    
    def on_connect(client, userdata, flags, rc):
        nonlocal connect_start
        if rc == 0:
            connect_duration = time.time() - connect_start
            stats.add_connect_time(connect_duration)
            
            # 订阅自己的主题
            topic = f"stress/test/{client_id}/#"
            client.subscribe(topic, qos=1)
            
            ready_event.set()  # 标记此客户端已就绪
            
            # 等待所有客户端就绪
            start_event.wait()
            
            # 开始发布消息
            for i in range(msg_count):
                topic = f"stress/test/{client_id}/msg{i}"
                payload = f"Message {i} from {client_id} at {time.time()}"
                pub_start = time.time()
                result = client.publish(topic, payload, qos=1)
                if result.rc == 0:
                    pub_duration = time.time() - pub_start
                    stats.add_publish_time(pub_duration)
                    stats.record_success()
                else:
                    stats.record_fail()
                time.sleep(0.005)  # 小幅延迟避免过快
        else:
            stats.record_fail()
            ready_event.set()
    
    def on_message(client, userdata, msg):
        nonlocal received_msgs
        received_msgs += 1
    
    def on_disconnect(client, userdata, rc):
        pass
    
    try:
        client = mqtt.Client(client_id=client_id)
        client.on_connect = on_connect
        client.on_message = on_message
        client.on_disconnect = on_disconnect
        
        connect_start = time.time()
        client.connect(host, port, 10)  # 缩短连接超时到10秒
        client.loop_start()
        
        # 等待连接建立或失败
        wait_count = 0
        while not ready_event.is_set() and wait_count < 100:  # 最多等待10秒
            time.sleep(0.1)
            wait_count += 1
        
        if not ready_event.is_set():
            print(f"Client {client_id} connection timeout after 10s")
            stats.record_fail()
            ready_event.set()
            return
        
        # 等待测试完成或超时
        timeout = 30  # 30秒超时
        start_wait = time.time()
        while received_msgs < expected_msgs and (time.time() - start_wait) < timeout:
            time.sleep(0.05)
        
        client.disconnect()
        client.loop_stop()
        
    except Exception as e:
        print(f"Client {client_id} error: {e}")
        stats.record_fail()
        ready_event.set()


def run_stress_test(host, port, num_clients, messages_per_client):
    """运行压力测试"""
    
    print("="*70)
    print("   MQTT Broker 压力测试")
    print("="*70)
    print(f"服务器: {host}:{port}")
    print(f"并发客户端数: {num_clients}")
    print(f"每客户端消息数: {messages_per_client}")
    print(f"总消息数: {num_clients * messages_per_client}")
    print("="*70)
    
    stats = TestStats()
    threads = []
    ready_events = []
    start_event = threading.Event()
    
    print("\n启动客户端...")
    start_time = time.time()
    
    # 创建并启动所有客户端线程
    for i in range(num_clients):
        ready_event = threading.Event()
        ready_events.append(ready_event)
        
        thread = threading.Thread(
            target=create_client,
            args=(f"stress_client_{i}", host, port, stats, 
                  messages_per_client, ready_event, start_event)
        )
        thread.daemon = True
        threads.append(thread)
        thread.start()
        
        # 每50个客户端显示一次进度
        if (i + 1) % 50 == 0:
            print(f"  已启动 {i + 1}/{num_clients} 个客户端...")
        
        # 高并发时延迟启动，避免瞬间连接风暴
        if num_clients > 200 and (i + 1) % 10 == 0:
            time.sleep(0.01)  # 每10个客户端延迟10ms
    
    # 等待所有客户端连接就绪
    print("等待所有客户端连接就绪...")
    for event in ready_events:
        event.wait(timeout=30)
    
    ready_time = time.time()
    print(f"所有客户端已就绪 (耗时: {ready_time - start_time:.2f}s)")
    
    # 同时启动所有客户端的测试
    print("\n开始压力测试...")
    start_event.set()
    
    # 等待所有线程完成
    for thread in threads:
        thread.join(timeout=60)
    
    end_time = time.time()
    total_duration = end_time - start_time
    
    print(f"\n测试完成 (总耗时: {total_duration:.2f}s)")
    print(f"吞吐量: {(num_clients * messages_per_client) / total_duration:.2f} msgs/s")
    
    # 打印统计结果
    stats.print_summary()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="MQTT Broker 压力测试")
    parser.add_argument("--host", default="localhost", help="Broker 主机地址")
    parser.add_argument("--port", type=int, default=1883, help="Broker 端口")
    parser.add_argument("--clients", type=int, default=50, help="并发客户端数量")
    parser.add_argument("--messages", type=int, default=10, help="每个客户端发送的消息数")
    
    args = parser.parse_args()
    
    run_stress_test(args.host, args.port, args.clients, args.messages)
