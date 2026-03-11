/*
 *  Copyright(C) 2021 hikyuu.org
 *
 *  Create on: 2021-03-21
 *     Author: fasiondog
 */

#pragma once

// HTTP 核心组件
#include "HttpError.h"
#include "HttpHandle.h"
#include "RestHandle.h"
#include "HttpServer.h"
#include "HttpService.h"

// 版本信息
#include "version.h"

// Pod 模块
#include "pod/all.h"

// 协程辅助工具
#include "coroutine_helpers.h"

/**
 * @namespace hku::httpd
 * @brief 基于 Boost.Beast 和 C++20 协程的 HTTP 服务器模块
 *
 * 特性:
 * - 完整的 C++20 协程支持
 * - 端到端异步 IO
 * - 简洁的同步式代码风格
 * - 完全兼容原 httpd 接口
 */
namespace hku::httpd {

/**
 * @brief 获取版本号
 */
inline const char* getVersion() {
    return HTTPD_VERSION;
}

/**
 * @brief 获取版本描述
 */
inline const char* getVersionString() {
    return HTTPD_VERSION_STRING;
}

}  // namespace hku::httpd
