// =============================================================================
// test_simulated_sensor.cpp
//
// Unit tests for SimulatedSensor in src/simulated_sensor.cpp. They check that
// the fake data is plausible, repeatable, and that the simulated fault window
// fails exactly the reads it is supposed to.
// =============================================================================
#include <gtest/gtest.h>

#include <optional>

#include "simulated_sensor.hpp"

namespace {

using namespace telemetry;

TEST(SimulatedSensor, InitializeSucceeds) {
    SimulatedSensor sensor;

    EXPECT_TRUE(sensor.initialize());
    EXPECT_EQ(sensor.sensor_name(), "simulated");
}

TEST(SimulatedSensor, ValuesStayInsideRealisticRanges) {
    SimulatedSensor sensor;
    ASSERT_TRUE(sensor.initialize());

    for (int i = 0; i < 5000; ++i) {
        const std::optional<SensorReading> reading = sensor.read_sensor();
        ASSERT_TRUE(reading.has_value()) << "read " << i;

        // 22.00 C +/- 2.00 swing +/- 0.05 noise
        EXPECT_GE(reading->temperature_centi_celsius, 1990);
        EXPECT_LE(reading->temperature_centi_celsius, 2410);
        // 101325 Pa +/- 50 swing +/- 3 noise
        EXPECT_GE(reading->pressure_pascals, 101265u);
        EXPECT_LE(reading->pressure_pascals, 101385u);
        // 45.00 % +/- 5.00 swing +/- 0.10 noise
        EXPECT_GE(reading->humidity_centi_percent, 3985u);
        EXPECT_LE(reading->humidity_centi_percent, 5015u);
    }
}

// Two sensors with the same seed must produce the same sequence. Repeatable
// fake data is what makes the other tests trustworthy.
TEST(SimulatedSensor, SameSeedGivesSameSequence) {
    SimulatedSensor first;
    SimulatedSensor second;
    ASSERT_TRUE(first.initialize());
    ASSERT_TRUE(second.initialize());

    for (int i = 0; i < 200; ++i) {
        const std::optional<SensorReading> a = first.read_sensor();
        const std::optional<SensorReading> b = second.read_sensor();
        ASSERT_TRUE(a.has_value());
        ASSERT_TRUE(b.has_value());
        EXPECT_EQ(a->temperature_centi_celsius, b->temperature_centi_celsius);
        EXPECT_EQ(a->pressure_pascals, b->pressure_pascals);
        EXPECT_EQ(a->humidity_centi_percent, b->humidity_centi_percent);
    }
}

TEST(SimulatedSensor, NeverFailsWhenNoFaultIsConfigured) {
    SimulatedSensor sensor;
    ASSERT_TRUE(sensor.initialize());

    for (int i = 0; i < 1000; ++i) {
        EXPECT_TRUE(sensor.read_sensor().has_value());
    }
}

// Fault window: start at read 10, last for 5 reads. So reads 10, 11, 12, 13
// and 14 fail, and every other read succeeds.
TEST(SimulatedSensor, FailsExactlyInsideTheFaultWindow) {
    SimulatedSensorConfig config;
    config.fault_start_read = 10;
    config.fault_length_reads = 5;
    SimulatedSensor sensor(config);
    ASSERT_TRUE(sensor.initialize());

    for (int read_number = 0; read_number < 30; ++read_number) {
        const bool expect_failure = (read_number >= 10 && read_number < 15);
        const std::optional<SensorReading> reading = sensor.read_sensor();

        EXPECT_EQ(reading.has_value(), !expect_failure) << "read " << read_number;
    }
    EXPECT_EQ(sensor.reads_attempted(), 30u);
}

TEST(SimulatedSensor, InitializeRestartsTheSequence) {
    SimulatedSensor sensor;
    ASSERT_TRUE(sensor.initialize());
    const std::optional<SensorReading> first_run = sensor.read_sensor();
    sensor.read_sensor();
    sensor.read_sensor();

    ASSERT_TRUE(sensor.initialize());
    const std::optional<SensorReading> second_run = sensor.read_sensor();

    ASSERT_TRUE(first_run.has_value());
    ASSERT_TRUE(second_run.has_value());
    EXPECT_EQ(first_run->temperature_centi_celsius, second_run->temperature_centi_celsius);
    EXPECT_EQ(sensor.reads_attempted(), 1u);
}

}  // namespace
