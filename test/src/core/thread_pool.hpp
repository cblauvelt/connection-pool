#include <functional>
#include <thread>
#include <vector>

void start_thread_pool(std::vector<std::jthread>& pool, uint num_threads,
                       std::function<void(void)> func) {
    for (int i = 0; i < num_threads - 1; i++) {
        pool.push_back(std::jthread(func));
    }
}

void stop_thread_pool(std::vector<std::jthread>& pool) {
    for (auto& thread : pool) {
        thread.join();
    }
}