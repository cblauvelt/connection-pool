#pragma once

#include <chrono>
// #include <iostream>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "back_off.hpp"
#include "condition_variable.hpp"
#include "error.hpp"
#include "timer.hpp"
#include "types.hpp"

namespace cpool {

// using std::cout;
// using std::endl;

template <class T> class connection_pool {

  public:
    connection_pool(net::any_io_executor exec,
                    std::function<std::unique_ptr<T>(void)> constructor_func,
                    size_t max_connections = default_max_connections)
        : idle_connections_()
        , busy_connections_()
        , cv_(std::move(exec))
        , constructor_func_(constructor_func)
        , max_connections_(max_connections) {}

    awaitable<T*> try_get_connection() {
        T* connection = nullptr;

        {
            std::lock_guard lock{mtx_};

            // if there's an idle connection ready, return the connection
            if (!idle_connections_.empty()) {
                // cout << "Idle connection available" << endl;
                auto first_connection = idle_connections_.begin();
                connection = first_connection->first;

                auto node = idle_connections_.extract(first_connection);
                busy_connections_.insert(std::move(node));
            }

            // we couldnt get a connection from the idle pool so try to create
            // a new connection
            if (connection == nullptr &&
                busy_connections_.size() < max_connections_) {
                // cout << "Idle connection not available. Creating new
                // connection" << endl;
                std::unique_ptr<T> uniq_connection = constructor_func_();
                connection = uniq_connection.get();
                busy_connections_.emplace(connection,
                                          std::move(uniq_connection));
            }
        }

        // attempt connection if not connected
        if (connection != nullptr && !connection->connected()) {
            auto err = co_await connection->connect();
            boost::asio::steady_timer timer(
                std::move(connection->get_executor()));

            // cout << "Attempting first connect" << endl;
            co_await connection->connect();

            int attempts = 1;
            while (!connection->connected()) {
                auto delay = timer_delay(++attempts);
                // cout << "connection failed; waiting " << delay.count() << "
                // milliseconds" << endl;

                timer.expires_from_now();
                co_await timer.async_wait(use_awaitable);

                // cout << "connection attempt " << attempts << endl;
                auto error = co_await connection->connect();
                if (error.error_code() == net::error::operation_aborted) {
                    co_return nullptr;
                }
            }
        }

        co_return connection;
    }

    awaitable<T*> get_connection() {
        T* connection = co_await try_get_connection();
        while (connection == nullptr) {
            co_await cv_.async_wait(
                [&]() { return size_busy() < max_connections_; });
            connection = co_await try_get_connection();
        }

        co_return connection;
    }

    std::unique_ptr<T> claim_connection(T* connection) {
        if (connection == nullptr) {
            return nullptr;
        }

        std::lock_guard lock{mtx_};
        auto connIt = busy_connections_.find(connection);
        if (connIt == busy_connections_.end()) {
            return nullptr;
        }

        auto uniq_connection = std::move(connIt->second);
        busy_connections_.erase(connIt);

        cv_.notify_one();
        return uniq_connection;
    }

    void release_connection(T* connection) {
        if (connection == nullptr) {
            return;
        }

        {
            std::lock_guard lock(mtx_);
            // find on busy stack
            auto it = busy_connections_.find(connection);
            if (it == busy_connections_.end()) {
                // This is a problem, either we have a bug or the
                // user tried to release the connection twice
                if (idle_connections_.contains(connection)) {
                    // it's all good, the user probably released twice
                    return;
                }

                // We're about to have a memory leak so throw an error to let
                // the user know
                throw std::runtime_error(
                    "connection could not be released, memory leak possible");
            }

            // test if connected
            auto node = busy_connections_.extract(it);
            if (connection->connected()) {
                idle_connections_.insert(std::move(node));
            }
            // node goes out of scope and the unique_ptr dies with it
        }

        cv_.notify_one();
        return;
    }

    size_t size() const {
        std::lock_guard<std::mutex> guard{mtx_};
        return idle_connections_.size() + busy_connections_.size();
    }

    size_t size_idle() const {
        std::lock_guard<std::mutex> guard{mtx_};
        return idle_connections_.size();
    }

    size_t size_busy() const {
        std::lock_guard<std::mutex> guard{mtx_};
        return busy_connections_.size();
    }

    size_t max_size() const { return max_connections_; }

  private:
    static const int default_max_connections = 16;

    mutable std::mutex mtx_;
    std::unordered_map<T*, std::unique_ptr<T>> idle_connections_;
    std::unordered_map<T*, std::unique_ptr<T>> busy_connections_;
    condition_variable cv_;

    std::function<std::unique_ptr<T>(void)> constructor_func_;
    size_t max_connections_;
};

} // namespace cpool