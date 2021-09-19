#pragma once

#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/as_tuple.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/write.hpp>

#include <fmt/core.h>

#include "error.hpp"
#include "timer.hpp"
#include "types.hpp"

using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;
using boost::asio::use_awaitable_t;
using boost::asio::ip::tcp;
using namespace boost::asio::experimental::awaitable_operators;
using boost::asio::experimental::as_tuple;
using boost::asio::experimental::as_tuple_t;
namespace asio = boost::asio;

class test_connection {

  public:
    test_connection(asio::io_context& ctx);

    asio::io_context& get_context();

    bool connected();

    awaitable<cpool::error> connect();
    awaitable<cpool::error> disconnect();

  private:
    asio::io_context& ctx_;
    bool connected_;
};