#pragma once

#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

#include "cpool/timer.hpp"
#include "cpool/types.hpp"

class test_connection {

  public:
    test_connection(cpool::net::any_io_executor exec);

    cpool::net::any_io_executor get_executor();

    bool connected();

    cpool::awaitable<cpool::error> async_connect();
    cpool::awaitable<cpool::error> async_disconnect();

  private:
    cpool::net::any_io_executor exec_;
    bool connected_;
};