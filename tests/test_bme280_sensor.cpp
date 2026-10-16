// =============================================================================
// test_bme280_sensor.cpp
//
// Unit tests for the BME280 driver in src/bme280_sensor.cpp. No hardware is
// involved. Two things are tested:
//
//   1. The compensation math, against the worked example Bosch prints in its
//      datasheet. For the calibration values and raw readings below, Bosch
//      states the correct answers are 25.08 C and 100653 Pa. Matching those
//      proves the formulas were transcribed correctly.
//
//   2. The driver's conversation with the chip, using FakeI2cBus
//      (tests/fake_i2c_bus.hpp): does it check the chip ID, write the
//      settings in the required order, decode the calibration bytes
//      correctly, and recover after the sensor is unplugged?
//
// WHAT THESE TESTS DO NOT PROVE
//   That a physical sensor behaves the way the datasheet says. That can only
//   be confirmed on the Raspberry Pi. See docs/RASPBERRY_PI_SETUP.md.
// =============================================================================
#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

#include "bme280_sensor.hpp"
#include "fake_i2c_bus.hpp"

namespace {

using namespace telemetry;

// ---- Reference values from the Bosch datasheet worked example -----------------

constexpr std::int32_t kReferenceRawTemperature = 519888;
constexpr std::int32_t kReferenceRawPressure = 415148;
constexpr std::int32_t kReferenceFineTemperature = 128422;
constexpr std::int32_t kReferenceTemperatureCentiCelsius = 2508;  // 25.08 C
constexpr std::uint32_t kReferencePressurePascals = 100653;

// The datasheet gives no worked example for humidity, so the humidity tests
// use typical calibration values and check behaviour (range and direction)
// instead of one exact number.
constexpr std::int32_t kTypicalRawHumidity = 30000;

Bme280Calibration make_reference_calibration() {
    Bme280Calibration calibration;
    calibration.dig_T1 = 27504;
    calibration.dig_T2 = 26435;
    calibration.dig_T3 = -1000;

    calibration.dig_P1 = 36477;
    calibration.dig_P2 = -10685;
    calibration.dig_P3 = 3024;
    calibration.dig_P4 = 2855;
    calibration.dig_P5 = 140;
    calibration.dig_P6 = -7;
    calibration.dig_P7 = 15500;
    calibration.dig_P8 = -14600;
    calibration.dig_P9 = 6000;

    calibration.dig_H1 = 75;
    calibration.dig_H2 = 362;
    calibration.dig_H3 = 0;
    calibration.dig_H4 = 313;
    calibration.dig_H5 = 50;
    calibration.dig_H6 = 30;
    return calibration;
}

// ---- Helpers that load a FakeI2cBus the way a real chip would be laid out -----

void write_unsigned_16(FakeI2cBus& bus, std::uint8_t register_address, std::uint16_t value) {
    bus.registers[register_address] = static_cast<std::uint8_t>(value & 0xFF);
    bus.registers[register_address + 1] = static_cast<std::uint8_t>(value >> 8);
}

void write_signed_16(FakeI2cBus& bus, std::uint8_t register_address, std::int16_t value) {
    write_unsigned_16(bus, register_address, static_cast<std::uint16_t>(value));
}

void load_calibration_registers(FakeI2cBus& bus, const Bme280Calibration& calibration) {
    write_unsigned_16(bus, 0x88, calibration.dig_T1);
    write_signed_16(bus, 0x8A, calibration.dig_T2);
    write_signed_16(bus, 0x8C, calibration.dig_T3);
    write_unsigned_16(bus, 0x8E, calibration.dig_P1);
    write_signed_16(bus, 0x90, calibration.dig_P2);
    write_signed_16(bus, 0x92, calibration.dig_P3);
    write_signed_16(bus, 0x94, calibration.dig_P4);
    write_signed_16(bus, 0x96, calibration.dig_P5);
    write_signed_16(bus, 0x98, calibration.dig_P6);
    write_signed_16(bus, 0x9A, calibration.dig_P7);
    write_signed_16(bus, 0x9C, calibration.dig_P8);
    write_signed_16(bus, 0x9E, calibration.dig_P9);
    bus.registers[0xA1] = calibration.dig_H1;
    write_signed_16(bus, 0xE1, calibration.dig_H2);
    bus.registers[0xE3] = calibration.dig_H3;
    // dig_H4 and dig_H5 are 12-bit values sharing register 0xE5.
    bus.registers[0xE4] = static_cast<std::uint8_t>(calibration.dig_H4 >> 4);
    bus.registers[0xE5] = static_cast<std::uint8_t>((calibration.dig_H4 & 0x0F) |
                                                    ((calibration.dig_H5 & 0x0F) << 4));
    bus.registers[0xE6] = static_cast<std::uint8_t>(calibration.dig_H5 >> 4);
    bus.registers[0xE7] = static_cast<std::uint8_t>(calibration.dig_H6);
}

void load_measurement_registers(FakeI2cBus& bus,
                                std::int32_t raw_pressure,
                                std::int32_t raw_temperature,
                                std::int32_t raw_humidity) {
    // 20-bit values are stored as: top 8 bits, middle 8 bits, low 4 bits in
    // the upper half of the third byte.
    bus.registers[0xF7] = static_cast<std::uint8_t>(raw_pressure >> 12);
    bus.registers[0xF8] = static_cast<std::uint8_t>(raw_pressure >> 4);
    bus.registers[0xF9] = static_cast<std::uint8_t>((raw_pressure & 0x0F) << 4);
    bus.registers[0xFA] = static_cast<std::uint8_t>(raw_temperature >> 12);
    bus.registers[0xFB] = static_cast<std::uint8_t>(raw_temperature >> 4);
    bus.registers[0xFC] = static_cast<std::uint8_t>((raw_temperature & 0x0F) << 4);
    bus.registers[0xFD] = static_cast<std::uint8_t>(raw_humidity >> 8);
    bus.registers[0xFE] = static_cast<std::uint8_t>(raw_humidity);
}

// A fake bus set up to look like a healthy BME280 holding the reference data.
FakeI2cBus make_healthy_chip() {
    FakeI2cBus bus;
    bus.registers[0xD0] = 0x60;  // chip ID
    load_calibration_registers(bus, make_reference_calibration());
    load_measurement_registers(bus, kReferenceRawPressure, kReferenceRawTemperature,
                               kTypicalRawHumidity);
    return bus;
}

// =============================================================================
// Part 1: compensation math
// =============================================================================

TEST(Bme280Compensation, TemperatureMatchesDatasheetExample) {
    const Bme280TemperatureResult result =
        compensate_temperature(kReferenceRawTemperature, make_reference_calibration());

    EXPECT_EQ(result.fine_temperature, kReferenceFineTemperature);
    EXPECT_EQ(result.temperature_centi_celsius, kReferenceTemperatureCentiCelsius);
}

TEST(Bme280Compensation, PressureMatchesDatasheetExample) {
    const std::uint32_t pressure = compensate_pressure_pascals(
        kReferenceRawPressure, kReferenceFineTemperature, make_reference_calibration());

    EXPECT_EQ(pressure, kReferencePressurePascals);
}

TEST(Bme280Compensation, PressureIsZeroInsteadOfDividingByZero) {
    Bme280Calibration calibration = make_reference_calibration();
    calibration.dig_P1 = 0;  // an impossible value that would cause divide by zero

    EXPECT_EQ(compensate_pressure_pascals(kReferenceRawPressure, kReferenceFineTemperature,
                                          calibration),
              0u);
}

TEST(Bme280Compensation, HumidityIsWithinValidRange) {
    const std::uint32_t humidity = compensate_humidity_centi_percent(
        kTypicalRawHumidity, kReferenceFineTemperature, make_reference_calibration());

    // A typical raw value at room temperature should land at a believable
    // indoor humidity, between 20 % and 80 %.
    EXPECT_GT(humidity, 2000u);
    EXPECT_LT(humidity, 8000u);
}

// A larger raw reading must never produce a smaller humidity.
TEST(Bme280Compensation, HumidityRisesWithRawValue) {
    const Bme280Calibration calibration = make_reference_calibration();
    std::uint32_t previous = 0;

    for (std::int32_t raw = 20000; raw <= 40000; raw += 500) {
        const std::uint32_t humidity =
            compensate_humidity_centi_percent(raw, kReferenceFineTemperature, calibration);
        EXPECT_GE(humidity, previous) << "raw value " << raw;
        previous = humidity;
    }
}

// Extreme raw values must be limited to 0 % and 100 %, never outside.
TEST(Bme280Compensation, HumidityIsClampedToZeroAndOneHundredPercent) {
    const Bme280Calibration calibration = make_reference_calibration();

    EXPECT_EQ(compensate_humidity_centi_percent(0, kReferenceFineTemperature, calibration), 0u);
    EXPECT_EQ(compensate_humidity_centi_percent(65535, kReferenceFineTemperature, calibration),
              10000u);
}

// =============================================================================
// Part 2: the driver talking to a (fake) chip
// =============================================================================

TEST(Bme280Sensor, InitializeSucceedsOnHealthyChip) {
    FakeI2cBus bus = make_healthy_chip();
    Bme280Sensor sensor(bus);

    EXPECT_TRUE(sensor.initialize());
    EXPECT_EQ(sensor.sensor_name(), "bme280");
}

TEST(Bme280Sensor, InitializeFailsWhenBusWillNotOpen) {
    FakeI2cBus bus = make_healthy_chip();
    bus.open_should_fail = true;
    Bme280Sensor sensor(bus);

    EXPECT_FALSE(sensor.initialize());
}

// If some other chip answers at that address, refuse to continue.
TEST(Bme280Sensor, InitializeFailsOnWrongChipId) {
    FakeI2cBus bus = make_healthy_chip();
    bus.registers[0xD0] = 0x58;  // 0x58 is the ID of a BMP280, a different chip
    Bme280Sensor sensor(bus);

    EXPECT_FALSE(sensor.initialize());
    EXPECT_TRUE(bus.writes.empty()) << "must not write to a chip it has not identified";
}

TEST(Bme280Sensor, InitializeDecodesCalibrationRegisters) {
    FakeI2cBus bus = make_healthy_chip();
    Bme280Sensor sensor(bus);
    ASSERT_TRUE(sensor.initialize());

    const Bme280Calibration expected = make_reference_calibration();
    const Bme280Calibration& actual = sensor.calibration();

    EXPECT_EQ(actual.dig_T1, expected.dig_T1);
    EXPECT_EQ(actual.dig_T2, expected.dig_T2);
    EXPECT_EQ(actual.dig_T3, expected.dig_T3);
    EXPECT_EQ(actual.dig_P1, expected.dig_P1);
    EXPECT_EQ(actual.dig_P2, expected.dig_P2);
    EXPECT_EQ(actual.dig_P3, expected.dig_P3);
    EXPECT_EQ(actual.dig_P4, expected.dig_P4);
    EXPECT_EQ(actual.dig_P5, expected.dig_P5);
    EXPECT_EQ(actual.dig_P6, expected.dig_P6);
    EXPECT_EQ(actual.dig_P7, expected.dig_P7);
    EXPECT_EQ(actual.dig_P8, expected.dig_P8);
    EXPECT_EQ(actual.dig_P9, expected.dig_P9);
    EXPECT_EQ(actual.dig_H1, expected.dig_H1);
    EXPECT_EQ(actual.dig_H2, expected.dig_H2);
    EXPECT_EQ(actual.dig_H3, expected.dig_H3);
    EXPECT_EQ(actual.dig_H4, expected.dig_H4);
    EXPECT_EQ(actual.dig_H5, expected.dig_H5);
    EXPECT_EQ(actual.dig_H6, expected.dig_H6);
}

// The datasheet requires: reset first, and the humidity setting (0xF2) before
// the measurement setting (0xF4). Getting this order wrong makes the chip
// silently ignore the humidity setting.
TEST(Bme280Sensor, InitializeWritesSettingsInRequiredOrder) {
    FakeI2cBus bus = make_healthy_chip();
    Bme280Sensor sensor(bus);
    ASSERT_TRUE(sensor.initialize());

    ASSERT_EQ(bus.writes.size(), 4u);
    EXPECT_EQ(bus.writes[0].first, 0xE0);   // reset register
    EXPECT_EQ(bus.writes[0].second, 0xB6);  // reset command
    EXPECT_EQ(bus.writes[1].first, 0xF2);   // humidity setting
    EXPECT_EQ(bus.writes[1].second, 0x01);
    EXPECT_EQ(bus.writes[2].first, 0xF5);   // config
    EXPECT_EQ(bus.writes[3].first, 0xF4);   // measurement setting, written last
    EXPECT_EQ(bus.writes[3].second, 0x27);
}

// The full path: bytes in registers -> driver -> real units.
TEST(Bme280Sensor, ReadReturnsDatasheetReferenceValues) {
    FakeI2cBus bus = make_healthy_chip();
    Bme280Sensor sensor(bus);
    ASSERT_TRUE(sensor.initialize());

    const std::optional<SensorReading> reading = sensor.read_sensor();

    ASSERT_TRUE(reading.has_value());
    EXPECT_EQ(reading->temperature_centi_celsius, kReferenceTemperatureCentiCelsius);
    EXPECT_EQ(reading->pressure_pascals, kReferencePressurePascals);
    EXPECT_GT(reading->humidity_centi_percent, 2000u);
    EXPECT_LT(reading->humidity_centi_percent, 8000u);
}

TEST(Bme280Sensor, ReadFailsWhenBusFails) {
    FakeI2cBus bus = make_healthy_chip();
    Bme280Sensor sensor(bus);
    ASSERT_TRUE(sensor.initialize());

    bus.io_should_fail = true;  // unplug the sensor

    EXPECT_FALSE(sensor.read_sensor().has_value());
}

// 0x80000 is what the chip reports before it has measured anything.
TEST(Bme280Sensor, ReadFailsWhenChipHasNotMeasuredYet) {
    FakeI2cBus bus = make_healthy_chip();
    load_measurement_registers(bus, 0x80000, 0x80000, 0x8000);
    Bme280Sensor sensor(bus);
    ASSERT_TRUE(sensor.initialize());

    EXPECT_FALSE(sensor.read_sensor().has_value());
}

// Unplug the sensor, then plug it back in. A real chip forgets its settings
// when it loses power, so the driver must set it up again by itself.
TEST(Bme280Sensor, RecoversAndReconfiguresAfterSensorIsReplugged) {
    FakeI2cBus bus = make_healthy_chip();
    Bme280Sensor sensor(bus);
    ASSERT_TRUE(sensor.initialize());
    ASSERT_TRUE(sensor.read_sensor().has_value());

    bus.io_should_fail = true;  // unplugged
    EXPECT_FALSE(sensor.read_sensor().has_value());
    EXPECT_FALSE(sensor.read_sensor().has_value());

    bus.io_should_fail = false;  // plugged back in
    bus.writes.clear();
    const std::optional<SensorReading> reading = sensor.read_sensor();

    ASSERT_TRUE(reading.has_value());
    EXPECT_EQ(reading->temperature_centi_celsius, kReferenceTemperatureCentiCelsius);
    EXPECT_EQ(bus.writes.size(), 4u) << "the chip should have been set up again";
}

}  // namespace
