#include <iostream>
#include <ranges>

#include "tcp_connection.hpp"

#include "echo_server.hpp"

using std::cout;
using std::endl;

constexpr uint16_t port_number = 55555;
constexpr int num_tests = 10;

template<typename T>
std::string bytes_to_string(const T& buffer) {
    std::stringstream retVal;
    for(auto byte : buffer) {
        retVal << byte;
    }

    return retVal.str();
}

awaitable<void> client_test(boost::asio::io_context& ctx, int& retVal) {
    auto executor = co_await this_coro::executor;
    
    do{
        cpool::tcp_connection connection(ctx, "localhost", port_number);

        auto error = co_await connection.connect();
        if(error) {
            cout << "Error connecting" << error << endl;
            retVal = 1;
            break;
        }

        size_t bytes = 0;
        std::string message = "Test message";
        
        std::tie(error, bytes) = co_await connection.write(boost::asio::buffer(message));
        if(error) {
            cout << "Error writing " << error << endl;
            retVal = 1;
            break;
        }
        if(bytes < message.length()) {
            cout << "Not all bytes were written. Expected " << message.length() << " bytes. Wrote " << bytes << endl;
            retVal = 1;
            break;
        }

        std::vector<std::uint8_t> buf(256);
        std::tie(error, bytes) = co_await connection.read_some(boost::asio::buffer(buf));
        if(error) {
            cout << "Error reading " << error << endl;
            retVal = 1;
            break;
        }
        if(bytes != message.length()) {
            cout << "Not all bytes were written. Expected " << message.length() << " bytes. Wrote " << bytes << endl;
            retVal = 1;
            break;
        }
        
        auto bufferMessage = bytes_to_string(std::views::counted(buf.begin(),bytes));
        if(bufferMessage != message) {
            cout << "Read does not equal write. Expected: " << message << ". Received: " << bufferMessage << endl;
            retVal = 1;
            break;
        }

        error = co_await connection.disconnect();
        if(error) {
            cout << "Error disconnecting" << error << endl;
            retVal = 1;
            break;
        }


    } while(false);

    ctx.stop();
}

int main() {
    int retVal = 0;
    boost::asio::io_context io_context(1);

    // maximum timeout
    boost::asio::steady_timer timer(io_context);
    timer.expires_from_now(std::chrono::seconds(30));
    timer.async_wait([&](const boost::system::error_code& ec){
        retVal = 1;
        io_context.stop();
    });

    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto){ retVal=1; io_context.stop();});

    co_spawn(io_context, listener(port_number), detached);

    cout << "Listening on " << port_number << endl;

    cout << "Running " << num_tests << " tests" << endl;
    for(int i=0; i < num_tests; i++) {
        co_spawn(io_context, client_test(std::ref(io_context), retVal), detached);
    }

    io_context.run();

    return retVal;
}