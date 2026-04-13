/*
 *  Copyright (c) 2026 hikyuu.org
 *
 *  Created on: 2026-04-13
 *      Author: fasiondog
 */

#pragma once

#include <version>
#include <variant>

#if __cpp_lib_expected >= 202211L
#define STDX_USE_STD 1
#include <expected>

namespace stdx {
using std::bad_expected_access;
using std::expected;
using std::make_unexpected;
using std::unexpect;
using std::unexpect_t;
using std::unexpected;
}  // namespace stdx

#else
#define STDX_USE_STD 0
#include <tl/expected.hpp>

namespace stdx {

// ------------------------------
// 纯别名，无继承、无包装
// 100% 协程兼容
// ------------------------------
template <typename T, typename E>
using expected = tl::expected<T, E>;

using tl::bad_expected_access;
using tl::make_unexpected;
using tl::unexpect;
using tl::unexpect_t;
using tl::unexpected;

// ------------------------------
// 全局禁用所有非标准扩展
// 用 C++20 约束强制删除
// ------------------------------
#if __cpp_concepts >= 201907L

template <typename Ex, typename F>
    requires requires(Ex ex, F f) { ex.map(f); }
[[deprecated("STDX: 禁止使用 map()，请改用标准 transform()")]]
void operator|(Ex&&, F&&) = delete;

template <typename Ex, typename F>
    requires requires(Ex ex, F f) { ex.flat_map(f); }
[[deprecated("STDX: 禁止使用 flat_map()，请改用 and_then()")]]
void operator|(Ex&&, F&&) = delete;

template <typename Ex, typename F>
    requires requires(Ex ex, F f) { ex.map_error(f); }
[[deprecated("STDX: 禁止使用 map_error()，请改用 transform_error()")]]
void operator|(Ex&&, F&&) = delete;

#endif

}  // namespace stdx

#endif

#undef STDX_USE_STD