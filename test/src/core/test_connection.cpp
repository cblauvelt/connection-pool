#include "test_connection.hpp"

test_connection::test_connection(cpool::net::any_io_executor exec)
    : exec_(std::move(exec))
    , connected_(false) {}

cpool::net::any_io_executor test_connection::get_executor() { return exec_; }

bool test_connection::connected() { return connected_; }

cpool::awaitable<cpool::error> test_connection::connect() {
    connected_ = true;
    co_return cpool::no_error;
}

cpool::awaitable<cpool::error> test_connection::disconnect() {
    connected_ = false;
    co_return cpool::no_error;
}