/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-03-14
 *      Author: fasiondog
 */

#include "hikyuu/utilities/thread/algorithm.h"
#include "gzip/compress.hpp"
#include "gzip/utils.hpp"
#include "pod/CommonPod.h"
#include "RestHandle.h"

namespace hku {

VoidBizResult RestHandle::before_run() noexcept {
    setResHeader("Content-Type", "application/json; charset=UTF-8");

    std::string data = getReqData();
    try {
        if (!data.empty()) {
            req = json::parse(data);
        }
    } catch (const std::exception& e) {
        HKU_ERROR("Failed parse json: {}", data);
        return BIZ_BASE_INVALID_JSON;
    } catch (...) {
        HKU_ERROR("Failed parse json: {}", data);
        return BIZ_BASE_INVALID_JSON;
    }

    return BIZ_OK;
}

VoidBizResult RestHandle::after_run() noexcept {
    json new_res;
    new_res["ret"] = 0;
    new_res["data"] = std::move(res);

    auto* ctx = static_cast<BeastContext*>(m_beast_context);

    std::string content = new_res.dump();
    if (content.size() < 1024) {
        ctx->res.body() = std::move(content);
        return BIZ_OK;
    }

    std::string encodings = getReqHeader("Accept-Encoding");
    size_t pos = encodings.find("gzip");
    if (pos != std::string::npos) {
        setResHeader("Content-Encoding", "gzip");
        gzip::Compressor comp(Z_DEFAULT_COMPRESSION);
        comp.compress(ctx->res.body(), content.data(), content.size());
    } else {
        ctx->res.body() = std::move(content);
    }

    return BIZ_OK;
}

VoidBizResult BizHandle::before_run() noexcept {
    setResHeader("Content-Type", "application/json; charset=UTF-8");

    std::string data = getReqData();
    try {
        if (!data.empty()) {
            req = json::parse(data);
        }
    } catch (const std::exception& e) {
        CLS_ERROR("Failed parse json: {}", data);
        return BIZ_BASE_INVALID_JSON;
    } catch (...) {
        CLS_ERROR("Failed parse json: {}", data);
        return BIZ_BASE_INVALID_JSON;
    }
    return BIZ_OK;
}

VoidBizResult BizHandle::after_run() noexcept {
    json new_res;
    new_res["ret"] = 0;
    new_res["data"] = std::move(res);

    auto* ctx = static_cast<BeastContext*>(m_beast_context);

    std::string content = new_res.dump();
    if (content.size() < 1024) {
        ctx->res.body() = std::move(content);
        return BIZ_OK;
    }

    std::string encodings = getReqHeader("Accept-Encoding");
    size_t pos = encodings.find("gzip");
    if (pos != std::string::npos) {
        setResHeader("Content-Encoding", "gzip");
        gzip::Compressor comp(Z_DEFAULT_COMPRESSION);
        comp.compress(ctx->res.body(), content.data(), content.size());
    } else {
        ctx->res.body() = std::move(content);
    }

    return BIZ_OK;
}

net::awaitable<VoidBizResult> BizHandle::run() {
    auto before_result = before_biz_run();
    if (!before_result) {
        co_return before_result;
    }
    co_return co_await co_run(pod::CommonPod::executor(), [this]() -> VoidBizResult {
        try {
            return biz_run();
        } catch (const BizException& e) {
            CLS_ERROR("BizException in biz_run: {}, {}", e.errcode(), e.what());
            return e.errcode();
        } catch (const std::exception& e) {
            CLS_ERROR("Exception in biz_run: {}", e.what());
            return BIZ_BASE_FAILED;
        } catch (...) {
            CLS_ERROR("Unknown exception in biz_run");
            return BIZ_BASE_FAILED;
        }
    });
}

}  // namespace hku