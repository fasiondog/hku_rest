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

std::unique_ptr<ResourcePool<MySQLConnect>> MySQLPod::ms_db_pool;

void MySQLPod::init() {
    auto& config = PodConfig::instance();
    CLS_WARN_IF_RETURN(!config.get<bool>("mysql_enable", false), void(), "mysql is disabled");

    CLS_INFO("Init MySQLPod ...");
    Parameter param;
    param.set<std::string>("host", config.get<std::string>("mysql_host"));
    param.set<int>("port", config.get<int>("mysql_port", 3306));
    param.set<std::string>("user", config.get<std::string>("mysql_user"));
    param.set<std::string>("pwd", config.get<std::string>("mysql_pwd"));
    param.set<std::string>("db", config.get<std::string>("mysql_db", ""));
    ms_db_pool =
      std::make_unique<ResourcePool<MySQLConnect>>(param, config.get<int>("mysql_max_connect", 20),
                                                   config.get<int>("mysql_max_idle_connect", 20));
    CLS_CHECK(ms_db_pool, "Failed allocate mysql connect pool!");
}

void MySQLPod::quit() {
    if (ms_db_pool) {
        CLS_INFO("release mysql pool! ");
        ms_db_pool.reset();
    }
}

}  // namespace pod
}  // namespace hku
