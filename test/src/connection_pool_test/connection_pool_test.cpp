#include <chrono>
#include <iostream>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "core/echo_server.hpp"
#include "core/test_connection.hpp"
#include "core/thread_pool.hpp"
#include "cpool/connection_pool.hpp"
#include "cpool/tcp_connection.hpp"

namespace {

using namespace std::chrono;
using namespace std::chrono_literals;
using namespace cpool;
using std::cout;
using std::endl;

constexpr uint16_t port_number = 55560;
constexpr uint16_t stop_port_number = port_number + 1;
constexpr uint16_t multi_stop_port_number = stop_port_number + 1;
constexpr int num_connections = 4;

template <typename T> std::string bytes_to_string(const T& buffer) {
    std::stringstream retVal;
    for (auto byte : buffer) {
        retVal << byte;
    }

    return retVal.str();
}

awaitable<void> mock_connection_test(asio::io_context& ctx) {
    auto executor = co_await net::this_coro::executor;
    auto connection_creator = [&]() -> std::unique_ptr<test_connection> {
        return std::make_unique<test_connection>(executor);
    };

    auto pool = connection_pool<test_connection>(executor, connection_creator,
                                                 num_connections);
    auto connection = co_await pool.get_connection();

    // we cant use ASSERT_NE because it doesn't support co_return
    EXPECT_NE(connection, nullptr);
    if (connection == nullptr) {
        co_return;
    }

    EXPECT_TRUE(connection->connected());
    EXPECT_EQ(pool.size_idle(), 0);
    EXPECT_EQ(pool.size_busy(), 1);
    EXPECT_EQ(pool.size(), 1);

    // release connection and test
    EXPECT_NO_THROW(pool.release_connection(connection));
    // should fail gracefully
    EXPECT_NO_THROW(pool.release_connection(connection));
    // check expected sizes
    EXPECT_EQ(pool.size_idle(), 1);
    EXPECT_EQ(pool.size_busy(), 0);
    EXPECT_EQ(pool.size(), 1);

    // should get a recycled connection
    auto connection2 = co_await pool.get_connection();
    EXPECT_NE(connection, nullptr);
    EXPECT_TRUE(connection->connected());
    EXPECT_EQ(connection, connection2);
    EXPECT_EQ(pool.size_idle(), 0);
    EXPECT_EQ(pool.size_busy(), 1);
    EXPECT_EQ(pool.size(), 1);

    connection2 = co_await pool.get_connection();
    EXPECT_EQ(pool.size_idle(), 0);
    EXPECT_EQ(pool.size_busy(), 2);
    EXPECT_EQ(pool.size(), 2);
    EXPECT_NO_THROW(pool.release_connection(connection));
    EXPECT_NO_THROW(pool.release_connection(connection2));
    EXPECT_EQ(pool.size_idle(), 2);
    EXPECT_EQ(pool.size_busy(), 0);
    EXPECT_EQ(pool.size(), 2);

    auto connection3 = co_await pool.get_connection();
    EXPECT_EQ(pool.size(), 2);
    auto uniq_connection = pool.claim_connection(connection3);
    EXPECT_NE(uniq_connection, nullptr);
    EXPECT_EQ(pool.size(), 1);

    // expect exception on releasing unmanaged connection
    auto unmanaged_connection = std::make_unique<test_connection>(executor);
    EXPECT_THROW(pool.release_connection(unmanaged_connection.get()),
                 std::runtime_error);

    ctx.stop();
}

awaitable<void> too_many_try_connections_test(asio::io_context& ctx) {
    auto executor = co_await net::this_coro::executor;
    auto connection_creator = [&]() -> std::unique_ptr<test_connection> {
        return std::make_unique<test_connection>(executor);
    };

    auto pool = connection_pool<test_connection>(executor, connection_creator,
                                                 num_connections);
    std::array<test_connection*, num_connections> connections;
    for (int i = 0; i < num_connections; i++) {
        connections[i] = co_await pool.try_get_connection();
        EXPECT_NE(connections[i], nullptr);
    }
    EXPECT_EQ(pool.size_idle(), 0);
    EXPECT_EQ(pool.size_busy(), num_connections);
    EXPECT_EQ(pool.size(), num_connections);

    auto connection = co_await pool.try_get_connection();
    EXPECT_EQ(connection, nullptr);

    ctx.stop();
}

awaitable<void> release_connection(connection_pool<test_connection>& pool,
                                   test_connection* connection) {
    auto executor = co_await net::this_coro::executor;

    timer timer(executor);
    co_await timer.async_wait(50ms);

    pool.release_connection(connection);
}

awaitable<void> too_many_connections_test(asio::io_context& ctx) {
    auto executor = co_await net::this_coro::executor;
    auto connection_creator = [&]() -> std::unique_ptr<test_connection> {
        return std::make_unique<test_connection>(executor);
    };

    auto pool = connection_pool<test_connection>(executor, connection_creator,
                                                 num_connections);
    std::array<test_connection*, num_connections> connections;
    for (int i = 0; i < num_connections; i++) {
        connections[i] = co_await pool.get_connection();
        EXPECT_NE(connections[i], nullptr);
    }
    EXPECT_EQ(pool.size_idle(), 0);
    EXPECT_EQ(pool.size_busy(), num_connections);
    EXPECT_EQ(pool.size(), num_connections);

    co_spawn(ctx, release_connection(pool, connections[0]), detached);

    auto connection = co_await pool.get_connection();
    EXPECT_NE(connection, nullptr);

    ctx.stop();
}

awaitable<void> echo_connection_test(asio::io_context& ctx) {
    auto executor = co_await net::this_coro::executor;
    auto connection_creator = [&]() -> std::unique_ptr<tcp_connection> {
        return std::make_unique<tcp_connection>(executor, "localhost",
                                                port_number);
    };

    auto pool = connection_pool<tcp_connection>(executor, connection_creator,
                                                num_connections);
    auto connection = co_await pool.get_connection();

    EXPECT_NE(connection, nullptr);
    if (connection == nullptr) {
        co_return;
    }

    EXPECT_EQ(pool.size_idle(), 0);
    EXPECT_EQ(pool.size_busy(), 1);
    EXPECT_EQ(pool.size(), 1);

    std::string message = "Test message";

    auto [err, bytes] = co_await connection->async_write(net::buffer(message));
    EXPECT_FALSE(err);
    EXPECT_EQ(bytes, message.length());

    std::vector<std::uint8_t> buf(256);
    std::tie(err, bytes) =
        co_await connection->async_read_some(net::buffer(buf));
    EXPECT_FALSE(err);
    EXPECT_EQ(bytes, message.length());

    auto bufferMessage = bytes_to_string(buf | std::views::take(bytes));
    EXPECT_EQ(bufferMessage, message);

    EXPECT_NO_THROW(pool.release_connection(connection));
    connection = nullptr;

    ctx.stop();
}

awaitable<void>
bounce_data_until_stopped(connection_pool<tcp_connection>& pool) {
    auto connection = co_await pool.get_connection();

    if (connection == nullptr) {
        EXPECT_TRUE(pool.stopped());
        co_return;
    }

    std::string message = "Test message";

    for (;;) {
        auto [err, bytes] =
            co_await connection->async_write(net::buffer(message));
        if (err) {
            EXPECT_EQ(err.value(), (int)net::error::operation_aborted);
            EXPECT_TRUE(pool.stopped());
            co_return;
        }

        EXPECT_EQ(bytes, message.length());

        std::vector<std::uint8_t> buf(256);
        std::tie(err, bytes) =
            co_await connection->async_read_some(net::buffer(buf));
        if (err) {
            // cout << err << endl;
            EXPECT_EQ(err.value(), (int)net::error::operation_aborted);
            EXPECT_TRUE(pool.stopped());
            co_return;
        }

        EXPECT_EQ(bytes, message.length());

        auto bufferMessage = bytes_to_string(buf | std::views::take(bytes));
        EXPECT_EQ(bufferMessage, message);
    }

    co_return;
}

awaitable<void> stop_echo_connection_test(asio::io_context& ctx) {
    auto executor = co_await net::this_coro::executor;
    auto connection_creator = [&]() -> std::unique_ptr<tcp_connection> {
        return std::make_unique<tcp_connection>(executor, "localhost",
                                                stop_port_number);
    };

    auto pool = connection_pool<tcp_connection>(executor, connection_creator,
                                                num_connections);

    co_spawn(ctx, bounce_data_until_stopped(std::ref(pool)), detached);

    cpool::timer timer(executor);
    co_await timer.async_wait(100ms);
    co_await pool.stop();
    co_await timer.async_wait(10ms);

    ctx.stop();
}

awaitable<void> multi_stop_echo_connection_test(asio::io_context& ctx,
                                                uint num_threads) {
    std::vector<std::thread> threads;
    auto executor = co_await net::this_coro::executor;
    auto connection_creator = [&]() -> std::unique_ptr<tcp_connection> {
        return std::make_unique<tcp_connection>(executor, "localhost",
                                                multi_stop_port_number);
    };

    auto pool = connection_pool<tcp_connection>(executor, connection_creator,
                                                num_connections);

    for (int i = 0; i < num_threads; i++) {
        co_spawn(ctx, bounce_data_until_stopped(std::ref(pool)), detached);
    }
    start_thread_pool(threads, num_threads, [&]() { ctx.run(); });

    cpool::timer timer(executor);
    co_await timer.async_wait(100ms);
    co_await pool.stop();
    co_await timer.async_wait(10ms);

    ctx.stop();

    stop_thread_pool(threads);
}

TEST(ConnectionPoolTest, Mock) {
    asio::io_context ctx(1);

    co_spawn(ctx, mock_connection_test(std::ref(ctx)), detached);

    ctx.run();
}

TEST(ConnectionPoolTest, TooManyTryConnections) {
    asio::io_context ctx(1);

    co_spawn(ctx, too_many_try_connections_test(std::ref(ctx)), detached);

    ctx.run();
}

TEST(ConnectionPoolTest, TooManyConnections) {
    asio::io_context ctx(1);

    co_spawn(ctx, too_many_connections_test(std::ref(ctx)), detached);

    ctx.run();
}

TEST(EchoTest, SingleEcho) {
    asio::io_context ctx(1);

    co_spawn(ctx, echo_listener(port_number), detached);

    co_spawn(ctx, echo_connection_test(std::ref(ctx)), detached);

    ctx.run();
}

TEST(EchoTest, Stop) {
    asio::io_context ctx(1);

    co_spawn(ctx, echo_listener(stop_port_number), detached);

    co_spawn(ctx, stop_echo_connection_test(std::ref(ctx)), detached);

    ctx.run();
}

TEST(EchoTest, MultiStop) {
    uint num_threads = 8;
    asio::io_context ctx(num_threads);

    co_spawn(ctx, echo_listener(multi_stop_port_number), detached);

    co_spawn(ctx, multi_stop_echo_connection_test(std::ref(ctx), num_threads),
             detached);

    ctx.run();
}

} // namespace