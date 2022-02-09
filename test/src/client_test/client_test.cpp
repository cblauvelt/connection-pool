#include <chrono>
#include <iostream>
#include <ranges>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "tcp_connection.hpp"

#include "core/echo_server.hpp"
#include "core/slow_echo_server.hpp"

namespace {

using std::cout;
using std::endl;
using namespace std::chrono_literals;
using namespace cpool;

constexpr uint16_t port_number = 55555;
constexpr uint16_t slow_port_number = port_number + 1;
constexpr int num_tests = 10;

template <typename T> std::string bytes_to_string(const T& buffer) {
    std::stringstream retVal;
    for (auto byte : buffer) {
        retVal << byte;
    }

    return retVal.str();
}

awaitable<error>
on_connection_state_change(const tcp_connection* conn,
                           const cpool::client_connection_state state) {
    switch (state) {
    case cpool::client_connection_state::resolving:
        EXPECT_TRUE(true);
        break;

    case cpool::client_connection_state::connecting:
        EXPECT_TRUE(true);
        break;

    case cpool::client_connection_state::connected:
        EXPECT_TRUE(true);
        break;

    case cpool::client_connection_state::disconnecting:
        EXPECT_TRUE(true);
        break;

    case cpool::client_connection_state::disconnected:
        EXPECT_TRUE(true);
        break;

    default:
        EXPECT_TRUE(false);
    }

    co_return error();
}

awaitable<void> client_test(boost::asio::io_context& ctx,
                            bool last_task = false) {
    auto executor = co_await net::this_coro::executor;
    cpool::tcp_connection connection(executor, "localhost", port_number);
    connection.set_state_change_handler(on_connection_state_change);

    auto error = co_await connection.async_connect();
    EXPECT_FALSE(error);

    size_t bytes = 0;
    std::string message = "Test message";

    std::tie(error, bytes) =
        co_await connection.async_write(boost::asio::buffer(message));
    EXPECT_FALSE(error);
    EXPECT_EQ(bytes, message.length());

    std::vector<std::uint8_t> buf(256);
    std::tie(error, bytes) =
        co_await connection.async_read_some(boost::asio::buffer(buf));
    EXPECT_FALSE(error);
    EXPECT_EQ(bytes, message.length());

    auto bufferMessage = bytes_to_string(buf | std::views::take(bytes));
    EXPECT_EQ(bufferMessage, message);

    error = co_await connection.async_disconnect();
    EXPECT_FALSE(error);

    if (last_task) {
        ctx.stop();
    }
}

awaitable<void> slow_client_test(boost::asio::io_context& ctx) {
    auto executor = co_await net::this_coro::executor;
    cpool::tcp_connection connection(executor, "localhost", slow_port_number);
    connection.set_state_change_handler(on_connection_state_change);

    auto error = co_await connection.async_connect();
    EXPECT_FALSE(error);

    size_t bytes = 0;
    std::string message = "Test message";
    auto delay = 500ms;

    std::tie(error, bytes) =
        co_await connection.async_write(boost::asio::buffer(message));
    EXPECT_FALSE(error);
    EXPECT_EQ(bytes, message.length());

    std::vector<std::uint8_t> buf(256);
    connection.expires_after(delay);
    std::tie(error, bytes) =
        co_await connection.async_read(boost::asio::buffer(buf));
    connection.expires_never();
    EXPECT_TRUE(error);
    EXPECT_EQ(error.error_code(), (int)boost::asio::error::timed_out);
    EXPECT_EQ(bytes, 0);

    error = co_await connection.async_disconnect();
    EXPECT_FALSE(error);

    ctx.stop();
}

TEST(TCP_Client, EchoTest) {
    boost::asio::io_context io_context(1);

    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) { io_context.stop(); });

    co_spawn(io_context, echo_listener(port_number), detached);

    cout << "Listening on " << port_number << endl;

    cout << "Running " << num_tests << " tests" << endl;
    for (int i = 0; i < num_tests - 1; i++) {
        co_spawn(io_context, client_test(std::ref(io_context)), detached);
    }
    co_spawn(io_context, client_test(std::ref(io_context), true), detached);

    io_context.run();
}

TEST(TCP_Client, TimeoutTest) {
    boost::asio::io_context io_context(1);

    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) { io_context.stop(); });

    co_spawn(io_context, slow_echo_listener(slow_port_number), detached);

    cout << "Listening on " << slow_port_number << endl;

    co_spawn(io_context, slow_client_test(std::ref(io_context)), detached);

    io_context.run();
}

} // namespace