#include "condition_variable.hpp"

#include <chrono>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "types.hpp"

namespace {

using namespace cpool;

awaitable<void> wait(condition_variable& cv, bool& condition,
                     std::atomic<int>& barrier) {

    co_await cv.wait([&]() { return condition; });
    barrier--;
}

awaitable<void> test_notify_all(net::io_context& ctx) {
    auto executor = co_await net::this_coro::executor;
    condition_variable cv(executor);
    bool stop_waiting = false;
    std::atomic<int> barrier = 2;

    int max_waiters = barrier;
    for (int i = 0; i < max_waiters; i++) {
        co_spawn(ctx, wait(cv, stop_waiting, barrier), detached);
    }

    net::steady_timer timer(executor);
    timer.expires_from_now(10ms);
    co_await timer.async_wait(use_awaitable);
    stop_waiting = true;
    cv.notify_all();

    // wait for the co-routines to decrement the barrier
    timer.expires_from_now(10ms);
    co_await timer.async_wait(use_awaitable);
    EXPECT_EQ(barrier, 0);

    ctx.stop();
}

awaitable<void> test_notify_one(net::io_context& ctx) {
    auto executor = co_await net::this_coro::executor;
    condition_variable cv(executor);
    bool stop_waiting = false;
    std::atomic<int> barrier = 2;

    int max_waiters = barrier;
    for (int i = 0; i < max_waiters; i++) {
        co_spawn(ctx, wait(cv, stop_waiting, barrier), detached);
    }

    // wait for them to start
    net::steady_timer timer(executor);
    timer.expires_from_now(10ms);
    co_await timer.async_wait(use_awaitable);

    // notify first
    stop_waiting = true;
    cv.notify_one();
    timer.expires_from_now(10ms);
    co_await timer.async_wait(use_awaitable);
    EXPECT_EQ(barrier, 1);

    // notify second
    cv.notify_one();
    timer.expires_from_now(10ms);
    co_await timer.async_wait(use_awaitable);
    EXPECT_EQ(barrier, 0);

    ctx.stop();
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