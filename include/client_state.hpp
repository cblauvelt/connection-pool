#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

namespace cpool {

namespace asio = boost::asio;

/**
 * Defines the available states fo the connection_state object.
 */
enum class client_connection_state : uint8_t {
    /**
     * @brief The TcpConnection is not connected to a remote endpoint
     *
     */
    disconnected = 0,

    /**
     * @brief The TcpConnection is resolving a URL to determine the IP address
     * of the remote endpoint
     *
     */
    resolving,

    /**
     * @brief The TcpConnection is connecting to a remote endpoint
     *
     */
    connecting,

    /**
     * @brief The TcpConnection is connected to a remote endpoint and able to
     * send requests and receive responses
     *
     */
    connected,

    /**
     * @brief The TcpConnection is disconnecting from a remote endpoint and is
     * no longer able to interact with it
     *
     */
    disconnecting
};

inline bool
error_means_client_disconnected(const boost::system::error_code& ec) {
    if (ec == asio::error::eof || ec == asio::error::broken_pipe ||
        ec == asio::error::connection_aborted ||
        ec == asio::error::connection_refused ||
        ec == asio::error::connection_reset ||
        ec == asio::error::network_down || ec == asio::error::network_reset ||
        ec == asio::error::network_unreachable ||
        ec == asio::error::not_connected || ec == asio::error::eof ||
        ec == asio::ssl::error::stream_truncated) {
        return true;
    }

    return false;
}

} // namespace cpool