// =============================================================================
// bme280_sensor.hpp
//
// PURPOSE
//   The driver for a real Bosch BME280 temperature, pressure and humidity
//   sensor chip. A "driver" is the code that knows one specific chip's
//   registers and math, and turns its raw bytes into usable values.
//
// HOW THE BME280 WORKS
//   1. Each chip is slightly different, so at the factory Bosch measures each
//      one and stores about 18 correction numbers inside it. These are the
//      "calibration coefficients" (named dig_T1, dig_P1, dig_H1 and so on in
//      the datasheet). The driver reads them once at startup.
//   2. The chip measures continuously and puts three raw numbers in its data
//      registers. These raw numbers ("ADC counts") are meaningless alone.
//   3. The driver feeds the raw numbers and the calibration coefficients
//      through formulas from the datasheet. This step is called
//      "compensation", and it produces real units.
//
// HOW THIS FILE IS ORGANISED
//   - Bme280Calibration: the coefficients.
//   - compensate_*():    the three datasheet formulas as plain functions with
//                        no hardware access, so they are unit tested directly
//                        against the reference numbers printed in Bosch's
//                        datasheet (tests/test_bme280_sensor.cpp).
//   - Bme280Sensor:      talks to the chip through I2cBusInterface.
//
// STATUS (PLEASE READ)
//   The compensation math is verified against Bosch's published reference
//   values. The register sequence is verified against a fake bus. Neither has
//   been run on a physical sensor yet. That happens when the Raspberry Pi
//   hardware arrives. See docs/RASPBERRY_PI_SETUP.md.
//
// REFERENCE
//   Bosch Sensortec BME280 datasheet, sections 4.2.2 (calibration), 4.2.3
//   (compensation formulas) and 5 (register map).
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "i2c_bus_interface.hpp"
#include "sensor_interface.hpp"

namespace telemetry {

// The factory correction numbers stored in each chip. The names match the
// datasheet so the code can be checked against it line by line.
struct Bme280Calibration {
    std::uint16_t dig_T1 = 0;
    std::int16_t dig_T2 = 0;
    std::int16_t dig_T3 = 0;

    std::uint16_t dig_P1 = 0;
    std::int16_t dig_P2 = 0;
    std::int16_t dig_P3 = 0;
    std::int16_t dig_P4 = 0;
    std::int16_t dig_P5 = 0;
    std::int16_t dig_P6 = 0;
    std::int16_t dig_P7 = 0;
    std::int16_t dig_P8 = 0;
    std::int16_t dig_P9 = 0;

    std::uint8_t dig_H1 = 0;
    std::int16_t dig_H2 = 0;
    std::uint8_t dig_H3 = 0;
    std::int16_t dig_H4 = 0;
    std::int16_t dig_H5 = 0;
    std::int8_t dig_H6 = 0;
};

// The result of the temperature formula. The pressure and humidity formulas
// both need `fine_temperature`, an internal high-resolution temperature
// value (called t_fine in the datasheet), so it is returned alongside.
struct Bme280TemperatureResult {
    std::int32_t fine_temperature = 0;
    std::int32_t temperature_centi_celsius = 0;
};

// Datasheet formula for temperature. `raw_temperature` is the 20-bit ADC
// count read from the chip.
Bme280TemperatureResult compensate_temperature(std::int32_t raw_temperature,
                                               const Bme280Calibration& calibration);

// Datasheet formula for pressure. Returns pascals.
std::uint32_t compensate_pressure_pascals(std::int32_t raw_pressure,
                                          std::int32_t fine_temperature,
                                          const Bme280Calibration& calibration);

// Datasheet formula for humidity. Returns hundredths of a percent, limited to
// the range 0 to 10000 (0 % to 100 %).
std::uint32_t compensate_humidity_centi_percent(std::int32_t raw_humidity,
                                                std::int32_t fine_temperature,
                                                const Bme280Calibration& calibration);

class Bme280Sensor : public SensorInterface {
public:
    // The sensor does not own the bus. The bus object must outlive it.
    explicit Bme280Sensor(I2cBusInterface& bus);

    // Opens the bus, confirms the chip really is a BME280, resets it, loads
    // its calibration and switches it to continuous measurement.
    bool initialize() override;

    // Reads one measurement. If a read fails (for example the sensor was
    // unplugged), the next call first tries to set the chip up again, so the
    // driver recovers on its own when the sensor is plugged back in.
    std::optional<SensorReading> read_sensor() override;

    std::string sensor_name() const override { return "bme280"; }

    const Bme280Calibration& calibration() const { return calibration_; }

private:
    bool configure_chip();
    bool load_calibration();

    I2cBusInterface& bus_;
    Bme280Calibration calibration_;
    bool chip_is_configured_ = false;
};

}  // namespace telemetry
