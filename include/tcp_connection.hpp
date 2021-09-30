#pragma once

#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

#include "client_state.hpp"
#include "error.hpp"
#include "timer.hpp"
#include "types.hpp"

namespace cpool {

class tcp_connection {

  public:
    tcp_connection() = delete;

    tcp_connection(net::any_io_executor exec)
        : socket_(exec)
        , timer_(exec)
        , host_()
        , port_(0)
        , state_(client_connection_state::disconnected)
        , state_change_handler_() {}

    tcp_connection(net::any_io_executor exec, std::string host, uint16_t port)
        : socket_(exec)
        , timer_(exec)
        , host_(host)
        , port_(port)
        , state_(client_connection_state::disconnected)
        , state_change_handler_() {}

    tcp_connection(const tcp_connection&) = delete;
    tcp_connection& operator=(const tcp_connection&) = delete;

    ~tcp_connection() {
        if (!socket_.is_open()) {
            return;
        }

        error_code err;
        socket_.close(err);
    }

    /**
     * @brief Returns the executor context for the connection
     *
     * @returns The executor context for the connection
     */
    net::any_io_executor get_executor() { return timer_.get_executor(); }

    /**
     * @returns A reference to the internal socket object.
     *
     * @secton WARNING: Operations performed on this object that are not
     * performed through the tcp_connection interface may prevent the interface
     * from recognizing changes in state.
     */
    tcp::socket& socket() { return socket_; }

    /**
     * @brief sets the host name or IP address of the remote endpoint.
     * @param host The host name or IP address of the remote endpoint.
     *
     * @section: The host name is only changed if the interface is not
     * connected. Otherwise it is dropped. The value of host should be compared
     * to a follow-up call to tcp_connection::host if the change is required and
     * the tcp_connection object not being in a connected state cannot be
     * guaranteed.
     */
    error set_host(std::string host) {
        // Don't change the host if a connection is already established
        if (connected()) {
            return error("Cannot change host once connected");
        }

        host_ = host;
        return no_error;
    }

    /**
     * @returns The host name or IP address of the requested remote endpoint.
     */
    std::string host() const { return host_; }

    /**
     * @brief sets the port number on the remote endpoint.
     * @param host The port number on the remote endpoint.
     *
     * @section: The port number is only changed if the interface is not
     * connected. Otherwise it is dropped. The value of port should be compared
     * to a follow-up call to tcp_connection::port if the change is required and
     * the tcp_connection object not being in a connected state cannot be
     * guaranteed.
     */
    error set_port(uint16_t port) {
        // Don't change the port if a connection is already established
        if (connected()) {
            return error("Cannot change port once connected");
        }

        port_ = port;
        return no_error;
    }

    /**
     * @returns The port number on the remote endpoint.
     */
    uint16_t port() const { return port_; }

    /**
     * @brief Returns the current state of the connection
     *
     * @return client_connection_state An enum representing the current state of
     * the connection
     */
    client_connection_state state() const { return state_; }

    /**
     * @brief Sets the amount of time before a blocking call will return.
     * @param ms The timeout interval in milliseconds
     */
    void expires_after(milliseconds ms) { timer_.expires_after(ms); }

    void expires_never() { timer_.expires_never(); }

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
        return (state_ == client_connection_state::connected &&
                socket_.is_open());
    }

    /**
     * @brief Sets the function object to call when the tcp_connection object
     * changes state as defined by ConnectionState.
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
     * @brief Make a non-blocking call to resolve the remote endpoint given by
     * host and connect to the remote endpoint
     */
    [[nodiscard]] awaitable<cpool::error> async_connect() {
        if (host_.empty() || port_ == 0) {
            co_return cpool::error(
                net::error::operation_aborted,
                fmt::format("Host or port have not been set"));
        }

        set_state(client_connection_state::resolving);

        tcp::resolver resolver(socket_.get_executor());
        auto [err, endpoints] = co_await resolver.async_resolve(
            host_, std::to_string(port_), as_tuple(use_awaitable));

        if (err) {
            co_return cpool::error(
                err, fmt::format("Could not resolve host {0}; {1}", host_,
                                 err.message()));
        }

        set_state(client_connection_state::connecting);
        std::tie(err, endpoint_) = co_await asio::async_connect(
            socket_, endpoints, as_tuple(use_awaitable));
        if (err) {
            set_state(client_connection_state::disconnected);
            co_return cpool::error(
                err, fmt::format("Could not connect to host {0}; {1}", host_,
                                 err.message()));
        } else {
            set_state(client_connection_state::connected);
        }

        co_return cpool::no_error;
    }

    /**
     * @brief Disconnects the tcp_connection object making it no longer able to
     * interact with the remote endpoint.
     */
    [[nodiscard]] awaitable<error> async_disconnect() {
        boost::system::error_code err;

        if (!socket_.is_open()) {
            co_return error(net::error::not_connected, "not connected");
        }

        set_state(client_connection_state::disconnecting);
        socket_.shutdown(tcp::socket::shutdown_both, err);
        socket_.close(err);
        set_state(client_connection_state::disconnected);

        co_return err;
    }

    /**
     * @brief Disconnects the tcp_connection object making it no longer able to
     * interact with the remote endpoint.
     */
    error disconnect() {
        boost::system::error_code err;

        if (!socket_.is_open()) {
            return error(net::error::not_connected, "not connected");
        }

        set_state(client_connection_state::disconnecting);
        socket_.shutdown(tcp::socket::shutdown_both, err);
        socket_.close(err);
        set_state(client_connection_state::disconnected);

        return error(err);
    }

    /**
     * @brief Executes a write to the socket.
     * @param buffer The buffer that contains the data to be written.
     * @returns write_result_t A tuple representing the error during writing and
     * the number of bytes written.
     */
    template <typename bT>
    [[nodiscard]] awaitable<write_result_t> async_write(const bT& buffer) {
        // if no timeout, wait forever
        if (!timer_.pending()) {
            auto [err, bytes_written] = co_await asio::async_write(
                socket_, buffer, as_tuple(use_awaitable));
            if (error_means_client_disconnected(err)) {
                set_state(client_connection_state::disconnected);
            }
            co_return std::make_tuple(error(err), bytes_written);
        }

        std::variant<detail::asio_write_result_t, std::monostate> response =
            co_await(
                asio::async_write(socket_, buffer, as_tuple(use_awaitable)) ||
                timer_.async_wait());

        if (std::holds_alternative<std::monostate>(response)) {
            co_return std::make_tuple(
                error((int)net::error::timed_out, "timed out"), 0);
        }

        auto [err, bytes_read] =
            std::get<detail::asio_write_result_t>(response);
        if (error_means_client_disconnected(err)) {
            set_state(client_connection_state::disconnected);
        }
        co_return std::make_tuple(error(err), bytes_read);
    }

    /**
     * @brief Executes a write to the socket.
     * @param buffer The buffer that contains the data to be written.
     * @returns write_result_t A tuple representing the error during writing and
     * the number of bytes written.
     */
    template <typename bT>
    [[nodiscard]] awaitable<write_result_t> async_write(const bT&& buffer) {
        // if no timeout, wait forever
        if (!timer_.pending()) {
            auto [err, bytes_written] = co_await asio::async_write(
                socket_, buffer, as_tuple(use_awaitable));
            if (error_means_client_disconnected(err)) {
                set_state(client_connection_state::disconnected);
            }
            co_return std::make_tuple(error(err), bytes_written);
        }

        std::variant<detail::asio_write_result_t, std::monostate> response =
            co_await(
                asio::async_write(socket_, buffer, as_tuple(use_awaitable)) ||
                timer_.async_wait());

        if (std::holds_alternative<std::monostate>(response)) {
            co_return std::make_tuple(
                error((int)net::error::timed_out, "timed out"), 0);
        }

        auto [err, bytes_read] =
            std::get<detail::asio_write_result_t>(response);
        if (error_means_client_disconnected(err)) {
            set_state(client_connection_state::disconnected);
        }
        co_return std::make_tuple(error(err), bytes_read);
    }

    /**
     * @brief Executes a blocking read of the socket. This call will block until
     * a number of bytes equal to buffer.size() has been read.
     * @param buffer The buffer that will contain the result of the read.
     */
    template <typename bT>
    [[nodiscard]] awaitable<read_result_t> async_read(const bT& buffer) {
        // if no timeout, wait forever
        if (!timer_.pending()) {
            auto [err, bytes_read] = co_await asio::async_read(
                socket_, buffer, as_tuple(use_awaitable));
            if (error_means_client_disconnected(err)) {
                set_state(client_connection_state::disconnected);
            }
            co_return std::make_tuple(error(err), bytes_read);
        }

        std::variant<detail::asio_read_result_t, std::monostate> response =
            co_await(
                asio::async_read(socket_, buffer, as_tuple(use_awaitable)) ||
                timer_.async_wait());

        if (std::holds_alternative<std::monostate>(response)) {
            co_return std::make_tuple(
                error((int)net::error::timed_out, "timed out"), 0);
        }

        auto [err, bytes_read] = std::get<detail::asio_read_result_t>(response);
        if (error_means_client_disconnected(err)) {
            set_state(client_connection_state::disconnected);
        }
        co_return std::make_tuple(error(err), bytes_read);
    }

    /**
     * @brief Executes a blocking read of the socket. This call will block until
     * a number of bytes equal to buffer.size() has been read.
     * @param buffer The buffer that will contain the result of the read.
     */
    template <typename bT>
    [[nodiscard]] awaitable<read_result_t> async_read(const bT&& buffer) {
        // if no timeout, wait forever
        if (!timer_.pending()) {
            auto [err, bytes_read] = co_await asio::async_read(
                socket_, buffer, as_tuple(use_awaitable));
            if (error_means_client_disconnected(err)) {
                set_state(client_connection_state::disconnected);
            }
            co_return std::make_tuple(error(err), bytes_read);
        }

        std::variant<detail::asio_read_result_t, std::monostate> response =
            co_await(
                asio::async_read(socket_, buffer, as_tuple(use_awaitable)) ||
                timer_.async_wait());

        if (std::holds_alternative<std::monostate>(response)) {
            co_return std::make_tuple(
                error((int)net::error::timed_out, "timed out"), 0);
        }

        auto [err, bytes_read] = std::get<detail::asio_read_result_t>(response);
        if (error_means_client_disconnected(err)) {
            set_state(client_connection_state::disconnected);
        }
        co_return std::make_tuple(error(err), bytes_read);
    }

    /**
     * @brief Executes a nonblocking read of the socket. This call will read all
     * available bytes but may return with fewer bytes than buffer.size().
     * @param buffer The buffer that will contain the result of the read.
     */
    template <typename bT>
    [[nodiscard]] awaitable<read_result_t> async_read_some(const bT& buffer) {
        auto [err, bytes_read] =
            co_await socket_.async_read_some(buffer, as_tuple(use_awaitable));
        if (error_means_client_disconnected(err)) {
            set_state(client_connection_state::disconnected);
        }
        co_return std::make_tuple(error(err), bytes_read);
    }

    /**
     * @brief Executes a nonblocking read of the socket. This call will read all
     * available bytes but may return with fewer bytes than buffer.size().
     * @param buffer The buffer that will contain the result of the read.
     */
    template <typename bT>
    [[nodiscard]] awaitable<read_result_t> async_read_some(const bT&& buffer) {
        auto [err, bytes_read] =
            co_await socket_.async_read_some(buffer, as_tuple(use_awaitable));
        if (error_means_client_disconnected(err)) {
            set_state(client_connection_state::disconnected);
        }
        co_return std::make_tuple(error(err), bytes_read);
    }

  private:
    void set_state(client_connection_state state) {
        if (state_ == state) {
            return;
        }

        state_ = state;
        if (state_change_handler_) {
            auto executor = timer_.get_executor();
            co_spawn(executor, bind(state_change_handler_, state_), detached);
        }
    }

  private:
    tcp::socket socket_;
    timer timer_;
    tcp::endpoint endpoint_;
    std::string host_;
    uint16_t port_;
    client_connection_state state_;
    connection_state_change_handler state_change_handler_;
};

} // namespace cpool