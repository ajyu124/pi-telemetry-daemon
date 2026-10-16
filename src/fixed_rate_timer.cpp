// =============================================================================
// fixed_rate_timer.cpp
//
// Implementation of FixedRateTimer. The explanation of drift, absolute
// deadlines and clock choice is in fixed_rate_timer.hpp.
// =============================================================================
#include "fixed_rate_timer.hpp"

#include <thread>

namespace telemetry {

namespace {

constexpr double kNanosecondsPerSecond = 1'000'000'000.0;

}  // namespace

FixedRateTimer::FixedRateTimer(double rate_hz)
    : period_(static_cast<std::chrono::nanoseconds::rep>(kNanosecondsPerSecond / rate_hz)),
      next_deadline_(std::chrono::steady_clock::now() + period_) {}

void FixedRateTimer::start() {
    next_deadline_ = std::chrono::steady_clock::now() + period_;
}

void FixedRateTimer::wait_for_next_tick() {
    const auto now = std::chrono::steady_clock::now();

    if (now >= next_deadline_) {
        // The cycle took longer than one period. Count it, then schedule the
        // next tick one full period from now instead of trying to catch up.
        ++missed_deadline_count_;
        next_deadline_ = now + period_;
        return;
    }

    std::this_thread::sleep_until(next_deadline_);

    // Advance from the previous DEADLINE, not from the current time. This one
    // line is what prevents drift.
    next_deadline_ += period_;
}

}  // namespace telemetry
