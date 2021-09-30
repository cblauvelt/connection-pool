#include <chrono>
#include <iostream>
#include <ranges>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "ssl_connection.hpp"

#include "core/root_certificates.hpp"

#include "core/ssl_echo_server.hpp"
// #include "slow_echo_server.hpp"

namespace {

using std::cout;
using std::endl;
using namespace std::chrono_literals;

constexpr uint16_t port_number = 55558;
constexpr uint16_t slow_port_number = port_number + 1;
constexpr int num_tests = 10;

template <typename T> std::string bytes_to_string(const T& buffer) {
    std::stringstream retVal;
    for (auto byte : buffer) {
        retVal << byte;
    }

    return retVal.str();
}

awaitable<void>
on_connection_state_change(const cpool::client_connection_state state) {
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

    co_return;
}

awaitable<void> client_test(boost::asio::io_context& ctx, ssl::context& ssl_ctx,
                            bool last_task = false) {
    auto exec = co_await cpool::net::this_coro::executor;
    cpool::ssl_connection connection(exec, ssl_ctx, "google.com", 443,
                                     cpool::ssl_options{.sni = true});
    connection.set_state_change_handler(
        std::bind(on_connection_state_change, std::placeholders::_1));

    auto error = co_await connection.async_connect();
    EXPECT_FALSE(error) << error.message();

    co_await connection.async_disconnect();

    if (last_task) {
        // Wait a few ms for the other coroutines to finish
        // This would be better handled using a barrier but that's not in gcc10
        cpool::timer timer(exec);
        co_await timer.async_wait(250ms);
        ctx.stop();
    }
}

TEST(SSL_Client, EchoTest) {
    boost::asio::io_context ctx(1);

    boost::asio::signal_set signals(ctx, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) { ctx.stop(); });

    co_spawn(ctx, ssl_echo_listener(port_number), detached);

    cout << "Listening on " << port_number << endl;

    // create clients
    cpool::ssl::context client_ssl_ctx(cpool::ssl::context::tlsv12_client);
    // This holds the root certificate used for verification
    EXPECT_NO_THROW(load_root_certificates(client_ssl_ctx));
    // Verify the remote server's certificate
    client_ssl_ctx.set_verify_mode(ssl::verify_peer);

    cout << "Running " << num_tests << " tests" << endl;
    for (int i = 0; i < num_tests - 1; i++) {
        co_spawn(ctx, client_test(std::ref(ctx), std::ref(client_ssl_ctx)),
                 detached);
    }
    co_spawn(ctx, client_test(std::ref(ctx), std::ref(client_ssl_ctx), true),
             detached);

    ctx.run();
}

} // namespace