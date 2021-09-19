#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/as_tuple.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/write.hpp>
#include <cstdio>

using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;
using boost::asio::experimental::as_tuple;
using boost::asio::ip::tcp;

namespace this_coro = boost::asio::this_coro;

awaitable<void> slow_echo_once(tcp::socket& socket);

awaitable<void> slow_echo(tcp::socket socket);

awaitable<void> slow_echo_listener(uint16_t port_num);
