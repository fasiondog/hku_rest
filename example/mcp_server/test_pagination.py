#!/usr/bin/env python3
"""
MCP Server 分页功能测试脚本

演示如何使用 MCP 协议的分页机制查询大型数据集
"""

import requests
import json
import base64
import sys


class MCPClient:
    """MCP 客户端，支持分页查询"""

    def __init__(self, base_url="http://localhost:8080"):
        self.base_url = base_url
        self.session_id = None
        self.request_id = 0

    def send_request(self, method, params=None):
        """发送 JSON-RPC 请求"""
        self.request_id += 1

        headers = {
            "Content-Type": "application/json",
        }

        # 如果有 session_id，添加到请求头
        if self.session_id:
            headers["Mcp-Session-Id"] = self.session_id

        request = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params or {},
            "id": self.request_id,
        }

        response = requests.post(
            f"{self.base_url}/mcp", headers=headers, json=request
        )

        # 保存 session_id（从响应头获取）
        if "Mcp-Session-Id" in response.headers:
            self.session_id = response.headers["Mcp-Session-Id"]
            print(f"✓ Session ID: {self.session_id}\n")

        return response.json()

    def initialize(self):
        """初始化 MCP 连接"""
        print("=" * 60)
        print("Step 1: Initialize MCP Connection")
        print("=" * 60)

        result = self.send_request(
            "initialize",
            {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "test-client", "version": "1.0.0"},
            },
        )

        print(f"Protocol Version: {result['result']['protocolVersion']}")
        print(f"Server: {result['result']['serverInfo']['name']} v{result['result']['serverInfo']['version']}")
        print()

        # 发送 initialized 通知（不需要等待响应）
        headers = {"Content-Type": "application/json"}
        if self.session_id:
            headers["Mcp-Session-Id"] = self.session_id
        
        notification = {
            "jsonrpc": "2.0",
            "method": "initialized",
            "params": {},
        }
        
        requests.post(f"{self.base_url}/mcp", headers=headers, json=notification)
        print("✓ Initialized\n")

    def list_tools(self):
        """列出所有可用工具"""
        print("=" * 60)
        print("Step 2: List Available Tools")
        print("=" * 60)

        result = self.send_request("tools/list")
        tools = result["result"]["tools"]

        print(f"Total tools available: {len(tools)}\n")
        for i, tool in enumerate(tools, 1):
            print(f"{i}. {tool['name']}")
            print(f"   Description: {tool['description']}")
            print()

        return tools

    def query_paginated_data(self, page_size=10, cursor=None):
        """查询分页数据"""
        params = {"page_size": page_size}
        if cursor:
            params["cursor"] = cursor

        result = self.send_request("tools/call", {
            "name": "query_paginated_data",
            "arguments": params,
        })

        return result["result"]

    def test_pagination(self):
        """测试分页功能"""
        print("=" * 60)
        print("Step 3: Test Pagination with query_paginated_data")
        print("=" * 60)
        print()

        page_num = 1
        cursor = None
        all_items = []

        while True:
            print(f"--- Page {page_num} ---")

            # 查询当前页
            result = self.query_paginated_data(page_size=5, cursor=cursor)

            # 显示文本消息
            text_content = result["content"][0]["text"]
            print(f"Message: {text_content}")

            # 显示分页元数据
            pagination = result.get("pagination", {})
            print(f"Total Items: {pagination.get('total_items', 'N/A')}")
            print(f"Current Range: {pagination.get('current_page_start', 'N/A')} - {pagination.get('current_page_end', 'N/A')}")
            print(f"Returned Count: {pagination.get('returned_count', 'N/A')}")
            print(f"Has More: {pagination.get('has_more', False)}")

            # 显示当前页的数据项
            items = result.get("items", [])
            print(f"\nItems on this page ({len(items)}):")
            for item in items:
                print(f"  - ID: {item['id']}, Name: {item['name']}, Value: {item['value']}")

            all_items.extend(items)

            # 检查是否有下一页
            next_cursor = result.get("nextCursor")
            if not next_cursor:
                print("\n✓ No more pages (reached end of dataset)")
                break

            print(f"\nNext Cursor: {next_cursor[:20]}... (truncated)\n")
            cursor = next_cursor
            page_num += 1

            # 为了演示，只获取前 3 页
            if page_num > 3:
                print(f"\n... (stopping after {page_num - 1} pages for demo)\n")
                break

        print(f"\nSummary:")
        print(f"  Total pages fetched: {page_num}")
        print(f"  Total items retrieved: {len(all_items)}")
        print(f"  First item ID: {all_items[0]['id'] if all_items else 'N/A'}")
        print(f"  Last item ID: {all_items[-1]['id'] if all_items else 'N/A'}")
        print()

    def test_invalid_cursor(self):
        """测试无效游标处理"""
        print("=" * 60)
        print("Step 4: Test Invalid Cursor Handling")
        print("=" * 60)
        print()

        try:
            result = self.query_paginated_data(page_size=5, cursor="invalid_cursor_!!!")
            print("✗ Should have raised an error!")
        except Exception as e:
            print(f"✓ Correctly handled invalid cursor")
            print(f"  Error: {e}")
            print()

    def test_custom_page_size(self):
        """测试自定义页面大小"""
        print("=" * 60)
        print("Step 5: Test Custom Page Size")
        print("=" * 60)
        print()

        # 测试不同的页面大小
        for page_size in [3, 7, 15]:
            print(f"Testing page_size={page_size}:")
            result = self.query_paginated_data(page_size=page_size)

            pagination = result.get("pagination", {})
            returned = pagination.get("returned_count", 0)
            print(f"  Requested: {page_size}, Returned: {returned}")

            has_more = result.get("nextCursor") is not None
            print(f"  Has More: {has_more}")
            print()


def main():
    """主函数"""
    print("\n" + "=" * 60)
    print("MCP Server Pagination Demo")
    print("=" * 60)
    print()

    client = MCPClient("http://localhost:8080")

    try:
        # 1. 初始化
        client.initialize()

        # 2. 列出工具
        client.list_tools()

        # 3. 测试分页
        client.test_pagination()

        # 4. 测试无效游标
        client.test_invalid_cursor()

        # 5. 测试自定义页面大小
        client.test_custom_page_size()

        print("=" * 60)
        print("✓ All tests completed successfully!")
        print("=" * 60)

    except requests.exceptions.ConnectionError:
        print("✗ Error: Cannot connect to MCP server")
        print("  Make sure the server is running: xmake r mcp_server")
        sys.exit(1)
    except Exception as e:
        print(f"✗ Error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
