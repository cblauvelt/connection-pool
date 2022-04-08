#include <chrono>
#include <fmt/format.h>
#include <random>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "core/thread_pool.hpp"
#include "cpool/awaitable_latch.hpp"
#include "cpool/timer.hpp"
#include "cpool/types.hpp"

namespace {

using namespace cpool;

awaitable<void> run_job(awaitable_latch& latch, uint job_num) {
    // Will be used to obtain a seed for the random number engine
    std::random_device rd;
    // Standard mersenne_twister_engine seeded with rd()
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(10, 50);
    std::chrono::milliseconds wait_time(distrib(gen));

    auto timer = cpool::timer(co_await net::this_coro::executor);
    CPOOL_TRACE_LOG("LATCH_TEST", "Job {} waiting {}ms", job_num,
                    wait_time.count());
    co_await timer.async_wait(wait_time);

    latch.count_down();
    CPOOL_TRACE_LOG("LATCH_TEST", "Job {} finished, jobs remaining {}", job_num,
                    latch.value());
}

awaitable<void> test_latch(net::io_context& ctx, uint num_jobs) {
    auto exec = co_await net::this_coro::executor;
    const std::ptrdiff_t firstDelta = 2;
    const std::ptrdiff_t defaultDelta = 1;
    awaitable_latch latch(exec, num_jobs + firstDelta + defaultDelta);

    latch.count_down(firstDelta);
    EXPECT_EQ(latch.value(), defaultDelta + (std::ptrdiff_t)num_jobs);
    latch.count_down();
    EXPECT_EQ(latch.value(), (std::ptrdiff_t)num_jobs);

    EXPECT_FALSE(latch.try_wait());

    for (uint i = 0; i < num_jobs - 1; i++) {
        co_spawn(exec, run_job(std::ref(latch), i), detached);
    }

    co_await latch.arrive_and_wait();

    ctx.stop();
}

TEST(LATCH, LatchTest) {
    uint num_jobs = 80;
    net::io_context ctx(1);
    std::vector<std::thread> threads;

    co_spawn(ctx, test_latch(std::ref(ctx), num_jobs), detached);

    ctx.run();
}

} // namespace