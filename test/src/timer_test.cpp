#include <chrono>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/as_tuple.hpp>

#include "timer.hpp"

using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;
using boost::asio::use_awaitable_t;
using boost::asio::ip::tcp;
using namespace boost::asio::experimental::awaitable_operators;
using boost::asio::experimental::as_tuple;
using boost::asio::experimental::as_tuple_t;

namespace {

namespace asio = boost::asio;
using namespace std::chrono_literals;

awaitable<void> basic_timer_test(asio::io_context& ctx) {
    cpool::detail::timer timer(ctx);
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
    EXPECT_GE((time_stop-time_start), delay);

    // test expires_at semantics
    time_start = std::chrono::steady_clock::now();
    EXPECT_FALSE(timer.pending());
    timer.expires_at(time_start + delay);
    EXPECT_TRUE(timer.pending());
    co_await timer.async_wait();
    EXPECT_FALSE(timer.pending());

    time_stop = std::chrono::steady_clock::now();
    EXPECT_GT(time_stop, time_start);
    EXPECT_GE((time_stop-time_start), delay);
    ctx.stop();
}

awaitable<void> cancel_timer_test(asio::io_context& ctx) {
    cpool::detail::timer timer(ctx);
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
    cpool::detail::timer timer(ctx);
    cpool::detail::timer timer2(ctx);
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
        detached
    );

    co_await timer.async_wait();

    auto time_stop = std::chrono::steady_clock::now();
    EXPECT_GT(time_stop, time_start);
    EXPECT_GE((time_stop-time_start), delay);

    ctx.stop();

    co_return;
}

TEST(Timer, BasicTimerTest)
{
    asio::io_context io_context;

    co_spawn(io_context, basic_timer_test(std::ref(io_context)), detached);

    io_context.run();
}

TEST(Errors, CancelTest)
{
    asio::io_context io_context;

    co_spawn(io_context, cancel_timer_test(std::ref(io_context)), detached);

    io_context.run();
}

TEST(Errors, NoThrowTest)
{
    asio::io_context io_context;

    co_spawn(io_context, nothrow_timer_test(std::ref(io_context)), detached);

    io_context.run();
}

} // namespace