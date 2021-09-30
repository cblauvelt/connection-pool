#include <chrono>
#include <iostream>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "connection_pool.hpp"
#include "core/echo_server.hpp"
#include "core/test_connection.hpp"
#include "tcp_connection.hpp"

namespace {

using namespace std::chrono;
using namespace std::chrono_literals;
using namespace cpool;

constexpr uint16_t port_number = 55557;
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

TEST(ConnectionPool, MockTest) {
    asio::io_context ctx(1);

    co_spawn(ctx, mock_connection_test(std::ref(ctx)), detached);

    ctx.run();
}

TEST(ConnectionPool, TooManyTryConnections) {
    asio::io_context ctx(1);

    co_spawn(ctx, too_many_try_connections_test(std::ref(ctx)), detached);

    ctx.run();
}

TEST(ConnectionPool, TooManyConnections) {
    asio::io_context ctx(1);

    co_spawn(ctx, too_many_connections_test(std::ref(ctx)), detached);

    ctx.run();
}

TEST(ConnectionPool, EchoTest) {
    asio::io_context ctx(1);

    co_spawn(ctx, echo_listener(port_number), detached);

    co_spawn(ctx, echo_connection_test(std::ref(ctx)), detached);

    ctx.run();
}

} // namespace