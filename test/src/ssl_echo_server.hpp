#include <boost/asio/co_spawn.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/experimental/as_tuple.hpp>
#include <cstdio>

#include "server_certificate.hpp"

using boost::asio::ip::tcp;
using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;
using boost::asio::experimental::as_tuple;

namespace this_coro = boost::asio::this_coro;

awaitable<void> ssl_echo_once(tcp::socket& socket);

awaitable<void> ssl_echo(tcp::socket socket);

awaitable<void> ssl_echo_listener(uint16_t port_num);
