/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-04-21
 *      Author: fasiondog
 */

#pragma once

#include "hikyuu/httpd/HttpError.h"

namespace hku {

constexpr BizErrCode BIZ_MOD_MCP = -3;
#define BIZ_MCP_ERR(code) MAKE_ERR(BIZ_MOD_MCP, code);
constexpr BizErrCode BIZ_MCP_PARSE_ERROR = BIZ_MCP_ERR(-2700);  // 收到无效 JSON
constexpr BizErrCode BIZ_MCP_INVALID_REQUEST =
  BIZ_MCP_ERR(-2600);  // JSON 合法，但不是有效请求（缺少 id/jsonrpc/method）
constexpr BizErrCode BIZ_MCP_METHOD_NOT_FOUND = BIZ_MCP_ERR(-2601);  // 调用的方法 / 工具不存在
constexpr BizErrCode BIZ_MCP_INVALID_PARAMS = BIZ_MCP_ERR(-2602);    // 参数缺失、类型错误、格式非法
constexpr BizErrCode BIZ_MCP_INTERNAL_ERROR =
  BIZ_MCP_ERR(-2603);  // 服务端内部异常（未捕获错误、崩溃）

constexpr BizErrCode BIZ_MCP_TOOL_EXECUTION_ERROR =
  BIZ_MCP_ERR(-2000);  // 工具找到但执行失败（业务逻辑错误）
constexpr BizErrCode BIZ_MCP_TOOL_NOT_FOUND = BIZ_MCP_ERR(-2001);  // 资源 / 工具不存在
constexpr BizErrCode BIZ_MCP_RATE_LIMITED = BIZ_MCP_ERR(-2002);    // 请求超限、限流
constexpr BizErrCode BIZ_MCP_TIMEOUT = BIZ_MCP_ERR(-2003);         // 执行超时
constexpr BizErrCode BIZ_MCP_UNAUTHORIZED =
  BIZ_MCP_ERR(-2004);  // 鉴权失败（token 无效 / 过期 / 无权限）
constexpr BizErrCode BIZ_MCP_SOURCE_UNAVAILABLE =
  BIZ_MCP_ERR(-2005);  // 资源暂时不可用（维护 / 过载）
constexpr BizErrCode BIZ_MCP_INVALID_RESOURCE_URI = BIZ_MCP_ERR(-2006);  // 资源 URI 格式错误
constexpr BizErrCode BIZ_MCP_PERMISSION_DENIED = BIZ_MCP_ERR(-2007);     // 已鉴权但无权限操作
constexpr BizErrCode BIZ_MCP_VERSION_MISMATCH =
  BIZ_MCP_ERR(-2008);  // 客户端 / 服务端协议版本不兼容

// Additional MCP error codes
constexpr BizErrCode BIZ_MCP_PROMPT_NOT_FOUND = BIZ_MCP_ERR(-2009);     // Prompt 不存在
constexpr BizErrCode BIZ_MCP_RESOURCE_NOT_FOUND = BIZ_MCP_ERR(-2010);   // Resource 不存在
constexpr BizErrCode BIZ_MCP_INVALID_PROMPT = BIZ_MCP_ERR(-2011);       // Prompt 格式或参数错误
constexpr BizErrCode BIZ_MCP_INVALID_RESOURCE = BIZ_MCP_ERR(-2012);     // Resource 格式或访问错误
constexpr BizErrCode BIZ_MCP_NOT_SUPPORTED = BIZ_MCP_ERR(-2013);        // 请求的功能/方法不支持
constexpr BizErrCode BIZ_MCP_SESSION_EXPIRED = BIZ_MCP_ERR(-2014);      // Session 已过期
constexpr BizErrCode BIZ_MCP_CONNECTION_CLOSED = BIZ_MCP_ERR(-2015);    // 连接意外关闭

}  // namespace hku