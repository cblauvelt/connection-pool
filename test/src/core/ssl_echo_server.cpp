#include "ssl_echo_server.hpp"

#include <iostream>

using boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;
using ssl_socket = boost::asio::ssl::stream<tcp::socket>;

awaitable<boost::system::error_code> ssl_echo_once(ssl_socket& socket) {
    std::array<char, 128> data;
    auto [ec, bytes] = co_await socket.async_read_some(
        boost::asio::buffer(data), as_tuple(use_awaitable));
    if (ec) {
        co_return ec;
    }

    std::tie(ec, bytes) = co_await async_write(
        socket, boost::asio::buffer(data, bytes), as_tuple(use_awaitable));

    co_return ec;
}

awaitable<void> ssl_echo(ssl_socket socket) {
    for (;;) {
        auto err = co_await ssl_echo_once(socket);
        if (err) {
            break;
        }
    }

    try {
        co_await socket.async_shutdown(use_awaitable);
    } catch (std::exception& ex) {
        std::cout << "Server shutdown exception: " << ex.what() << std::endl;
    }
}

awaitable<void> ssl_echo_listener(uint16_t port_num) {
    auto executor = co_await this_coro::executor;

    // The SSL context is required, and holds certificates
    ssl::context ctx{ssl::context::tlsv12};

    // This holds the self-signed certificate used by the server
    load_server_certificate(ctx);

    tcp::acceptor acceptor(executor, {tcp::v4(), port_num});
    for (;;) {
        tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
        co_spawn(executor, ssl_echo(ssl_socket(std::move(socket), ctx)),
                 detached);
    }
}