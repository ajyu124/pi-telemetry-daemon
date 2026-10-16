// =============================================================================
// crc16.hpp
//
// PURPOSE
//   Computes a CRC-16 checksum over a block of bytes.
//
// WHAT IS A CRC?
//   CRC stands for "cyclic redundancy check". It is a small number calculated
//   from a block of data. The sender calculates it and attaches it to the end
//   of the message. The receiver calculates it again over the bytes it
//   received. If the two numbers differ, at least one bit was corrupted in
//   transit and the message must be thrown away.
//
// WHICH CRC IS THIS?
//   "CRC-16/CCITT-FALSE", one of the most common 16-bit CRCs in embedded and
//   aerospace protocols. Its parameters are:
//     - polynomial:     0x1021
//     - starting value: 0xFFFF
//     - no bit reflection, no final XOR
//   The standard self-test for this CRC: the ASCII text "123456789" must
//   produce 0x29B1. tests/test_crc16.cpp checks exactly that.
//
// WHERE IT IS USED
//   src/telemetry_frame.cpp calls this when building and validating frames.
//   tools/telemetry_frame.py contains an independent Python version of the
//   same algorithm, so the two implementations check each other.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace telemetry {

// Returns the CRC-16/CCITT-FALSE checksum of `byte_count` bytes starting at
// `data`. Passing a byte_count of 0 returns the starting value 0xFFFF.
std::uint16_t compute_crc16(const std::uint8_t* data, std::size_t byte_count);

}  // namespace telemetry
