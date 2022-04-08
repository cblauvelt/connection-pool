#include <absl/cleanup/cleanup.h>
#include <chrono>
#include <iostream>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "core/echo_server.hpp"
#include "core/test_connection.hpp"
#include "core/thread_pool.hpp"
#include "core/unreliable_echo_server.hpp"
#include "cpool/awaitable_latch.hpp"
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
constexpr uint16_t unreliable_port_number = multi_stop_port_number + 1;
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
    pool.release_connection(unmanaged_connection.get());

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
        return std::make_unique<tcp_connection>(executor, "127.0.0.1",
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

awaitable<void> bounce_data_until_stopped(connection_pool<tcp_connection>& pool,
                                          awaitable_latch& latch) {
    std::string message = "Test message";

    while (!pool.stopped()) {
        auto connection = co_await pool.get_connection();
        if (connection == nullptr) {
            EXPECT_TRUE(pool.stopped());
            break;
        }

        absl::Cleanup defer([&]() { pool.release_connection(connection); });

        auto [err, bytes] =
            co_await connection->async_write(net::buffer(message));
        if (err) {
            EXPECT_THAT((std::array{(int)net::error::operation_aborted,
                                    (int)net::error::not_connected}),
                        testing::Contains(err.value()));
            EXPECT_TRUE(pool.stopped());
            break;
        }

        EXPECT_EQ(bytes, message.length());

        std::vector<std::uint8_t> buf(256);
        std::tie(err, bytes) =
            co_await connection->async_read_some(net::buffer(buf));
        if (err) {
            EXPECT_THAT((std::array{(int)net::error::operation_aborted,
                                    (int)net::error::not_connected}),
                        testing::Contains(err.value()));
            EXPECT_TRUE(pool.stopped());

            break;
        }

        EXPECT_EQ(bytes, message.length());

        auto bufferMessage = bytes_to_string(buf | std::views::take(bytes));
        EXPECT_EQ(bufferMessage, message);
    }

    latch.count_down();
    CPOOL_TRACE_LOG("CPOOL_TEST", "Remaining jobs {}", latch.value());
    co_return;
}

awaitable<void> stop_echo_connection_test(asio::io_context& ctx) {
    auto executor = co_await net::this_coro::executor;
    awaitable_latch latch(executor, 1);
    auto connection_creator = [&]() -> std::unique_ptr<tcp_connection> {
        return std::make_unique<tcp_connection>(executor, "127.0.0.1",
                                                stop_port_number);
    };

    auto pool = connection_pool<tcp_connection>(executor, connection_creator,
                                                num_connections);

    co_spawn(ctx, bounce_data_until_stopped(std::ref(pool), std::ref(latch)),
             detached);

    cpool::timer timer(executor);
    co_await timer.async_wait(100ms);
    co_await pool.stop();
    co_await latch.wait();

    ctx.stop();
}

awaitable<void> multi_stop_echo_connection_test(asio::io_context& ctx,
                                                uint num_threads) {
    auto executor = co_await net::this_coro::executor;
    awaitable_latch latch(executor, num_threads);
    auto connection_creator = [&]() -> std::unique_ptr<tcp_connection> {
        return std::make_unique<tcp_connection>(executor, "127.0.0.1",
                                                multi_stop_port_number);
    };

    auto pool = connection_pool<tcp_connection>(executor, connection_creator,
                                                num_connections);

    for (int i = 0; i < num_threads; i++) {
        co_spawn(ctx,
                 bounce_data_until_stopped(std::ref(pool), std::ref(latch)),
                 detached);
    }

    cpool::timer timer(executor);
    co_await timer.async_wait(100ms);
    co_await pool.stop();
    co_await latch.wait();

    ctx.stop();
}

awaitable<void> unreliable_echo_once(cpool::tcp_connection* connection) {
    EXPECT_NE(connection, nullptr);
    if (connection == nullptr) {
        co_return;
    }

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
}

awaitable<void> unreliable_echo_fail(cpool::tcp_connection* connection) {
    EXPECT_NE(connection, nullptr);
    if (connection == nullptr) {
        co_return;
    }

    std::string message = "Test message";

    auto [err, bytes] = co_await connection->async_write(net::buffer(message));
    EXPECT_FALSE(err);
    EXPECT_EQ(bytes, message.length());

    std::vector<std::uint8_t> buf(256);
    std::tie(err, bytes) =
        co_await connection->async_read_some(net::buffer(buf));
    EXPECT_TRUE(err);
    EXPECT_EQ(bytes, 0);

    auto bufferMessage = bytes_to_string(buf | std::views::take(bytes));
    EXPECT_NE(bufferMessage, message);
}

awaitable<void> restart_server_after_delay(unreliable_echo_server& server,
                                           std::chrono::milliseconds delay) {
    cpool::timer timer(co_await net::this_coro::executor);
    co_await timer.async_wait(delay);
    server.run();
}

awaitable<void> unreliable_echo_test(boost::asio::io_context& ctx,
                                     unreliable_echo_server& server) {
    auto exec = co_await net::this_coro::executor;
    auto connection_creator = [&]() -> std::unique_ptr<tcp_connection> {
        return std::make_unique<tcp_connection>(exec, "127.0.0.1",
                                                unreliable_port_number);
    };

    auto pool = connection_pool<tcp_connection>(exec, connection_creator,
                                                num_connections);
    auto connection = co_await pool.get_connection();

    EXPECT_EQ(pool.size_idle(), 0);
    EXPECT_EQ(pool.size_busy(), 1);
    EXPECT_EQ(pool.size(), 1);

    co_await unreliable_echo_once(connection);

    EXPECT_NO_THROW(pool.release_connection(connection));
    connection = nullptr;

    co_await server.stop_all();

    connection = co_await pool.get_connection();
    co_await unreliable_echo_fail(connection);
    EXPECT_FALSE(connection->connected());
    EXPECT_NO_THROW(pool.release_connection(connection));
    EXPECT_EQ(pool.size_idle(), 0);
    EXPECT_EQ(pool.size_busy(), 0);
    EXPECT_EQ(pool.size(), 0);

    co_spawn(exec, restart_server_after_delay(std::ref(server), 100ms),
             detached);

    connection = co_await pool.get_connection();
    EXPECT_EQ(pool.size_idle(), 0);
    EXPECT_EQ(pool.size_busy(), 1);
    EXPECT_EQ(pool.size(), 1);
    co_await unreliable_echo_once(connection);

    co_await server.stop_all();

    ctx.stop();
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
    uint num_jobs = 4;
    asio::io_context ctx(1);
    std::vector<std::thread> threads;

    co_spawn(ctx, echo_listener(multi_stop_port_number), detached);

    co_spawn(ctx, multi_stop_echo_connection_test(std::ref(ctx), num_jobs),
             detached);

    ctx.run();
}

TEST(EchoTest, Disconnect_Reconnect) {
    boost::asio::io_context io_context(1);

    unreliable_echo_server server(io_context.get_executor(),
                                  unreliable_port_number);
    server.run();
    co_spawn(io_context,
             unreliable_echo_test(std::ref(io_context), std::ref(server)),
             detached);

    io_context.run();
}

} // namespace