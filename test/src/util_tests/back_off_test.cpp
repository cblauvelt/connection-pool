#include "back_off.hpp"

#include <chrono>
#include <iostream>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using namespace std::chrono;
using namespace std::chrono_literals;
using namespace cpool;

TEST(BackOff, Postive) {
    milliseconds delay = timer_delay(2);

    EXPECT_GE(delay.count(), (4s).count());
    EXPECT_LE(delay.count(), (4500ms).count());
}

TEST(BackOff, Zero) {
    milliseconds delay = timer_delay(0);

    EXPECT_GE(delay.count(), (1s).count());
    EXPECT_LE(delay.count(), (1500ms).count());
}

TEST(BackOff, Max) {
    milliseconds delay = timer_delay(10);

    EXPECT_EQ(delay.count(), 32000);
}

} // namespace