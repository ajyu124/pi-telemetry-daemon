// =============================================================================
// test_health_monitor.cpp
//
// Unit tests for the HealthMonitor state machine in src/health_monitor.cpp.
// Each test walks one path through the state diagram drawn in
// src/health_monitor.hpp. Read that diagram first.
//
// Default thresholds: 1 failure -> DEGRADED, 5 in a row -> FAULT,
// 10 good reads in a row -> recovered from FAULT.
// =============================================================================
#include <gtest/gtest.h>

#include "health_monitor.hpp"

namespace {

using namespace telemetry;

// Small helpers so the tests read like sentences.
void record_failures(HealthMonitor& monitor, int count) {
    for (int i = 0; i < count; ++i) {
        monitor.record_read_failure();
    }
}

void record_successes(HealthMonitor& monitor, int count) {
    for (int i = 0; i < count; ++i) {
        monitor.record_read_success();
    }
}

TEST(HealthMonitor, StartsNominal) {
    HealthMonitor monitor;

    EXPECT_EQ(monitor.current_state(), HealthState::Nominal);
    EXPECT_EQ(monitor.consecutive_failures(), 0);
    EXPECT_EQ(monitor.state_change_count(), 0u);
}

TEST(HealthMonitor, StaysNominalWhileReadsSucceed) {
    HealthMonitor monitor;

    record_successes(monitor, 100);

    EXPECT_EQ(monitor.current_state(), HealthState::Nominal);
    EXPECT_EQ(monitor.state_change_count(), 0u);
}

TEST(HealthMonitor, OneFailureMeansDegraded) {
    HealthMonitor monitor;

    record_failures(monitor, 1);

    EXPECT_EQ(monitor.current_state(), HealthState::Degraded);
    EXPECT_EQ(monitor.consecutive_failures(), 1);
}

TEST(HealthMonitor, OneSuccessClearsDegraded) {
    HealthMonitor monitor;
    record_failures(monitor, 3);
    ASSERT_EQ(monitor.current_state(), HealthState::Degraded);

    record_successes(monitor, 1);

    EXPECT_EQ(monitor.current_state(), HealthState::Nominal);
    EXPECT_EQ(monitor.consecutive_failures(), 0);
}

TEST(HealthMonitor, FourFailuresIsStillDegraded) {
    HealthMonitor monitor;

    record_failures(monitor, 4);

    EXPECT_EQ(monitor.current_state(), HealthState::Degraded);
}

TEST(HealthMonitor, FiveFailuresInARowMeansFault) {
    HealthMonitor monitor;

    record_failures(monitor, 5);

    EXPECT_EQ(monitor.current_state(), HealthState::Fault);
    EXPECT_EQ(monitor.consecutive_failures(), 5);
}

// Failures that are not in a row must not add up to a FAULT.
TEST(HealthMonitor, ScatteredFailuresNeverReachFault) {
    HealthMonitor monitor;

    for (int i = 0; i < 20; ++i) {
        record_failures(monitor, 4);
        record_successes(monitor, 1);
    }

    EXPECT_EQ(monitor.current_state(), HealthState::Nominal);
}

// Hysteresis: 9 good reads are not enough to leave FAULT.
TEST(HealthMonitor, FaultDoesNotClearBeforeTenSuccesses) {
    HealthMonitor monitor;
    record_failures(monitor, 5);
    ASSERT_EQ(monitor.current_state(), HealthState::Fault);

    record_successes(monitor, 9);

    EXPECT_EQ(monitor.current_state(), HealthState::Fault);
    EXPECT_EQ(monitor.consecutive_failures(), 0);
}

TEST(HealthMonitor, FaultClearsAfterTenSuccessesInARow) {
    HealthMonitor monitor;
    record_failures(monitor, 5);

    record_successes(monitor, 10);

    EXPECT_EQ(monitor.current_state(), HealthState::Nominal);
}

// A failure during recovery restarts the count of good reads.
TEST(HealthMonitor, FailureDuringRecoveryRestartsTheCount) {
    HealthMonitor monitor;
    record_failures(monitor, 5);

    record_successes(monitor, 9);
    record_failures(monitor, 1);  // interrupts the recovery
    record_successes(monitor, 9);

    EXPECT_EQ(monitor.current_state(), HealthState::Fault);

    record_successes(monitor, 1);  // now 10 in a row

    EXPECT_EQ(monitor.current_state(), HealthState::Nominal);
}

// An intermittent connection (works every other read) must hold a steady
// FAULT and not flicker. This is the scenario hysteresis exists for.
TEST(HealthMonitor, IntermittentSensorDoesNotFlickerOutOfFault) {
    HealthMonitor monitor;
    record_failures(monitor, 5);
    const std::uint32_t changes_after_fault = monitor.state_change_count();

    for (int i = 0; i < 50; ++i) {
        record_successes(monitor, 1);
        record_failures(monitor, 1);
    }

    EXPECT_EQ(monitor.current_state(), HealthState::Fault);
    EXPECT_EQ(monitor.state_change_count(), changes_after_fault);
}

TEST(HealthMonitor, CountsEachStateChangeOnce) {
    HealthMonitor monitor;

    record_failures(monitor, 5);    // NOMINAL -> DEGRADED -> FAULT (2 changes)
    record_successes(monitor, 10);  // FAULT -> NOMINAL             (1 change)

    EXPECT_EQ(monitor.state_change_count(), 3u);
}

// The failure counter must stop at its maximum, not wrap around to 0.
TEST(HealthMonitor, FailureCounterSaturatesInsteadOfWrapping) {
    HealthMonitor monitor;

    record_failures(monitor, 70000);  // more than the 16-bit maximum of 65535

    EXPECT_EQ(monitor.consecutive_failures(), 65535);
    EXPECT_EQ(monitor.current_state(), HealthState::Fault);
}

TEST(HealthMonitor, CustomThresholdsAreRespected) {
    HealthMonitorConfig config;
    config.failures_until_degraded = 2;
    config.failures_until_fault = 3;
    config.successes_until_recovered = 2;
    HealthMonitor monitor(config);

    record_failures(monitor, 1);
    EXPECT_EQ(monitor.current_state(), HealthState::Nominal);

    record_failures(monitor, 1);
    EXPECT_EQ(monitor.current_state(), HealthState::Degraded);

    record_failures(monitor, 1);
    EXPECT_EQ(monitor.current_state(), HealthState::Fault);

    record_successes(monitor, 2);
    EXPECT_EQ(monitor.current_state(), HealthState::Nominal);
}

}  // namespace
