#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

namespace cpool {

namespace asio = boost::asio;

/**
 * Defines the available states fo the connection_state object.
 */
enum class client_connection_state : uint8_t {
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

inline bool error_means_client_disconnected(const boost::system::error_code& ec) {
    if(
        ec == asio::error::broken_pipe ||
        ec == asio::error::connection_aborted ||
        ec == asio::error::connection_refused ||
        ec == asio::error::connection_reset ||
        ec == asio::error::network_down ||
        ec == asio::error::network_reset ||
        ec == asio::error::network_unreachable ||
        ec == asio::error::not_connected ||
        ec == asio::error::eof ||
        ec == asio::ssl::error::stream_truncated
    )
    {
        return true;
    }

    return false;
}



}