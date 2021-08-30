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
        mContext(io_context),
        mSocket(io_context),
        mTimer(io_context),
        mTimeout(defaultTimeout),
        mHost(),
        mPort(0),
        mState(connection_state::disconnected),
        mStateChangeHandler()
    {}

    tcp_connection(boost::asio::io_context& io_context, std::string host, uint16_t port) :
        mContext(io_context),
        mSocket(io_context),
        mTimer(io_context),
        mTimeout(defaultTimeout),
        mHost(host),
        mPort(port),
        mState(connection_state::disconnected),
        mStateChangeHandler()
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
        return mSocket;
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
        if(!mSocket.is_open()) {
            mHost = host;   
        }
    }


    /**
     * @returns The host name or IP address of the requested remote endpoint.
     */
    std::string host() const {
        return mHost;
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
        if(!mSocket.is_open()) {
            mPort = port;   
        }
    }

    /**
     * @returns The port number on the remote endpoint.
     */
    uint16_t port() const {
        return mPort;
    }

    /**
     * @brief Sets the timeout interval in milliseconds for connection requests. The default is 10 s.
     * @param milliseconds The timeout interval in milliseconds
     */
    void set_timeout(uint32_t milliseconds) {
        mTimeout = milliseconds;
    }

    /**
     * @returns The connection timeout interval in milliseconds.
     */
    uint32_t timeout() const {
        return mTimeout;
    }

    /**
     * @returns Whether the socket is connected.
     */
    bool connected() const {
        return (mState == connection_state::connected && mSocket.is_open());
    }

    /**
     * @brief Sets the function object to call when the TcpConnection object changes state as defined by ConnectionState.
     * @param handler The function oject to call.
     */
    void set_state_change_handler(connection_state_change_handler handler) {
        mStateChangeHandler = std::move(handler);
    }

    /**
     * @returns How many bytes can be read without blocking.
     */
    std::tuple<size_t, boost::system::error_code> bytesAvailable() const {
        boost::system::error_code error;
        size_t size = mSocket.available(error);
        return std::make_tuple(size, error);
    }

    /**
     * @brief Make a non-blocking call to resolve the remote endpoint given by host.
     * @param handler The callback to execute once this function is complete.
     */
    awaitable<boost::system::error_code> connect() {
        if(mHost.empty() || mPort == 0) {
            // TODO: send a better error
            co_return boost::asio::error::eof;
        }
        
        set_state(connection_state::resolving);

        tcp::resolver resolver(mSocket.get_executor());
        auto [error, endpoints] = co_await resolver.async_resolve(
            mHost,
            std::to_string(mPort),
            as_tuple(use_awaitable));

        if (error) {
            co_return error;
        }

        std::tie(error, mEndpoint) = co_await asio::async_connect(mSocket, endpoints, as_tuple(use_awaitable));
        co_return error;
    }

    /**
     * @brief Disconnects the TcpConnection object making it no longer able to interact with the remote endpoint.
     */
    awaitable<boost::system::error_code>  disconnect() {
        if(!mSocket.is_open()) {
            co_return boost::asio::error::eof;
        }

        set_state(connection_state::disconnecting);
        boost::system::error_code error;
        mSocket.shutdown(tcp::socket::shutdown_both, error);
        if(error) {
            co_return error;
        }
        
        mSocket.close(error);
        if(error) {
            co_return error;
        }

        set_state(connection_state::disconnected);
    }

    /**
     * @brief Executes a write to the socket.
     * @param buffer The buffer that contains the data to be written.
     * @returns write_result_t A tuple representing the error during writing and the number of bytes written.
     */
    template <typename bT>
    awaitable<write_result_t> write(const bT& buffer) {
        return asio::async_write(mSocket, buffer, as_tuple(use_awaitable));
    }

    /**
     * @brief Executes a write to the socket.
     * @param buffer The buffer that contains the data to be written.
     * @returns write_result_t A tuple representing the error during writing and the number of bytes written.
     */
    template <typename bT>
    awaitable<write_result_t> write(const bT&& buffer) {
        return asio::async_write(mSocket, buffer, as_tuple(use_awaitable));
    }

    /**
     * @brief Executes a non-blocking read of the socket. This call will block until a number of bytes equal to buffer.size() has been read.
     * @param buffer The buffer that will contain the result of the read.
     */
    template <typename bT>
    awaitable<read_result_t> read(const bT& buffer) {
        return asio::async_read(mSocket, buffer, as_tuple(use_awaitable));
    }

    /**
     * @brief Executes a non-blocking read of the socket. This call will block until a number of bytes equal to buffer.size() has been read.
     * @param buffer The buffer that will contain the result of the read.
     */
    template <typename bT>
    awaitable<read_result_t> read(const bT&& buffer) {
        return asio::async_read(mSocket, buffer, as_tuple(use_awaitable));
    }

    /**
     * @brief Executes a nonblocking read of the socket. This call will read all available bytes but may return with fewer bytes than
     * buffer.size().
     * @param buffer The buffer that will contain the result of the read.
     */
    template <typename bT>
    awaitable<read_result_t> read_some( const bT& buffer) {
        return mSocket.async_read_some(buffer, as_tuple(use_awaitable));
    }

    /**
     * @brief Executes a nonblocking read of the socket. This call will read all available bytes but may return with fewer bytes than
     * buffer.size().
     * @param buffer The buffer that will contain the result of the read.
     */
    template <typename bT>
    awaitable<read_result_t> read_some( const bT&& buffer) {
        return mSocket.async_read_some(buffer, as_tuple(use_awaitable));
    }

private:
    void set_state(connection_state state) {
        if(mState == state) {
            return;
        }

        mState = state;
        if(mStateChangeHandler) {
            mContext.post(
                bind(
                    mStateChangeHandler,
                    mState
                )
            );
        }
    }

private:
    constexpr static uint32_t defaultTimeout = 10000;
    
    boost::asio::io_context& mContext;
    tcp::socket mSocket;
    asio::steady_timer mTimer;
    tcp::endpoint mEndpoint;
    std::atomic<uint32_t> mTimeout;
    std::string mHost;
    uint16_t mPort;
    connection_state mState;
    connection_state_change_handler mStateChangeHandler;
};

}