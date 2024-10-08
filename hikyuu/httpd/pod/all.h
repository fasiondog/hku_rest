/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-25
 *      Author: fasiondog
 */

#pragma once

#include <string>
#include "hikyuu/httpd/config.h"
#include "hikyuu/utilities/mo/mo.h"
#include "PodConfig.h"
#include "CommonPod.h"

#if HKU_HTTPD_POD_USE_SQLITE
#include "sqlite/SQLitePod.h"
#endif

#if HKU_HTTPD_POD_USE_MYSQL
#include "mysql/MySQLPod.h"
#endif

namespace hku {
namespace pod {

/**
 * 初始化所有资源, 需要在调用前自行通过 PodConfig 进行配置，否则使用默认参数
 */
void HKU_HTTPD_API init();

/**
 * 从 ini 文件中获取配置信息，并初始化所有资源
 */
void HKU_HTTPD_API init(const std::string& filename);

/**
 * 清理所有资源
 */
void HKU_HTTPD_API quit();

}  // namespace pod
}  // namespace hku