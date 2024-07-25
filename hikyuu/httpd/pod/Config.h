/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-25
 *      Author: fasiondog
 */

#pragma once

#include <unordered_map>
#include <hikyuu/utilities/ini_parser/IniParser.h>
#include <hikyuu/utilities/arithmetic.h>
#include <hikyuu/utilities/Log.h>

namespace hku {
namespace pod {

class Config {
public:
    void loadConfig(const std::string& filename);

    template <class T>
    T get(const std::string& key) const {
        auto iter = m_params.find(key);
        if (iter == m_params.end()) {
            HKU_THROW_EXCEPTION(std::out_of_range, "Not found key: {}", key);
        }
        return iter->second;
    }

    template <class T>
    T get(const std::string& key, const T& default_val) const {
        auto iter = m_params.find(key);
        return iter != m_params.end() ? iter->second : default_val;
    }

private:
    void _loadFromIni(const IniParser& ini, const std::string& deploy);
    void _loadFromIniFile(const std::string& filename);

private:
    std::unordered_map<std::string, std::string> m_params;
};

template <>
inline int Config::get(const std::string& key) const {
    auto iter = m_params.find(key);
    if (iter == m_params.end()) {
        HKU_THROW_EXCEPTION(std::out_of_range, "Not found key: {}", key);
    }
    return std::stoi(iter->second);
}

template <>
inline int64_t Config::get(const std::string& key) const {
    auto iter = m_params.find(key);
    if (iter == m_params.end()) {
        HKU_THROW_EXCEPTION(std::out_of_range, "Not found key: {}", key);
    }
    return std::stoll(iter->second);
}

template <>
inline double Config::get(const std::string& key) const {
    auto iter = m_params.find(key);
    if (iter == m_params.end()) {
        HKU_THROW_EXCEPTION(std::out_of_range, "Not found key: {}", key);
    }
    return std::stod(iter->second);
}

template <>
inline float Config::get(const std::string& key) const {
    auto iter = m_params.find(key);
    if (iter == m_params.end()) {
        HKU_THROW_EXCEPTION(std::out_of_range, "Not found key: {}", key);
    }
    return std::stof(iter->second);
}

template <>
inline bool Config::get(const std::string& key) const {
    auto iter = m_params.find(key);
    if (iter == m_params.end()) {
        HKU_THROW_EXCEPTION(std::out_of_range, "Not found key: {}", key);
    }
    bool result = false;
    std::string val = iter->second;
    to_upper(val);
    if (val == "1" || val == "TRUE" || val == "YES") {
        result = true;
    } else if (val == "0" || val == "FALSE" || val == "NO") {
        result = false;
    } else {
        HKU_THROW_EXCEPTION(std::invalid_argument, "Could't convert to bool, key: {}, val: {}", key,
                            val);
    }
    return result;
}

template <>
inline int Config::get(const std::string& key, const int& default_val) const {
    auto iter = m_params.find(key);
    if (iter != m_params.end()) {
        return std::stoi(iter->second);
    }
    return default_val;
}

template <>
inline int64_t Config::get(const std::string& key, const int64_t& default_val) const {
    auto iter = m_params.find(key);
    if (iter != m_params.end()) {
        return std::stoll(iter->second);
    }
    return default_val;
}

template <>
inline double Config::get(const std::string& key, const double& default_val) const {
    auto iter = m_params.find(key);
    if (iter != m_params.end()) {
        return std::stod(iter->second);
    }
    return default_val;
}

template <>
inline float Config::get(const std::string& key, const float& default_val) const {
    auto iter = m_params.find(key);
    if (iter != m_params.end()) {
        return std::stof(iter->second);
    }
    return default_val;
}

template <>
inline bool Config::get(const std::string& key, const bool& default_val) const {
    auto iter = m_params.find(key);
    if (iter != m_params.end()) {
        bool result = false;
        std::string val = iter->second;
        to_upper(val);
        if (val == "1" || val == "TRUE" || val == "YES") {
            result = true;
        } else if (val == "0" || val == "FALSE" || val == "NO") {
            result = false;
        } else {
            HKU_THROW_EXCEPTION(std::invalid_argument, "Could't convert to bool, key: {}, val: {}",
                                key, val);
        }
        return result;
    }
    return default_val;
}

}  // namespace pod
}  // namespace hku
