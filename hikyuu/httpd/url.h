/*
 *  Copyright(C) 2021 hikyuu.org
 *
 *  Create on: 2021-03-07
 *     Author: fasiondog
 */

#pragma once

#include <string>

#ifndef HKU_HTTP_API
#define HKU_HTTP_API
#endif

namespace hku {

std::string HKU_HTTP_API url_escape(const char* istr);
std::string HKU_HTTP_API url_unescape(const char* istr);

}  // namespace hku