#include "ssl_echo_server.hpp"

#include <iostream>

using boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;
using ssl_socket = boost::asio::ssl::stream<tcp::socket>;

awaitable<void> ssl_echo_once(ssl_socket& socket)
{
  std::array<char, 128> data;
  auto [ec, bytesRead] = co_await socket.async_read_some(boost::asio::buffer(data), as_tuple(use_awaitable));
  if(bytesRead != 0) {
    // std::cout << "Bytes Read by server: " << bytesRead << std::endl;
    co_await async_write(socket, boost::asio::buffer(data, bytesRead), use_awaitable);
  } else {
    std::cout << "echo error: " << ec << std::endl;
  }

}

awaitable<void> ssl_echo(ssl_socket socket)
{
  try
  {
    for (;;)
    {
      // The asynchronous operations to echo a single chunk of data have been
      // refactored into a separate function. When this function is called, the
      // operations are still performed in the context of the current
      // coroutine, and the behaviour is functionally equivalent.
      co_await ssl_echo_once(socket);
    }
  }
  catch (std::exception& e)
  {
    std::printf("echo Exception: %s\n", e.what());
  }
}

awaitable<void> ssl_echo_listener(uint16_t port_num)
{
  auto executor = co_await this_coro::executor;

  // The SSL context is required, and holds certificates
  ssl::context ctx{ssl::context::tlsv12};

  // This holds the self-signed certificate used by the server
  load_server_certificate(ctx);

  tcp::acceptor acceptor(executor, {tcp::v4(), port_num});
  for (;;)
  {
    tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
    co_spawn(executor, ssl_echo(ssl_socket(std::move(socket), ctx)), detached);
  }
}