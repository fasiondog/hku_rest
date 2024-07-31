/*
 *  Copyright (c) 2022 hikyuu.org
 *
 *  Created on: 2022-04-09
 *      Author: fasiondog
 */

#pragma once

#include <memory>
#include <hikyuu/utilities/thread/thread.h>
#include <hikyuu/utilities/TimerManager.h>
#include <hikyuu/utilities/snowflake.h>
#include <hikyuu/utilities/Log.h>

namespace hku {
namespace pod {

class HKU_HTTPD_API CommonPod {
    CLASS_LOGGER_IMP(CommonPod)

public:
    CommonPod() = delete;
    static void init();
    static void quit();

    using TaskGroup = ThreadPool;

    static TaskGroup *getTaskGroup() {
        CLS_CHECK(ms_tg, "Not enbale common task group!");
        return ms_tg.get();
    }

    static TimerManager *getScheduler() {
        return ms_scheduler.get();
    }

    static int64_t nextid() {
        return ms_msgid_generator.nextid();
    }

private:
    static std::unique_ptr<TaskGroup> ms_tg;            // 公共任务线程池
    static std::unique_ptr<TimerManager> ms_scheduler;  // 公共定时调度器

    using snowflake_t = snowflake<1288834974657L, std::mutex>;
    static snowflake_t ms_msgid_generator;  // 用于生成kafak消息id
};

}  // namespace pod
}  // namespace hku