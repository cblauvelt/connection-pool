#include "slow_echo_server.hpp"

#include <iostream>

awaitable<void> slow_echo_once(tcp::socket& socket) {
    std::array<char, 128> data;
    boost::asio::steady_timer timer(socket.get_executor());

    auto [ec, bytesRead] = co_await socket.async_read_some(
        boost::asio::buffer(data), as_tuple(use_awaitable));
    // std::cout << "Bytes Read by slow server: " << bytesRead << std::endl;

    timer.expires_from_now(std::chrono::seconds(1));
    co_await timer.async_wait(as_tuple(use_awaitable));

    co_await async_write(socket, boost::asio::buffer(data, bytesRead),
                         use_awaitable);
}

awaitable<void> slow_echo(tcp::socket socket) {
    try {
        for (;;) {
            // The asynchronous operations to echo a single chunk of data have
            // been refactored into a separate function. When this function is
            // called, the operations are still performed in the context of the
            // current coroutine, and the behaviour is functionally equivalent.
            co_await slow_echo_once(socket);
        }
    } catch (std::exception& e) {
        std::printf("echo Exception: %s\n", e.what());
    }
}

awaitable<void> slow_echo_listener(uint16_t port_num) {
    auto executor = co_await this_coro::executor;
    tcp::acceptor acceptor(executor, {tcp::v4(), port_num});
    for (;;) {
        tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
        co_spawn(executor, slow_echo(std::move(socket)), detached);
    }
}