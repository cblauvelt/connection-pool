#include <iostream>
#include <ranges>
#include <chrono>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "tcp_connection.hpp"

#include "echo_server.hpp"
#include "slow_echo_server.hpp"

using std::cout;
using std::endl;
using namespace std::chrono_literals;

namespace {

constexpr uint16_t port_number = 55555;
constexpr uint16_t slow_port_number = port_number + 1;
constexpr int num_tests = 10;

template<typename T>
std::string bytes_to_string(const T& buffer) {
    std::stringstream retVal;
    for(auto byte : buffer) {
        retVal << byte;
    }

    return retVal.str();
}

void on_connection_state_change(const cpool::connection_state state) {
    switch (state) {
    case cpool::connection_state::resolving:
        EXPECT_TRUE(true);
        break;
    
    case cpool::connection_state::connecting:
        EXPECT_TRUE(true);
        break;

    case cpool::connection_state::connected:
        EXPECT_TRUE(true);
        break;

    case cpool::connection_state::disconnecting:
        EXPECT_TRUE(true);
        break;

    case cpool::connection_state::disconnected:
        EXPECT_TRUE(true);
        break;

    default:
        EXPECT_TRUE(false);
        break;
    }
}

awaitable<void> client_test(boost::asio::io_context& ctx, bool last_task=false) {
    cpool::tcp_connection connection(ctx, "localhost", port_number);
    connection.set_state_change_handler(std::bind(on_connection_state_change, std::placeholders::_1));

    auto error = co_await connection.connect();
    EXPECT_FALSE(error);

    size_t bytes = 0;
    std::string message = "Test message";
    
    std::tie(error, bytes) = co_await connection.write(boost::asio::buffer(message));
    EXPECT_FALSE(error);
    EXPECT_EQ(bytes, message.length());

    std::vector<std::uint8_t> buf(256);
    std::tie(error, bytes) = co_await connection.read_some(boost::asio::buffer(buf));
    EXPECT_FALSE(error);
    EXPECT_EQ(bytes, message.length());
    
    auto bufferMessage = bytes_to_string(buf | std::views::take(bytes));
    EXPECT_EQ(bufferMessage, message);

    error = co_await connection.disconnect();
    EXPECT_FALSE(error);

    if(last_task) {
        ctx.stop();
    }
}

awaitable<void> slow_client_test(boost::asio::io_context& ctx) {
    cpool::tcp_connection connection(ctx, "localhost", slow_port_number);
    connection.set_state_change_handler(std::bind(on_connection_state_change, std::placeholders::_1));

    auto error = co_await connection.connect();
    EXPECT_FALSE(error);

    size_t bytes = 0;
    std::string message = "Test message";
    auto delay = 500ms;
    
    std::tie(error, bytes) = co_await connection.write(boost::asio::buffer(message));
    EXPECT_FALSE(error);
    EXPECT_EQ(bytes, message.length());

    std::vector<std::uint8_t> buf(256);
    connection.expires_after(delay);
    std::tie(error, bytes) = co_await connection.read(boost::asio::buffer(buf));
    connection.expires_never();
    EXPECT_TRUE(error);
    EXPECT_EQ(error, boost::asio::error::timed_out);
    EXPECT_EQ(bytes, 0);

    error = co_await connection.disconnect();
    EXPECT_FALSE(error);

    ctx.stop();
}

TEST(TCP_Client, EchoTest) {
    boost::asio::io_context io_context(1);

    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto){ io_context.stop();});

    co_spawn(io_context, echo_listener(port_number), detached);

    cout << "Listening on " << port_number << endl;

    cout << "Running " << num_tests << " tests" << endl;
    for(int i=0; i < num_tests-1; i++) {
        co_spawn(io_context, client_test(std::ref(io_context)), detached);
    }
    co_spawn(io_context, client_test(std::ref(io_context), true), detached);

    io_context.run();
}

TEST(TCP_Client, TimeoutTest) {
    boost::asio::io_context io_context(1);

    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto){ io_context.stop();});

    co_spawn(io_context, slow_echo_listener(slow_port_number), detached);

    cout << "Listening on " << slow_port_number << endl;

    co_spawn(io_context, slow_client_test(std::ref(io_context)), detached);
    
    io_context.run();
}

}