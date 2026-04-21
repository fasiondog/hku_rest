/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-04-21
 *      Author: fasiondog
 */

#include "McpError.h"

namespace hku {

static const char* biz_mcp_err_msg(BizErrCode ec) noexcept {
    switch (get_biz_code(ec)) {
        // Standard JSON-RPC 2.0 error codes
        case BIZ_MCP_PARSE_ERROR:
            return "Parse error";

        case BIZ_MCP_INVALID_REQUEST:
            return "Invalid request";

        case BIZ_MCP_METHOD_NOT_FOUND:
            return "Method not found";

        case BIZ_MCP_INVALID_PARAMS:
            return "Invalid params";

        case BIZ_MCP_INTERNAL_ERROR:
            return "Internal error";

        // MCP application-level error codes
        case BIZ_MCP_TOOL_EXECUTION_ERROR:
            return "Tool execution failed";

        case BIZ_MCP_TOOL_NOT_FOUND:
            return "Tool not found";

        case BIZ_MCP_RATE_LIMITED:
            return "Rate limit exceeded";

        case BIZ_MCP_TIMEOUT:
            return "Request timeout";

        case BIZ_MCP_UNAUTHORIZED:
            return "Unauthorized";

        case BIZ_MCP_SOURCE_UNAVAILABLE:
            return "Source unavailable";

        case BIZ_MCP_INVALID_RESOURCE_URI:
            return "Invalid resource URI";

        case BIZ_MCP_PERMISSION_DENIED:
            return "Permission denied";

        case BIZ_MCP_VERSION_MISMATCH:
            return "Protocol version mismatch";

        // Additional MCP error codes
        case BIZ_MCP_PROMPT_NOT_FOUND:
            return "Prompt not found";

        case BIZ_MCP_RESOURCE_NOT_FOUND:
            return "Resource not found";

        case BIZ_MCP_INVALID_PROMPT:
            return "Invalid prompt";

        case BIZ_MCP_INVALID_RESOURCE:
            return "Invalid resource";

        case BIZ_MCP_NOT_SUPPORTED:
            return "Not supported";

        case BIZ_MCP_SESSION_EXPIRED:
            return "Session expired";

        case BIZ_MCP_CONNECTION_CLOSED:
            return "Connection closed";

        default:
            return "Unknown error";
    }
}

// 全局自动注册
static inline bool mcp_mod_reg = [] {
    register_mcp_error_module(BIZ_MOD_MCP, biz_mcp_err_msg);
    return true;
}();

}  // namespace hku