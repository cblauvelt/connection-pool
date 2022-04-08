#define CPOOL_CPOOL_TRACE_LOGGING

#include <cpool/cpool.hpp>
#include <fmt/format.h>
#include <iostream>
#include <optional>

using std::cout;
using std::endl;
using namespace cpool;

const std::string ENV_HOST = "CLIENT_HOST";
const std::string DEFAULT_HOST = "host.docker.internal";
const std::string ENV_PORT = "CLIENT_PORT";
const std::string DEFAULT_PORT = "80";

std::optional<std::string> get_env_var(std::string const& key) {
    char* val = getenv(key.c_str());
    return (val == NULL) ? std::nullopt : std::optional(std::string(val));
}

awaitable<batteries::errors::error>
on_connection_state_change(cpool::tcp_connection* conn,
                           const cpool::client_connection_state state) {
    switch (state) {
    case cpool::client_connection_state::disconnected:
        cout << fmt::format("disconnected from {0}:{1}", conn->host(),
                            conn->port())
             << endl;
        break;

    case cpool::client_connection_state::resolving:
        cout << fmt::format("resolving {0}", conn->host()) << endl;
        break;

    case cpool::client_connection_state::connecting:
        cout << fmt::format("connecting to {0}:{1}", conn->host(), conn->port())
             << endl;
        break;

    case cpool::client_connection_state::connected:
        cout << fmt::format("connected to {0}:{1}", conn->host(), conn->port())
             << endl;
        break;

    case cpool::client_connection_state::disconnecting:
        cout << fmt::format("disconnecting from {0}:{1}", conn->host(),
                            conn->port())
             << endl;
        break;

    default:
        cout << fmt::format("unknown client_connection_state: {0}", (int)state)
             << endl;
    }

    co_return batteries::errors::error();
}

awaitable<void> run_client(boost::asio::io_context& ctx) {
    auto executor = co_await net::this_coro::executor;
    auto host = get_env_var(ENV_HOST).value_or(DEFAULT_HOST);
    auto portString = get_env_var(ENV_PORT).value_or(DEFAULT_PORT);
    uint16_t port = std::stoi(portString);

    tcp::socket socket(executor);

    tcp::resolver resolver(socket.get_executor());
    auto [err, endpoints] = co_await resolver.async_resolve(
        host, portString, as_tuple(use_awaitable));

    if (err) {
        cout << fmt::format("Could not resolve host {0}; {1}", host,
                            err.message())
             << endl;
        co_return;
    }

    for (auto& endpoint_entry : endpoints) {
        cout << fmt::format("{}",
                            endpoint_entry.endpoint().address().to_string())
             << endl;
    }

    auto [connect_err, endpoint] = co_await asio::async_connect(
        socket, endpoints, as_tuple(use_awaitable));
    if (connect_err) {
        cout << fmt::format("could not connect to host {0}. Reason {2}",
                            endpoint.address().to_string(), portString,
                            connect_err.message())
             << endl;
        co_return;
    }

    cout << "Connected . . . " << endl;
    cpool::timer timer(executor);
    co_await timer.async_wait(1s);

    cout << "Disconnecting . . . " << endl;

    socket.shutdown(tcp::socket::shutdown_both, err);
    if (err) {
        cout << "Error shutting down " << err.message() << endl;
    }

    socket.close(err);

    if (err) {
        cout << "Error disconnecting " << err.message() << endl;
    }

    ctx.stop();
}

int main(void) {
    net::io_context ctx(1);
    co_spawn(ctx, run_client(std::ref(ctx)), detached);
    ctx.run();
    return 0;
}