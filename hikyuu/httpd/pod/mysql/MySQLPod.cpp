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

void MySQLPod::init() {}

void MySQLPod::quit() {
    if (ms_db_pool) {
        CLS_INFO("release mysql pool! ");
        ms_db_pool.reset();
    }
}

}  // namespace pod
}  // namespace hku
