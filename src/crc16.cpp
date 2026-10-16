// =============================================================================
// crc16.cpp
//
// Implementation of compute_crc16(). See crc16.hpp for what a CRC is and which
// variant this is.
//
// This is the "bit by bit" version of the algorithm. It is slower than a
// lookup-table version, but it uses no memory for a table and is easy to read,
// which is why it is common on small microcontrollers. At 50 frames per second
// and 32 checksummed bytes per frame, speed is not a concern here.
// =============================================================================
#include "crc16.hpp"

namespace telemetry {

namespace {

constexpr std::uint16_t kCrcStartingValue = 0xFFFF;
constexpr std::uint16_t kCrcPolynomial = 0x1021;
constexpr int kBitsPerByte = 8;

}  // namespace

std::uint16_t compute_crc16(const std::uint8_t* data, std::size_t byte_count) {
    std::uint16_t crc = kCrcStartingValue;

    for (std::size_t byte_index = 0; byte_index < byte_count; ++byte_index) {
        // Mix the next data byte into the top 8 bits of the running CRC.
        crc = static_cast<std::uint16_t>(crc ^ (static_cast<std::uint16_t>(data[byte_index]) << 8));

        // Process that byte one bit at a time, most significant bit first.
        for (int bit = 0; bit < kBitsPerByte; ++bit) {
            const bool top_bit_is_set = (crc & 0x8000) != 0;
            crc = static_cast<std::uint16_t>(crc << 1);
            if (top_bit_is_set) {
                crc = static_cast<std::uint16_t>(crc ^ kCrcPolynomial);
            }
        }
    }

    return crc;
}

}  // namespace telemetry
