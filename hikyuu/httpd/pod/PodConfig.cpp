/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-25
 *      Author: fasiondog
 */

#include "PodConfig.h"

namespace hku {
namespace pod {

pod::Config& PodConfig::instance() {
    static Config g_config;
    return g_config;
}

void PodConfig::loadConfig(const std::string& filename) {
    auto& config = instance();
    config.loadConfig(filename);
}

}  // namespace pod
}  // namespace hku