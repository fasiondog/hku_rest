#!/usr/bin/env python3
"""
HTTP 流式分批传输功能测试脚本

测试场景：
1. SSE (Server-Sent Events) 实时推送
2. 大数据量 CSV 导出
3. 文件下载（需要创建测试文件）
"""

import requests
import time
import sys

BASE_URL = "http://localhost:8765"

def test_sse_streaming():
    """测试 SSE 实时数据推送"""
    print("\n" + "="*60)
    print("测试 1: SSE (Server-Sent Events) 实时推送")
    print("="*60)
    
    try:
        url = f"{BASE_URL}/api/sse"
        print(f"\n请求 URL: {url}")
        print("预期：每秒接收 1 条消息，共 10 条\n")
        
        start_time = time.time()
        response = requests.get(url, stream=True, timeout=15)
        response.raise_for_status()
        
        message_count = 0
        for line in response.iter_lines():
            if line:
                decoded_line = line.decode('utf-8')
                print(f"收到：{decoded_line}")
                message_count += 1
        
        elapsed = time.time() - start_time
        print(f"\n✅ SSE 测试完成！")
        print(f"   接收消息数：{message_count}")
        print(f"   耗时：{elapsed:.2f}秒")
        print(f"   平均速率：{message_count/elapsed:.2f}条/秒")
        
    except requests.exceptions.RequestException as e:
        print(f"\n❌ SSE 测试失败：{e}")
        return False
    
    return True


def test_csv_export():
    """测试大数据量 CSV 导出"""
    print("\n" + "="*60)
    print("测试 2: 大数据量 CSV 导出（10000 条记录）")
    print("="*60)
    
    try:
        url = f"{BASE_URL}/api/export/csv"
        print(f"\n请求 URL: {url}")
        print("预期：分批次接收 10000 条 CSV 数据\n")
        
        start_time = time.time()
        response = requests.get(url, stream=True, timeout=30)
        response.raise_for_status()
        
        # 检查响应头
        print(f"响应头:")
        print(f"  Content-Type: {response.headers.get('Content-Type')}")
        print(f"  Content-Disposition: {response.headers.get('Content-Disposition')}")
        
        # 读取并统计行数
        line_count = 0
        header_printed = False
        for line in response.iter_lines():
            if line:
                decoded_line = line.decode('utf-8')
                if not header_printed:
                    print(f"\nCSV 头部：{decoded_line}")
                    header_printed = True
                line_count += 1
        
        elapsed = time.time() - start_time
        
        # 保存到文件
        output_file = "/tmp/test_export.csv"
        with open(output_file, 'wb') as f:
            response.raw.decode_content = True
            # 重新请求一次以保存完整内容
            response = requests.get(url, stream=True)
            for chunk in response.iter_content(chunk_size=8192):
                f.write(chunk)
        
        print(f"\n✅ CSV 导出测试完成！")
        print(f"   总行数：{line_count}")
        print(f"   耗时：{elapsed:.2f}秒")
        print(f"   导出速率：{line_count/elapsed:.2f}条/秒")
        print(f"   保存文件：{output_file}")
        
    except requests.exceptions.RequestException as e:
        print(f"\n❌ CSV 导出测试失败：{e}")
        return False
    
    return True


def test_file_download(filepath="/tmp/test_large_file.bin"):
    """测试大文件下载"""
    print("\n" + "="*60)
    print("测试 3: 大文件下载")
    print("="*60)
    
    # 创建测试文件
    file_size = 1 * 1024 * 1024  # 1MB
    print(f"\n创建测试文件：{filepath} ({file_size/1024/1024:.1f}MB)")
    
    try:
        with open(filepath, 'wb') as f:
            # 写入随机数据
            import os
            f.write(os.urandom(file_size))
        
        print(f"测试文件已创建")
        
        # 测试文件下载
        url = f"{BASE_URL}/api/download?file={filepath}"
        print(f"\n请求 URL: {url}")
        print("预期：分块下载文件\n")
        
        start_time = time.time()
        response = requests.get(url, stream=True, timeout=30)
        response.raise_for_status()
        
        # 检查响应头
        print(f"响应头:")
        print(f"  Content-Type: {response.headers.get('Content-Type')}")
        print(f"  Content-Disposition: {response.headers.get('Content-Disposition')}")
        print(f"  Content-Length: {response.headers.get('Content-Length')}")
        
        # 下载文件
        download_path = "/tmp/downloaded_file.bin"
        downloaded_size = 0
        with open(download_path, 'wb') as f:
            for chunk in response.iter_content(chunk_size=8192):
                if chunk:
                    f.write(chunk)
                    downloaded_size += len(chunk)
        
        elapsed = time.time() - start_time
        
        # 验证文件完整性
        import hashlib
        with open(filepath, 'rb') as f:
            original_hash = hashlib.md5(f.read()).hexdigest()
        
        with open(download_path, 'rb') as f:
            downloaded_hash = hashlib.md5(f.read()).hexdigest()
        
        match = original_hash == downloaded_hash
        
        print(f"\n✅ 文件下载测试完成！")
        print(f"   原始文件大小：{file_size} bytes")
        print(f"   下载文件大小：{downloaded_size} bytes")
        print(f"   耗时：{elapsed:.2f}秒")
        print(f"   下载速度：{downloaded_size/elapsed/1024:.2f} KB/s")
        print(f"   MD5 校验：{'✅ 匹配' if match else '❌ 不匹配'}")
        print(f"   保存位置：{download_path}")
        
    except Exception as e:
        print(f"\n❌ 文件下载测试失败：{e}")
        return False
    
    return True


def main():
    print("\n" + "="*60)
    print("HTTP 流式分批传输功能测试")
    print("="*60)
    print(f"服务器地址：{BASE_URL}")
    print("请确保 websocket_server 已启动并监听 8080 端口\n")
    
    # 等待服务器就绪
    print("检查服务器是否在线...")
    for i in range(5):
        try:
            response = requests.get(f"{BASE_URL}/api/hello", timeout=2)
            if response.status_code == 200:
                print("✅ 服务器在线\n")
                break
        except:
            print(f"  等待服务器就绪... ({i+1}/5)")
            time.sleep(1)
    else:
        print("\n❌ 服务器未响应，请先启动 websocket_server")
        print("   启动命令：xmake r websocket_server")
        sys.exit(1)
    
    # 执行测试
    results = []
    
    # 测试 1: SSE
    results.append(("SSE 实时推送", test_sse_streaming()))
    
    # 测试 2: CSV 导出
    results.append(("CSV 大数据导出", test_csv_export()))
    
    # 测试 3: 文件下载
    results.append(("文件下载", test_file_download()))
    
    # 汇总结果
    print("\n" + "="*60)
    print("测试结果汇总")
    print("="*60)
    
    passed = sum(1 for _, result in results if result)
    total = len(results)
    
    for name, result in results:
        status = "✅ 通过" if result else "❌ 失败"
        print(f"{status}: {name}")
    
    print(f"\n总计：{passed}/{total} 测试通过")
    
    if passed == total:
        print("\n🎉 所有测试通过！")
        return 0
    else:
        print("\n⚠️  部分测试失败")
        return 1


if __name__ == "__main__":
    sys.exit(main())
