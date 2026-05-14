/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-25
 *      Author: fasiondog
 */

#include <hikyuu/utilities/thread/algorithm.h>
#include "../PodConfig.h"
#include "../CommonPod.h"
#include "SQLitePod.h"

namespace hku {
namespace pod {

std::unique_ptr<ResourceHybridPool<AsyncSQLiteConnect>> SQLitePod::ms_async_db_pool;
std::unique_ptr<ResourcePool<SQLiteConnect>> SQLitePod::ms_db_pool;

void SQLitePod::init() {
    auto& config = PodConfig::instance();
    bool enable_async = config.get<bool>("sqlite_async_enable", false);
    bool enable_sync = config.get<bool>("sqlite_sync_enable", false);

    CLS_INFO("sqlite_async_enable: {}", enable_async);
    CLS_INFO("sqlite_sync_enable: {}", enable_sync);
    CLS_WARN_IF_RETURN(!enable_async && !enable_sync, void(), "sqlite is disabled");

    CLS_INFO("Init SQLitePod ...");
    Parameter param;
    param.set<std::string>("db", config.get<std::string>("sqlite_db"));

    if (enable_async) {
        ms_async_db_pool = std::make_unique<ResourceHybridPool<AsyncSQLiteConnect>>(param);
        CLS_ASSERT(ms_async_db_pool);

        // 创建临时 io_context 用于异步初始化 SQLite PRAGMA 设置
        asio::io_context init_ctx;
        asio::co_spawn(
          init_ctx,
          []() -> asio::awaitable<void> {
              try {
                  auto connect_result = co_await ms_async_db_pool->asyncGet();
                  if (connect_result) {
                      // exec 返回 int64_t，成功时直接执行
                      co_await connect_result.value()->exec(
                        "PRAGMA synchronous = OFF;"
                        "PRAGMA case_sensitive_like = 1;"  // 影响中文搜索
                      );
                  } else {
                      CLS_ERROR("Failed to get async connection: {}", connect_result.error());
                  }
              } catch (const std::exception& e) {
                  CLS_ERROR("Init SQLitePod async exception: {}", e.what());
              }
          },
          asio::detached);

        // 运行 io_context 让协程执行完成
        init_ctx.run();
    }

    if (enable_sync) {
        ms_db_pool = std::make_unique<ResourcePool<SQLiteConnect>>(param);
        CLS_ASSERT(ms_db_pool);
        if (!enable_async) {
            auto connect = ms_db_pool->get();
            connect->exec(
              "PRAGMA synchronous = OFF;"
              "PRAGMA case_sensitive_like = 1;"  // 影响中文搜索
                                                 //"PRAGMA journal_mode=WAL;"
                                                 //"PRAGMA wal_autocheckpoint=100;"
            );
        }
    }
}

void SQLitePod::quit() {
    if (ms_db_pool) {
        CLS_INFO("release local db pool!");
        ms_db_pool.reset();
    }
}

}  // namespace pod
}  // namespace hku
