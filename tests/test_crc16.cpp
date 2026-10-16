// =============================================================================
// test_crc16.cpp
//
// Unit tests for compute_crc16() in src/crc16.cpp.
//
// HOW TO READ A GOOGLETEST FILE
//   TEST(GroupName, TestName) { ... } defines one test.
//   EXPECT_EQ(a, b) checks that a equals b. If it does not, the test fails
//   and GoogleTest prints both values. EXPECT_NE checks "not equal".
// =============================================================================
#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "crc16.hpp"

namespace {

using telemetry::compute_crc16;

// Helper: CRC of a text string.
std::uint16_t crc_of_text(const std::string& text) {
    return compute_crc16(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

// The published "check value" for CRC-16/CCITT-FALSE. Every correct
// implementation of this CRC must return 0x29B1 for the text "123456789".
// This is the single most important test in this file: it proves the
// algorithm matches the standard, not just that it matches itself.
TEST(Crc16, MatchesPublishedCheckValue) {
    EXPECT_EQ(crc_of_text("123456789"), 0x29B1);
}

TEST(Crc16, EmptyInputReturnsStartingValue) {
    EXPECT_EQ(compute_crc16(nullptr, 0), 0xFFFF);
}

TEST(Crc16, SingleByteKnownValue) {
    // The CRC-16/CCITT-FALSE of the single letter "A" is 0xB915.
    EXPECT_EQ(crc_of_text("A"), 0xB915);
}

// The whole point of a CRC: changing even one bit changes the result.
TEST(Crc16, SingleBitFlipChangesResult) {
    std::uint8_t data[] = {0x10, 0x20, 0x30, 0x40};
    const std::uint16_t original = compute_crc16(data, sizeof(data));

    data[2] ^= 0x01;  // flip the lowest bit of the third byte

    EXPECT_NE(compute_crc16(data, sizeof(data)), original);
}

// Swapping two bytes must also be detected. A simple "add up all the bytes"
// checksum would miss this, which is one reason CRCs are used instead.
TEST(Crc16, ByteOrderMatters) {
    const std::uint8_t forward[] = {0x01, 0x02};
    const std::uint8_t swapped[] = {0x02, 0x01};

    EXPECT_NE(compute_crc16(forward, sizeof(forward)), compute_crc16(swapped, sizeof(swapped)));
}

}  // namespace
