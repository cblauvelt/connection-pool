#pragma once

#include <tuple>
#include <functional>

#include <boost/asio.hpp>

using tcp = boost::asio::ip::tcp;

namespace cpool
{

    enum class connection_state : uint8_t;

    /**
     * @brief The type that is returned from a resolve attempt.
     * @param error The error code returned. If there was no error it will return a value of 0 or asio::error::Success.
     * @param iterator An iterator object that represents the endpoint found or the start of a list of endpoints
     * found if there were multiple. If there was an error iterator will be equal to tcp::resolver::iterator::end().
     */
    using resolve_result_t = std::tuple<boost::system::error_code, boost::asio::ip::tcp::resolver::iterator>;

    /**
     * @brief The type that is returned from a read attempt.
     * @param error The error code returned. If there was no error it will return a value of 0 or asio::error::Success.
     * @param bytesTransferred The number of bytes transferred during the read.
     */
    using read_result_t = std::tuple<boost::system::error_code, std::size_t>;

    /**
     * @brief The type that is returned from a write attempt.
     * @param error The error code returned. If there was no error it will return a value of 0 or asio::error::Success.
     * @param bytesTransferred The number of bytes transferred during the read.
     */
    using write_result_t = std::tuple<boost::system::error_code, std::size_t>;

    /**
     * @brief The function object that is called when a call to TcpConnection::asyncConnect is complete.
     * @param error The error code returned. If there was no error it will return a value of 0 or asio::error::Success.
     * @param iterator An iterator object that represents the endpoint to which the TcpConnection object is now connected.
     * If there was an error iterator will be equal to tcp::resolver::iterator::end().
     */
    using connect_handler = std::function<void(const boost::system::error_code& error, tcp::resolver::iterator iterator)>;

    /**
     * @brief The function object that is called whenever the status of the TcpAcceptor object has changed state.
     * @param state The new state of the object as defined by the enum sio::network::ConnectionState.
     */
    using connection_state_change_handler = std::function<void(const connection_state state)>;

}
