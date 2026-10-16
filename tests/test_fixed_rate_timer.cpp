// =============================================================================
// test_fixed_rate_timer.cpp
//
// Unit tests for FixedRateTimer in src/fixed_rate_timer.cpp.
//
// A NOTE ON TESTING TIME
//   Tests that measure real time are risky: a busy computer can make them
//   fail for no real reason (a "flaky" test). So these tests only assert
//   things that must be true no matter how busy the machine is. In
//   particular they check that the loop never runs FASTER than requested,
//   and use very generous upper limits.
// =============================================================================
#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "fixed_rate_timer.hpp"

namespace {

using telemetry::FixedRateTimer;
using std::chrono::milliseconds;
using std::chrono::steady_clock;

TEST(FixedRateTimer, PeriodIsOneOverRate) {
    EXPECT_EQ(FixedRateTimer(50.0).period(), milliseconds(20));
    EXPECT_EQ(FixedRateTimer(100.0).period(), milliseconds(10));
    EXPECT_EQ(FixedRateTimer(1.0).period(), milliseconds(1000));
}

// 20 ticks at 200 Hz (5 ms each) must take at least 100 ms in total.
TEST(FixedRateTimer, NeverRunsFasterThanRequested) {
    FixedRateTimer timer(200.0);
    const auto start_time = steady_clock::now();
    timer.start();

    for (int i = 0; i < 20; ++i) {
        timer.wait_for_next_tick();
    }

    const auto elapsed = steady_clock::now() - start_time;
    EXPECT_GE(elapsed, milliseconds(100));
    // Very loose upper limit, only to catch a timer that is badly broken.
    EXPECT_LT(elapsed, milliseconds(2000));
}

// The key property: work done inside the loop does not slow the loop down.
// Each cycle does 10 ms of "work" inside a 20 ms period. With a naive
// "work then sleep 20 ms" loop, 10 cycles would take 300 ms. With absolute
// deadlines they take about 200 ms.
TEST(FixedRateTimer, WorkTimeDoesNotCauseDrift) {
    FixedRateTimer timer(50.0);
    const auto start_time = steady_clock::now();
    timer.start();

    for (int i = 0; i < 10; ++i) {
        std::this_thread::sleep_for(milliseconds(10));  // pretend to work
        timer.wait_for_next_tick();
    }

    const auto elapsed = steady_clock::now() - start_time;
    EXPECT_GE(elapsed, milliseconds(200));
    // A drifting loop would take at least 300 ms. Allow generous slack for a
    // busy machine but stay below that.
    EXPECT_LT(elapsed, milliseconds(280));
}

// If a cycle takes longer than the period, the timer counts it and returns
// straight away instead of sleeping.
TEST(FixedRateTimer, CountsMissedDeadlines) {
    FixedRateTimer timer(1000.0);  // 1 ms period
    timer.start();
    EXPECT_EQ(timer.missed_deadline_count(), 0u);

    std::this_thread::sleep_for(milliseconds(20));  // overrun by a wide margin
    timer.wait_for_next_tick();

    EXPECT_EQ(timer.missed_deadline_count(), 1u);
}

// After a long overrun the timer must NOT fire a burst of catch-up ticks.
TEST(FixedRateTimer, DoesNotBurstAfterAnOverrun) {
    FixedRateTimer timer(100.0);  // 10 ms period
    timer.start();
    std::this_thread::sleep_for(milliseconds(100));  // miss about 10 deadlines
    timer.wait_for_next_tick();                      // counts one miss, reschedules

    const auto start_time = steady_clock::now();
    for (int i = 0; i < 5; ++i) {
        timer.wait_for_next_tick();
    }
    const auto elapsed = steady_clock::now() - start_time;

    // Five normal ticks take about 50 ms. A catch-up burst would take about 0.
    EXPECT_GE(elapsed, milliseconds(40));
    EXPECT_EQ(timer.missed_deadline_count(), 1u);
}

}  // namespace
