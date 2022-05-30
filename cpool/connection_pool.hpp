#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "back_off.hpp"
#include "condition_variable.hpp"

#include "cpool/timer.hpp"
#include "cpool/types.hpp"

namespace cpool {

template <class T> class connection_pool {

  public:
    connection_pool(net::any_io_executor exec,
                    std::function<std::unique_ptr<T>(void)> constructor_func,
                    size_t max_connections = default_max_connections)
        : idle_connections_()
        , busy_connections_()
        , connection_cv_(exec)
        , stop_(false)
        , constructor_func_(constructor_func)
        , max_connections_(max_connections) {}

    [[nodiscard]] awaitable<T*> try_get_connection() {
        T* connection = nullptr;
        {
            std::lock_guard lock{mtx_};

            if (stopped()) {
                CPOOL_TRACE_LOG("CPOOL", "pool stopped")
                // cause all waiters to stop
                connection_cv_.notify_all();
                co_return nullptr;
            }

            // we can't get an idle connection and we're already at max
            // connections
            if (idle_connections_.empty() &&
                busy_connections_.size() >= max_size()) {
                CPOOL_TRACE_LOG("CPOOL", "max connections of {} reached",
                                max_size());
                co_return nullptr;
            }

            // if there's an idle connection ready, return the connection
            if (!idle_connections_.empty()) {
                CPOOL_TRACE_LOG("CPOOL", "idle connection available")
                auto first_connection = idle_connections_.begin();
                connection = first_connection->first;

                auto node = idle_connections_.extract(first_connection);
                busy_connections_.insert(std::move(node));

                co_return connection;
            }

            CPOOL_TRACE_LOG(
                "CPOOL",
                "idle connection not available, creating new connection")
            std::unique_ptr<T> uniq_connection = constructor_func_();
            connection = uniq_connection.get();
            busy_connections_.emplace(connection, std::move(uniq_connection));
        }

        // attempt connection
        boost::asio::steady_timer timer(connection->get_executor());
        int attempts = 0;

        do {
            CPOOL_TRACE_LOG("CPOOL", "connection attempt {} to: {}:{}",
                            attempts + 1, connection->host(),
                            connection->port());
            auto err = co_await connection->async_connect();

            if (err) {
                CPOOL_TRACE_LOG("CPOOL", "error connecting: {}", err.message());

                if (err.value() == (int)net::error::operation_aborted) {
                    co_return nullptr;
                }

                auto delay = timer_delay(attempts++);
                CPOOL_TRACE_LOG("CPOOL", "connection failed; waiting {}ms",
                                delay.count());

                timer.expires_from_now(delay);
                co_await timer.async_wait(use_awaitable);
            }

        } while (!connection->connected());

        CPOOL_TRACE_LOG("CPOOL", "connection success to: {}:{}",
                        connection->host(), connection->port());

        co_return connection;
    }

    [[nodiscard]] awaitable<T*> get_connection() {
        if (stopped()) {
            co_return nullptr;
        }

        T* connection = nullptr;
        do {
            co_await connection_cv_.async_wait(
                [&]() { return size_busy() < max_size(); });

            connection = co_await try_get_connection();
        } while (connection == nullptr && !stopped());

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

        connection_cv_.notify_one();
        return uniq_connection;
    }

    void release_connection(T* connection) noexcept {
        if (connection == nullptr) {
            return;
        }

        {
            std::lock_guard lock(mtx_);
            // find on busy stack
            auto it = busy_connections_.find(connection);
            if (it == busy_connections_.end()) {
                return;
            }

            // test if connected
            auto node = busy_connections_.extract(it);
            if (connection->connected() && !stopped()) {
                CPOOL_TRACE_LOG("CPOOL", "[{}] returned to idle pool",
                                static_cast<void*>(connection))
                idle_connections_.insert(std::move(node));
            }

#ifdef CPOOL_TRACE_LOGGING
            else {
                // node goes out of scope and the unique_ptr dies with it
                CPOOL_TRACE_LOG("CPOOL", "[{}] deleting. Busy Size: {}",
                                static_cast<void*>(connection),
                                busy_connections_.size());
            }
#endif
        }

        connection_cv_.notify_one();
        return;
    }

    [[nodiscard]] awaitable<void> stop() {
        std::lock_guard<std::mutex> guard{mtx_};
        // prevent new connections from being made
        stop_ = true;

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
        connection_cv_.notify_all();

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

    size_t max_size() const { return max_connections_.load(); }

    bool stopped() const { return stop_.load(); }

  private:
    static const int default_max_connections = 16;

    mutable std::mutex mtx_;
    std::unordered_map<T*, std::unique_ptr<T>> idle_connections_;
    std::unordered_map<T*, std::unique_ptr<T>> busy_connections_;
    condition_variable connection_cv_;
    std::atomic_bool stop_;

    std::function<std::unique_ptr<T>(void)> constructor_func_;
    std::atomic_size_t max_connections_;
};

} // namespace cpool