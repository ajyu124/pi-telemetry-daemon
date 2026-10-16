// =============================================================================
// health_monitor.hpp
//
// PURPOSE
//   Tracks whether the sensor is healthy, using a small state machine.
//
// WHAT IS A STATE MACHINE?
//   A design where the code is always in exactly one of a few named states,
//   and there are written rules for when it moves from one state to another.
//   It turns "is the sensor OK?" from scattered if-statements into one small
//   class with a diagram you can test.
//
// THE STATES AND RULES
//
//                    1 failed read                5 failed reads in a row
//      +---------+ ----------------> +----------+ ---------------------> +-------+
//      | NOMINAL |                   | DEGRADED |                        | FAULT |
//      +---------+ <---------------- +----------+                        +-------+
//           ^        1 good read                                             |
//           |                                                                |
//           +----------------------------------------------------------------+
//                           10 good reads in a row
//
//   NOMINAL:  reads are succeeding.
//   DEGRADED: at least one recent read failed. A single good read clears it.
//   FAULT:    several reads in a row failed. The sensor is considered down.
//
// WHY IS RECOVERY FROM FAULT SLOWER THAN ENTERING IT? (HYSTERESIS)
//   Leaving FAULT takes 10 good reads in a row, not 1. Requiring more evidence
//   to leave a bad state than to enter it is called hysteresis. Without it, a
//   loose cable that works on every other read would make the state flicker
//   between FAULT and NOMINAL many times per second, which is useless to
//   whoever is watching.
//
// NOTE
//   This class knows nothing about sensors, sockets or time. It only counts
//   successes and failures. That is why it is trivial to unit test
//   (tests/test_health_monitor.cpp).
// =============================================================================
#pragma once

#include <cstdint>

#include "telemetry_frame.hpp"  // for HealthState

namespace telemetry {

// The thresholds from the diagram above. Grouped in a struct so tests can
// use different values without changing the class.
struct HealthMonitorConfig {
    std::uint16_t failures_until_degraded = 1;
    std::uint16_t failures_until_fault = 5;
    std::uint16_t successes_until_recovered = 10;
};

class HealthMonitor {
public:
    HealthMonitor() = default;
    explicit HealthMonitor(const HealthMonitorConfig& config);

    // Call once per cycle after a sensor read that worked.
    void record_read_success();

    // Call once per cycle after a sensor read that failed.
    void record_read_failure();

    HealthState current_state() const { return state_; }

    // How many reads have failed in a row. Resets to 0 on any success.
    // Stops counting at 65535 instead of wrapping back to 0.
    std::uint16_t consecutive_failures() const { return consecutive_failures_; }

    // How many times the state has changed since startup. Useful in logs.
    std::uint32_t state_change_count() const { return state_change_count_; }

private:
    void change_state(HealthState new_state);

    HealthMonitorConfig config_;
    HealthState state_ = HealthState::Nominal;
    std::uint16_t consecutive_failures_ = 0;
    std::uint16_t consecutive_successes_ = 0;
    std::uint32_t state_change_count_ = 0;
};

}  // namespace telemetry
