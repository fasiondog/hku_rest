/*
 *  Copyright (c) 2022 hikyuu.org
 *
 *  Created on: 2022-04-09
 *      Author: linjinhai
 */

#include "PodConfig.h"
#include "CommonPod.h"

namespace hku {
namespace pod {

std::unique_ptr<CommonPod::TaskGroup> CommonPod::ms_tg;
std::unique_ptr<TimerManager> CommonPod::ms_scheduler;

CommonPod::snowflake_t CommonPod::ms_msgid_generator;

void CommonPod::init() {
    auto& config = PodConfig::instance();
    int thread_num = config.get<int>("pod_task_thread_num", 0);
    CLS_INFO("pod_task_thread_num: {}", thread_num);
    CLS_WARN_IF(thread_num <= 0, "Common task group is disabled!");
    if (thread_num > 0) {
        ms_tg = std::make_unique<TaskGroup>(thread_num);
        CLS_CHECK(ms_tg, "Failed allocate TaskPod::ms_tg!");
    }

    thread_num = config.get<int>("pod_timer_thread_num", 1);
    CLS_INFO("pod_timer_thread_num: {}", thread_num);
    CLS_CHECK(thread_num > 0, "pod_timer_thread_num must > 0");
    ms_scheduler = std::make_unique<TimerManager>(thread_num);
    CLS_CHECK(ms_scheduler, "Failed allocate TaskPod::ms_scheduler!");
    ms_scheduler->start();

    // 生成 消息 id
    int pod_workerid = config.get<int>("pod_workerid", 1);
    int pod_datacenterid = config.get<int>("pod_datacenterid", 1);
    ms_msgid_generator.init(pod_workerid, pod_datacenterid);
    CLS_INFO("pod_workerid: {}", pod_workerid);
    CLS_INFO("pod_datacenterid: {}", pod_datacenterid);
}

void CommonPod::quit() {
    CLS_INFO("CommonPod quit");
    if (ms_scheduler) {
        ms_scheduler->stop();
        ms_scheduler.reset();
    }

    if (ms_tg) {
        ms_tg->stop();
        ms_tg.reset();
    }
}

}  // namespace pod
}  // namespace hku