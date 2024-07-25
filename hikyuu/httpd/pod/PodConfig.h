/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-25
 *      Author: fasiondog
 */

#pragma once

#include "Config.h"

namespace hku {
namespace pod {

class HKU_HTTPD_API PodConfig {
public:
    PodConfig(const PodConfig&) = delete;
    PodConfig& operator=(const PodConfig&) = delete;

    static Config& instance();

    void loadConfig(const std::string& filename);

private:
    PodConfig() = default;
};

}  // namespace pod
}  // namespace hku