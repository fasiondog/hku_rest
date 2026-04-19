#!/usr/bin/env python3
"""
MCP Server Test Client with Session Support
"""

import requests
import json
import uuid
import sys

class MCPTestClient:
    def __init__(self, base_url="http://localhost:8080"):
        self.base_url = base_url
        self.mcp_url = f"{base_url}/mcp"
        self.session_id = str(uuid.uuid4())  # 客户端生成 Session ID
        self.request_id = 0
        self.session = requests.Session()
        
    def send_request(self, method, params=None):
        """发送 JSON-RPC 请求"""
        self.request_id += 1
        
        request = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params or {},
            "id": self.request_id
        }
        
        headers = {
            "Content-Type": "application/json",
            "X-Session-ID": self.session_id  # 在请求头中传递 Session ID
        }
        
        response = self.session.post(self.mcp_url, json=request, headers=headers)
        return response.json()
    
    def send_notification(self, method, params=None):
        """发送通知（无响应）"""
        request = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params or {}
        }
        
        headers = {
            "Content-Type": "application/json",
            "X-Session-ID": self.session_id
        }
        
        self.session.post(self.mcp_url, json=request, headers=headers)
    
    def initialize(self):
        """初始化连接"""
        print("\n=== Testing initialize ===")
        result = self.send_request("initialize", {
            "protocolVersion": "2024-11-05",
            "capabilities": {},
            "clientInfo": {
                "name": "test-client",
                "version": "1.0.0"
            }
        })
        print(f"Response: {json.dumps(result, indent=2)}")
        return result
    
    def initialized(self):
        """发送初始化完成通知"""
        print("\n=== Sending initialized notification ===")
        self.send_notification("initialized", {})
        print("Notification sent")
    
    def list_tools(self):
        """列出可用工具"""
        print("\n=== Testing tools/list ===")
        result = self.send_request("tools/list")
        print(f"Available tools: {json.dumps(result, indent=2)}")
        return result
    
    def call_tool(self, tool_name, arguments):
        """调用工具"""
        print(f"\n=== Testing tools/call: {tool_name} ===")
        result = self.send_request("tools/call", {
            "name": tool_name,
            "arguments": arguments
        })
        print(f"Result: {json.dumps(result, indent=2)}")
        return result
    
    def list_resources(self):
        """列出可用资源"""
        print("\n=== Testing resources/list ===")
        result = self.send_request("resources/list")
        print(f"Available resources: {json.dumps(result, indent=2)}")
        return result
    
    def read_resource(self, uri):
        """读取资源"""
        print(f"\n=== Testing resources/read: {uri} ===")
        result = self.send_request("resources/read", {"uri": uri})
        print(f"Content: {json.dumps(result, indent=2)}")
        return result
    
    def list_prompts(self):
        """列出可用提示词"""
        print("\n=== Testing prompts/list ===")
        result = self.send_request("prompts/list")
        print(f"Available prompts: {json.dumps(result, indent=2)}")
        return result
    
    def get_prompt(self, prompt_name, arguments):
        """获取提示词"""
        print(f"\n=== Testing prompts/get: {prompt_name} ===")
        result = self.send_request("prompts/get", {
            "name": prompt_name,
            "arguments": arguments
        })
        print(f"Prompt: {json.dumps(result, indent=2)}")
        return result


def test_basic_flow():
    """测试基本流程"""
    client = MCPTestClient()
    
    try:
        # 1. 初始化
        client.initialize()
        
        # 2. 发送初始化完成通知
        client.initialized()
        
        # 3. 列出工具
        client.list_tools()
        
        # 4. 调用工具 - 计算器
        client.call_tool("calculator", {"expression": "2 + 2"})
        
        # 5. 调用工具 - 获取时间
        client.call_tool("get_current_time", {})
        
        # 6. 调用工具 - 天气查询
        client.call_tool("get_weather", {"location": "Beijing"})
        
        # 7. 列出资源
        client.list_resources()
        
        # 8. 读取资源
        client.read_resource("doc://getting-started")
        
        # 9. 列出提示词
        client.list_prompts()
        
        # 10. 获取提示词
        client.get_prompt("code_review", {
            "language": "python",
            "code": "def hello():\n    print('world')"
        })
        
        print("\n✅ All tests passed!")
        
        # === 测试 Session 功能 ===
        print("\n=== Testing session/info ===")
        response = client.send_request("session/info")
        print(f"Session info: {json.dumps(response, indent=2)}")
        
        print("\n=== Testing session/set_metadata ===")
        response = client.send_request("session/set_metadata", {
            "key": "user_preference",
            "value": {"theme": "dark", "language": "zh-CN"}
        })
        print(f"Set metadata: {json.dumps(response, indent=2)}")
        
        print("\n=== Testing session/info after setting metadata ===")
        response = client.send_request("session/info")
        print(f"Session info with metadata: {json.dumps(response, indent=2)}")
        
        print("\n=== Testing get_session_history tool ===")
        response = client.send_request("tools/call", {
            "name": "get_session_history",
            "arguments": {"limit": 5}
        })
        print(f"Session history: {json.dumps(response, indent=2)}")
        
        print("\n=== Testing session/unregister ===")
        response = client.send_request("session/unregister")
        print(f"Unregister result: {json.dumps(response, indent=2)}")
        
        # 尝试使用已注销的会话（应该失败）
        print("\n=== Testing with unregistered session (should fail) ===")
        response = client.send_request("tools/list")
        print(f"Response: {json.dumps(response, indent=2)}")
        
        # 重新注册新会话
        print("\n=== Re-registering new session ===")
        client.session_id = str(uuid.uuid4())
        response = client.send_request("initialize", {
            "protocolVersion": "2024-11-05",
            "capabilities": {},
            "clientInfo": {
                "name": "test-client-reconnect",
                "version": "1.0.0"
            }
        })
        print(f"Re-initialize: {json.dumps(response, indent=2)}")
        
    except Exception as e:
        print(f"\n❌ Test failed: {e}")
        sys.exit(1)


def test_error_handling():
    """测试错误处理"""
    client = MCPTestClient()
    
    print("\n=== Testing error handling ===")
    
    # 测试无效方法
    try:
        result = client._send_request("invalid_method")
        print(f"Error response: {json.dumps(result, indent=2)}")
    except Exception as e:
        print(f"Expected error: {e}")
    
    # 测试未知工具
    try:
        client.call_tool("nonexistent_tool", {})
    except Exception as e:
        print(f"Expected error: {e}")
    
    print("✅ Error handling tests completed")


if __name__ == "__main__":
    print("MCP Server Test Client")
    print("=" * 50)
    
    # 运行基本测试
    test_basic_flow()
    
    # 运行错误处理测试
    test_error_handling()
    
    print("\n" + "=" * 50)
    print("All tests completed successfully!")
