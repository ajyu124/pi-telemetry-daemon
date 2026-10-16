// =============================================================================
// fake_i2c_bus.hpp
//
// PURPOSE
//   A pretend I2C bus for unit tests. It implements I2cBusInterface
//   (src/i2c_bus_interface.hpp) using a plain array of 256 bytes in place of
//   a real chip's registers.
//
// WHAT IS A "FAKE"?
//   A simplified stand-in for a real component, used in tests. A test
//   prepares the fake ("register 0xD0 holds 0x60"), runs the code under test,
//   then inspects what the code did ("which registers were written, and in
//   what order?"). This is how the BME280 driver is tested with no hardware.
//
// TEST CONTROLS
//   registers        set these before the test to preload chip contents
//   open_should_fail make open_bus() fail
//   io_should_fail   make every read and write fail, like an unplugged sensor
//   writes           a log of every register write, in order
// =============================================================================
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "i2c_bus_interface.hpp"

namespace telemetry {

class FakeI2cBus : public I2cBusInterface {
public:
    // One slot per possible register address (0x00 to 0xFF).
    std::array<std::uint8_t, 256> registers{};

    bool open_should_fail = false;
    bool io_should_fail = false;

    // Each entry is (register address, value written).
    std::vector<std::pair<std::uint8_t, std::uint8_t>> writes;

    bool open_bus() override { return !open_should_fail; }

    bool write_register(std::uint8_t register_address, std::uint8_t value) override {
        if (io_should_fail) {
            return false;
        }
        writes.emplace_back(register_address, value);
        registers[register_address] = value;
        return true;
    }

    bool read_registers(std::uint8_t first_register_address,
                        std::uint8_t* destination,
                        std::size_t byte_count) override {
        if (io_should_fail) {
            return false;
        }
        for (std::size_t i = 0; i < byte_count; ++i) {
            // "& 0xFF" keeps the index inside the array if a read runs past
            // register 0xFF.
            destination[i] = registers[(first_register_address + i) & 0xFF];
        }
        return true;
    }
};

}  // namespace telemetry
