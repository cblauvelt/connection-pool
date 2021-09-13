#include <chrono>
#include <iostream>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "connection_pool.hpp"
#include "tcp_connection.hpp"
#include "core/test_connection.hpp"
#include "core/echo_server.hpp"

namespace {

using namespace std::chrono;
using namespace std::chrono_literals;
using namespace cpool;


constexpr uint16_t port_number = 55557;
constexpr int num_connections = 4;

template<typename T>
std::string bytes_to_string(const T& buffer) {
    std::stringstream retVal;
    for(auto byte : buffer) {
        retVal << byte;
    }

    return retVal.str();
}

awaitable<void> mock_connection_test(asio::io_context& ctx) {
    auto connection_creator = [&]() -> std::unique_ptr<test_connection> {
        return std::make_unique<test_connection>(ctx);
    };

    auto pool = connection_pool<test_connection>(connection_creator, num_connections);
    auto connection = co_await pool.get_connection();

    EXPECT_NE(connection, nullptr);
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

    // expect exception on releasing unmanaged connection
    auto unmanaged_connection = std::make_unique<test_connection>(ctx);
    EXPECT_THROW(pool.release_connection(unmanaged_connection.get()), std::runtime_error);
        
    ctx.stop();

    co_return;
}

awaitable<void> too_many_connections_test(asio::io_context& ctx) {
    auto connection_creator = [&]() -> std::unique_ptr<test_connection> {
        return std::make_unique<test_connection>(ctx);
    };

    auto pool = connection_pool<test_connection>(connection_creator, num_connections);
    std::array<test_connection*, num_connections> connections;
    for(int i=0; i < num_connections; i++) {
        connections[i] = co_await pool.get_connection();
    }
    EXPECT_EQ(pool.size_idle(), 0);
    EXPECT_EQ(pool.size_busy(), num_connections);
    EXPECT_EQ(pool.size(), num_connections);

    auto connection = co_await pool.get_connection();
    EXPECT_EQ(connection, nullptr);

    ctx.stop();
}

awaitable<void> echo_connection_test(asio::io_context& ctx) {
    auto connection_creator = [&]() -> std::unique_ptr<tcp_connection> {
        return std::make_unique<tcp_connection>(ctx, "localhost", port_number);
    };

    auto pool = connection_pool<tcp_connection>(connection_creator, num_connections);
    auto connection = co_await pool.get_connection();

    EXPECT_NE(connection, nullptr);
    EXPECT_EQ(pool.size_idle(), 0);
    EXPECT_EQ(pool.size_busy(), 1);
    EXPECT_EQ(pool.size(), 1);

    std::string message = "Test message";
    
    auto [err, bytes] = co_await connection->write(boost::asio::buffer(message));
    EXPECT_FALSE(err);
    EXPECT_EQ(bytes, message.length());

    std::vector<std::uint8_t> buf(256);
    std::tie(err, bytes) = co_await connection->read_some(boost::asio::buffer(buf));
    EXPECT_FALSE(err);
    EXPECT_EQ(bytes, message.length());
    
    auto bufferMessage = bytes_to_string(buf | std::views::take(bytes));
    EXPECT_EQ(bufferMessage, message);

    EXPECT_NO_THROW(pool.release_connection(connection));
    connection = nullptr;

    ctx.stop();
}

TEST(ConnectionPool, MockTest)
{
    asio::io_context ctx(1);

    co_spawn(ctx, mock_connection_test(std::ref(ctx)), detached);

    ctx.run();
}

TEST(ConnectionPool, TooManyConnections)
{
    asio::io_context ctx(1);

    co_spawn(ctx, too_many_connections_test(std::ref(ctx)), detached);

    ctx.run();
}

TEST(ConnectionPool, EchoTest)
{
    asio::io_context ctx(1);

    co_spawn(ctx, echo_listener(port_number), detached);

    
    co_spawn(ctx, echo_connection_test(std::ref(ctx)), detached);

    ctx.run();
}

} // namespace