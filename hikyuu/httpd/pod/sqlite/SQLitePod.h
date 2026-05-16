/*
 *  Copyright (c) 2024 hikyuu.org
 *
 *  Created on: 2024-07-25
 *      Author: fasiondog
 */

#pragma once

#include "hikyuu/utilities/db_connect/DBConnect.h"
#include "hikyuu/utilities/ResourcePool.h"
#include "hikyuu/utilities/ResourceHybridPool.h"
#include "hikyuu/utilities/Log.h"

#ifndef HKU_HTTPD_API
#define HKU_HTTPD_API
#endif

namespace hku {
namespace pod {

class HKU_HTTPD_API SQLitePod {
    CLASS_LOGGER_IMP(SQLitePod)

public:
    SQLitePod() = delete;

    static void init();
    static void quit();

    /**
     * 获取异步数据库连接
     *
     * @param timeout 获取连接的超时时间，默认为5秒
     * @return 异步MySQL连接智能指针的协程，如果超时则返回错误信息
     */
    static asio::awaitable<AsyncDBConnectPtr> getAsyncConnect(
      std::chrono::steady_clock::duration timeout = std::chrono::seconds(5)) {
        HKU_ASSERT(ms_async_db_pool);
        auto ret = co_await ms_async_db_pool->asyncGet(timeout);
        if (!ret) {
            HKU_THROW("Failed get connect! {}", ret.error());
        }
        co_return ret.value();
    }

    /** 获取本地数据库空闲连接数量 */
    static DBConnectPtr getConnect() {
        HKU_ASSERT(ms_db_pool);
        return ms_db_pool->getAndWait();
    }

    static std::unique_ptr<ResourceHybridPool<AsyncSQLiteConnect>>& getAsyncDBPool() noexcept {
        return ms_async_db_pool;
    }

    static std::unique_ptr<ResourcePool<SQLiteConnect>>& getDBPool() noexcept {
        return ms_db_pool;
    }

private:
    static std::unique_ptr<ResourceHybridPool<AsyncSQLiteConnect>>
      ms_async_db_pool;                                              // 异步任务数据库
    static std::unique_ptr<ResourcePool<SQLiteConnect>> ms_db_pool;  // 本地任务数据库
};

}  // namespace pod
}  // namespace hku
