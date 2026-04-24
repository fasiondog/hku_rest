/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-03-28
 *      Author: fasiondog
 */

#include "Router.h"

namespace hku {

// ============================================================================
// Router 实现
// ============================================================================
void Router::registerHandler(std::string method, std::string path, HandlerFunc handler) {
    m_routes.emplace_back(RouteKey{std::move(method), std::move(path)}, std::move(handler));
}

Router::HandlerFunc Router::findHandler(const std::string& method, const std::string& path) {
    // 从 path 中提取路径部分（去掉查询参数）
    // 例如：/api/download?file=xxx -> /api/download
    std::string_view path_view(path);
    auto query_pos = path_view.find('?');
    std::string_view path_only =
      (query_pos != std::string_view::npos) ? path_view.substr(0, query_pos) : path_view;

    // 精确匹配优先（线性搜索，路由数量有限时性能优于哈希表）
    for (const auto& [key, handler] : m_routes) {
        if (key.method == method && key.path == path_only) {
            return handler;
        }
    }

    // 通配符路由匹配（简单的前缀匹配）
    // 注意：通配符路由应该在注册时放在 vector 后部，避免每次都要遍历
    for (const auto& [key, handler] : m_routes) {
        if (key.method == method && !key.path.empty() && key.path.back() == '*') {
            // 前缀匹配：/api/* 匹配 /api/users
            std::string_view prefix(key.path.data(), key.path.size() - 1);
            if (path_only.find(prefix) == 0) {
                return handler;
            }
        }
    }

    return nullptr;
}

// ============================================================================
// WebSocketRouter 实现
// ============================================================================

void WebSocketRouter::registerHandler(std::string path, HandleFactory factory) {
    m_routes.emplace_back(std::move(path), std::move(factory));
}

WebSocketRouter::HandleFactory WebSocketRouter::findHandler(const std::string& path) {
    // 精确匹配优先（线性搜索）
    for (const auto& [route_path, factory] : m_routes) {
        if (route_path == path) {
            return factory;
        }
    }

    return nullptr;
}

}  // namespace hku