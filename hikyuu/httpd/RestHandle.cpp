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
    std::string data = getReqData();
    try {
        if (!data.empty()) {
            auto content_type = getReqHeader("Content-Type");
            if (content_type.empty()) {
                req = json::parse(data);
            } else if (content_type.find("application/json") != std::string::npos) {
                req = json::parse(data);
            } else if (content_type.find("application/msgpack") != std::string::npos) {
                req = json::from_msgpack(data);
            } else if (content_type.find("application/cbor") != std::string::npos) {
                req = json::from_cbor(data);
            } else {
                req = json::parse(data);
            }
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

VoidBizResult RestHandle::after_run() noexcept {
    try {
        json new_res = {{"ret", 0}, {"data", std::move(res)}};
        auto accept_type = getReqHeader("Accept");
        int code_type = 0;  // 0: json; 1: msgpack; 2: cbor
        if (!accept_type.empty()) {
            if (accept_type.find("application/msgpack") != std::string::npos) {
                code_type = 1;
            } else if (accept_type.find("application/cbor") != std::string::npos) {
                code_type = 2;
            }
        }

        auto* ctx = static_cast<BeastContext*>(m_beast_context);
        if (code_type == 0) {
            std::string content = new_res.dump();
            if (content.size() >= 10240) [[unlikely]] {
                std::string encodings = getReqHeader("Accept-Encoding");
                size_t pos = encodings.find("gzip");
                if (pos != std::string::npos) {
                    setResHeader("Content-Encoding", "gzip");
                    gzip::Compressor comp(Z_DEFAULT_COMPRESSION);
                    comp.compress(ctx->res.body(), content.data(), content.size());
                } else {
                    ctx->res.body() = std::move(content);
                }
            } else {
                ctx->res.body() = std::move(content);
            }

            setResHeader("Content-Type", "application/json; charset=UTF-8");
            return BIZ_OK;
        }

        std::vector<std::uint8_t> data;
        if (code_type == 1) {
            data = json::to_msgpack(new_res);
            ctx->res.body().assign(data.begin(), data.end());
            setResHeader("Content-Type", "application/msgpack");
            return BIZ_OK;
        }

        data = json::to_cbor(new_res);
        ctx->res.body().assign(data.begin(), data.end());
        setResHeader("Content-Type", "application/cbor");
        return BIZ_OK;

    } catch (const std::exception& e) {
        CLS_ERROR("{}", e.what());
        return BIZ_BASE_FAILED;
    } catch (...) {
        HKU_ERROR_UNKNOWN;
        return BIZ_BASE_FAILED;
    }
}

VoidBizResult BizHandle::before_run() noexcept {
    std::string data = getReqData();
    try {
        if (!data.empty()) {
            auto content_type = getReqHeader("Content-Type");
            if (content_type.empty()) {
                req = json::parse(data);
            } else if (content_type.find("application/json") != std::string::npos) {
                req = json::parse(data);
            } else if (content_type.find("application/msgpack") != std::string::npos) {
                req = json::from_msgpack(data);
            } else if (content_type.find("application/cbor") != std::string::npos) {
                req = json::from_cbor(data);
            } else {
                req = json::parse(data);
            }
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
    try {
        json new_res = {{"ret", 0}, {"data", std::move(res)}};
        auto accept_type = getReqHeader("Accept");
        int code_type = 0;  // 0: json; 1: msgpack; 2: cbor
        if (!accept_type.empty()) {
            if (accept_type.find("application/msgpack") != std::string::npos) {
                code_type = 1;
            } else if (accept_type.find("application/cbor") != std::string::npos) {
                code_type = 2;
            }
        }

        auto* ctx = static_cast<BeastContext*>(m_beast_context);
        if (code_type == 0) {
            std::string content = new_res.dump();
            if (content.size() >= 10240) [[unlikely]] {
                std::string encodings = getReqHeader("Accept-Encoding");
                size_t pos = encodings.find("gzip");
                if (pos != std::string::npos) {
                    setResHeader("Content-Encoding", "gzip");
                    gzip::Compressor comp(Z_DEFAULT_COMPRESSION);
                    comp.compress(ctx->res.body(), content.data(), content.size());
                } else {
                    ctx->res.body() = std::move(content);
                }
            } else {
                ctx->res.body() = std::move(content);
            }

            setResHeader("Content-Type", "application/json; charset=UTF-8");
            return BIZ_OK;
        }

        std::vector<std::uint8_t> data;
        if (code_type == 1) {
            data = json::to_msgpack(new_res);
            ctx->res.body().assign(data.begin(), data.end());
            setResHeader("Content-Type", "application/msgpack");
            return BIZ_OK;
        }

        data = json::to_cbor(new_res);
        ctx->res.body().assign(data.begin(), data.end());
        setResHeader("Content-Type", "application/cbor");
        return BIZ_OK;

    } catch (const std::exception& e) {
        CLS_ERROR("{}", e.what());
        return BIZ_BASE_FAILED;
    } catch (...) {
        HKU_ERROR_UNKNOWN;
        return BIZ_BASE_FAILED;
    }
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