#include "test_connection.hpp"

test_connection::test_connection(asio::io_context& ctx) :
    ctx_(ctx),
    connected_(false)
{}
    
asio::io_context& test_connection::get_context() {
    return ctx_;
}

bool test_connection::connected() {
    return connected_;
}

awaitable<cpool::error> test_connection::connect() {
    connected_ = true;
    co_return cpool::no_error;
}

awaitable<cpool::error> test_connection::disconnect() {
    connected_ = false;
    co_return cpool::no_error;
}