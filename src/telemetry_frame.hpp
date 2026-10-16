// =============================================================================
// telemetry_frame.hpp
//
// PURPOSE
//   Defines the telemetry frame: the fixed-size binary message the daemon
//   sends over the network 50 times per second, plus the functions that
//   convert it to bytes (encode) and back (decode).
//
// TERMS
//   Telemetry:   measurements sent automatically from a remote device.
//   Frame:       one complete message with a fixed layout.
//   Encode:      turn a C++ struct into raw bytes for sending.
//   Decode:      turn raw received bytes back into a C++ struct, rejecting
//                anything malformed.
//   Endianness:  the order in which a multi-byte number's bytes are stored.
//                "Little-endian" puts the least significant byte first. The
//                number 0x12345678 is sent as 78 56 34 12. This protocol is
//                little-endian everywhere.
//
// FRAME LAYOUT (34 bytes total, all multi-byte fields little-endian)
//
//   Offset  Size  Field                         Notes
//   ------  ----  ----------------------------  ------------------------------
//    0       1    sync byte 1                   always 0xAA
//    1       1    sync byte 2                   always 0x55
//    2       1    protocol version              currently 1
//    3       1    health state                  0 NOMINAL, 1 DEGRADED, 2 FAULT
//    4       1    flags                         bit 0: reading is fresh
//    5       1    reserved                      always 0
//    6       2    consecutive read failures     saturates at 65535
//    8       4    sequence number               +1 per frame, detects drops
//   12       8    timestamp                     ms since 1970-01-01 UTC
//   20       4    temperature                   signed, 0.01 C per count
//   24       4    pressure                      pascals
//   28       4    humidity                      0.01 % per count
//   32       2    CRC-16                        over bytes 0 through 31
//
//   The sync bytes let a receiver confirm it is looking at the start of a
//   frame. The CRC (see crc16.hpp) detects corruption.
//
// IMPORTANT DESIGN CHOICE
//   Bytes are written and read one at a time with shifts. The code does NOT
//   copy the struct's memory directly onto the wire. Copying the struct would
//   be shorter, but it silently depends on the compiler's padding and the
//   CPU's endianness, so the bytes could differ between a laptop and a Pi.
//   See docs/DESIGN_DECISIONS.md, decision 4.
//
// THE PYTHON TWIN
//   tools/telemetry_frame.py decodes this same layout. If you change the
//   layout here you must change it there too, and bump kProtocolVersion.
// =============================================================================
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "sensor_reading.hpp"

namespace telemetry {

// ---- Protocol constants -----------------------------------------------------

constexpr std::uint8_t kSyncByte1 = 0xAA;
constexpr std::uint8_t kSyncByte2 = 0x55;
constexpr std::uint8_t kProtocolVersion = 1;

constexpr std::size_t kFrameSizeBytes = 34;
constexpr std::size_t kCrcSizeBytes = 2;
// The CRC covers every byte before the CRC field itself.
constexpr std::size_t kCrcCoveredBytes = kFrameSizeBytes - kCrcSizeBytes;

// Bit 0 of the flags byte. Set when the measurement in this frame was read
// from the sensor during this cycle. Clear when the read failed and the frame
// is repeating the last good values.
constexpr std::uint8_t kFlagReadingIsFresh = 0x01;

// ---- Health state -----------------------------------------------------------

// The daemon's opinion of the sensor. The rules for moving between states
// live in src/health_monitor.hpp.
enum class HealthState : std::uint8_t {
    Nominal = 0,   // sensor reads are succeeding
    Degraded = 1,  // some reads have failed recently
    Fault = 2,     // many reads in a row have failed
};

// Returns "NOMINAL", "DEGRADED" or "FAULT" for log messages.
const char* health_state_to_text(HealthState state);

// ---- The frame --------------------------------------------------------------

// The decoded, easy-to-use form of a frame. This struct is never sent
// directly. encode_frame() turns it into bytes.
struct TelemetryFrame {
    HealthState health_state = HealthState::Nominal;
    bool reading_is_fresh = false;
    std::uint16_t consecutive_read_failures = 0;
    std::uint32_t sequence_number = 0;
    std::uint64_t timestamp_milliseconds = 0;
    SensorReading reading;
};

// A frame in wire form. std::array is a fixed-size array that lives on the
// stack, so building a frame never allocates heap memory.
using EncodedFrame = std::array<std::uint8_t, kFrameSizeBytes>;

// ---- Encode -----------------------------------------------------------------

// Converts a frame to its 34 wire bytes, including sync bytes and CRC.
EncodedFrame encode_frame(const TelemetryFrame& frame);

// ---- Decode -----------------------------------------------------------------

// Every reason a block of bytes can be rejected. Having a specific reason
// (instead of just true/false) makes tests and debugging far easier.
enum class DecodeStatus {
    Ok,
    WrongSize,           // not exactly 34 bytes
    BadSyncBytes,        // does not start with AA 55
    CrcMismatch,         // bytes were corrupted in transit
    UnsupportedVersion,  // sender uses a protocol version we do not know
    InvalidHealthState,  // health state byte is not 0, 1 or 2
};

// Returns a short description such as "CRC mismatch".
const char* decode_status_to_text(DecodeStatus status);

struct DecodeResult {
    DecodeStatus status = DecodeStatus::WrongSize;
    // Only meaningful when status == DecodeStatus::Ok.
    TelemetryFrame frame;
};

// Validates `byte_count` bytes at `data` and, if they form a valid frame,
// returns it. Checks are done in this order: size, sync bytes, CRC, version,
// health state. The CRC is checked before the content fields so that
// corrupted content is always reported as corruption.
DecodeResult decode_frame(const std::uint8_t* data, std::size_t byte_count);

}  // namespace telemetry
