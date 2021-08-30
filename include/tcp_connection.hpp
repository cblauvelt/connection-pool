#pragma once

#include <vector>
#include <memory>
#include <functional>
#include <chrono>
#include <string>
#include <iostream>
#include <system_error>
#include <tuple>

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/as_tuple.hpp>

#include "types.hpp"
#include "timer.hpp"

using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;
using boost::asio::use_awaitable_t;
using boost::asio::ip::tcp;
using namespace boost::asio::experimental::awaitable_operators;
using boost::asio::experimental::as_tuple;
using boost::asio::experimental::as_tuple_t;

namespace cpool {

namespace asio = boost::asio;
using milliseconds = std::chrono::milliseconds;
using boost::asio::ip::tcp;

/**
 * Defines the available states fo the connection_state object.
 */
enum class connection_state : uint8_t {
    /// The TcpConnection is not connected to a remote endpoint
    disconnected = 0,
    
    /// The TcpConnection is resolving a URL to determine the IP address of the remote endpoint
    resolving,
    
    /// The TcpConnection is connecting to a remote endpoint
    connecting,
    
    /// The TcpConnection is connected to a remote endpoint and able to send requests and receive responses
    connected,
    
    /// The TcpConnection is disconnecting from a remote endpoint and is no longer able to interact with it
    disconnecting
};


class tcp_connection {

public:
    tcp_connection() = delete;

    tcp_connection(boost::asio::io_context& io_context) :
        ctx_(io_context),
        socket_(io_context),
        timer_(io_context),
        host_(),
        port_(0),
        state_(connection_state::disconnected),
        state_change_handler_()
    {}

    tcp_connection(boost::asio::io_context& io_context, std::string host, uint16_t port) :
        ctx_(io_context),
        socket_(io_context),
        timer_(io_context),
        host_(host),
        port_(port),
        state_(connection_state::disconnected),
        state_change_handler_()
    {}

    tcp_connection(const tcp_connection&) = delete;
    tcp_connection& operator=(const tcp_connection&) = delete;

    /**
     * @returns A reference to the internal socket object.
     * 
     * @secton WARNING: Operations performed on this object that are not performed through the TcpConnection interface
     * may prevent the interface from recognizing changes in state.
     */
    tcp::socket& socket() {
        return socket_;
    }

    /**
     * @brief sets the host name or IP address of the remote endpoint.
     * @param host The host name or IP address of the remote endpoint.
     * 
     * @section: The host name is only changed if the interface is not connected. Otherwise it is dropped. The value
     * of host should be compared to a follow-up call to TcpConnection::host if the change is required and the
     * TcpConnection object not being in a connected state cannot be guaranteed.
     */
    void set_host(std::string host) {
        // Don't change the host if a connection is already established
        if(!socket_.is_open()) {
            host_ = host;   
        }
    }


    /**
     * @returns The host name or IP address of the requested remote endpoint.
     */
    std::string host() const {
        return host_;
    }

    /**
     * @brief sets the port number on the remote endpoint.
     * @param host The port number on the remote endpoint.
     * 
     * @section: The port number is only changed if the interface is not connected. Otherwise it is dropped. The value
     * of port should be compared to a follow-up call to TcpConnection::port if the change is required and the
     * TcpConnection object not being in a connected state cannot be guaranteed.
     */
    void set_port(uint16_t port) {
        // Don't change the port if a connection is already established
        if(!socket_.is_open()) {
            port_ = port;   
        }
    }

    /**
     * @returns The port number on the remote endpoint.
     */
    uint16_t port() const {
        return port_;
    }

    /**
     * @brief Sets the amount of time before a blocking call will return.
     * @param ms The timeout interval in milliseconds
     */
    void expires_after(milliseconds ms) {
        timer_.expires_after(ms);
    }

    void expires_never() {
        timer_.expires_never();
    }

    /**
     * @returns The time remaining before the timer expires.
     */
    std::chrono::steady_clock::time_point expires() const {
        return timer_.expires();
    }

    /**
     * @returns Whether the socket is connected.
     */
    bool connected() const {
        return (state_ == connection_state::connected && socket_.is_open());
    }

    /**
     * @brief Sets the function object to call when the TcpConnection object changes state as defined by ConnectionState.
     * @param handler The function oject to call.
     */
    void set_state_change_handler(connection_state_change_handler handler) {
        state_change_handler_ = std::move(handler);
    }

    /**
     * @returns How many bytes can be read without blocking.
     */
    std::tuple<size_t, boost::system::error_code> bytes_available() const {
        boost::system::error_code error;
        size_t size = socket_.available(error);
        return std::make_tuple(size, error);
    }

    /**
     * @brief Make a non-blocking call to resolve the remote endpoint given by host.
     * @param handler The callback to execute once this function is complete.
     */
    awaitable<boost::system::error_code> connect() {
        if(host_.empty() || port_ == 0) {
            co_return boost::asio::error::operation_aborted;
        }
        
        set_state(connection_state::resolving);

        tcp::resolver resolver(socket_.get_executor());
        auto [error, endpoints] = co_await resolver.async_resolve(
            host_,
            std::to_string(port_),
            as_tuple(use_awaitable));

        if (error) {
            co_return error;
        }

        set_state(connection_state::connecting);
        std::tie(error, endpoint_) = co_await asio::async_connect(socket_, endpoints, as_tuple(use_awaitable));
        if(error) {
            set_state(connection_state::disconnected);
        } else {
            set_state(connection_state::connected);
        }
        co_return error;
    }

    /**
     * @brief Disconnects the TcpConnection object making it no longer able to interact with the remote endpoint.
     */
    awaitable<boost::system::error_code> disconnect() {
        boost::system::error_code error;

        if(!socket_.is_open()) {
            co_return boost::asio::error::not_connected;
        }

        set_state(connection_state::disconnecting);
        socket_.shutdown(tcp::socket::shutdown_both, error);
        if(error) {
            co_return error;
        }
        
        socket_.close(error);
        set_state(connection_state::disconnected);
        
        co_return error;
    }

    /**
     * @brief Executes a write to the socket.
     * @param buffer The buffer that contains the data to be written.
     * @returns write_result_t A tuple representing the error during writing and the number of bytes written.
     */
    template <typename bT>
    awaitable<write_result_t> write(const bT& buffer) {
        return asio::async_write(socket_, buffer, as_tuple(use_awaitable));
    }

    /**
     * @brief Executes a write to the socket.
     * @param buffer The buffer that contains the data to be written.
     * @returns write_result_t A tuple representing the error during writing and the number of bytes written.
     */
    template <typename bT>
    awaitable<write_result_t> write(const bT&& buffer) {
        return asio::async_write(socket_, buffer, as_tuple(use_awaitable));
    }

    /**
     * @brief Executes a non-blocking read of the socket. This call will block until a number of bytes equal to buffer.size() has been read.
     * @param buffer The buffer that will contain the result of the read.
     */
    template <typename bT>
    awaitable<read_result_t> read(const bT& buffer) {
        // if no timeout, wait forever
        if(!timer_.pending()) {
            read_result_t result = co_await asio::async_read(socket_, buffer, as_tuple(use_awaitable));
            co_return result;
        }

        std::variant<read_result_t, std::monostate> response = co_await (
            asio::async_read(socket_, buffer, as_tuple(use_awaitable)) ||
            timer_.async_wait()
        );

        if(std::holds_alternative<std::monostate>(response)) {
            co_return std::make_tuple<boost::system::error_code, size_t>(boost::asio::error::timed_out, 0);
        }

        co_return std::get<read_result_t>(response);
    }

    /**
     * @brief Executes a non-blocking read of the socket. This call will block until a number of bytes equal to buffer.size() has been read.
     * @param buffer The buffer that will contain the result of the read.
     */
    template <typename bT>
    awaitable<read_result_t> read(const bT&& buffer) {
        const bT& tempBuffer = buffer;
        return read(tempBuffer);
    }

    /**
     * @brief Executes a nonblocking read of the socket. This call will read all available bytes but may return with fewer bytes than
     * buffer.size().
     * @param buffer The buffer that will contain the result of the read.
     */
    template <typename bT>
    awaitable<read_result_t> read_some( const bT& buffer) {
        return socket_.async_read_some(buffer, as_tuple(use_awaitable));
    }

    /**
     * @brief Executes a nonblocking read of the socket. This call will read all available bytes but may return with fewer bytes than
     * buffer.size().
     * @param buffer The buffer that will contain the result of the read.
     */
    template <typename bT>
    awaitable<read_result_t> read_some( const bT&& buffer) {
        return socket_.async_read_some(buffer, as_tuple(use_awaitable));
    }

private:
    void set_state(connection_state state) {
        if(state_ == state) {
            return;
        }

        state_ = state;
        if(state_change_handler_) {
            ctx_.post(
                bind(
                    state_change_handler_,
                    state_
                )
            );
        }
    }

private:    
    boost::asio::io_context& ctx_;
    tcp::socket socket_;
    detail::timer timer_;
    tcp::endpoint endpoint_;
    std::string host_;
    uint16_t port_;
    connection_state state_;
    connection_state_change_handler state_change_handler_;
};

}