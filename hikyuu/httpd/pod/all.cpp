/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-25
 *      Author: fasiondog
 */

#include "all.h"
#include "PodConfig.h"

namespace hku {
namespace pod {

void init(const std::string& filename) {
    auto& config = PodConfig::instance();
    config.loadConfig(filename);
    CommonPod::init();

#if HKU_ENABLE_SQLITE
    SQLitePod::init();
#endif

#if HKU_ENABLE_MYSQL
    MySQLPod::init();
#endif
}

void quit() {
    CommonPod::quit();

#if HKU_ENABLE_SQLITE
    SQLitePod::quit();
#endif

#if HKU_ENABLE_MYSQL
    MySQLPod::quit();
#endif
}

}  // namespace pod
}  // namespace hku