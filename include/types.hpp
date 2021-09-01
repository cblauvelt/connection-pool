#pragma once

#include <tuple>
#include <functional>

#include <boost/asio.hpp>

#include "error.hpp"

namespace cpool
{
    
    using tcp = boost::asio::ip::tcp;

    enum class connection_state : uint8_t;

    using asio_error = boost::system::error_code;

    /**
     * @brief The type that is returned from a read attempt.
     * @param error The error code returned. If there was no error it will return a value of 0 or asio::error::Success.
     * @param bytes_transferred The number of bytes transferred during the read.
     */
    using read_result_t = std::tuple<cpool::error, std::size_t>;

    /**
     * @brief The type that is returned from a write attempt.
     * @param error The error code returned. If there was no error it will return a value of 0 or asio::error::Success.
     * @param bytes_transferred The number of bytes transferred during the read.
     */
    using write_result_t = std::tuple<cpool::error, std::size_t>;

    /**
     * @brief The function object that is called whenever the status of the TcpAcceptor object has changed state.
     * @param state The new state of the object as defined by the enum sio::network::ConnectionState.
     */
    using connection_state_change_handler = std::function<void(const connection_state state)>;

    namespace detail {
        /**
         * @brief The type that is returned from a read attempt.
         * @param error The error code returned. If there was no error it will return a value of 0 or asio::error::Success.
         * @param bytes_transferred The number of bytes transferred during the read.
         */
        using asio_read_result_t = std::tuple<boost::system::error_code, std::size_t>;

        /**
         * @brief The type that is returned from a write attempt.
         * @param error The error code returned. If there was no error it will return a value of 0 or asio::error::Success.
         * @param bytes_transferred The number of bytes transferred during the read.
         */
        using asio_write_result_t = std::tuple<boost::system::error_code, std::size_t>;
    }

}
