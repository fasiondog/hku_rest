/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-25
 *      Author: fasiondog
 */

#pragma once

#include <string>
#include "hikyuu/httpd/config.h"

#if HKU_ENABLE_SQLITE
#include "sqlite/SQLitePod.h"
#endif

#if HKU_ENABLE_MYSQL
#include "mysql/MySQLPod.h"
#endif

namespace hku {
namespace pod {

/**
 * 初始化所有资源
 */
void HKU_HTTPD_API init(const std::string& filename);

/**
 * 清理所有资源
 */
void HKU_HTTPD_API quit();

}  // namespace pod
}  // namespace hku