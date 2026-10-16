// =============================================================================
// linux_i2c_bus.hpp
//
// PURPOSE
//   The real I2C bus, implemented with the Linux kernel's I2C interface.
//   This is the only file in the project that touches actual hardware.
//
// HOW LINUX EXPOSES I2C
//   Linux presents each I2C bus as a special file, for example /dev/i2c-1 on
//   a Raspberry Pi. A program opens that file, tells the kernel which chip
//   address it wants (with a call named ioctl), and then uses ordinary
//   read() and write() to exchange bytes with the chip.
//
// STATUS
//   This class compiles on any Linux machine, but it has NOT yet been run
//   against a physical sensor. On a laptop there is no /dev/i2c-1, so
//   open_bus() simply returns false with a clear message. It will be verified
//   when the Raspberry Pi hardware arrives. See docs/RASPBERRY_PI_SETUP.md.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>

#include "i2c_bus_interface.hpp"

namespace telemetry {

class LinuxI2cBus : public I2cBusInterface {
public:
    //   device_path:  the bus device file, normally "/dev/i2c-1" on a Pi.
    //   chip_address: the chip's I2C address. The BME280 is 0x77 or 0x76.
    LinuxI2cBus(std::string device_path, std::uint8_t chip_address);
    ~LinuxI2cBus() override;

    LinuxI2cBus(const LinuxI2cBus&) = delete;
    LinuxI2cBus& operator=(const LinuxI2cBus&) = delete;

    bool open_bus() override;
    bool write_register(std::uint8_t register_address, std::uint8_t value) override;
    bool read_registers(std::uint8_t first_register_address,
                        std::uint8_t* destination,
                        std::size_t byte_count) override;

    const std::string& last_error() const { return last_error_; }

private:
    void close_bus();

    static constexpr int kNoDevice = -1;

    std::string device_path_;
    std::uint8_t chip_address_;
    int device_descriptor_ = kNoDevice;
    std::string last_error_;
};

}  // namespace telemetry
