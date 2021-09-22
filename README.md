# connection-pool
A coroutines based connection pool written in C++.

## How to Use
This library uses coroutines and requires C++20. The library is tested using gcc10.
If you would like to use the library in vscode, consider using the devcontainer at
[https://github.com/cblauvelt/docker-vscode-cpp](https://github.com/cblauvelt/docker-vscode-cpp)

If you are using conan you can simply type

```bash
conan create .
```

If you're not using conan, you can simply copy the include files into your project.

## Examples

Using the TCP Connection. See the test directory for how the echo server works.

```c++
awaitable<void> slow_client_test(boost::asio::io_context& ctx) {
cpool::tcp_connection connection(ctx, "localhost", slow_port_number);
    connection.set_state_change_handler(
        std::bind(on_connection_state_change, std::placeholders::_1));

    auto error = co_await connection.connect();
    EXPECT_FALSE(error);

    size_t bytes = 0;
    std::string message = "Test message";
    auto delay = 500ms;

    std::tie(error, bytes) =
        co_await connection.write(boost::asio::buffer(message));

    std::vector<std::uint8_t> buf(256);
    connection.expires_after(delay);
    std::tie(error, bytes) = co_await connection.read(boost::asio::buffer(buf));
    connection.expires_never();
    
    error = co_await connection.disconnect();
    
    ctx.stop();
}

void main() {
    boost::asio::io_context io_context(1);

    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) { io_context.stop(); });

    co_spawn(io_context, echo_listener(port_number), detached);

    cout << "Listening on " << port_number << endl;

    for (int i = 0; i < num_tests - 1; i++) {
        co_spawn(io_context, client_test(std::ref(io_context)), detached);
    }
    co_spawn(io_context, client_test(std::ref(io_context), true), detached);

    io_context.run();
}
```

Using the connection pool

```c++
awaitable<void> echo_connection_test(asio::io_context& ctx) {
    auto connection_creator = [&]() -> std::unique_ptr<tcp_connection> {
        return std::make_unique<tcp_connection>(ctx, "localhost", port_number);
    };

    auto pool =
        connection_pool<tcp_connection>(connection_creator, num_connections);
    auto connection = co_await pool.get_connection();

    std::string message = "Test message";

    auto [err, bytes] =
        co_await connection->write(boost::asio::buffer(message));
    

    std::vector<std::uint8_t> buf(256);
    std::tie(err, bytes) =
        co_await connection->read_some(boost::asio::buffer(buf));
    

    auto bufferMessage = bytes_to_string(buf | std::views::take(bytes));
    

    pool.release_connection(connection);
    connection = nullptr;

    ctx.stop();
}

void main() {
    asio::io_context ctx(1);

    co_spawn(ctx, echo_listener(port_number), detached);

    co_spawn(ctx, echo_connection_test(std::ref(ctx)), detached);

    ctx.run();
}
```