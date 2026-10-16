// =============================================================================
// i2c_bus_interface.hpp
//
// PURPOSE
//   Defines I2cBusInterface, the contract for talking to a chip over I2C.
//
// WHAT IS I2C?
//   I2C (say "eye squared see") is a two-wire connection used to link small
//   chips such as sensors to a processor. One wire carries a clock and the
//   other carries data. Several chips can share the same two wires, so each
//   chip has an "address" (a number such as 0x77) to say which one is being
//   spoken to.
//
// WHAT IS A REGISTER?
//   A chip exposes its settings and results as a row of numbered one-byte
//   slots called registers. To configure the chip you write a byte to a
//   register number. To get a measurement you read bytes from register
//   numbers. The chip's datasheet lists what each register means.
//
// WHY AN INTERFACE HERE TOO?
//   This is the second layer of the hardware abstraction (the first is
//   SensorInterface). The BME280 driver in src/bme280_sensor.cpp talks to this
//   interface, not to Linux directly. So the driver's register logic and math
//   can be unit tested with a fake bus that returns bytes from a table
//   (tests/fake_i2c_bus.hpp), with no chip attached.
//
//   Real implementation:  src/linux_i2c_bus.hpp
//   Test implementation:  tests/fake_i2c_bus.hpp
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace telemetry {

class I2cBusInterface {
public:
    virtual ~I2cBusInterface() = default;

    // Opens the bus and selects the chip. Returns true on success.
    virtual bool open_bus() = 0;

    // Writes one byte to one register. Returns true on success.
    virtual bool write_register(std::uint8_t register_address, std::uint8_t value) = 0;

    // Reads `byte_count` bytes starting at `first_register_address` into
    // `destination`. Chips like the BME280 automatically move on to the next
    // register after each byte, so one call can read a whole block.
    // Returns true only if every byte was read.
    virtual bool read_registers(std::uint8_t first_register_address,
                                std::uint8_t* destination,
                                std::size_t byte_count) = 0;
};

}  // namespace telemetry
