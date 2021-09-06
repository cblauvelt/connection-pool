#pragma once

#include <chrono>
#include <random>
#include <stdexcept>

namespace cpool {

using namespace std::chrono_literals;
using std::chrono::seconds;
using std::chrono::milliseconds;

inline milliseconds timer_delay(uint8_t num_retries, std::chrono::milliseconds maximum_backoff = 32s) {
    // prevent int rollover
    int retries = std::min(num_retries, (uint8_t)29);

    std::random_device rd;  //Will be used to obtain a seed for the random number engine
    std::mt19937 gen(rd()); //Standard mersenne_twister_engine seeded with rd()
    std::uniform_int_distribution<> distrib(1, 500);

    milliseconds retry_time = seconds((int)std::pow(2, retries)) + milliseconds(distrib(gen));

    return std::min(retry_time, maximum_backoff);
}

}