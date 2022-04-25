#pragma once

#include "cpool/types.hpp"

namespace cpool {

class timer {

  public:
    timer(net::any_io_executor exec)
        : timer_(std::move(exec))
        , pending_(false) {}

    net::any_io_executor get_executor() { return timer_.get_executor(); }

    void expires_at(time_point tp) {
        if (tp == time_point::max()) {
            expires_never();
            return;
        }

        timer_.expires_at(tp);
        pending_ = true;
    }

    void expires_after(std::chrono::milliseconds ms) {

        timer_.expires_after(ms);
        pending_ = true;
    }

    void expires_never() {
        timer_.expires_at(time_point::max());
        pending_ = false;
    }

    time_point expires() const { return timer_.expiry(); }

    [[nodiscard]] awaitable<void> async_wait() {
        co_await timer_.async_wait(use_awaitable);
        pending_ = false;
    }

    [[nodiscard]] awaitable<void> async_wait(std::chrono::milliseconds ms) {
        expires_after(ms);
        co_await timer_.async_wait(use_awaitable);
        pending_ = false;
    }

    bool pending() const { return pending_; }

    bool expired() const {
        return (pending_ &&
                timer_.expiry() <= std::chrono::steady_clock::now());
    }

  private:
    net::steady_timer timer_;
    bool pending_;
};

} // namespace cpool
