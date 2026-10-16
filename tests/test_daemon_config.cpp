// =============================================================================
// test_daemon_config.cpp
//
// Unit tests for parse_command_line() in src/daemon_config.cpp. They cover
// the defaults, every option, and every way the input can be wrong.
// =============================================================================
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "daemon_config.hpp"

namespace {

using namespace telemetry;

// Shorthand so each test can write parse({"--port", "6000"}).
ParseResult parse(const std::vector<std::string>& arguments) {
    return parse_command_line(arguments);
}

TEST(DaemonConfig, NoArgumentsGivesDefaults) {
    const ParseResult result = parse({});

    ASSERT_EQ(result.outcome, ParseOutcome::Run);
    EXPECT_EQ(result.config.rate_hz, 50u);
    EXPECT_EQ(result.config.destination_ip, "127.0.0.1");
    EXPECT_EQ(result.config.destination_port, 5005);
    EXPECT_TRUE(result.config.database_enabled);
    EXPECT_EQ(result.config.database_path, "telemetry.db");
    EXPECT_EQ(result.config.sensor_type, SensorType::Simulated);
    EXPECT_EQ(result.config.i2c_device_path, "/dev/i2c-1");
    EXPECT_EQ(result.config.i2c_address, 0x77);
    EXPECT_EQ(result.config.frame_limit, 0u);
    EXPECT_EQ(result.config.fault_length_frames, 0u);
    EXPECT_FALSE(result.config.quiet);
}

TEST(DaemonConfig, ParsesEveryOption) {
    const ParseResult result = parse({
        "--rate-hz", "100",
        "--host", "192.168.1.20",
        "--port", "6000",
        "--database", "/tmp/flight.db",
        "--sensor", "bme280",
        "--i2c-device", "/dev/i2c-3",
        "--i2c-address", "0x76",
        "--frame-limit", "500",
        "--fault-start-frame", "150",
        "--fault-length-frames", "50",
        "--quiet",
    });

    ASSERT_EQ(result.outcome, ParseOutcome::Run) << result.error_message;
    EXPECT_EQ(result.config.rate_hz, 100u);
    EXPECT_EQ(result.config.destination_ip, "192.168.1.20");
    EXPECT_EQ(result.config.destination_port, 6000);
    EXPECT_EQ(result.config.database_path, "/tmp/flight.db");
    EXPECT_EQ(result.config.sensor_type, SensorType::Bme280);
    EXPECT_EQ(result.config.i2c_device_path, "/dev/i2c-3");
    EXPECT_EQ(result.config.i2c_address, 0x76);
    EXPECT_EQ(result.config.frame_limit, 500u);
    EXPECT_EQ(result.config.fault_start_frame, 150u);
    EXPECT_EQ(result.config.fault_length_frames, 50u);
    EXPECT_TRUE(result.config.quiet);
}

TEST(DaemonConfig, NoDatabaseDisablesRecording) {
    const ParseResult result = parse({"--no-database"});

    ASSERT_EQ(result.outcome, ParseOutcome::Run);
    EXPECT_FALSE(result.config.database_enabled);
}

TEST(DaemonConfig, HelpIsRecognised) {
    EXPECT_EQ(parse({"--help"}).outcome, ParseOutcome::ShowHelp);
    EXPECT_EQ(parse({"-h"}).outcome, ParseOutcome::ShowHelp);
}

TEST(DaemonConfig, HelpTextMentionsEveryOption) {
    const std::string help = build_help_text();

    for (const char* option :
         {"--rate-hz", "--host", "--port", "--database", "--no-database", "--sensor",
          "--i2c-device", "--i2c-address", "--frame-limit", "--fault-start-frame",
          "--fault-length-frames", "--quiet", "--help"}) {
        // std::string::npos means "not found".
        EXPECT_NE(help.find(option), std::string::npos) << option << " missing from help";
    }
}

TEST(DaemonConfig, I2cAddressAcceptsDecimalToo) {
    const ParseResult result = parse({"--i2c-address", "118"});  // 118 = 0x76

    ASSERT_EQ(result.outcome, ParseOutcome::Run);
    EXPECT_EQ(result.config.i2c_address, 0x76);
}

// ---- Errors ------------------------------------------------------------------

TEST(DaemonConfig, RejectsUnknownOption) {
    const ParseResult result = parse({"--speed", "9000"});

    EXPECT_EQ(result.outcome, ParseOutcome::Error);
    EXPECT_NE(result.error_message.find("--speed"), std::string::npos);
}

TEST(DaemonConfig, RejectsOptionWithMissingValue) {
    EXPECT_EQ(parse({"--port"}).outcome, ParseOutcome::Error);
}

TEST(DaemonConfig, RejectsTextWhereNumberIsNeeded) {
    EXPECT_EQ(parse({"--rate-hz", "fast"}).outcome, ParseOutcome::Error);
    EXPECT_EQ(parse({"--rate-hz", "50abc"}).outcome, ParseOutcome::Error);
    EXPECT_EQ(parse({"--rate-hz", ""}).outcome, ParseOutcome::Error);
    EXPECT_EQ(parse({"--rate-hz", "-5"}).outcome, ParseOutcome::Error);
    EXPECT_EQ(parse({"--rate-hz", "2.5"}).outcome, ParseOutcome::Error);
    EXPECT_EQ(parse({"--i2c-address", "0x"}).outcome, ParseOutcome::Error);
}

TEST(DaemonConfig, RejectsRateOutOfRange) {
    EXPECT_EQ(parse({"--rate-hz", "0"}).outcome, ParseOutcome::Error);
    EXPECT_EQ(parse({"--rate-hz", "1001"}).outcome, ParseOutcome::Error);
    EXPECT_EQ(parse({"--rate-hz", "1"}).outcome, ParseOutcome::Run);
    EXPECT_EQ(parse({"--rate-hz", "1000"}).outcome, ParseOutcome::Run);
}

TEST(DaemonConfig, RejectsPortOutOfRange) {
    EXPECT_EQ(parse({"--port", "0"}).outcome, ParseOutcome::Error);
    EXPECT_EQ(parse({"--port", "65536"}).outcome, ParseOutcome::Error);
    EXPECT_EQ(parse({"--port", "65535"}).outcome, ParseOutcome::Run);
}

TEST(DaemonConfig, RejectsI2cAddressOutOfRange) {
    EXPECT_EQ(parse({"--i2c-address", "0x07"}).outcome, ParseOutcome::Error);
    EXPECT_EQ(parse({"--i2c-address", "0x78"}).outcome, ParseOutcome::Error);
}

TEST(DaemonConfig, RejectsUnknownSensorName) {
    const ParseResult result = parse({"--sensor", "thermometer"});

    EXPECT_EQ(result.outcome, ParseOutcome::Error);
}

// A number too large for 64 bits must be an error, not a silent wrap-around.
TEST(DaemonConfig, RejectsNumberThatOverflows) {
    EXPECT_EQ(parse({"--frame-limit", "99999999999999999999999"}).outcome, ParseOutcome::Error);
}

}  // namespace
