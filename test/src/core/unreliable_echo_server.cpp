#include "unreliable_echo_server.hpp"
#include "unreliable_echo_session.hpp"

#include <iostream>
using std::cout;
using std::endl;

unreliable_echo_server::unreliable_echo_server(
    boost::asio::any_io_executor exec, uint16_t port)
    : exec_(exec)
    , port_(port)
    , acceptor_(exec)
    , stop_(false)
    , num_connections_(0)
    , session_set_()
    , stop_cv_(exec) {}

unreliable_echo_server::~unreliable_echo_server() {
    if (acceptor_.is_open()) {
        boost::system::error_code ignored_err;
        acceptor_.close(ignored_err);
    }
}

awaitable<void> unreliable_echo_server::echo_listener() {
    cout << "Listening on " << port_ << endl;
    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), port_);
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen();

    while (!stop_) {
        tcp::socket socket = co_await acceptor_.async_accept(use_awaitable);
        if (!socket.is_open()) {
            break;
        }
        auto session = std::make_shared<unreliable_echo_session>(
            exec_, this, std::move(socket));
        co_spawn(exec_, session->run(), detached);

        session_set_.emplace(std::move(session));
    }

    cout << "server on " << port_ << " shutdown" << endl;
}

void unreliable_echo_server::run() {
    if (acceptor_.is_open()) {
        return;
    }

    stop_ = false;
    co_spawn(exec_, echo_listener(), detached);
}

awaitable<void> unreliable_echo_server::stop_all() {
    stop_ = true;
    acceptor_.close();
    for (auto& session_ptr : session_set_) {
        session_ptr->stop();
    }
    co_await stop_cv_.async_wait([&]() { return num_connections_ == 0; });
}

void unreliable_echo_server::stop(
    std::shared_ptr<unreliable_echo_session> session) {
    session->stop();
}

void unreliable_echo_server::notify_stopped(
    std::shared_ptr<unreliable_echo_session> session) {
    session_set_.erase(session);
    num_connections_--;
    stop_cv_.notify_all();
}