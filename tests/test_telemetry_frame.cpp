// =============================================================================
// test_telemetry_frame.cpp
//
// Unit tests for encode_frame() and decode_frame() in src/telemetry_frame.cpp.
//
// Three kinds of test are here:
//   1. Round trip:  encode then decode gives back the original frame.
//   2. Golden bytes: the encoded bytes match a layout written out by hand,
//      which pins down the wire format (offsets and little-endian order).
//   3. Rejection:   every kind of malformed input is refused with the right
//      reason.
//
// ASSERT_EQ vs EXPECT_EQ: both check equality. ASSERT stops the current test
// immediately on failure. It is used when the lines after it would be
// meaningless or unsafe if the check failed.
// =============================================================================
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

#include "crc16.hpp"
#include "telemetry_frame.hpp"

namespace {

using namespace telemetry;

// Builds a frame with recognisable, non-zero values in every field.
TelemetryFrame make_example_frame() {
    TelemetryFrame frame;
    frame.health_state = HealthState::Degraded;
    frame.reading_is_fresh = true;
    frame.consecutive_read_failures = 3;
    frame.sequence_number = 0x12345678;
    frame.timestamp_milliseconds = 0x0102030405060708ULL;
    frame.reading.temperature_centi_celsius = 2257;  // 22.57 C
    frame.reading.pressure_pascals = 101325;
    frame.reading.humidity_centi_percent = 4512;  // 45.12 %
    return frame;
}

void expect_frames_equal(const TelemetryFrame& actual, const TelemetryFrame& expected) {
    EXPECT_EQ(actual.health_state, expected.health_state);
    EXPECT_EQ(actual.reading_is_fresh, expected.reading_is_fresh);
    EXPECT_EQ(actual.consecutive_read_failures, expected.consecutive_read_failures);
    EXPECT_EQ(actual.sequence_number, expected.sequence_number);
    EXPECT_EQ(actual.timestamp_milliseconds, expected.timestamp_milliseconds);
    EXPECT_EQ(actual.reading.temperature_centi_celsius,
              expected.reading.temperature_centi_celsius);
    EXPECT_EQ(actual.reading.pressure_pascals, expected.reading.pressure_pascals);
    EXPECT_EQ(actual.reading.humidity_centi_percent, expected.reading.humidity_centi_percent);
}

// Recalculates and rewrites the CRC at the end of an encoded frame. Used by
// tests that deliberately change a content byte and want the CRC to still be
// valid, so that the check being tested is the one that fires.
void rewrite_crc(EncodedFrame& bytes) {
    const std::uint16_t crc = compute_crc16(bytes.data(), kCrcCoveredBytes);
    bytes[32] = static_cast<std::uint8_t>(crc & 0xFF);
    bytes[33] = static_cast<std::uint8_t>(crc >> 8);
}

// ---- Round trip --------------------------------------------------------------

TEST(TelemetryFrame, RoundTripPreservesEveryField) {
    const TelemetryFrame original = make_example_frame();

    const EncodedFrame bytes = encode_frame(original);
    const DecodeResult result = decode_frame(bytes.data(), bytes.size());

    ASSERT_EQ(result.status, DecodeStatus::Ok);
    expect_frames_equal(result.frame, original);
}

TEST(TelemetryFrame, RoundTripPreservesNegativeTemperature) {
    TelemetryFrame original = make_example_frame();
    original.reading.temperature_centi_celsius = -1234;  // -12.34 C

    const EncodedFrame bytes = encode_frame(original);
    const DecodeResult result = decode_frame(bytes.data(), bytes.size());

    ASSERT_EQ(result.status, DecodeStatus::Ok);
    EXPECT_EQ(result.frame.reading.temperature_centi_celsius, -1234);
}

TEST(TelemetryFrame, RoundTripPreservesExtremeValues) {
    TelemetryFrame original;
    original.health_state = HealthState::Fault;
    original.reading_is_fresh = false;
    original.consecutive_read_failures = std::numeric_limits<std::uint16_t>::max();
    original.sequence_number = std::numeric_limits<std::uint32_t>::max();
    original.timestamp_milliseconds = std::numeric_limits<std::uint64_t>::max();
    original.reading.temperature_centi_celsius = std::numeric_limits<std::int32_t>::min();
    original.reading.pressure_pascals = std::numeric_limits<std::uint32_t>::max();
    original.reading.humidity_centi_percent = std::numeric_limits<std::uint32_t>::max();

    const EncodedFrame bytes = encode_frame(original);
    const DecodeResult result = decode_frame(bytes.data(), bytes.size());

    ASSERT_EQ(result.status, DecodeStatus::Ok);
    expect_frames_equal(result.frame, original);
}

// ---- Golden bytes ------------------------------------------------------------

TEST(TelemetryFrame, EncodedSizeIs34Bytes) {
    EXPECT_EQ(encode_frame(make_example_frame()).size(), 34u);
    EXPECT_EQ(kFrameSizeBytes, 34u);
}

// Checks each byte position against the layout table in telemetry_frame.hpp.
// If someone reorders a field or changes endianness, this test fails.
TEST(TelemetryFrame, EncodedBytesMatchDocumentedLayout) {
    const EncodedFrame bytes = encode_frame(make_example_frame());

    EXPECT_EQ(bytes[0], 0xAA);  // sync byte 1
    EXPECT_EQ(bytes[1], 0x55);  // sync byte 2
    EXPECT_EQ(bytes[2], 1);     // protocol version
    EXPECT_EQ(bytes[3], 1);     // health state: DEGRADED
    EXPECT_EQ(bytes[4], 0x01);  // flags: fresh
    EXPECT_EQ(bytes[5], 0);     // reserved

    // consecutive failures = 3, little-endian: 03 00
    EXPECT_EQ(bytes[6], 0x03);
    EXPECT_EQ(bytes[7], 0x00);

    // sequence = 0x12345678, little-endian: 78 56 34 12
    EXPECT_EQ(bytes[8], 0x78);
    EXPECT_EQ(bytes[9], 0x56);
    EXPECT_EQ(bytes[10], 0x34);
    EXPECT_EQ(bytes[11], 0x12);

    // timestamp = 0x0102030405060708, little-endian: 08 07 06 05 04 03 02 01
    for (std::size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(bytes[12 + i], 8 - i) << "timestamp byte " << i;
    }

    // temperature = 2257 = 0x000008D1, little-endian: D1 08 00 00
    EXPECT_EQ(bytes[20], 0xD1);
    EXPECT_EQ(bytes[21], 0x08);
    EXPECT_EQ(bytes[22], 0x00);
    EXPECT_EQ(bytes[23], 0x00);

    // pressure = 101325 = 0x00018BCD, little-endian: CD 8B 01 00
    EXPECT_EQ(bytes[24], 0xCD);
    EXPECT_EQ(bytes[25], 0x8B);
    EXPECT_EQ(bytes[26], 0x01);
    EXPECT_EQ(bytes[27], 0x00);

    // humidity = 4512 = 0x000011A0, little-endian: A0 11 00 00
    EXPECT_EQ(bytes[28], 0xA0);
    EXPECT_EQ(bytes[29], 0x11);
    EXPECT_EQ(bytes[30], 0x00);
    EXPECT_EQ(bytes[31], 0x00);

    // The last two bytes are the CRC of the first 32, low byte first.
    const std::uint16_t expected_crc = compute_crc16(bytes.data(), 32);
    EXPECT_EQ(bytes[32], expected_crc & 0xFF);
    EXPECT_EQ(bytes[33], expected_crc >> 8);
}

TEST(TelemetryFrame, StaleReadingClearsFreshFlag) {
    TelemetryFrame frame = make_example_frame();
    frame.reading_is_fresh = false;

    const EncodedFrame bytes = encode_frame(frame);

    EXPECT_EQ(bytes[4], 0x00);
}

// ---- Rejection ---------------------------------------------------------------

TEST(TelemetryFrame, RejectsTooShortInput) {
    const EncodedFrame bytes = encode_frame(make_example_frame());

    const DecodeResult result = decode_frame(bytes.data(), bytes.size() - 1);

    EXPECT_EQ(result.status, DecodeStatus::WrongSize);
}

TEST(TelemetryFrame, RejectsEmptyAndNullInput) {
    const EncodedFrame bytes = encode_frame(make_example_frame());

    EXPECT_EQ(decode_frame(bytes.data(), 0).status, DecodeStatus::WrongSize);
    EXPECT_EQ(decode_frame(nullptr, kFrameSizeBytes).status, DecodeStatus::WrongSize);
}

TEST(TelemetryFrame, RejectsTooLongInput) {
    std::array<std::uint8_t, kFrameSizeBytes + 1> too_long{};
    const EncodedFrame bytes = encode_frame(make_example_frame());
    std::copy(bytes.begin(), bytes.end(), too_long.begin());

    const DecodeResult result = decode_frame(too_long.data(), too_long.size());

    EXPECT_EQ(result.status, DecodeStatus::WrongSize);
}

TEST(TelemetryFrame, RejectsBadSyncBytes) {
    EncodedFrame bytes = encode_frame(make_example_frame());
    bytes[0] = 0x00;

    const DecodeResult result = decode_frame(bytes.data(), bytes.size());

    EXPECT_EQ(result.status, DecodeStatus::BadSyncBytes);
}

// Flip one bit in every content byte in turn. Each must be caught by the CRC.
TEST(TelemetryFrame, DetectsSingleBitCorruptionInEveryContentByte) {
    const EncodedFrame good_bytes = encode_frame(make_example_frame());

    // Start at 2 because corrupting bytes 0 and 1 is reported as bad sync.
    for (std::size_t position = 2; position < kFrameSizeBytes; ++position) {
        EncodedFrame corrupted = good_bytes;
        corrupted[position] ^= 0x04;

        const DecodeResult result = decode_frame(corrupted.data(), corrupted.size());

        EXPECT_EQ(result.status, DecodeStatus::CrcMismatch) << "corrupted byte " << position;
    }
}

TEST(TelemetryFrame, RejectsUnsupportedVersion) {
    EncodedFrame bytes = encode_frame(make_example_frame());
    bytes[2] = 99;       // a version this code does not know
    rewrite_crc(bytes);  // keep the CRC valid so the version check is what fires

    const DecodeResult result = decode_frame(bytes.data(), bytes.size());

    EXPECT_EQ(result.status, DecodeStatus::UnsupportedVersion);
}

TEST(TelemetryFrame, RejectsInvalidHealthState) {
    EncodedFrame bytes = encode_frame(make_example_frame());
    bytes[3] = 7;  // only 0, 1 and 2 are valid
    rewrite_crc(bytes);

    const DecodeResult result = decode_frame(bytes.data(), bytes.size());

    EXPECT_EQ(result.status, DecodeStatus::InvalidHealthState);
}

// ---- Text helpers ------------------------------------------------------------

TEST(TelemetryFrame, HealthStateText) {
    EXPECT_STREQ(health_state_to_text(HealthState::Nominal), "NOMINAL");
    EXPECT_STREQ(health_state_to_text(HealthState::Degraded), "DEGRADED");
    EXPECT_STREQ(health_state_to_text(HealthState::Fault), "FAULT");
}

}  // namespace
