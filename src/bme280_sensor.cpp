// =============================================================================
// bme280_sensor.cpp
//
// Implementation of the BME280 driver. See bme280_sensor.hpp for how the chip
// works and for the verification status of this code.
//
// The three compensate_* functions are Bosch's integer formulas from datasheet
// section 4.2.3. The odd-looking constants and shifts are theirs. Do not
// "tidy" them, because the results must match the datasheet exactly.
//
// A note on shifts: `x >> n` divides by 2 to the power n, and `x << n`
// multiplies by it. In C++17, left-shifting a NEGATIVE number is undefined
// behaviour, so wherever the datasheet left-shifts a value that could be
// negative, this file multiplies by the equivalent power of two instead.
// =============================================================================
#include "bme280_sensor.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <thread>

namespace telemetry {

namespace {

// ---- Register addresses (datasheet section 5.3) -----------------------------

constexpr std::uint8_t kRegisterChipId = 0xD0;
constexpr std::uint8_t kRegisterReset = 0xE0;
constexpr std::uint8_t kRegisterControlHumidity = 0xF2;
constexpr std::uint8_t kRegisterControlMeasurement = 0xF4;
constexpr std::uint8_t kRegisterConfig = 0xF5;
constexpr std::uint8_t kRegisterDataStart = 0xF7;         // 8 data bytes: F7 to FE
constexpr std::uint8_t kRegisterCalibrationBlock1 = 0x88;  // 26 bytes: 88 to A1
constexpr std::uint8_t kRegisterCalibrationBlock2 = 0xE1;  // 7 bytes:  E1 to E7

constexpr std::size_t kCalibrationBlock1Size = 26;
constexpr std::size_t kCalibrationBlock2Size = 7;
constexpr std::size_t kDataBlockSize = 8;

// ---- Values written to or expected from registers ----------------------------

constexpr std::uint8_t kExpectedChipId = 0x60;  // every BME280 reports 0x60
constexpr std::uint8_t kResetCommand = 0xB6;    // the datasheet's reset code

// Humidity oversampling x1. "Oversampling" means averaging several samples
// per measurement. x1 means no averaging, which is the fastest setting.
constexpr std::uint8_t kControlHumidityValue = 0x01;

// Bits 7-5: temperature oversampling x1 (001)
// Bits 4-2: pressure oversampling x1    (001)
// Bits 1-0: mode = normal (11), meaning "measure continuously"
// Binary 001 001 11 = 0x27.
constexpr std::uint8_t kControlMeasurementValue = 0x27;

// Shortest standby time between measurements (0.5 ms), filter off.
// With these settings one measurement takes under 10 ms, which fits inside
// the 20 ms cycle of a 50 Hz loop.
constexpr std::uint8_t kConfigValue = 0x00;

// A raw value of 0x80000 is what the chip reports when it has not measured
// anything yet.
constexpr std::int32_t kRawValueNotMeasured = 0x80000;

constexpr auto kDelayAfterReset = std::chrono::milliseconds(10);
constexpr auto kDelayForFirstMeasurement = std::chrono::milliseconds(10);

// ---- Helpers for combining calibration bytes ---------------------------------
// Calibration values are stored low byte first (little-endian).

std::uint16_t combine_unsigned(std::uint8_t low_byte, std::uint8_t high_byte) {
    return static_cast<std::uint16_t>(low_byte | (high_byte << 8));
}

std::int16_t combine_signed(std::uint8_t low_byte, std::uint8_t high_byte) {
    return static_cast<std::int16_t>(combine_unsigned(low_byte, high_byte));
}

}  // namespace

// =============================================================================
// Compensation formulas (pure math, no hardware)
// =============================================================================

Bme280TemperatureResult compensate_temperature(std::int32_t raw_temperature,
                                               const Bme280Calibration& calibration) {
    const std::int32_t t1 = calibration.dig_T1;
    const std::int32_t t2 = calibration.dig_T2;
    const std::int32_t t3 = calibration.dig_T3;

    const std::int32_t var1 = (((raw_temperature >> 3) - (t1 * 2)) * t2) >> 11;
    const std::int32_t difference = (raw_temperature >> 4) - t1;
    const std::int32_t var2 = (((difference * difference) >> 12) * t3) >> 14;

    Bme280TemperatureResult result;
    result.fine_temperature = var1 + var2;
    result.temperature_centi_celsius = (result.fine_temperature * 5 + 128) >> 8;
    return result;
}

std::uint32_t compensate_pressure_pascals(std::int32_t raw_pressure,
                                          std::int32_t fine_temperature,
                                          const Bme280Calibration& calibration) {
    // 64-bit integers are needed because the intermediate values are large.
    const std::int64_t p1 = calibration.dig_P1;
    const std::int64_t p2 = calibration.dig_P2;
    const std::int64_t p3 = calibration.dig_P3;
    const std::int64_t p4 = calibration.dig_P4;
    const std::int64_t p5 = calibration.dig_P5;
    const std::int64_t p6 = calibration.dig_P6;
    const std::int64_t p7 = calibration.dig_P7;
    const std::int64_t p8 = calibration.dig_P8;
    const std::int64_t p9 = calibration.dig_P9;

    std::int64_t var1 = static_cast<std::int64_t>(fine_temperature) - 128000;
    std::int64_t var2 = var1 * var1 * p6;
    var2 = var2 + (var1 * p5) * 131072;        // datasheet: << 17
    var2 = var2 + p4 * 34359738368LL;          // datasheet: << 35
    var1 = ((var1 * var1 * p3) >> 8) + (var1 * p2) * 4096;  // datasheet: << 12
    var1 = ((140737488355328LL + var1) * p1) >> 33;         // 140737488355328 = 1 << 47

    if (var1 == 0) {
        return 0;  // would divide by zero. Only happens with bad calibration.
    }

    std::int64_t pressure = 1048576 - raw_pressure;
    pressure = (((pressure * 2147483648LL) - var2) * 3125) / var1;  // datasheet: << 31
    var1 = (p9 * (pressure >> 13) * (pressure >> 13)) >> 25;
    var2 = (p8 * pressure) >> 19;
    pressure = ((pressure + var1 + var2) >> 8) + p7 * 16;  // datasheet: << 4

    if (pressure < 0) {
        return 0;
    }

    // The datasheet result is in units of 1/256 pascal. Add half (128) before
    // dividing so the result is rounded to the nearest pascal.
    return static_cast<std::uint32_t>((pressure + 128) / 256);
}

std::uint32_t compensate_humidity_centi_percent(std::int32_t raw_humidity,
                                                std::int32_t fine_temperature,
                                                const Bme280Calibration& calibration) {
    const std::int64_t h1 = calibration.dig_H1;
    const std::int64_t h2 = calibration.dig_H2;
    const std::int64_t h3 = calibration.dig_H3;
    const std::int64_t h4 = calibration.dig_H4;
    const std::int64_t h5 = calibration.dig_H5;
    const std::int64_t h6 = calibration.dig_H6;

    const std::int64_t temperature_term = static_cast<std::int64_t>(fine_temperature) - 76800;

    // The datasheet writes this as one very long expression. It is split into
    // two factors here so each can be read on its own.
    const std::int64_t factor_a =
        ((static_cast<std::int64_t>(raw_humidity) * 16384)  // datasheet: << 14
         - (h4 * 1048576)                                   // datasheet: << 20
         - (h5 * temperature_term) + 16384) >> 15;

    const std::int64_t factor_b =
        (((((((temperature_term * h6) >> 10) *
             (((temperature_term * h3) >> 11) + 32768)) >> 10) + 2097152) * h2 + 8192) >> 14);

    std::int64_t humidity = factor_a * factor_b;
    humidity = humidity - (((((humidity >> 15) * (humidity >> 15)) >> 7) * h1) >> 4);

    // Limit to the valid range, 0 % to 100 %, in the datasheet's units.
    humidity = std::clamp<std::int64_t>(humidity, 0, 419430400);

    // (humidity >> 12) is percent in units of 1/1024. Convert to 1/100.
    const std::int64_t humidity_1024ths = humidity >> 12;
    return static_cast<std::uint32_t>((humidity_1024ths * 100) / 1024);
}

// =============================================================================
// Bme280Sensor
// =============================================================================

Bme280Sensor::Bme280Sensor(I2cBusInterface& bus) : bus_(bus) {}

bool Bme280Sensor::initialize() {
    chip_is_configured_ = false;
    if (!bus_.open_bus()) {
        return false;
    }
    return configure_chip();
}

bool Bme280Sensor::configure_chip() {
    chip_is_configured_ = false;

    // Step 1: confirm we are talking to a BME280 and not some other chip.
    std::uint8_t chip_id = 0;
    if (!bus_.read_registers(kRegisterChipId, &chip_id, 1) || chip_id != kExpectedChipId) {
        return false;
    }

    // Step 2: reset the chip so it starts from a known state.
    if (!bus_.write_register(kRegisterReset, kResetCommand)) {
        return false;
    }
    std::this_thread::sleep_for(kDelayAfterReset);

    // Step 3: read this chip's factory calibration.
    if (!load_calibration()) {
        return false;
    }

    // Step 4: configure and start measuring. The datasheet requires the
    // humidity setting to be written BEFORE the measurement setting, or the
    // humidity setting is ignored.
    if (!bus_.write_register(kRegisterControlHumidity, kControlHumidityValue) ||
        !bus_.write_register(kRegisterConfig, kConfigValue) ||
        !bus_.write_register(kRegisterControlMeasurement, kControlMeasurementValue)) {
        return false;
    }

    // Give the chip time to finish its first measurement, so the first read
    // does not come back as "not measured yet".
    std::this_thread::sleep_for(kDelayForFirstMeasurement);

    chip_is_configured_ = true;
    return true;
}

bool Bme280Sensor::load_calibration() {
    std::array<std::uint8_t, kCalibrationBlock1Size> block1{};
    std::array<std::uint8_t, kCalibrationBlock2Size> block2{};

    if (!bus_.read_registers(kRegisterCalibrationBlock1, block1.data(), block1.size()) ||
        !bus_.read_registers(kRegisterCalibrationBlock2, block2.data(), block2.size())) {
        return false;
    }

    // Block 1 starts at register 0x88, so block1[0] is register 0x88,
    // block1[1] is 0x89, and so on.
    calibration_.dig_T1 = combine_unsigned(block1[0], block1[1]);
    calibration_.dig_T2 = combine_signed(block1[2], block1[3]);
    calibration_.dig_T3 = combine_signed(block1[4], block1[5]);

    calibration_.dig_P1 = combine_unsigned(block1[6], block1[7]);
    calibration_.dig_P2 = combine_signed(block1[8], block1[9]);
    calibration_.dig_P3 = combine_signed(block1[10], block1[11]);
    calibration_.dig_P4 = combine_signed(block1[12], block1[13]);
    calibration_.dig_P5 = combine_signed(block1[14], block1[15]);
    calibration_.dig_P6 = combine_signed(block1[16], block1[17]);
    calibration_.dig_P7 = combine_signed(block1[18], block1[19]);
    calibration_.dig_P8 = combine_signed(block1[20], block1[21]);
    calibration_.dig_P9 = combine_signed(block1[22], block1[23]);
    // block1[24] is register 0xA0, which is unused.
    calibration_.dig_H1 = block1[25];  // register 0xA1

    // Block 2 starts at register 0xE1.
    calibration_.dig_H2 = combine_signed(block2[0], block2[1]);
    calibration_.dig_H3 = block2[2];
    // dig_H4 and dig_H5 are 12-bit values that share register 0xE5:
    //   dig_H4 = E4 (upper 8 bits) + low  4 bits of E5
    //   dig_H5 = E6 (upper 8 bits) + high 4 bits of E5
    // The cast to int8_t first keeps the sign of the upper byte.
    calibration_.dig_H4 = static_cast<std::int16_t>(
        static_cast<std::int8_t>(block2[3]) * 16 + (block2[4] & 0x0F));
    calibration_.dig_H5 = static_cast<std::int16_t>(
        static_cast<std::int8_t>(block2[5]) * 16 + (block2[4] >> 4));
    calibration_.dig_H6 = static_cast<std::int8_t>(block2[6]);

    return true;
}

std::optional<SensorReading> Bme280Sensor::read_sensor() {
    // If an earlier read failed, the chip may have lost power and forgotten
    // its settings. Try to set it up again before reading.
    if (!chip_is_configured_ && !configure_chip()) {
        return std::nullopt;
    }

    // Read all 8 data bytes in one go so the three values belong to the same
    // measurement.
    std::array<std::uint8_t, kDataBlockSize> data{};
    if (!bus_.read_registers(kRegisterDataStart, data.data(), data.size())) {
        chip_is_configured_ = false;
        return std::nullopt;
    }

    // Pressure and temperature are 20-bit values spread over 3 bytes each:
    // 8 bits, 8 bits, then the top 4 bits of the third byte.
    const std::int32_t raw_pressure = (data[0] << 12) | (data[1] << 4) | (data[2] >> 4);
    const std::int32_t raw_temperature = (data[3] << 12) | (data[4] << 4) | (data[5] >> 4);
    // Humidity is a 16-bit value over 2 bytes.
    const std::int32_t raw_humidity = (data[6] << 8) | data[7];

    if (raw_temperature == kRawValueNotMeasured || raw_pressure == kRawValueNotMeasured) {
        return std::nullopt;  // chip has not completed a measurement yet
    }

    const Bme280TemperatureResult temperature =
        compensate_temperature(raw_temperature, calibration_);

    SensorReading reading;
    reading.temperature_centi_celsius = temperature.temperature_centi_celsius;
    reading.pressure_pascals =
        compensate_pressure_pascals(raw_pressure, temperature.fine_temperature, calibration_);
    reading.humidity_centi_percent =
        compensate_humidity_centi_percent(raw_humidity, temperature.fine_temperature, calibration_);
    return reading;
}

}  // namespace telemetry
