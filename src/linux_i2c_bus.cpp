// =============================================================================
// linux_i2c_bus.cpp
//
// Implementation of LinuxI2cBus. See linux_i2c_bus.hpp for how Linux exposes
// I2C and for the current verification status of this file.
// =============================================================================
#include "linux_i2c_bus.hpp"

#include <fcntl.h>          // open, O_RDWR
#include <linux/i2c-dev.h>  // I2C_SLAVE
#include <sys/ioctl.h>      // ioctl
#include <unistd.h>         // read, write, close

#include <cerrno>
#include <cstring>
#include <utility>

namespace telemetry {

LinuxI2cBus::LinuxI2cBus(std::string device_path, std::uint8_t chip_address)
    : device_path_(std::move(device_path)), chip_address_(chip_address) {}

LinuxI2cBus::~LinuxI2cBus() {
    close_bus();
}

void LinuxI2cBus::close_bus() {
    if (device_descriptor_ != kNoDevice) {
        ::close(device_descriptor_);
        device_descriptor_ = kNoDevice;
    }
}

bool LinuxI2cBus::open_bus() {
    close_bus();

    const int descriptor = ::open(device_path_.c_str(), O_RDWR);
    if (descriptor < 0) {
        last_error_ = "could not open " + device_path_ + ": " + std::strerror(errno) +
                      " (is I2C enabled, and is this a Raspberry Pi?)";
        return false;
    }

    // I2C_SLAVE is the kernel's name for the request "all following reads and
    // writes on this file go to the chip at this address".
    if (::ioctl(descriptor, I2C_SLAVE, static_cast<unsigned long>(chip_address_)) < 0) {
        last_error_ = std::string("could not select I2C chip address: ") + std::strerror(errno);
        ::close(descriptor);
        return false;
    }

    device_descriptor_ = descriptor;
    last_error_.clear();
    return true;
}

bool LinuxI2cBus::write_register(std::uint8_t register_address, std::uint8_t value) {
    if (device_descriptor_ == kNoDevice) {
        last_error_ = "write_register called before open_bus";
        return false;
    }

    // An I2C register write is two bytes: which register, then the value.
    const std::uint8_t message[2] = {register_address, value};
    const ssize_t bytes_written = ::write(device_descriptor_, message, sizeof(message));
    if (bytes_written != static_cast<ssize_t>(sizeof(message))) {
        last_error_ = std::string("I2C write failed: ") + std::strerror(errno);
        return false;
    }
    return true;
}

bool LinuxI2cBus::read_registers(std::uint8_t first_register_address,
                                 std::uint8_t* destination,
                                 std::size_t byte_count) {
    if (device_descriptor_ == kNoDevice) {
        last_error_ = "read_registers called before open_bus";
        return false;
    }

    // Step 1: tell the chip which register we want to start reading from.
    if (::write(device_descriptor_, &first_register_address, 1) != 1) {
        last_error_ = std::string("I2C register select failed: ") + std::strerror(errno);
        return false;
    }

    // Step 2: read the bytes. The chip advances to the next register itself.
    const ssize_t bytes_read = ::read(device_descriptor_, destination, byte_count);
    if (bytes_read != static_cast<ssize_t>(byte_count)) {
        last_error_ = std::string("I2C read failed: ") + std::strerror(errno);
        return false;
    }
    return true;
}

}  // namespace telemetry
