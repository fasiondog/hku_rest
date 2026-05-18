/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-25
 *      Author: fasiondog
 */

#include "../PodConfig.h"
#include "MySQLPod.h"

namespace hku {
namespace pod {

std::unique_ptr<ResourceHybridPool<AsyncMySQLConnect>> MySQLPod::ms_async_db_pool;
std::unique_ptr<ResourcePool<MySQLConnect>> MySQLPod::ms_db_pool;

void MySQLPod::init() {
    auto& config = PodConfig::instance();
    bool enable_async = config.get<bool>("mysql_async_enable", false);
    bool enable_sync = config.get<bool>("mysql_sync_enable", false);

    CLS_INFO("mysql_async_enable: {}", enable_async);
    CLS_INFO("mysql_sync_enable: {}", enable_sync);
    CLS_WARN_IF_RETURN(!enable_async && !enable_sync, void(), "mysql is disabled");

    CLS_INFO("Init MySQLPod ...");
    Parameter param;
    param.set<std::string>("host", config.get<std::string>("mysql_host"));
    param.set<int>("port", config.get<int>("mysql_port", 3306));
    param.set<std::string>("user", config.get<std::string>("mysql_user"));
    param.set<std::string>("pwd", config.get<std::string>("mysql_pwd"));
    param.set<std::string>("db", config.get<std::string>("mysql_db", ""));

    if (enable_async) {
        ms_async_db_pool = std::make_unique<ResourceHybridPool<AsyncMySQLConnect>>(
          param, config.get<int>("mysql_async_tls_connect", 2),
          config.get<int>("mysql_async_max_connect", 20));
        CLS_ASSERT(ms_async_db_pool);
    }

    if (enable_sync) {
        ms_db_pool = std::make_unique<ResourcePool<MySQLConnect>>(
          param, config.get<int>("mysql_sync_max_connect", 20),
          config.get<int>("mysql_sync_max_idle_connect", 20));
        CLS_ASSERT(ms_db_pool);
    }
}

void MySQLPod::quit() {
    if (ms_db_pool) {
        CLS_INFO("release mysql pool! ");
        ms_db_pool.reset();
    }
}

}  // namespace pod
}  // namespace hku
