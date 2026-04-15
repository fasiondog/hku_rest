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

    return "unknown error! not found mod!";
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

// 全局自动注册
static inline bool base_mod_reg = [] {
    register_biz_error_module(BIZ_MOD_BASE, biz_base_err_msg);
    register_biz_error_module(BIZ_MOD_AUTH, biz_auth_err_msg);
    return true;
}();

}  // namespace hku