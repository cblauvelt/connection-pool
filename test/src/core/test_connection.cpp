#include "test_connection.hpp"

test_connection::test_connection(cpool::net::any_io_executor exec)
    : exec_(std::move(exec))
    , connected_(false) {}

cpool::net::any_io_executor test_connection::get_executor() { return exec_; }

std::string test_connection::host() const { return "test host"; }

uint16_t test_connection::port() const { return 0; }

bool test_connection::connected() { return connected_; }

cpool::awaitable<cpool::error> test_connection::async_connect() {
    connected_ = true;
    co_return cpool::error();
}

cpool::awaitable<cpool::error> test_connection::async_disconnect() {
    connected_ = false;
    co_return cpool::error();
}