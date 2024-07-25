/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-25
 *      Author: fasiondog
 */

#pragma once

#include <string>
#include "SQLitePod.h"

namespace hku {
namespace pod {

/**
 * 初始化所有资源
 */
void init(const std::string& filename);

/**
 * 清理所有资源
 */
void quit();

}  // namespace pod
}  // namespace hku