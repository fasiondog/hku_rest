/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-03-14
 *      Author: fasiondog
 */

#include "RestHandle.h"
#include "gzip/compress.hpp"
#include "gzip/utils.hpp"

namespace hku {

net::awaitable<void> RestHandle::before_run() {
    setResHeader("Content-Type", "application/json; charset=UTF-8");

    std::string data = getReqData();
    try {
        if (!data.empty()) {
            req = json::parse(data);
        }
    } catch (json::exception& e) {
        HKU_ERROR("Failed parse json: {}", data);
        throw HttpBadRequestError(BadRequestErrorCode::INVALID_JSON_REQUEST, e.what());
    }

    co_return;
}

net::awaitable<void> RestHandle::after_run() {
    json new_res;
    new_res["ret"] = 0;
    new_res["data"] = std::move(res);

    auto* ctx = static_cast<BeastContext*>(m_beast_context);

    std::string content = new_res.dump();
    if (content.size() < 1024) {
        ctx->res.body() = std::move(content);
        co_return;
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

    co_return;
}

}  // namespace hku