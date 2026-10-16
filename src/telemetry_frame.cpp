// =============================================================================
// telemetry_frame.cpp
//
// Implementation of encode_frame() and decode_frame(). The frame layout table
// is in telemetry_frame.hpp. Read that first.
// =============================================================================
#include "telemetry_frame.hpp"

#include "crc16.hpp"

namespace telemetry {

namespace {

// ---- Byte offsets of each field (must match the table in the header) --------

constexpr std::size_t kOffsetSync1 = 0;
constexpr std::size_t kOffsetSync2 = 1;
constexpr std::size_t kOffsetVersion = 2;
constexpr std::size_t kOffsetHealthState = 3;
constexpr std::size_t kOffsetFlags = 4;
constexpr std::size_t kOffsetReserved = 5;
constexpr std::size_t kOffsetConsecutiveFailures = 6;
constexpr std::size_t kOffsetSequenceNumber = 8;
constexpr std::size_t kOffsetTimestamp = 12;
constexpr std::size_t kOffsetTemperature = 20;
constexpr std::size_t kOffsetPressure = 24;
constexpr std::size_t kOffsetHumidity = 28;
constexpr std::size_t kOffsetCrc = 32;

// ---- Little-endian write helpers ---------------------------------------------
// Each helper writes the least significant byte first. `>> 8` shifts the
// number right by one byte, and `& 0xFF` keeps only the lowest byte.

void write_uint16(std::uint8_t* destination, std::uint16_t value) {
    destination[0] = static_cast<std::uint8_t>(value & 0xFF);
    destination[1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

void write_uint32(std::uint8_t* destination, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        destination[i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
    }
}

void write_uint64(std::uint8_t* destination, std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i) {
        destination[i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
    }
}

// ---- Little-endian read helpers ----------------------------------------------

std::uint16_t read_uint16(const std::uint8_t* source) {
    return static_cast<std::uint16_t>(source[0] | (source[1] << 8));
}

std::uint32_t read_uint32(const std::uint8_t* source) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(source[i]) << (8 * i);
    }
    return value;
}

std::uint64_t read_uint64(const std::uint8_t* source) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(source[i]) << (8 * i);
    }
    return value;
}

}  // namespace

const char* health_state_to_text(HealthState state) {
    switch (state) {
        case HealthState::Nominal:  return "NOMINAL";
        case HealthState::Degraded: return "DEGRADED";
        case HealthState::Fault:    return "FAULT";
    }
    return "UNKNOWN";
}

const char* decode_status_to_text(DecodeStatus status) {
    switch (status) {
        case DecodeStatus::Ok:                 return "ok";
        case DecodeStatus::WrongSize:          return "wrong size";
        case DecodeStatus::BadSyncBytes:       return "bad sync bytes";
        case DecodeStatus::CrcMismatch:        return "CRC mismatch";
        case DecodeStatus::UnsupportedVersion: return "unsupported protocol version";
        case DecodeStatus::InvalidHealthState: return "invalid health state";
    }
    return "unknown";
}

EncodedFrame encode_frame(const TelemetryFrame& frame) {
    EncodedFrame bytes{};  // the {} fills the array with zeros
    std::uint8_t* out = bytes.data();

    out[kOffsetSync1] = kSyncByte1;
    out[kOffsetSync2] = kSyncByte2;
    out[kOffsetVersion] = kProtocolVersion;
    out[kOffsetHealthState] = static_cast<std::uint8_t>(frame.health_state);
    out[kOffsetFlags] = frame.reading_is_fresh ? kFlagReadingIsFresh : std::uint8_t{0};
    out[kOffsetReserved] = 0;

    write_uint16(out + kOffsetConsecutiveFailures, frame.consecutive_read_failures);
    write_uint32(out + kOffsetSequenceNumber, frame.sequence_number);
    write_uint64(out + kOffsetTimestamp, frame.timestamp_milliseconds);

    // Temperature is signed. Converting a signed 32-bit integer to unsigned
    // keeps the same bit pattern (two's complement), so negative values
    // survive the trip and are converted back in decode_frame().
    write_uint32(out + kOffsetTemperature,
                 static_cast<std::uint32_t>(frame.reading.temperature_centi_celsius));
    write_uint32(out + kOffsetPressure, frame.reading.pressure_pascals);
    write_uint32(out + kOffsetHumidity, frame.reading.humidity_centi_percent);

    // The CRC goes last because it is calculated over everything before it.
    const std::uint16_t crc = compute_crc16(out, kCrcCoveredBytes);
    write_uint16(out + kOffsetCrc, crc);

    return bytes;
}

DecodeResult decode_frame(const std::uint8_t* data, std::size_t byte_count) {
    DecodeResult result;

    // Check 1: size. Must come first, because every later check reads bytes
    // at fixed offsets and would read out of bounds on a short buffer.
    if (data == nullptr || byte_count != kFrameSizeBytes) {
        result.status = DecodeStatus::WrongSize;
        return result;
    }

    // Check 2: sync bytes.
    if (data[kOffsetSync1] != kSyncByte1 || data[kOffsetSync2] != kSyncByte2) {
        result.status = DecodeStatus::BadSyncBytes;
        return result;
    }

    // Check 3: CRC. Recalculate it and compare with the one in the frame.
    const std::uint16_t expected_crc = compute_crc16(data, kCrcCoveredBytes);
    const std::uint16_t received_crc = read_uint16(data + kOffsetCrc);
    if (expected_crc != received_crc) {
        result.status = DecodeStatus::CrcMismatch;
        return result;
    }

    // Check 4: protocol version.
    if (data[kOffsetVersion] != kProtocolVersion) {
        result.status = DecodeStatus::UnsupportedVersion;
        return result;
    }

    // Check 5: the health state byte must be one of the three known values.
    const std::uint8_t health_state_byte = data[kOffsetHealthState];
    if (health_state_byte > static_cast<std::uint8_t>(HealthState::Fault)) {
        result.status = DecodeStatus::InvalidHealthState;
        return result;
    }

    TelemetryFrame& frame = result.frame;
    frame.health_state = static_cast<HealthState>(health_state_byte);
    frame.reading_is_fresh = (data[kOffsetFlags] & kFlagReadingIsFresh) != 0;
    frame.consecutive_read_failures = read_uint16(data + kOffsetConsecutiveFailures);
    frame.sequence_number = read_uint32(data + kOffsetSequenceNumber);
    frame.timestamp_milliseconds = read_uint64(data + kOffsetTimestamp);
    frame.reading.temperature_centi_celsius =
        static_cast<std::int32_t>(read_uint32(data + kOffsetTemperature));
    frame.reading.pressure_pascals = read_uint32(data + kOffsetPressure);
    frame.reading.humidity_centi_percent = read_uint32(data + kOffsetHumidity);

    result.status = DecodeStatus::Ok;
    return result;
}

}  // namespace telemetry
