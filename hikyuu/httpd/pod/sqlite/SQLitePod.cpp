/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-25
 *      Author: fasiondog
 */

#include "../PodConfig.h"
#include "SQLitePod.h"

namespace hku {
namespace pod {

std::unique_ptr<ResourcePool<SQLiteConnect>> SQLitePod::ms_db_pool;

void SQLitePod::init() {
    auto& config = PodConfig::instance();
    HKU_IF_RETURN(!config.get<bool>("sqlite_enable"), void());

    CLS_INFO("Init SQLitePod ...");
    Parameter param;
    param.set<std::string>("db", config.get<std::string>("sqlite_db"));
    ms_db_pool = std::make_unique<ResourcePool<SQLiteConnect>>(param);
    HKU_CHECK(ms_db_pool, "Failed allocate sqlite connect pool!");

    auto connect = ms_db_pool->get();
    connect->exec(
      "PRAGMA synchronous = OFF;"
      "PRAGMA case_sensitive_like = 1;"  // 影响中文搜索
                                         //"PRAGMA journal_mode=WAL;"
                                         //"PRAGMA wal_autocheckpoint=100;"
    );
}

void SQLitePod::quit() {
    if (ms_db_pool) {
        CLS_INFO("release local db pool!");
        ms_db_pool.reset();
    }
}

}  // namespace pod
}  // namespace hku
