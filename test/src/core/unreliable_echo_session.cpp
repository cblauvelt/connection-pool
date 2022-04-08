#include "unreliable_echo_session.hpp"

unreliable_echo_session::unreliable_echo_session(
    boost::asio::any_io_executor exec, unreliable_echo_server* server,
    tcp::socket socket)
    : exec_(exec)
    , server_(server)
    , socket_(std::move(socket))
    , stop_(false) {}

unreliable_echo_session::~unreliable_echo_session() {
    if (socket_.is_open()) {
        boost::system::error_code err;
        socket_.close(err);
    }
}

awaitable<boost::system::error_code> unreliable_echo_session::echo_once() {
    std::array<char, 128> data;
    auto [ec, bytesRead] = co_await socket_.async_read_some(
        boost::asio::buffer(data), as_tuple(use_awaitable));
    if (ec) {
        co_return ec;
    }

    auto [write_err, bytes_written] = co_await async_write(
        socket_, boost::asio::buffer(data, bytesRead), as_tuple(use_awaitable));

    co_return write_err;
}

awaitable<void> unreliable_echo_session::run() {
    boost::system::error_code err;
    while (!stop_) {
        err = co_await echo_once();
        if (err) {
            break;
        }
    }

    boost::system::error_code ignored_err;
    socket_.shutdown(boost::asio::socket_base::shutdown_both, ignored_err);
    socket_.close(ignored_err);

    server_->notify_stopped(shared_from_this());

    co_return;
}

void unreliable_echo_session::stop() {
    stop_ = true;
    socket_.cancel();
}
