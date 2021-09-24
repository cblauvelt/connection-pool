#pragma once

#include <functional>
#include <tuple>

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/as_tuple.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/write.hpp>

#include <fmt/core.h>

#include "error.hpp"

namespace cpool {

namespace net = boost::asio;
namespace ssl = net::ssl;
using namespace boost::asio::experimental::awaitable_operators;
using namespace std::chrono_literals;

using tcp = net::ip::tcp;
using error_code = boost::system::error_code;
using system_error = boost::system::system_error;
using time_point = typename std::chrono::steady_clock::time_point;
using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;
using boost::asio::experimental::as_tuple;
using std::chrono::milliseconds;
using std::chrono::seconds;
using ssl_socket = ssl::stream<tcp::socket>;

enum class client_connection_state : uint8_t;
enum class server_connection_state : uint8_t;

/**
 * @brief The type that is returned from a read attempt.
 * @param error The error code returned. If there was no error it will return a
 * value of 0 or asio::error::Success.
 * @param bytes_transferred The number of bytes transferred during the read.
 */
using read_result_t = std::tuple<cpool::error, std::size_t>;

/**
 * @brief The type that is returned from a write attempt.
 * @param error The error code returned. If there was no error it will return a
 * value of 0 or asio::error::Success.
 * @param bytes_transferred The number of bytes transferred during the read.
 */
using write_result_t = std::tuple<cpool::error, std::size_t>;

/**
 * @brief The function object that is called whenever the status of the
 * TcpAcceptor object has changed state.
 * @param state The new state of the object as defined by the enum
 * sio::network::ConnectionState.
 */
using connection_state_change_handler =
    std::function<void(const client_connection_state state)>;

namespace detail {
/**
 * @brief The type that is returned from a read attempt.
 * @param error The error code returned. If there was no error it will return a
 * value of 0 or asio::error::Success.
 * @param bytes_transferred The number of bytes transferred during the read.
 */
using asio_read_result_t = std::tuple<boost::system::error_code, std::size_t>;

/**
 * @brief The type that is returned from a write attempt.
 * @param error The error code returned. If there was no error it will return a
 * value of 0 or asio::error::Success.
 * @param bytes_transferred The number of bytes transferred during the read.
 */
using asio_write_result_t = std::tuple<boost::system::error_code, std::size_t>;
} // namespace detail

} // namespace cpool
