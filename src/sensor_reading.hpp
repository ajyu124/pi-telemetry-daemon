// =============================================================================
// sensor_reading.hpp
//
// PURPOSE
//   Defines SensorReading, the plain data record that every sensor returns.
//
// WHY INTEGERS INSTEAD OF FLOATING POINT?
//   Values are stored as scaled integers ("fixed point"), for example
//   22.57 degrees Celsius is stored as the integer 2257. Embedded protocols
//   usually do this because:
//     - integers have one unambiguous byte layout, so the sender and receiver
//       can never disagree about the format
//     - many small microcontrollers have no floating point hardware
//     - integer comparisons in tests are exact
//   The unit is written into every field name so it cannot be forgotten.
// =============================================================================
#pragma once

#include <cstdint>

namespace telemetry {

struct SensorReading {
    // Temperature in hundredths of a degree Celsius. 2257 means 22.57 C.
    // Signed, because temperatures can be below zero.
    std::int32_t temperature_centi_celsius = 0;

    // Air pressure in pascals. Sea level is about 101325 Pa.
    std::uint32_t pressure_pascals = 0;

    // Relative humidity in hundredths of a percent. 4512 means 45.12 %.
    std::uint32_t humidity_centi_percent = 0;
};

}  // namespace telemetry
