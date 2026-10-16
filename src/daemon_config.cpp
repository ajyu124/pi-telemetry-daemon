// =============================================================================
// daemon_config.cpp
//
// Implementation of parse_command_line(). See daemon_config.hpp.
// =============================================================================
#include "daemon_config.hpp"

#include <charconv>
#include <optional>

namespace telemetry {

namespace {

constexpr std::uint32_t kMinimumRateHz = 1;
constexpr std::uint32_t kMaximumRateHz = 1000;
constexpr std::uint64_t kMaximumPort = 65535;
// Valid 7-bit I2C addresses for ordinary chips run from 0x08 to 0x77.
constexpr std::uint64_t kMinimumI2cAddress = 0x08;
constexpr std::uint64_t kMaximumI2cAddress = 0x77;

// Converts text to a whole number. Accepts decimal ("50") and hexadecimal
// with a 0x prefix ("0x77"). Returns std::nullopt if the text is not a clean
// number, for example "50abc", "-3" or "".
//
// std::from_chars is the C++17 number parser. Unlike older functions such as
// atoi, it reports errors instead of silently returning 0.
std::optional<std::uint64_t> parse_whole_number(const std::string& text) {
    int base = 10;
    const char* first = text.data();
    const char* last = text.data() + text.size();

    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        first += 2;
    }
    if (first == last) {
        return std::nullopt;
    }

    std::uint64_t value = 0;
    const std::from_chars_result result = std::from_chars(first, last, value, base);
    const bool whole_text_was_used = (result.ptr == last);
    if (result.ec != std::errc() || !whole_text_was_used) {
        return std::nullopt;
    }
    return value;
}

ParseResult make_error(const std::string& message) {
    ParseResult result;
    result.outcome = ParseOutcome::Error;
    result.error_message = message;
    return result;
}

}  // namespace

std::string build_help_text() {
    return
        "telemetry_daemon: reads a sensor at a fixed rate and streams telemetry frames over UDP.\n"
        "\n"
        "Usage: telemetry_daemon [options]\n"
        "\n"
        "Options:\n"
        "  --rate-hz N              Frames per second, 1 to 1000 (default 50)\n"
        "  --host IP                Destination IPv4 address (default 127.0.0.1)\n"
        "  --port N                 Destination UDP port, 1 to 65535 (default 5005)\n"
        "  --database PATH          SQLite file to record frames in (default telemetry.db)\n"
        "  --no-database            Do not record frames to a database\n"
        "  --sensor NAME            'simulated' (default) or 'bme280' (Raspberry Pi only)\n"
        "  --i2c-device PATH        I2C bus device for bme280 (default /dev/i2c-1)\n"
        "  --i2c-address N          I2C address for bme280, 0x77 or 0x76 (default 0x77)\n"
        "  --frame-limit N          Stop after N frames, 0 = run until Ctrl+C (default 0)\n"
        "  --fault-start-frame N    Simulated sensor: first frame of a simulated fault\n"
        "  --fault-length-frames N  Simulated sensor: how many frames the fault lasts (0 = none)\n"
        "  --quiet                  Do not print the once-per-second status line\n"
        "  --help                   Show this text\n"
        "\n"
        "Example (10 seconds, with a 1 second sensor fault starting at second 3):\n"
        "  telemetry_daemon --frame-limit 500 --fault-start-frame 150 --fault-length-frames 50\n";
}

ParseResult parse_command_line(const std::vector<std::string>& arguments) {
    ParseResult result;
    DaemonConfig& config = result.config;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string& option = arguments[index];

        // ---- Options that take no value ----
        if (option == "--help" || option == "-h") {
            result.outcome = ParseOutcome::ShowHelp;
            return result;
        }
        if (option == "--no-database") {
            config.database_enabled = false;
            continue;
        }
        if (option == "--quiet") {
            config.quiet = true;
            continue;
        }

        // ---- Options that take one value ----
        const bool is_known_option_with_value =
            option == "--rate-hz" || option == "--host" || option == "--port" ||
            option == "--database" || option == "--sensor" || option == "--i2c-device" ||
            option == "--i2c-address" || option == "--frame-limit" ||
            option == "--fault-start-frame" || option == "--fault-length-frames";

        if (!is_known_option_with_value) {
            return make_error("unknown option '" + option + "' (try --help)");
        }
        if (index + 1 >= arguments.size()) {
            return make_error("option '" + option + "' needs a value");
        }
        const std::string& value = arguments[++index];

        // Text options.
        if (option == "--host") {
            config.destination_ip = value;
            continue;
        }
        if (option == "--database") {
            config.database_path = value;
            continue;
        }
        if (option == "--i2c-device") {
            config.i2c_device_path = value;
            continue;
        }
        if (option == "--sensor") {
            if (value == "simulated") {
                config.sensor_type = SensorType::Simulated;
            } else if (value == "bme280") {
                config.sensor_type = SensorType::Bme280;
            } else {
                return make_error("--sensor must be 'simulated' or 'bme280', not '" + value + "'");
            }
            continue;
        }

        // Every remaining option takes a whole number.
        const std::optional<std::uint64_t> number = parse_whole_number(value);
        if (!number) {
            return make_error("option '" + option + "' needs a whole number, not '" + value + "'");
        }

        if (option == "--rate-hz") {
            if (*number < kMinimumRateHz || *number > kMaximumRateHz) {
                return make_error("--rate-hz must be between 1 and 1000");
            }
            config.rate_hz = static_cast<std::uint32_t>(*number);
        } else if (option == "--port") {
            if (*number < 1 || *number > kMaximumPort) {
                return make_error("--port must be between 1 and 65535");
            }
            config.destination_port = static_cast<std::uint16_t>(*number);
        } else if (option == "--i2c-address") {
            if (*number < kMinimumI2cAddress || *number > kMaximumI2cAddress) {
                return make_error("--i2c-address must be between 0x08 and 0x77");
            }
            config.i2c_address = static_cast<std::uint8_t>(*number);
        } else if (option == "--frame-limit") {
            config.frame_limit = *number;
        } else if (option == "--fault-start-frame") {
            config.fault_start_frame = *number;
        } else if (option == "--fault-length-frames") {
            config.fault_length_frames = *number;
        }
    }

    return result;
}

}  // namespace telemetry
