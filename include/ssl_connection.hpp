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
#include <boost/asio/ssl.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/as_tuple.hpp>

#include <fmt/core.h>

#include "types.hpp"
#include "timer.hpp"
#include "error.hpp"
#include "client_state.hpp"

namespace cpool {

namespace asio = boost::asio;
namespace ssl = boost::asio::ssl;

using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;
using boost::asio::ip::tcp;
using namespace boost::asio::experimental::awaitable_operators;
using boost::asio::experimental::as_tuple;
using milliseconds = std::chrono::milliseconds;
using boost::asio::ip::tcp;
using ssl_socket = ssl::stream<tcp::socket>;

struct ssl_options {
    /// Adds the Server Name Indication extension; https://en.wikipedia.org/wiki/Server_Name_Indication
    bool sni = false;
};

constexpr ssl_options default_ssl_options{false};

class ssl_connection {

public:
    ssl_connection() = delete;

    ssl_connection(asio::io_context& io_context, ssl::context& ssl_ctx) :
        ctx_(io_context),
        stream_(io_context, ssl_ctx),
        timer_(io_context),
        host_(),
        port_(0),
        ssl_options_(default_ssl_options),
        state_(client_connection_state::disconnected),
        state_change_handler_()
    {}

    ssl_connection(asio::io_context& io_context, ssl::context& ssl_ctx, std::string host, uint16_t port, ssl_options options=default_ssl_options) :
        ctx_(io_context),
        stream_(io_context, ssl_ctx),
        timer_(io_context),
        host_(host),
        port_(port),
        ssl_options_(options),
        state_(client_connection_state::disconnected),
        state_change_handler_()
    {}

    ssl_connection(const ssl_connection&) = delete;
    ssl_connection& operator=(const ssl_connection&) = delete;

    /**
     * @brief Returns the executor context for the connection
     * 
     * @returns The executor context for the connection
     */
    asio::io_context& get_context() {
        return ctx_;
    }

    /**
     * @returns A reference to the internal socket object.
     * 
     * @secton WARNING: Operations performed on this object that are not performed through the TcpConnection interface
     * may prevent the interface from recognizing changes in state.
     */
    ssl_socket& stream() {
        return stream_;
    }

    /**
     * @brief sets the host name or IP address of the remote endpoint.
     * @param host The host name or IP address of the remote endpoint.
     * 
     * @section: The host name is only changed if the interface is not connected. Otherwise it is dropped. The value
     * of host should be compared to a follow-up call to TcpConnection::host if the change is required and the
     * TcpConnection object not being in a connected state cannot be guaranteed.
     */
    error set_host(std::string host) {
        // Don't change the host if a connection is already established
        if(connected()) {
            return error("Cannot change host once connected");
        }
        
        host_ = host;
        return no_error;
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
    error set_port(uint16_t port) {
        // Don't change the port if a connection is already established
        if(connected()) {
            return error("Cannot change port once connected");
        }

        port_ = port;   
        return no_error;
    }

    /**
     * @returns The port number on the remote endpoint.
     */
    uint16_t port() const {
        return port_;
    }

    /**
     * @brief Returns the current state of the connection
     * 
     * @return client_connection_state An enum representing the current state of the connection
     */
    client_connection_state state() const {
        return state_;
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
        return (state_ == client_connection_state::connected && stream_.lowest_layer().is_open());
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
        size_t size = stream_.lowest_layer().available(error);
        return std::make_tuple(size, error);
    }

    /**
     * @brief Make a non-blocking call to resolve the remote endpoint given by host.
     * @param handler The callback to execute once this function is complete.
     */
    awaitable<cpool::error> connect() {
        if(host_.empty() || port_ == 0) {
            co_return cpool::error(boost::asio::error::operation_aborted, fmt::format("Host or port have not been set"));
        }
        
        set_state(client_connection_state::resolving);

        tcp::resolver resolver(stream_.get_executor());
        auto [err, endpoints] = co_await resolver.async_resolve(
            host_,
            std::to_string(port_),
            as_tuple(use_awaitable));

        if (err) {
            co_return cpool::error(err, fmt::format("Could not resolve host {0}; {1}", host_, err.message()));
        }

        set_state(client_connection_state::connecting);

        // Set SNI Hostname (many hosts need this to handshake successfully)
        if(ssl_options_.sni && ! SSL_set_tlsext_host_name(stream_.native_handle(), host_.c_str()))
        {
            boost::system::error_code err = {static_cast<int>(::ERR_get_error()), asio::error::get_ssl_category()};
            co_return err;
        }

        std::tie(err, endpoint_) = co_await asio::async_connect(stream_.lowest_layer(), endpoints, as_tuple(use_awaitable));
        if(err) {
            set_state(client_connection_state::disconnected);
            co_return cpool::error(err, fmt::format("Could not connect to host {0}; {1}", host_, err.message()));
        }

        stream_.lowest_layer().set_option(tcp::no_delay(true));

        // complete handshake
        co_await stream_.async_handshake(ssl::stream_base::client, use_awaitable);

        set_state(client_connection_state::connected);
        
        co_return cpool::no_error;
    }

    /**
     * @brief Disconnects the ssl connection object making it no longer able to interact with the remote endpoint.
     */
    awaitable<error> disconnect() {
        boost::system::error_code err;

        if(!stream_.lowest_layer().is_open()) {
            co_return error(boost::asio::error::not_connected, "not connected");
        }

        set_state(client_connection_state::disconnecting);
        stream_.shutdown(err);
        if(err == asio::error::eof) {
            // Rationale:
            // http://stackoverflow.com/questions/25587403/boost-asio-ssl-async-shutdown-always-finishes-with-an-error
            err = {};
        }
        
        set_state(client_connection_state::disconnected);
        
        co_return err;
    }

    /**
     * @brief Executes a write to the socket.
     * @param buffer The buffer that contains the data to be written.
     * @returns write_result_t A tuple representing the error during writing and the number of bytes written.
     */
    template <typename bT>
    awaitable<write_result_t> write(const bT& buffer) {
        // if no timeout, wait forever
        if(!timer_.pending()) {
            auto [err, bytes_written] = co_await asio::async_write(stream_, buffer, as_tuple(use_awaitable));
            co_return std::make_tuple(error(err), bytes_written);
        }

        std::variant<detail::asio_write_result_t, std::monostate> response = co_await (
            asio::async_write(stream_, buffer, as_tuple(use_awaitable)) ||
            timer_.async_wait()
        );

        if(std::holds_alternative<std::monostate>(response)) {
            co_return std::make_tuple(error((int)boost::asio::error::timed_out, "timed out"), 0);
        }

        auto [err, bytes_read] = std::get<detail::asio_write_result_t>(response);
        if(error_means_client_disconnected(err)) {
            set_state(client_connection_state::disconnected);
        }
        co_return std::make_tuple(error(err), bytes_read);
    }

    /**
     * @brief Executes a nonblocking read of the socket. This call will read all available bytes but may return with fewer bytes than
     * buffer.size().
     * @param buffer The buffer that will contain the result of the read.
     */
    template <typename bT>
    awaitable<read_result_t> read_some( const bT& buffer) {
        auto [err, bytes_read] = co_await stream_.async_read_some(buffer, as_tuple(use_awaitable));
        if(error_means_client_disconnected(err)) {
            set_state(client_connection_state::disconnected);
        }
        co_return std::make_tuple(error(err), bytes_read);
    }

    /**
     * @brief Executes a nonblocking read of the socket. This call will read all available bytes but may return with fewer bytes than
     * buffer.size().
     * @param buffer The buffer that will contain the result of the read.
     */
    template <typename bT>
    awaitable<read_result_t> read_some( const bT&& buffer) {
        auto [err, bytes_read] = co_await stream_.async_read_some(buffer, as_tuple(use_awaitable));
        if(error_means_client_disconnected(err)) {
            set_state(client_connection_state::disconnected);
        }
        co_return std::make_tuple(error(err), bytes_read);
    }

private:
    void set_state(client_connection_state state) {
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
    asio::io_context& ctx_;
    ssl_socket stream_;
    detail::timer timer_;
    tcp::endpoint endpoint_;
    std::string host_;
    uint16_t port_;
    ssl_options ssl_options_;
    client_connection_state state_;
    connection_state_change_handler state_change_handler_;
};

}