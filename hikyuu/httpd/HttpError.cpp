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

// 使用函数局部静态变量，确保线程安全且在使用时才初始化（C++11 保证）
static std::unordered_map<int32_t, BizErrMsgFunc>& get_biz_err_map() {
    static std::unordered_map<int32_t, BizErrMsgFunc> biz_err_map;
    static bool initialized = []() {
        biz_err_map[BIZ_MOD_BASE] = biz_base_err_msg;
        biz_err_map[BIZ_MOD_AUTH] = biz_auth_err_msg;
        return true;
    }();
    (void)initialized;  // 避免未使用警告
    return biz_err_map;
}

HKU_HTTPD_API void register_biz_error_module(int32_t biz_mod, BizErrMsgFunc func) {
    auto& biz_err_map = get_biz_err_map();
    if (biz_err_map.find(biz_mod) != biz_err_map.end()) {
        HKU_ERROR("Duplicate register biz error module: {}", biz_mod);
    }
    biz_err_map[biz_mod] = func;
}

HKU_HTTPD_API const char* biz_err_msg(BizErrCode e) {
    if (e == BIZ_OK) {
        return "OK";
    }

    int32_t mod = get_biz_mod(e);
    auto& biz_err_map = get_biz_err_map();
    auto iter = biz_err_map.find(mod);
    if (iter != biz_err_map.end()) {
        return iter->second(e);
    }

    HKU_WARN("Not found biz error module: {}", mod);
    return "unknown error";
}

}  // namespace hku