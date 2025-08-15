/*
 *  Copyright(C) 2021 hikyuu.org
 *
 *  Create on: 2021-05-02
 *     Author: fasiondog
 */

#include "hikyuu/utilities/os.h"
#include "hikyuu/utilities/Log.h"
#include "MOHelper.h"

namespace hku {
namespace pod {
std::unordered_map<std::string, moFileLib::moFileReader> MOHelper::ms_mo;

void MOHelper::loadLanguage(const std::string &lang, const std::string &filename) {
    HKU_WARN_IF_RETURN(!existFile(filename), void(),
                       "There is no internationalized language file: {}", filename);
    auto [iter, success] = ms_mo.insert(std::make_pair(lang, moFileLib::moFileReader()));
    HKU_ERROR_IF_RETURN(!success, void(), "Failed load language file {}!", filename);
    iter->second.ReadFile(filename.c_str());
}

std::string MOHelper::translate(const std::string &lang, const char *id) {
    std::string ret(id);
    auto lang_iter = ms_mo.find(lang);
    HKU_IF_RETURN(lang_iter == ms_mo.end(), ret);
    ret = lang_iter->second.Lookup(id);
    return ret;
}

std::string MOHelper::translate(const std::string &lang, const char *ctx, const char *id) {
    std::string ret(id);
    auto lang_iter = ms_mo.find(lang);
    HKU_IF_RETURN(lang_iter == ms_mo.end(), ret);
    ret = lang_iter->second.LookupWithContext(ctx, id);
    return ret;
}

}  // namespace pod
}  // namespace hku