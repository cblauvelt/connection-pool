#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/as_tuple.hpp>

namespace cpool
{

namespace detail {

namespace asio = boost::asio;

using time_point = typename std::chrono::steady_clock::time_point;
using boost::asio::awaitable;
using boost::asio::use_awaitable;
using boost::asio::experimental::as_tuple;

class timer {

public:
    timer(asio::io_context& context) :
    timer_(context)
    {}

    void expires_at(time_point tp) {
        timer_.expires_at(tp);
        pending_ = true;
    }

    void expires_after(std::chrono::milliseconds ms) {
        timer_.expires_after(ms);
        pending_ = true;
    }

    void expires_never() {
        timer_.cancel();
        pending_ = false;
    }

    std::chrono::steady_clock::time_point expires() const {
        return timer_.expiry();
    }

    awaitable<void> async_wait() {
        co_await timer_.async_wait(use_awaitable);
        pending_ = false;
    }

    bool pending() const {
        return pending_;
    }

    bool expired() const {
        return (pending_ && timer_.expiry() > std::chrono::steady_clock::now());
    }

private:
    asio::steady_timer timer_;
    bool pending_;
};

} // namespace detail

} // namespace cpool
