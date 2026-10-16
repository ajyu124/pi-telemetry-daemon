// =============================================================================
// test_telemetry_daemon.cpp
//
// Unit tests for TelemetryDaemon in src/telemetry_daemon.cpp.
//
// These tests drive the daemon one cycle at a time with a ScriptedSensor: a
// fake sensor that succeeds or fails exactly when the test says so. This is
// the payoff of the hardware abstraction layer. The real loop logic is tested
// in microseconds, with exact control over failures, and with no hardware,
// no network and no waiting.
// =============================================================================
#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "telemetry_daemon.hpp"

namespace {

using namespace telemetry;

// A fake sensor controlled by the test. `fail_next_reads` says how many of
// the upcoming reads should fail. Successful reads return a temperature that
// goes up by 1 each time, so a test can tell readings apart.
class ScriptedSensor : public SensorInterface {
public:
    int fail_next_reads = 0;

    bool initialize() override { return true; }

    std::optional<SensorReading> read_sensor() override {
        if (fail_next_reads > 0) {
            --fail_next_reads;
            return std::nullopt;
        }
        SensorReading reading;
        reading.temperature_centi_celsius = next_temperature_++;
        reading.pressure_pascals = 101325;
        reading.humidity_centi_percent = 4500;
        return reading;
    }

    std::string sensor_name() const override { return "scripted"; }

private:
    std::int32_t next_temperature_ = 2000;
};

constexpr std::uint64_t kAnyTimestamp = 1'700'000'000'000ULL;

TEST(TelemetryDaemon, FirstFrameIsNominalFreshAndSequenceZero) {
    ScriptedSensor sensor;
    TelemetryDaemon daemon(sensor, nullptr, nullptr);

    const TelemetryFrame frame = daemon.execute_cycle(kAnyTimestamp);

    EXPECT_EQ(frame.sequence_number, 0u);
    EXPECT_EQ(frame.health_state, HealthState::Nominal);
    EXPECT_TRUE(frame.reading_is_fresh);
    EXPECT_EQ(frame.consecutive_read_failures, 0);
    EXPECT_EQ(frame.timestamp_milliseconds, kAnyTimestamp);
    EXPECT_EQ(frame.reading.temperature_centi_celsius, 2000);
}

TEST(TelemetryDaemon, SequenceNumberIncreasesByOneEveryCycle) {
    ScriptedSensor sensor;
    TelemetryDaemon daemon(sensor, nullptr, nullptr);

    for (std::uint32_t expected = 0; expected < 100; ++expected) {
        EXPECT_EQ(daemon.execute_cycle(kAnyTimestamp).sequence_number, expected);
    }
}

// The sequence number counts frames, not successful reads. It must keep
// increasing through a failure so the receiver can tell "sensor down" from
// "frames lost in the network".
TEST(TelemetryDaemon, SequenceNumberKeepsIncreasingWhileSensorFails) {
    ScriptedSensor sensor;
    TelemetryDaemon daemon(sensor, nullptr, nullptr);
    daemon.execute_cycle(kAnyTimestamp);

    sensor.fail_next_reads = 3;

    EXPECT_EQ(daemon.execute_cycle(kAnyTimestamp).sequence_number, 1u);
    EXPECT_EQ(daemon.execute_cycle(kAnyTimestamp).sequence_number, 2u);
    EXPECT_EQ(daemon.execute_cycle(kAnyTimestamp).sequence_number, 3u);
}

TEST(TelemetryDaemon, FailedReadRepeatsLastGoodValuesMarkedStale) {
    ScriptedSensor sensor;
    TelemetryDaemon daemon(sensor, nullptr, nullptr);
    const TelemetryFrame good_frame = daemon.execute_cycle(kAnyTimestamp);

    sensor.fail_next_reads = 1;
    const TelemetryFrame stale_frame = daemon.execute_cycle(kAnyTimestamp);

    EXPECT_FALSE(stale_frame.reading_is_fresh);
    EXPECT_EQ(stale_frame.health_state, HealthState::Degraded);
    EXPECT_EQ(stale_frame.consecutive_read_failures, 1);
    EXPECT_EQ(stale_frame.reading.temperature_centi_celsius,
              good_frame.reading.temperature_centi_celsius);
}

TEST(TelemetryDaemon, FailureBeforeAnyGoodReadReportsZeros) {
    ScriptedSensor sensor;
    sensor.fail_next_reads = 1;
    TelemetryDaemon daemon(sensor, nullptr, nullptr);

    const TelemetryFrame frame = daemon.execute_cycle(kAnyTimestamp);

    EXPECT_FALSE(frame.reading_is_fresh);
    EXPECT_EQ(frame.reading.temperature_centi_celsius, 0);
    EXPECT_EQ(frame.reading.pressure_pascals, 0u);
}

// Walks the whole fault story one frame at a time:
// NOMINAL -> DEGRADED -> FAULT -> (hysteresis) -> NOMINAL.
TEST(TelemetryDaemon, FullFaultAndRecoverySequence) {
    ScriptedSensor sensor;
    TelemetryDaemon daemon(sensor, nullptr, nullptr);

    // Three healthy cycles.
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(daemon.execute_cycle(kAnyTimestamp).health_state, HealthState::Nominal);
    }

    // Eight failures. The first four are DEGRADED, the rest are FAULT.
    sensor.fail_next_reads = 8;
    std::vector<HealthState> states_during_failure;
    for (int i = 0; i < 8; ++i) {
        states_during_failure.push_back(daemon.execute_cycle(kAnyTimestamp).health_state);
    }
    const std::vector<HealthState> expected_during_failure = {
        HealthState::Degraded, HealthState::Degraded, HealthState::Degraded,
        HealthState::Degraded, HealthState::Fault,    HealthState::Fault,
        HealthState::Fault,    HealthState::Fault,
    };
    EXPECT_EQ(states_during_failure, expected_during_failure);

    // Sensor comes back. Nine good reads: data is fresh, but still FAULT.
    for (int i = 0; i < 9; ++i) {
        const TelemetryFrame frame = daemon.execute_cycle(kAnyTimestamp);
        EXPECT_TRUE(frame.reading_is_fresh);
        EXPECT_EQ(frame.health_state, HealthState::Fault) << "good read " << (i + 1);
    }

    // The tenth good read in a row clears the fault.
    EXPECT_EQ(daemon.execute_cycle(kAnyTimestamp).health_state, HealthState::Nominal);
}

TEST(TelemetryDaemon, StatisticsCountFramesAndFailures) {
    ScriptedSensor sensor;
    TelemetryDaemon daemon(sensor, nullptr, nullptr);

    daemon.execute_cycle(kAnyTimestamp);
    sensor.fail_next_reads = 2;
    daemon.execute_cycle(kAnyTimestamp);
    daemon.execute_cycle(kAnyTimestamp);
    daemon.execute_cycle(kAnyTimestamp);

    EXPECT_EQ(daemon.statistics().frames_built, 4u);
    EXPECT_EQ(daemon.statistics().sensor_read_failures, 2u);
    EXPECT_EQ(daemon.statistics().frames_sent, 0u) << "no sender was provided";
}

// Every frame is recorded, including the stale ones produced during a fault.
TEST(TelemetryDaemon, StoresEveryFrameIncludingStaleOnes) {
    ScriptedSensor sensor;
    TelemetryStore store;
    ASSERT_TRUE(store.open(":memory:", 100));
    TelemetryDaemon daemon(sensor, nullptr, &store);

    daemon.execute_cycle(kAnyTimestamp);
    sensor.fail_next_reads = 2;
    daemon.execute_cycle(kAnyTimestamp);
    daemon.execute_cycle(kAnyTimestamp);
    ASSERT_TRUE(store.commit_pending_frames());

    EXPECT_EQ(store.count_rows_in_database(), 3);
    EXPECT_EQ(daemon.statistics().store_failures, 0u);
}

// A problem with the database must not stop telemetry from being produced.
TEST(TelemetryDaemon, KeepsRunningWhenTheStoreFails) {
    ScriptedSensor sensor;
    TelemetryStore store_that_was_never_opened;
    TelemetryDaemon daemon(sensor, nullptr, &store_that_was_never_opened);

    const TelemetryFrame frame = daemon.execute_cycle(kAnyTimestamp);

    EXPECT_TRUE(frame.reading_is_fresh);
    EXPECT_EQ(daemon.statistics().frames_built, 1u);
    EXPECT_EQ(daemon.statistics().store_failures, 1u);
}

// ---- run() -------------------------------------------------------------------

TEST(TelemetryDaemon, RunStopsAtFrameLimit) {
    ScriptedSensor sensor;
    TelemetryDaemon daemon(sensor, nullptr, nullptr);
    RunOptions options;
    options.rate_hz = 1000;  // fast, so the test takes about 20 ms
    options.frame_limit = 20;

    daemon.run(options, [] { return true; });

    EXPECT_EQ(daemon.statistics().frames_built, 20u);
}

// This is the Ctrl+C path: the "keep running" function starts returning false.
TEST(TelemetryDaemon, RunStopsWhenAskedTo) {
    ScriptedSensor sensor;
    TelemetryDaemon daemon(sensor, nullptr, nullptr);
    RunOptions options;
    options.rate_hz = 1000;

    int cycles_allowed = 7;
    daemon.run(options, [&cycles_allowed] { return cycles_allowed-- > 0; });

    EXPECT_EQ(daemon.statistics().frames_built, 7u);
}

TEST(TelemetryDaemon, RunPrintsStatusOncePerSecondAndOnStateChange) {
    ScriptedSensor sensor;
    TelemetryDaemon daemon(sensor, nullptr, nullptr);
    std::ostringstream captured_output;  // collects printed text in memory
    RunOptions options;
    options.rate_hz = 200;
    options.frame_limit = 200;  // exactly one second of frames
    options.status_output = &captured_output;

    daemon.run(options, [] { return true; });

    const std::string output = captured_output.str();
    EXPECT_NE(output.find("state=NOMINAL"), std::string::npos);
    EXPECT_NE(output.find("seq=199"), std::string::npos);
}

TEST(TelemetryDaemon, CurrentUnixTimeIsPlausible) {
    // Any time after 2023-11-14 and before the year 2100.
    const std::uint64_t now = current_unix_time_milliseconds();

    EXPECT_GT(now, 1'700'000'000'000ULL);
    EXPECT_LT(now, 4'102'444'800'000ULL);
}

}  // namespace
