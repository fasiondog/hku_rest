/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-25
 *      Author: fasiondog
 */

#include "all.h"
#include "PodConfig.h"
#include "SQLitePod.h"

namespace hku {
namespace pod {

void init(const std::string& filename) {
    auto& config = PodConfig::instance();
    config.loadConfig(filename);
    // CommonPod::init();

#if HKU_ENABLE_SQLITE
    SQLitePod::init();
#endif
}

void quit() {
    HKU_INFO("Release all Pods.");

#if HKU_ENABLE_SQLITE
    SQLitePod::quit();
#endif
}

}  // namespace pod
}  // namespace hku