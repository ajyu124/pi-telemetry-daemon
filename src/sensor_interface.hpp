// =============================================================================
// sensor_interface.hpp
//
// PURPOSE
//   Defines SensorInterface, the "contract" every sensor must follow.
//
// WHAT IS AN INTERFACE (ABSTRACT BASE CLASS)?
//   A class with only pure virtual functions (the "= 0" at the end). It
//   contains no code of its own. It only lists the functions that a real
//   sensor class must provide. Code that uses a sensor holds a
//   `SensorInterface&` and does not know or care which real class is behind it.
//
// WHY THIS MATTERS (HARDWARE ABSTRACTION LAYER)
//   This file is the project's hardware abstraction layer, or HAL: a boundary
//   that hides the hardware from the rest of the program. Because the daemon
//   loop only knows about SensorInterface, it can run with:
//     - SimulatedSensor (src/simulated_sensor.hpp): fake data, no hardware
//     - Bme280Sensor    (src/bme280_sensor.hpp):    a real chip on a Pi
//     - a scripted fake inside the unit tests (tests/test_telemetry_daemon.cpp)
//   That is what makes the whole program testable on a laptop.
// =============================================================================
#pragma once

#include <optional>
#include <string>

#include "sensor_reading.hpp"

namespace telemetry {

class SensorInterface {
public:
    // A virtual destructor is required on any class meant to be inherited
    // from, so that deleting through a base pointer cleans up correctly.
    virtual ~SensorInterface() = default;

    // Prepares the sensor for use (opens devices, loads calibration, etc).
    // Returns true on success. Must be called once before read_sensor().
    virtual bool initialize() = 0;

    // Takes one measurement.
    //
    // Returns std::optional<SensorReading>. std::optional is a C++17 type
    // that either holds a value or holds nothing (std::nullopt). Here:
    //   - a value   means the read succeeded
    //   - nullopt   means the read failed (sensor unplugged, bus error, ...)
    // This forces the caller to check for failure before using the data.
    virtual std::optional<SensorReading> read_sensor() = 0;

    // A short human-readable name for log messages, such as "simulated".
    virtual std::string sensor_name() const = 0;
};

}  // namespace telemetry
