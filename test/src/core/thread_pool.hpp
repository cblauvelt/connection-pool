#include <functional>
#include <thread>
#include <vector>

void start_thread_pool(std::vector<std::thread>& pool, uint num_threads,
                       std::function<void(void)> func) {
    for (int i = 0; i < num_threads - 1; i++) {
        pool.push_back(std::thread(func));
    }
}

void stop_thread_pool(std::vector<std::thread>& pool) {
    for (auto& thread : pool) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}