/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-25
 *      Author: fasiondog
 */

#include "Config.h"

namespace hku {
namespace pod {

void Config::loadConfig(const std::string& filename) {
    std::lock_guard<std::mutex> lock(m_mutex);
    _loadFromIniFile(filename);
}

void Config::_loadFromIniFile(const std::string& filename) {  // NOSONAR
    HKU_INFO("loading pod configure from ini file: {}", filename);

    IniParser ini;
    ini.read(filename);

    // 读取当前部署环境
    std::string deploy = ini.get("deploy", "current");
    HKU_INFO("current deploy environment: {}", deploy);
    _loadFromIni(ini, deploy);
}

void Config::_loadFromIni(const IniParser& ini, const std::string& deploy) {
    // 读取当前部署环境
    auto options = ini.getOptionList(deploy);
    for (auto iter = options->begin(), end = options->end(); iter != end; ++iter) {
        m_params[*iter] = ini.get(deploy, *iter);
    }
}

}  // namespace pod
}  // namespace hku