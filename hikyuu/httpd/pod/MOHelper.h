/*
 *  Copyright(C) 2021 hikyuu.org
 *
 *  Create on: 2021-05-01
 *     Author: fasiondog
 */

#pragma once

#include <unordered_map>
#include "hikyuu/utilities/string_view.h"
#include "hikyuu/utilities/moFileReader.h"

#if defined(_MSC_VER)
// moFileReader.hpp 最后打开了4251告警，这里关闭
#pragma warning(disable : 4251)
#endif /* _MSC_VER */

#ifndef HKU_HTTPD_API
#define HKU_HTTPD_API
#endif

namespace hku {
namespace pod {

class HKU_HTTPD_API MOHelper {
public:
    /**
     * 加载多语言文件
     * @param textdomain 多语言文件名，通常为程序名
     * @param lang 语言
     * @param path 多语言文件路径
     */
    static void loadLanguage(const std::string &lang, const std::string &filename);

    static std::string translate(const std::string &lang, const char *id);

    static std::string translate(const std::string &lang, const char *ctx, const char *id);

private:
    static std::unordered_map<std::string, moFileLib::moFileReader> ms_mo;  // lang->moFileReader
};

}  // namespace pod
}  // namespace hku