#include "cpool/condition_variable.hpp"

#include <chrono>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cpool/types.hpp"

namespace {

using namespace cpool;

awaitable<void> wait_for(condition_variable& cv, bool& condition,
                         std::atomic<int>& barrier) {
    co_await cv.async_wait([&]() { return condition; });
    barrier--;
}

awaitable<void> test_notify_all(net::io_context& ctx) {
    try {
        auto executor = co_await net::this_coro::executor;
        condition_variable cv(executor);
        bool stop_waiting = false;
        std::atomic<int> barrier = 2;

        int max_waiters = barrier;
        for (int i = 0; i < max_waiters; i++) {
            co_spawn(ctx, wait_for(cv, stop_waiting, barrier), detached);
        }
        stop_waiting = true;
        cv.notify_all();

        net::steady_timer timer(executor);
        timer.expires_from_now(10ms);
        co_await timer.async_wait(use_awaitable);
        EXPECT_EQ(barrier, 0);

        ctx.stop();
    } catch (...) {
        EXPECT_FALSE(true);
    }
}

awaitable<void> test_notify_one(net::io_context& ctx) {
    try {
        auto executor = co_await net::this_coro::executor;
        condition_variable cv(executor);
        bool stop_waiting = false;
        std::atomic<int> barrier = 2;

        int max_waiters = barrier;
        for (int i = 0; i < max_waiters; i++) {
            co_spawn(ctx, wait_for(cv, stop_waiting, barrier), detached);
        }
        stop_waiting = true;
        cv.notify_one();

        net::steady_timer timer(executor);
        timer.expires_from_now(10ms);
        co_await timer.async_wait(use_awaitable);
        EXPECT_EQ(barrier, 1);

        ctx.stop();
    } catch (...) {
        EXPECT_FALSE(true);
    }
}

TEST(ConditionVariable, NotifyAll) {
    net::io_context ctx(1);

    co_spawn(ctx, test_notify_all(ctx), detached);

    ctx.run();
}

TEST(ConditionVariable, NotifyOne) {
    net::io_context ctx(1);

    co_spawn(ctx, test_notify_one(ctx), detached);

    ctx.run();
}

} // namespace