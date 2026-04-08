#!/usr/bin/env python3
"""
MQTT Broker 基础功能测试
"""

import paho.mqtt.client as mqtt
import time
import json
import sys

# 配置
BROKER_HOST = "localhost"
BROKER_PORT = 1883
TEST_TOPIC = "test/msg"
TEST_DURATION = 5

# 测试结果统计
test_results = {
    'total_sent': 0,
    'total_received': 0,
    'errors': []
}

def on_connect(client, userdata, flags, rc):
    """连接回调"""
    if rc == 0:
        print(f'✓ Connected to {BROKER_HOST}:{BROKER_PORT}')
        client.subscribe(f'{TEST_TOPIC}/#')
    else:
        print(f'✗ Connection failed with code {rc}')
        sys.exit(1)

def on_message(client, userdata, msg):
    """消息接收回调"""
    try:
        payload = msg.payload.decode('utf-8')
        test_results['total_received'] += 1
        print(f'  Received [{msg.topic}]: {payload}')
    except Exception as e:
        test_results['errors'].append(f'Message decode error: {e}')

def on_publish(client, userdata, mid):
    """发布回调"""
    pass

def test_basic_pubsub():
    """测试基本的发布/订阅功能"""
    print('\n=== Test 1: Basic Publish/Subscribe ===')
    
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    client.on_publish = on_publish
    
    try:
        # 连接
        client.connect(BROKER_HOST, BROKER_PORT, 60)
        client.loop_start()
        time.sleep(1)
        
        # 发布测试消息
        print('\nPublishing test messages...')
        for i in range(5):
            msg_data = {
                'id': i + 1,
                'message': f'Test message {i + 1}',
                'timestamp': time.time()
            }
            topic = f'{TEST_TOPIC}/{i + 1}'
            result = client.publish(topic, json.dumps(msg_data), qos=1)
            test_results['total_sent'] += 1
            print(f'  Published [{topic}]: {json.dumps(msg_data)}')
            time.sleep(0.2)
        
        # 等待消息处理
        time.sleep(2)
        
        # 检查结果
        print(f'\n--- Results ---')
        print(f'Sent: {test_results["total_sent"]}')
        print(f'Received: {test_results["total_received"]}')
        
        if test_results['total_received'] == test_results['total_sent']:
            print('✓ Test PASSED')
            return True
        else:
            print('✗ Test FAILED - Message count mismatch')
            return False
            
    except Exception as e:
        print(f'✗ Test ERROR: {e}')
        return False
    finally:
        client.loop_stop()
        client.disconnect()

def test_qos_levels():
    """测试不同 QoS 级别"""
    print('\n=== Test 2: QoS Levels ===')
    
    received_messages = []
    
    def on_message_qos(client, userdata, msg):
        received_messages.append({
            'topic': msg.topic,
            'qos': msg.qos,
            'payload': msg.payload.decode('utf-8')
        })
    
    client = mqtt.Client()
    client.on_connect = lambda c, u, f, r: c.subscribe('qos/test/#', qos=2)
    client.on_message = on_message_qos
    
    try:
        client.connect(BROKER_HOST, BROKER_PORT, 60)
        client.loop_start()
        time.sleep(1)
        
        # 测试不同 QoS
        for qos in [0, 1, 2]:
            topic = f'qos/test/qos{qos}'
            msg = f'QoS {qos} test message'
            client.publish(topic, msg, qos=qos)
            print(f'  Published QoS {qos}: {msg}')
            time.sleep(0.5)
        
        time.sleep(2)
        
        print(f'\nReceived {len(received_messages)} messages')
        for msg in received_messages:
            print(f'  [{msg["topic"]}] QoS={msg["qos"]}: {msg["payload"]}')
        
        if len(received_messages) >= 3:
            print('✓ Test PASSED')
            return True
        else:
            print('✗ Test FAILED')
            return False
            
    except Exception as e:
        print(f'✗ Test ERROR: {e}')
        return False
    finally:
        client.loop_stop()
        client.disconnect()

def main():
    """主测试函数"""
    print('=' * 60)
    print('   MQTT Broker Functional Tests')
    print('=' * 60)
    print(f'Broker: {BROKER_HOST}:{BROKER_PORT}')
    print(f'Duration: ~{TEST_DURATION}s\n')
    
    results = []
    
    # 运行测试
    results.append(('Basic Pub/Sub', test_basic_pubsub()))
    results.append(('QoS Levels', test_qos_levels()))
    
    # 汇总结果
    print('\n' + '=' * 60)
    print('   Test Summary')
    print('=' * 60)
    
    passed = sum(1 for _, result in results if result)
    total = len(results)
    
    for name, result in results:
        status = '✓ PASSED' if result else '✗ FAILED'
        print(f'{name:30s} {status}')
    
    print('-' * 60)
    print(f'Total: {passed}/{total} tests passed')
    
    if passed == total:
        print('\n✓ All tests passed!')
        return 0
    else:
        print(f'\n✗ {total - passed} test(s) failed')
        return 1

if __name__ == '__main__':
    sys.exit(main())
