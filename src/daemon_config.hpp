// =============================================================================
// daemon_config.hpp
//
// PURPOSE
//   Holds every setting the daemon can be started with, and turns the
//   command line (the words typed after the program name) into those
//   settings.
//
// EXAMPLE
//   ./build/telemetry_daemon --rate-hz 50 --port 5005 --frame-limit 500
//
//   Run `./build/telemetry_daemon --help` for the full list.
//
// WHY PARSING IS SEPARATE FROM main()
//   parse_command_line() takes a list of strings and returns a result. It
//   never prints and never exits the program. That makes every rule (valid
//   ranges, unknown options, missing values) unit testable in
//   tests/test_daemon_config.cpp without launching a process.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace telemetry {

enum class SensorType {
    Simulated,  // fake data, no hardware (the default)
    Bme280,     // real BME280 chip over I2C, Raspberry Pi only
};

struct DaemonConfig {
    // How many frames per second to produce. Hz (hertz) means "per second".
    std::uint32_t rate_hz = 50;

    // Where to send frames. 127.0.0.1 is "this same computer" (localhost).
    std::string destination_ip = "127.0.0.1";
    std::uint16_t destination_port = 5005;

    // Local SQLite recording.
    bool database_enabled = true;
    std::string database_path = "telemetry.db";

    // Which sensor to use.
    SensorType sensor_type = SensorType::Simulated;
    std::string i2c_device_path = "/dev/i2c-1";
    std::uint8_t i2c_address = 0x77;

    // Stop after this many frames. 0 means run until Ctrl+C.
    std::uint64_t frame_limit = 0;

    // Simulated sensor only: make reads fail for `fault_length_frames` frames
    // starting at frame number `fault_start_frame`. A length of 0 disables it.
    std::uint64_t fault_start_frame = 0;
    std::uint64_t fault_length_frames = 0;

    // When true, do not print the once-per-second status line.
    bool quiet = false;
};

enum class ParseOutcome {
    Run,       // settings are valid, start the daemon
    ShowHelp,  // the user asked for --help
    Error,     // something was wrong, see error_message
};

struct ParseResult {
    ParseOutcome outcome = ParseOutcome::Run;
    DaemonConfig config;
    std::string error_message;  // only set when outcome == Error
};

// Converts command line arguments into a DaemonConfig.
// `arguments` must NOT include the program name itself.
ParseResult parse_command_line(const std::vector<std::string>& arguments);

// Returns the text shown for --help.
std::string build_help_text();

}  // namespace telemetry
