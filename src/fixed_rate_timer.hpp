// =============================================================================
// fixed_rate_timer.hpp
//
// PURPOSE
//   Makes a loop run at a steady rate, for example exactly 50 times a second.
//
// THE PROBLEM IT SOLVES (DRIFT)
//   The obvious way to run a loop at 50 Hz is "do the work, then sleep 20 ms".
//   That is wrong: if the work takes 3 ms, each cycle takes 23 ms and the loop
//   really runs at about 43 Hz. The error also piles up over time. This is
//   called drift.
//
// THE FIX (ABSOLUTE DEADLINES)
//   Instead of sleeping FOR a duration, sleep UNTIL a fixed point in time:
//       deadline 1 = start + 20 ms
//       deadline 2 = start + 40 ms
//       deadline 3 = start + 60 ms ...
//   Now the time the work takes is automatically absorbed, and the loop stays
//   locked to the wall-clock schedule no matter how long it has been running.
//
// WHICH CLOCK? (MONOTONIC VS WALL CLOCK)
//   This uses std::chrono::steady_clock, a "monotonic" clock: it only ever
//   moves forward at a constant rate. The normal wall clock
//   (std::chrono::system_clock) can jump when the machine syncs its time over
//   the network, which would make a timing loop stall or race. Rule of thumb:
//   steady_clock for measuring intervals, system_clock for timestamps a human
//   will read.
//
// MISSED DEADLINES
//   If a cycle overruns and the next deadline is already in the past, the
//   timer counts a "missed deadline" and reschedules from the current time.
//   It does NOT fire several cycles back to back to catch up, because a burst
//   of late frames is worse than a small gap.
//
// HONEST LIMITATION
//   Linux is not a real-time operating system, so it can occasionally delay
//   a sleeping program by a few milliseconds. This timer gives a steady
//   average rate with no drift. It does not give hard real-time guarantees.
// =============================================================================
#pragma once

#include <chrono>
#include <cstdint>

namespace telemetry {

class FixedRateTimer {
public:
    // `rate_hz` is cycles per second. It must be greater than 0.
    explicit FixedRateTimer(double rate_hz);

    // Records "now" as the start of the schedule. Call once before the loop.
    void start();

    // Blocks (pauses the program) until the next deadline, then returns.
    // Call once at the end of each loop cycle.
    void wait_for_next_tick();

    // Number of cycles that finished after their deadline had already passed.
    std::uint64_t missed_deadline_count() const { return missed_deadline_count_; }

    // The time between ticks. At 50 Hz this is 20 milliseconds.
    std::chrono::nanoseconds period() const { return period_; }

private:
    std::chrono::nanoseconds period_;
    std::chrono::steady_clock::time_point next_deadline_;
    std::uint64_t missed_deadline_count_ = 0;
};

}  // namespace telemetry
