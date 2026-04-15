/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-04-14
 *      Author: fasiondog
 */

#include <unordered_map>
#include "hikyuu/utilities/Log.h"
#include "HttpError.h"

namespace hku {

const char* BizException::what() const noexcept {
    return m_msg.c_str();
}

static std::unordered_map<int32_t, BizErrMsgFunc> g_biz_err_map;

HKU_HTTPD_API void register_biz_error_module(int32_t biz_mod, BizErrMsgFunc func) {
    if (g_biz_err_map.find(biz_mod) != g_biz_err_map.end()) {
        HKU_ERROR("Duplicate register biz error module: {}", biz_mod);
    }
    g_biz_err_map[biz_mod] = func;
}

HKU_HTTPD_API const char* biz_err_msg(BizErrCode e) {
    if (e == BIZ_OK) {
        return "OK";
    }

    int32_t mod = (e / 1000) * 1000;
    auto iter = g_biz_err_map.find(mod);
    if (iter != g_biz_err_map.end()) {
        return iter->second(e);
    }

    HKU_WARN("Biz error module not found: {}", mod);
    return "unknown error";
}

static const char* biz_base_err_msg(BizErrCode ec) noexcept {
    switch (get_biz_code(ec)) {
        case 0:
            return "bissness failed";
        case 1:
            return "invalid json";
        case 2:
            return "missing parameter";
        case 3:
            return "wrong parameter";
        case 4:
            return "wrong parameter type";
        case 5:
            return "too many query params";
        case 6:
            return "too long url";
        case 7:
            return "invalid url";
        default:
            return "unknown error";
    }
}

static const char* biz_auth_err_msg(BizErrCode ec) noexcept {
    switch (get_biz_code(ec)) {
        case 0:
            return "failed authorized";
        case 1:
            return "missing token";
        case 2:
            return "authorize expired";
        default:
            return "auth unknown error";
    }
}

// 自动注册函数，确保在程序启动时执行
static void auto_register_biz_errors() {
    register_biz_error_module(BIZ_MOD_BASE, biz_base_err_msg);
    register_biz_error_module(BIZ_MOD_AUTH, biz_auth_err_msg);
}

// 跨平台自动初始化
#if defined(_MSC_VER)
// MSVC: 使用 section 和 initializer
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void (*init_func)(void) = auto_register_biz_errors;
#elif defined(__GNUC__) || defined(__clang__)
// GCC/Clang: 使用 constructor 属性
__attribute__((constructor)) static void init_biz_errors() {
    auto_register_biz_errors();
}
#else
#error "Unsupported compiler for automatic initialization"
#endif

}  // namespace hku