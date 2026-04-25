/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-04-25
 *      Author: fasiondog
 */

#pragma once

#include "hikyuu/mcp/McpService.h"

namespace hku {

using json = nlohmann::json;

extern std::vector<json> prompts_list;
extern std::vector<json> resources_list;
extern std::vector<json> tools_list;

net::awaitable<JsonResult> promptGetMethod(McpHandle* handle, const nlohmann::json& params,
                                           const std::string& session_id);

net::awaitable<JsonResult> resourceReadMethod(McpHandle* handle, const nlohmann::json& params,
                                              const std::string& session_id);

net::awaitable<JsonResult> executeCalculator(McpHandle* handle, const nlohmann::json& arguments,
                                             const std::string& session_id);

net::awaitable<JsonResult> executeGetCurrentTime(McpHandle* handle, const nlohmann::json& arguments,
                                                 const std::string& session_id);

net::awaitable<JsonResult> executeGetWeather(McpHandle* handle, const nlohmann::json& arguments,
                                             const std::string& session_id);

net::awaitable<JsonResult> executeGetSessionHistory(McpHandle* handle,
                                                    const nlohmann::json& arguments,
                                                    const std::string& session_id);

net::awaitable<JsonResult> executeQueryPaginatedData(McpHandle* handle,
                                                     const nlohmann::json& arguments,
                                                     const std::string& session_id);

net::awaitable<JsonResult> executeLongRunningTask(McpHandle* handle,
                                                  const nlohmann::json& arguments,
                                                  const std::string& session_id);

}  // namespace hku
