/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-03-28
 *      Author: fasiondog
 */

#pragma once

#include "HttpHandle.h"
#include "WebSocketHandle.h"

#include <string>
#include <functional>
#include <vector>
#include <memory>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>

namespace hku {

namespace net = boost::asio;

/**
 * HTTP 路由器 - 负责注册和分发请求到对应的 Handle
 */
class Router {
public:
    using HandlerFunc = std::function<net::awaitable<void>(void*)>;

    struct RouteKey {
        std::string method;
        std::string path;

        bool operator==(const RouteKey& other) const {
            return method == other.method && path == other.path;
        }
    };

    void registerHandler(const std::string& method, const std::string& path, HandlerFunc handler);
    HandlerFunc findHandler(const std::string& method, const std::string& path);

private:
    // 使用 vector 存储路由表，避免 map 的哈希开销和动态分配
    // 路由数量有限（通常 < 100），线性搜索性能足够且缓存友好
    std::vector<std::pair<RouteKey, HandlerFunc>> m_routes;
};

/**
 * WebSocket 专用路由器 - 负责根据路径创建 Handle 实例
 */
class WebSocketRouter {
public:
    using HandleFactory = std::function<std::shared_ptr<WebSocketHandle>(void*)>;

    void registerHandler(const std::string& path, HandleFactory factory);
    HandleFactory findHandler(const std::string& path);

private:
    std::vector<std::pair<std::string, HandleFactory>> m_routes;
};

}  // namespace hku