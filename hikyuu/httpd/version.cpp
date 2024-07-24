/*
 *  Copyright (c) 2019~2021 hikyuu.org
 *
 *  Created on: 2021/12/13
 *      Author: fasiondog
 */

#include <fmt/format.h>
#include "hikyuu/utilities/osdef.h"
#include "version.h"

namespace hku {
namespace utils {

std::string HKU_HTTPD_API getVersion() {
    return HKU_HTTPD_VERSION;
}

std::string HKU_HTTPD_API getVersionWithBuild() {
#if HKU_ARCH_ARM
    return fmt::format("{}_{}_arm_{}", HKU_HTTPD_VERSION, HKU_HTTPD_VERSION_BUILD,
                       HKU_COMPILE_MODE);
#elif HKU_ARCH_ARM64
    return fmt::format("{}_{}_aarch64_{}", HKU_HTTPD_VERSION, HKU_HTTPD_VERSION_BUILD,
                       HKU_COMPILE_MODE);
#elif HKU_ARCH_X64
    return fmt::format("{}_{}_x86_64_{}", HKU_HTTPD_VERSION, HKU_HTTPD_VERSION_BUILD,
                       HKU_COMPILE_MODE);
#elif HKU_ARCH_X86
    return fmt::format("{}_{}_i386_{}", HKU_HTTPD_VERSION, HKU_HTTPD_VERSION_BUILD,
                       HKU_COMPILE_MODE);
#else
    return fmt::format("{}_{}_i386_Unknow_arch_{}", HKU_HTTPD_VERSION, HKU_HTTPD_VERSION_BUILD,
                       HKU_COMPILE_MODE);
#endif
}

}  // namespace utils
}  // namespace hku