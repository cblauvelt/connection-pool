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
#include <set>

#include "cpool/condition_variable.hpp"

using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;
using boost::asio::experimental::as_tuple;
using boost::asio::ip::tcp;

namespace this_coro = boost::asio::this_coro;
class unreliable_echo_session;

class unreliable_echo_server {
  public:
    unreliable_echo_server(boost::asio::any_io_executor exec, uint16_t port);
    ~unreliable_echo_server();

    awaitable<void> echo_listener();

    void run();
    awaitable<void> stop_all();
    void stop(std::shared_ptr<unreliable_echo_session> session);

  private:
    friend class unreliable_echo_session;
    void notify_stopped(std::shared_ptr<unreliable_echo_session> session);

    boost::asio::any_io_executor exec_;
    uint16_t port_;
    tcp::acceptor acceptor_;
    std::atomic_bool stop_;
    std::atomic_uint16_t num_connections_;
    std::set<std::shared_ptr<unreliable_echo_session>> session_set_;
    cpool::condition_variable stop_cv_;
};