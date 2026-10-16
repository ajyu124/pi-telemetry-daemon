// =============================================================================
// health_monitor.cpp
//
// Implementation of the HealthMonitor state machine. The state diagram and
// the reasoning behind the rules are in health_monitor.hpp.
// =============================================================================
#include "health_monitor.hpp"

#include <limits>

namespace telemetry {

namespace {

// Adds 1 to a counter but stops at the maximum value instead of wrapping
// around to 0. A counter that silently wraps would make a sensor that has
// been dead for a long time briefly look healthy.
std::uint16_t increment_without_wrapping(std::uint16_t value) {
    if (value == std::numeric_limits<std::uint16_t>::max()) {
        return value;
    }
    return static_cast<std::uint16_t>(value + 1);
}

}  // namespace

HealthMonitor::HealthMonitor(const HealthMonitorConfig& config) : config_(config) {}

void HealthMonitor::record_read_success() {
    consecutive_failures_ = 0;
    consecutive_successes_ = increment_without_wrapping(consecutive_successes_);

    switch (state_) {
        case HealthState::Nominal:
            break;  // already healthy, nothing to do

        case HealthState::Degraded:
            // One good read is enough to clear DEGRADED.
            change_state(HealthState::Nominal);
            break;

        case HealthState::Fault:
            // Leaving FAULT needs several good reads in a row (hysteresis).
            if (consecutive_successes_ >= config_.successes_until_recovered) {
                change_state(HealthState::Nominal);
            }
            break;
    }
}

void HealthMonitor::record_read_failure() {
    consecutive_successes_ = 0;
    consecutive_failures_ = increment_without_wrapping(consecutive_failures_);

    if (consecutive_failures_ >= config_.failures_until_fault) {
        change_state(HealthState::Fault);
    } else if (state_ == HealthState::Nominal &&
               consecutive_failures_ >= config_.failures_until_degraded) {
        // Only NOMINAL moves to DEGRADED. If we are already in FAULT and a
        // recovery attempt is interrupted by a failure, we stay in FAULT.
        change_state(HealthState::Degraded);
    }
}

void HealthMonitor::change_state(HealthState new_state) {
    if (new_state == state_) {
        return;
    }
    state_ = new_state;
    ++state_change_count_;
}

}  // namespace telemetry
