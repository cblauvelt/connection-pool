#include "echo_server.hpp"

#include <iostream>

awaitable<boost::system::error_code> echo_once(tcp::socket& socket) {
    std::array<char, 128> data;
    auto [ec, bytesRead] = co_await socket.async_read_some(boost::asio::buffer(data), as_tuple(use_awaitable));
    if(ec) {
        co_return ec;
    }

    co_await async_write(socket, boost::asio::buffer(data, bytesRead), use_awaitable);
}

awaitable<void> echo(tcp::socket socket) {
    boost::system::error_code err;
    for (;;) {
        err = co_await echo_once(socket);
        if(err) { break; }
    }

    std::cout << "server error: " << err << std::endl;
    if(socket.is_open()) {
        socket.shutdown(tcp::socket::shutdown_both, err);
        if(err) { co_return; }
        
        socket.close(err);
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