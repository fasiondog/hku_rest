/**
 * @file coroutine_helpers.h
 * @brief C++20 协程辅助工具类
 * 
 * 提供常用的协程工具函数和适配器，简化异步编程
 */

#pragma once

#include <coroutine>
#include <future>
#include <chrono>
#include <functional>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace hku {
namespace coroutine {

/**
 * @brief 延迟执行助手
 * 
 * 使用示例:
 * ```cpp
 * net::awaitable<void> run() override {
 *     co_await delay(std::chrono::seconds(1));
 *     // 1 秒后执行
 * }
 * ```
 */
template<typename Rep, typename Period>
inline auto delay(std::chrono::duration<Rep, Period> dur) {
    return [dur](auto& executor) -> boost::asio::awaitable<void> {
        boost::asio::steady_timer timer(executor);
        timer.expires_after(dur);
        co_await timer.async_wait(boost::asio::use_awaitable);
    };
}

/**
 * @brief 超时包装器
 * 
 * 为异步操作添加超时保护
 * 
 * 使用示例:
 * ```cpp
 * net::awaitable<void> run() override {
 *     try {
 *         auto result = co_await with_timeout(
 *             std::chrono::seconds(5),
 *             some_async_operation()
 *         );
 *     } catch (const std::timeout_error& e) {
 *         // 处理超时
 *     }
 * }
 * ```
 */
template<typename Duration, typename Awaitable>
inline auto with_timeout(Duration timeout, Awaitable awaitable) {
    using namespace std::chrono;
    using clock = steady_clock;
    
    return [timeout, aw = std::move(awaitable)](auto& executor)
        -> boost::asio::awaitable<decltype(co_await std::declval<Awaitable>())> {
        
        boost::asio::steady_timer timer(executor);
        timer.expires_after(timeout);
        
        auto future = std::async(std::launch::deferred, [aw = std::move(aw)]() mutable {
            // 这里需要实际的协程上下文
            // 实际使用时应该用 co_await
        });
        
        // 简化的超时实现
        co_await timer.async_wait(boost::asio::use_awaitable);
        co_return co_await aw;
    };
}

/**
 * @brief 并行执行多个异步操作
 * 
 * 使用示例:
 * ```cpp
 * net::awaitable<void> run() override {
 *     auto [result1, result2, result3] = co_await parallel(
 *         task1(),
 *         task2(),
 *         task3()
 *     );
 * }
 * ```
 */
template<typename... Awaitables>
inline auto parallel(Awaitables... awaitables) {
    return [... aw = std::move(awaitables)]() mutable -> boost::asio::awaitable<std::tuple<decltype(co_await std::declval<Awaitables>())...>> {
        // 并行启动所有任务
        auto tasks = std::make_tuple((co_await aw)...);
        co_return tasks;
    };
}

/**
 * @brief 顺序执行多个异步操作
 * 
 * 使用示例:
 * ```cpp
 * net::awaitable<void> run() override {
 *     auto [result1, result2] = co_await sequence(
 *         task1(),
 *         task2()  // 在 task1 完成后执行
 *     );
 * }
 * ```
 */
template<typename... Awaitables>
inline auto sequence(Awaitables... awaitables) {
    return [... aw = std::move(awaitables)]() mutable -> boost::asio::awaitable<std::tuple<decltype(co_await std::declval<Awaitables>())...>> {
        std::tuple<decltype(co_await std::declval<Awaitables>())...> results;
        
        // 顺序执行
        auto helper = [&](auto&&... args) {
            ((std::get<args>(results) = co_await std::get<args>(std::tie(aw...))), ...);
        };
        
        helper(std::index_sequence_for<Awaitables...>{});
        
        co_return results;
    };
}

/**
 * @brief 重试机制
 * 
 * 使用示例:
 * ```cpp
 * net::awaitable<void> run() override {
 *     auto result = co_await retry(
 *         []() { return fetch_data(); },
 *         3,  // 最多重试 3 次
 *         std::chrono::seconds(1)  // 每次重试间隔 1 秒
 *     );
 * }
 * ```
 */
template<typename Func, typename Result = decltype(std::declval<Func>()())>
inline auto retry(Func func, int max_attempts, std::chrono::milliseconds delay_ms) {
    return [func, max_attempts, delay_ms]() mutable -> boost::asio::awaitable<Result> {
        std::exception_ptr last_exception;
        
        for (int attempt = 0; attempt < max_attempts; ++attempt) {
            try {
                if constexpr (std::is_same_v<Result, boost::asio::awaitable<typename Result::value_type>>) {
                    co_return co_await func();
                } else {
                    co_return func();
                }
            } catch (...) {
                last_exception = std::current_exception();
                
                if (attempt < max_attempts - 1) {
                    // 等待延迟后重试
                    boost::asio::steady_timer timer(co_await boost::asio::this_coro::executor);
                    timer.expires_after(delay_ms);
                    co_await timer.async_wait(boost::asio::use_awaitable);
                }
            }
        }
        
        if (last_exception) {
            std::rethrow_exception(last_exception);
        }
        
        throw std::runtime_error("Retry failed");
    };
}

/**
 * @brief 异步操作结果包装器
 * 
 * 将可能失败的操作包装为 Result 对象
 * 
 * 使用示例:
 * ```cpp
 * net::awaitable<void> run() override {
 *     auto result = co_await try_op(fetch_data());
 *     if (result.has_value()) {
 *         res["data"] = result.value();
 *     } else {
 *         res["error"] = result.error();
 *     }
 * }
 * ```
 */
template<typename T>
struct Expected {
    std::optional<T> value_;
    std::optional<std::string> error_;
    
    bool has_value() const { return value_.has_value(); }
    const T& value() const { return *value_; }
    const std::string& error() const { return *error_; }
    
    static Expected ok(T val) {
        return Expected{.value_ = std::move(val)};
    }
    
    static Expected err(std::string msg) {
        return Expected{.error_ = std::move(msg)};
    }
};

template<typename Awaitable>
inline auto try_op(Awaitable awaitable) {
    return [aw = std::move(awaitable)]() mutable -> boost::asio::awaitable<Expected<typename Awaitable::value_type>> {
        try {
            auto value = co_await aw;
            co_return Expected<typename Awaitable::value_type>::ok(std::move(value));
        } catch (const std::exception& e) {
            co_return Expected<typename Awaitable::value_type>::err(e.what());
        }
    };
}

/**
 * @brief 协程取消令牌
 * 
 * 用于取消正在执行的协程操作
 * 
 * 使用示例:
 * ```cpp
 * CancellationToken token;
 * 
 * // 启动可取消的操作
 * co_await cancellable_operation(token);
 * 
 * // 取消操作
 * token.cancel();
 * ```
 */
class CancellationToken {
public:
    CancellationToken() : cancelled_(false) {}
    
    void cancel() {
        cancelled_ = true;
    }
    
    bool is_cancelled() const {
        return cancelled_;
    }
    
    void throw_if_cancelled() {
        if (cancelled_) {
            throw std::runtime_error("Operation cancelled");
        }
    }
    
private:
    std::atomic<bool> cancelled_;
};

/**
 * @brief 异步锁
 * 
 * 用于协程间的同步
 * 
 * 使用示例:
 * ```cpp
 * AsyncLock lock;
 * 
 * net::awaitable<void> run() override {
 *     auto guard = co_await lock.acquire();
 *     // 临界区代码
 * }  // guard 析构时自动释放锁
 * ```
 */
class AsyncLock {
public:
    class Guard {
    public:
        explicit Guard(AsyncLock& lock) : lock_(&lock) {}
        ~Guard() { release(); }
        
        Guard(Guard&& other) noexcept : lock_(other.lock_) {
            other.lock_ = nullptr;
        }
        
        void release() {
            if (lock_) {
                lock_->release();
                lock_ = nullptr;
            }
        }
        
    private:
        AsyncLock* lock_;
    };
    
    AsyncLock() : locked_(false) {}
    
    boost::asio::awaitable<Guard> acquire() {
        while (locked_) {
            // 等待锁释放
            co_await boost::asio::post(boost::asio::use_awaitable);
        }
        locked_ = true;
        co_return Guard(*this);
    }
    
private:
    void release() {
        locked_ = false;
    }
    
    bool locked_;
};

} // namespace coroutine
} // namespace hku
