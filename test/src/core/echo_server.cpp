#include "echo_server.hpp"

#include <iostream>

awaitable<boost::system::error_code> echo_once(tcp::socket& socket) {
    std::array<char, 128> data;
    auto [ec, bytesRead] = co_await socket.async_read_some(
        boost::asio::buffer(data), as_tuple(use_awaitable));
    if (ec) {
        co_return ec;
    }

    auto [write_err, bytes_written] = co_await async_write(
        socket, boost::asio::buffer(data, bytesRead), as_tuple(use_awaitable));

    co_return write_err;
}

awaitable<void> echo(tcp::socket socket) {
    boost::system::error_code err;
    for (;;) {
        err = co_await echo_once(socket);
        if (err) {
            break;
        }
    }

    co_return;
}

awaitable<void> echo_listener(uint16_t port_num) {
    auto executor = co_await this_coro::executor;
    tcp::acceptor acceptor(executor, {tcp::v4(), port_num});
    for (;;) {
        tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
        co_spawn(executor, echo(std::move(socket)), detached);
    }
}
