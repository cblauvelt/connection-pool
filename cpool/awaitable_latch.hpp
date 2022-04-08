#pragma once

#include <atomic>
#include <cstddef>

#include "cpool/condition_variable.hpp"
#include "cpool/types.hpp"

namespace cpool {

class awaitable_latch {
  public:
    awaitable_latch(net::any_io_executor exec, std::ptrdiff_t expected);
    awaitable_latch(const awaitable_latch&) = delete;
    bool operator=(const awaitable_latch&) = delete;

    void count_down(std::ptrdiff_t n = 1);
    std::ptrdiff_t value() const;
    bool try_wait() const noexcept;
    [[nodiscard]] awaitable<void> wait();
    [[nodiscard]] awaitable<void> arrive_and_wait(std::ptrdiff_t n = 1);

  public:
    static constexpr std::ptrdiff_t max() noexcept;

  private:
    cpool::condition_variable cv_;
    std::atomic_ptrdiff_t cnt_;
};

inline awaitable_latch::awaitable_latch(net::any_io_executor exec,
                                        std::ptrdiff_t expected)
    : cv_(exec)
    , cnt_(expected) {}

inline void awaitable_latch::count_down(std::ptrdiff_t n) {
    cnt_ -= n;
    cv_.notify_all();
}

inline std::ptrdiff_t awaitable_latch::value() const { return cnt_.load(); }

inline bool awaitable_latch::try_wait() const noexcept { return cnt_ <= 0; }

inline awaitable<void> awaitable_latch::wait() {
    co_await cv_.async_wait([&]() { return try_wait(); });
}

inline awaitable<void> awaitable_latch::arrive_and_wait(std::ptrdiff_t n) {
    count_down(n);
    co_await wait();
}

constexpr std::ptrdiff_t awaitable_latch::max() noexcept { return PTRDIFF_MAX; }

} // namespace cpool