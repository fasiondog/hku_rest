/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-25
 *      Author: fasiondog
 */

#pragma once

#include "hikyuu/utilities/ResourcePool.h"
#include "hikyuu/utilities/db_connect/DBConnect.h"
#include "hikyuu/utilities/log.h"

namespace hku {
namespace pod {

class HKU_HTTPD_API MySQLPod {
    CLASS_LOGGER_IMP(MySQLPod)

public:
    MySQLPod() = delete;

    static void init();
    static void quit();

private:
    static std::unique_ptr<ResourcePool<MySQLConnect>> ms_db_pool;  // 本地任务数据库
};

}  // namespace pod
}  // namespace hku