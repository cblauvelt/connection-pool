#pragma once

#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

#include <fmt/format.h>

#include "cpool/client_state.hpp"
#include "cpool/condition_variable.hpp"
#include "cpool/timer.hpp"
#include "cpool/types.hpp"

namespace cpool {

class tcp_connection {

  public:
    tcp_connection() = delete;

    tcp_connection(net::any_io_executor exec)
        : socket_(exec)
        , timer_(exec)
        , host_()
        , port_(0)
        , state_cv_(exec)
        , state_(client_connection_state::disconnected)
        , state_change_handler_()
        , error_condition_() {}

    tcp_connection(net::any_io_executor exec, std::string host, uint16_t port)
        : socket_(exec)
        , timer_(exec)
        , host_(host)
        , port_(port)
        , state_cv_(exec)
        , state_(client_connection_state::disconnected)
        , state_change_handler_()
        , error_condition_() {}

    tcp_connection(const tcp_connection&) = delete;
    tcp_connection& operator=(const tcp_connection&) = delete;

    ~tcp_connection() {
        CPOOL_TRACE_LOG("TCP", "[{0}] destroying connection",
                        static_cast<void*>(this));

        error_code err;
        socket_.shutdown(tcp::socket::shutdown_both, err);
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
        return error();
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
        return error();
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
     * @brief Waits until the connection state is state
     *
     * @param state The state that this function will block until the states are
     * equal
     */
    awaitable<void> wait_for(client_connection_state state) {
        co_await state_cv_.async_wait([&]() { return state_ == state; });
    }

    /**
     * @brief Sets the amount of time before a blocking call will return.
     * @param ms The timeout interval in milliseconds
     */
    void expires_after(milliseconds ms) { timer_.expires_after(ms); }

    /**
     * @brief Resets the time and cancels all tasks waiting on the timer.
     *
     */
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
    void set_state_change_handler(
        connection_state_change_handler<tcp_connection> handler) {
        state_change_handler_ = std::move(handler);
    }

    /**
     * @brief error_condition If a set_state_change_handler returns an error
     * condition, error_condition will be an error.
     * @returns The most recent error.
     *
     */
    error error_condition() const { return error_condition_; }

    /**
     * @returns How many bytes can be read without blocking.
     */
    std::tuple<size_t, boost::system::error_code> bytes_available() const {
        boost::system::error_code error;
        size_t size = socket_.available(error);
        return std::make_tuple(size, error);
    }

    /**
     * @brief Make a non-blocking call to resolve the remote endpoint given
     * by host and connect to the remote endpoint
     */
    [[nodiscard]] awaitable<cpool::error> async_connect() {
        if (host_.empty() || port_ == 0) {
            co_return cpool::error(
                asio::error::make_error_code(net::error::operation_aborted),
                "Host or port have not been set");
        }

        co_await set_state(client_connection_state::resolving);

        tcp::resolver resolver(socket_.get_executor());
        auto [err, endpoints] = co_await resolver.async_resolve(
            host_, std::to_string(port_), as_tuple(use_awaitable));

        if (err) {
            co_return cpool::error(
                err, fmt::format("Could not resolve host {0}; {1}", host_,
                                 err.message()));
        }

#ifdef CPOOL_CPOOL_TRACE_LOGGING
        CPOOL_TRACE_LOG("TCP", "connecting to one of: ");
        for (auto& endpoint_entry : endpoints) {
            CPOOL_TRACE_LOG("TCP", "{}",
                            endpoint_entry.endpoint().address().to_string());
        }
#endif

        co_await set_state(client_connection_state::connecting);
        std::tie(err, endpoint_) = co_await asio::async_connect(
            socket_, endpoints, as_tuple(use_awaitable));
        if (err) {
            co_await set_state(client_connection_state::disconnected);
            co_return cpool::error(
                err, fmt::format("could not connect to host {0}", host_));
        }

        co_await set_state(client_connection_state::connected);
        co_return cpool::error();
    }

    /**
     * @brief Disconnects the tcp_connection object making it no longer able
     * to interact with the remote endpoint.
     */
    [[nodiscard]] awaitable<error> async_disconnect() {
        boost::system::error_code err;

        if (!socket_.is_open()) {
            co_return error(
                asio::error::make_error_code(net::error::not_connected));
        }

        co_await set_state(client_connection_state::disconnecting);
        socket_.shutdown(tcp::socket::shutdown_both, err);
        socket_.close(err);
        co_await set_state(client_connection_state::disconnected);

        co_return err;
    }

    /**
     * @brief Executes a write to the socket.
     * @param buffer The buffer that contains the data to be written.
     * @returns write_result_t A tuple representing the error during writing
     * and the number of bytes written.
     */
    template <typename bT>
    [[nodiscard]] awaitable<write_result_t> async_write(const bT& buffer) {
        return async_write(std::move(buffer));
    }

    /**
     * @brief Executes a write to the socket.
     * @param buffer The buffer that contains the data to be written.
     * @returns write_result_t A tuple representing the error during writing
     * and the number of bytes written.
     */
    template <typename bT>
    [[nodiscard]] awaitable<write_result_t> async_write(const bT&& buffer) {
        if (!connected()) {
            co_return write_result_t(
                net::error::make_error_code(net::error::not_connected), 0);
        }
        // if no timeout, wait forever
        if (!timer_.pending()) {
            CPOOL_TRACE_LOG("TCP", "[{}] No timeout pending for write",
                            static_cast<void*>(this));
            auto [err, bytes_written] = co_await asio::async_write(
                socket_, buffer, as_tuple(use_awaitable));
            // CPOOL_TRACE_LOG("TCP", "error writing: {}",
            // cpool::error(err).message())
            if (error_means_client_disconnected(err)) {
                co_await set_state(client_connection_state::disconnected);
                co_return write_result_t(
                    net::error::make_error_code(net::error::not_connected),
                    bytes_written);
            }
            co_return write_result_t(err, bytes_written);
        }

        CPOOL_TRACE_LOG(
            "TCP", "[{}] Timeout remaining for write: {}",
            static_cast<void*>(this),
            (timer_.expires() - std::chrono::steady_clock::now()).count());

        std::variant<detail::asio_write_result_t, std::monostate> response =
            co_await(
                asio::async_write(socket_, buffer, as_tuple(use_awaitable)) ||
                timer_.async_wait());

        if (std::holds_alternative<std::monostate>(response)) {
            co_return write_result_t(
                error(asio::error::make_error_code(net::error::timed_out)), 0);
        }

        auto [err, bytes_written] =
            std::get<detail::asio_write_result_t>(response);
        if (error_means_client_disconnected(err)) {
            co_await set_state(client_connection_state::disconnected);
            co_return write_result_t(
                net::error::make_error_code(net::error::not_connected),
                bytes_written);
        }
        co_return write_result_t(err, bytes_written);
    }

    /**
     * @brief Executes a blocking read of the socket. This call will block
     * until a number of bytes equal to buffer.size() has been read.
     * @param buffer The buffer that will contain the result of the read.
     */
    template <typename bT>
    [[nodiscard]] awaitable<read_result_t> async_read(const bT& buffer) {
        return async_read(std::move(buffer));
        // // if no timeout, wait forever
        // if (!timer_.pending()) {
        //     auto [err, bytes_read] = co_await asio::async_read(
        //         socket_, buffer, as_tuple(use_awaitable));
        //     if (error_means_client_disconnected(err)) {
        //         co_await set_state(client_connection_state::disconnected);
        //         co_return std::make_tuple(net::error::not_connected,
        //                                   bytes_read);
        //     }
        //     co_return std::make_tuple(err, bytes_read);
        // }

        // std::variant<detail::asio_read_result_t, std::monostate> response =
        //     co_await(
        //         asio::async_read(socket_, buffer, as_tuple(use_awaitable)) ||
        //         timer_.async_wait());

        // if (std::holds_alternative<std::monostate>(response)) {
        //     co_return std::make_tuple(
        //         error(asio::error::make_error_code(net::error::timed_out)),
        //         0);
        // }

        // auto [err, bytes_read] =
        // std::get<detail::asio_read_result_t>(response); if
        // (error_means_client_disconnected(err)) {
        //     co_await set_state(client_connection_state::disconnected);
        //     co_return std::make_tuple(net::error::not_connected, bytes_read);
        // }
        // co_return std::make_tuple(err, bytes_read);
    }

    /**
     * @brief Executes a blocking read of the socket. This call will block
     * until a number of bytes equal to buffer.size() has been read.
     * @param buffer The buffer that will contain the result of the read.
     */
    template <typename bT>
    [[nodiscard]] awaitable<read_result_t> async_read(const bT&& buffer) {
        if (!connected()) {
            co_return read_result_t(
                net::error::make_error_code(net::error::not_connected), 0);
        }

        // if no timeout, wait forever
        if (!timer_.pending()) {
            auto [err, bytes_read] = co_await asio::async_read(
                socket_, buffer, as_tuple(use_awaitable));
            if (error_means_client_disconnected(err)) {
                co_await set_state(client_connection_state::disconnected);
            }
            co_return read_result_t(err, bytes_read);
        }

        std::variant<detail::asio_read_result_t, std::monostate> response =
            co_await(
                asio::async_read(socket_, buffer, as_tuple(use_awaitable)) ||
                timer_.async_wait());

        if (std::holds_alternative<std::monostate>(response)) {
            co_return read_result_t(
                error(asio::error::make_error_code(net::error::timed_out)), 0);
        }

        auto [err, bytes_read] = std::get<detail::asio_read_result_t>(response);
        if (error_means_client_disconnected(err)) {
            co_await set_state(client_connection_state::disconnected);
            co_return read_result_t(
                net::error::make_error_code(net::error::not_connected),
                bytes_read);
        }
        co_return read_result_t(err, bytes_read);
    }

    /**
     * @brief Executes a nonblocking read of the socket. This call will read
     * all available bytes but may return with fewer bytes than
     * buffer.size().
     * @param buffer The buffer that will contain the result of the read.
     */
    template <typename bT>
    [[nodiscard]] awaitable<read_result_t> async_read_some(const bT& buffer) {
        return async_read_some(std::move(buffer));
        // auto [err, bytes_read] =
        //     co_await socket_.async_read_some(buffer,
        //     as_tuple(use_awaitable));
        // if (error_means_client_disconnected(err)) {
        //     co_await set_state(client_connection_state::disconnected);
        //     co_return std::make_tuple(net::error::not_connected, bytes_read);
        // }
        // co_return std::make_tuple(err, bytes_read);
    }

    /**
     * @brief Executes a nonblocking read of the socket. This call will read
     * all available bytes but may return with fewer bytes than
     * buffer.size().
     * @param buffer The buffer that will contain the result of the read.
     */
    template <typename bT>
    [[nodiscard]] awaitable<read_result_t> async_read_some(const bT&& buffer) {
        if (!connected()) {
            co_return read_result_t(
                net::error::make_error_code(net::error::not_connected), 0);
        }

        auto [err, bytes_read] =
            co_await socket_.async_read_some(buffer, as_tuple(use_awaitable));
        if (error_means_client_disconnected(err)) {
            co_await set_state(client_connection_state::disconnected);
            co_return read_result_t(
                net::error::make_error_code(net::error::not_connected),
                bytes_read);
        }
        co_return read_result_t(err, bytes_read);
    }

    /**
     * @brief Closes the stream and cancels all in flight operations
     *
     */
    [[nodiscard]] awaitable<void> stop() {
        co_await set_state(client_connection_state::disconnecting);
        error_code ignored_err;
        socket_.cancel(ignored_err);
        co_await set_state(client_connection_state::disconnected);

        co_return;
    }

  private:
    [[nodiscard]] awaitable<void> set_state(client_connection_state state) {
        if (state_ == state) {
            co_return;
        }

        state_ = state;

        if (state_change_handler_) {
            error_condition_ = co_await state_change_handler_(this, state_);
        }

        state_cv_.notify_all();

        co_return;
    }

  private:
    tcp::socket socket_;
    timer timer_;
    tcp::endpoint endpoint_;
    std::string host_;
    uint16_t port_;
    condition_variable state_cv_;
    client_connection_state state_;
    connection_state_change_handler<tcp_connection> state_change_handler_;
    error error_condition_;
};

} // namespace cpool