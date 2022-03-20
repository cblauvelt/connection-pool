#pragma once

#include <chrono>
// #include <iostream>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "back_off.hpp"
#include "condition_variable.hpp"

#include "cpool/timer.hpp"
#include "cpool/types.hpp"

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
        , stop_(false)
        , constructor_func_(constructor_func)
        , max_connections_(max_connections) {}

    [[nodiscard]] awaitable<T*> try_get_connection() {
        if (stopped()) {
            co_return nullptr;
        }

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
            boost::asio::steady_timer timer(connection->get_executor());

            // cout << "Attempting first connect" << endl;
            auto err = co_await connection->async_connect();
            if (err.value() == (int)net::error::operation_aborted) {
                co_return nullptr;
            }

            int attempts = 1;
            while (!connection->connected()) {
                auto delay = timer_delay(++attempts);
                // cout << "connection failed; waiting " << delay.count() << "
                // milliseconds" << endl;

                timer.expires_from_now(delay);
                co_await timer.async_wait(use_awaitable);

                // cout << "connection attempt " << attempts << endl;
                auto error = co_await connection->async_connect();
                if (error.value() == (int)net::error::operation_aborted) {
                    co_return nullptr;
                }
            }
        }

        co_return connection;
    }

    [[nodiscard]] awaitable<T*> get_connection() {
        T* connection = co_await try_get_connection();
        while (connection == nullptr) {
            co_await cv_.async_wait(
                [&]() { return size_busy() < max_connections_ && !stopped(); });

            if (stopped()) {
                co_return nullptr;
            }

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

    [[nodiscard]] awaitable<void> stop() {
        std::lock_guard<std::mutex> guard{mtx_};
        // prevent new connections from being made
        stop_ = true;
        max_connections_ = 0;

        // stop used connections
        // don't clear them, the user still has a raw pointer and this can lead
        // to unexpected crashes, connections will be destroyed with the pool
        // object
        for (auto& conn_pair : busy_connections_) {
            co_await conn_pair.second->stop();
        }

        // clear idle connections, sockets are closed in dtor
        idle_connections_.clear();

        // wake up all coroutines waiting for a connection
        cv_.notify_all();

        co_return;
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

    bool stopped() const { return (bool)stop_; }

  private:
    static const int default_max_connections = 16;

    mutable std::mutex mtx_;
    std::unordered_map<T*, std::unique_ptr<T>> idle_connections_;
    std::unordered_map<T*, std::unique_ptr<T>> busy_connections_;
    condition_variable cv_;
    std::atomic_bool stop_;

    std::function<std::unique_ptr<T>(void)> constructor_func_;
    size_t max_connections_;
};

} // namespace cpool