#include <chrono>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "timer.hpp"
#include "types.hpp"

namespace {

namespace asio = boost::asio;
using namespace std::chrono_literals;
using namespace cpool;

awaitable<void> basic_timer_test(asio::io_context& ctx) {
    auto executor = co_await net::this_coro::executor;
    cpool::timer timer(executor);
    auto delay = 100ms;

    // test expires_after semantics
    auto time_start = std::chrono::steady_clock::now();
    EXPECT_FALSE(timer.pending());
    timer.expires_after(delay);
    EXPECT_TRUE(timer.pending());
    EXPECT_LE(time_start + delay, timer.expires());
    co_await timer.async_wait();
    EXPECT_FALSE(timer.pending());

    auto time_stop = std::chrono::steady_clock::now();
    EXPECT_GT(time_stop, time_start);
    EXPECT_GE((time_stop - time_start), delay);

    // test async param semantics
    time_start = std::chrono::steady_clock::now();
    co_await timer.async_wait(delay);
    EXPECT_FALSE(timer.pending());

    time_stop = std::chrono::steady_clock::now();
    EXPECT_GT(time_stop, time_start);
    EXPECT_GE((time_stop - time_start), delay);

    // test expires_at semantics
    time_start = std::chrono::steady_clock::now();
    EXPECT_FALSE(timer.pending());
    timer.expires_at(time_start + delay);
    EXPECT_TRUE(timer.pending());
    co_await timer.async_wait();
    EXPECT_FALSE(timer.pending());

    time_stop = std::chrono::steady_clock::now();
    EXPECT_GT(time_stop, time_start);
    EXPECT_GE((time_stop - time_start), delay);
    ctx.stop();
}

awaitable<void> cancel_timer_test(asio::io_context& ctx) {
    auto executor = co_await net::this_coro::executor;
    cpool::timer timer(executor);
    auto delay = 100ms;

    timer.expires_after(delay);
    timer.expires_never();
    EXPECT_FALSE(timer.pending());

    // does the timer still work?
    timer.expires_after(delay);
    EXPECT_TRUE(timer.pending());
    co_await timer.async_wait();
    EXPECT_FALSE(timer.pending());

    ctx.stop();

    co_return;
}

awaitable<void> nothrow_timer_test(asio::io_context& ctx) {
    auto executor = co_await net::this_coro::executor;
    cpool::timer timer(executor);
    cpool::timer timer2(executor);
    auto delay = 100ms;
    auto delay2 = 50ms;

    auto time_start = std::chrono::steady_clock::now();
    timer.expires_after(delay);
    timer2.expires_after(delay2);

    co_spawn(
        ctx,
        [&]() -> awaitable<void> {
            co_await timer2.async_wait();
            timer.expires_never();
        },
        detached);

    co_await timer.async_wait();

    auto time_stop = std::chrono::steady_clock::now();
    EXPECT_GT(time_stop, time_start);
    EXPECT_GE((time_stop - time_start), delay);

    ctx.stop();

    co_return;
}

TEST(Timer, BasicTimerTest) {
    asio::io_context io_context;

    co_spawn(io_context, basic_timer_test(std::ref(io_context)), detached);

    io_context.run();
}

TEST(Errors, CancelTest) {
    asio::io_context io_context;

    co_spawn(io_context, cancel_timer_test(std::ref(io_context)), detached);

    io_context.run();
}

TEST(Errors, NoThrowTest) {
    asio::io_context io_context;

    co_spawn(io_context, nothrow_timer_test(std::ref(io_context)), detached);

    io_context.run();
}

} // namespace