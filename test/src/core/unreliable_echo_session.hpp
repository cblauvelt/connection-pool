#pragma once

#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/as_tuple.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/write.hpp>
#include <cstdio>

#include "cpool/condition_variable.hpp"
#include "unreliable_echo_server.hpp"

using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;
using boost::asio::experimental::as_tuple;
using boost::asio::ip::tcp;

namespace this_coro = boost::asio::this_coro;

class unreliable_echo_session
    : std::enable_shared_from_this<unreliable_echo_session> {
  public:
    unreliable_echo_session(boost::asio::any_io_executor exec,
                            unreliable_echo_server* server, tcp::socket socket);
    ~unreliable_echo_session();

    awaitable<boost::system::error_code> echo_once();

    awaitable<void> run();
    void stop();

  private:
    boost::asio::any_io_executor exec_;
    unreliable_echo_server* server_;
    tcp::socket socket_;
    std::atomic_bool stop_;
};