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

void init() {
    CommonPod::init();

#if HKU_HTTPD_POD_USE_SQLITE
    SQLitePod::init();
#endif

#if HKU_HTTPD_POD_USE_MYSQL
    MySQLPod::init();
#endif
}

void init(const std::string& filename) {
    auto& config = PodConfig::instance();
    config.loadConfig(filename);
    init();
}

void quit() {
    CommonPod::quit();

#if HKU_HTTPD_POD_USE_SQLITE
    SQLitePod::quit();
#endif

#if HKU_HTTPD_POD_USE_MYSQL
    MySQLPod::quit();
#endif
}

}  // namespace pod
}  // namespace hku