/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-25
 *      Author: fasiondog
 */

#pragma once

#include "hikyuu/utilities/ResourcePool.h"
#include "hikyuu/utilities/db_connect/DBConnect.h"
#include "hikyuu/utilities/Log.h"

#ifndef HKU_HTTPD_API
#define HKU_HTTPD_API
#endif

namespace hku {
namespace pod {

class HKU_HTTPD_API MySQLPod {
    CLASS_LOGGER_IMP(MySQLPod)

public:
    MySQLPod() = delete;

    static void init();
    static void quit();

    /** 获取本地数据库空闲连接数量 */
    static DBConnectPtr getConnect() {
        return ms_db_pool->getAndWait();
    }

    /** 获取本地数据库连接总数量 */
    static size_t getCount() {
        return ms_db_pool->count();
    }

    /** 获取本地数据库空闲连接数量 */
    static size_t getIdleCount() {
        return ms_db_pool->idleCount();
    }

private:
    static std::unique_ptr<ResourcePool<MySQLConnect>> ms_db_pool;  // 本地任务数据库
};

}  // namespace pod
}  // namespace hku