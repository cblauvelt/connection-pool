// Derivative work of Richard Hodges
// (https://cppalliance.org/richard/2020/12/22/RichardsDecemberUpdate.html)

#pragma once

#include <boost/asio.hpp>
#include <mutex>

#include "cpool/types.hpp"

namespace cpool {

class condition_variable {

  public:
    condition_variable(net::any_io_executor exec)
        : timer_(exec) {
        timer_.expires_at(std::chrono::steady_clock::time_point::max());
    }

    [[nodiscard]] net::awaitable<void> async_wait();

    template <class Pred>
    [[nodiscard]] net::awaitable<void> async_wait(Pred pred);

    void notify_one() { timer_.cancel_one(); }

    void notify_all() { timer_.cancel(); }

  private:
    net::steady_timer timer_;
};

inline net::awaitable<void> condition_variable::async_wait() {
    error_code ec;
    co_await timer_.async_wait(net::redirect_error(net::use_awaitable, ec));
}

template <class Pred>
net::awaitable<void> condition_variable::async_wait(Pred pred) {
    error_code ec;
    while (!pred()) {
        co_await async_wait();
    }
}

} // namespace cpool
