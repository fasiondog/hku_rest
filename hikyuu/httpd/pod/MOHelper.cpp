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

std::unordered_map<std::string, std::unordered_map<std::string, moFileLib::moFileReader>>
  MOHelper::ms_mo;

void MOHelper::loadLanguage(const std::string &textdomain, const std::string &lang,
                            const std::string &path) {
    std::string filename = fmt::format("{}/{}/{}.mo", path, lang, textdomain);
    HKU_WARN_IF_RETURN(!existFile(filename), void(),
                       "There is no internationalized language file: {}", filename);
    ms_mo[textdomain][lang] = moFileLib::moFileReader();
    ms_mo[textdomain][lang].ReadFile(filename.c_str());
}

std::string MOHelper::translate(const std::string &textdomain, const std::string &lang,
                                const char *id) {
    std::string ret(id);
    auto domain_iter = ms_mo.find(textdomain);
    HKU_IF_RETURN(domain_iter == ms_mo.end(), ret);

    auto lang_iter = domain_iter->second.find(lang);
    HKU_IF_RETURN(lang_iter == domain_iter->second.end(), ret);
    ret = lang_iter->second.Lookup(id);
    return ret;
}

std::string MOHelper::translate(const std::string &textdomain, const std::string &lang,
                                const char *ctx, const char *id) {
    std::string ret(id);
    auto domain_iter = ms_mo.find(textdomain);
    HKU_IF_RETURN(domain_iter == ms_mo.end(), ret);
    auto lang_iter = domain_iter->second.find(lang);
    HKU_IF_RETURN(lang_iter == domain_iter->second.end(), ret);
    ret = lang_iter->second.LookupWithContext(ctx, id);
    return ret;
}

}  // namespace pod
}  // namespace hku