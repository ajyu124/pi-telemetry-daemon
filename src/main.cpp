// =============================================================================
// main.cpp
//
// PURPOSE
//   The program's entry point. It contains no telemetry logic of its own.
//   Its only job is to build the real objects and connect them:
//
//     1. read the command line                (daemon_config.hpp)
//     2. create the chosen sensor             (simulated_sensor / bme280_sensor)
//     3. open the UDP sender                  (udp_sender.hpp)
//     4. open the SQLite store                (telemetry_store.hpp)
//     5. install Ctrl+C handling              (below)
//     6. hand everything to TelemetryDaemon   (telemetry_daemon.hpp)
//     7. print a summary and exit
//
//   Keeping main() this thin is deliberate. Everything testable lives in the
//   `telemetry_core` library, and the unit tests link against that library.
//
// EXIT CODES
//   0  finished normally (frame limit reached, or stopped with Ctrl+C)
//   1  could not start (bad option, sensor or socket would not open)
// =============================================================================
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "daemon_config.hpp"
#include "sensor_interface.hpp"
#include "simulated_sensor.hpp"
#include "telemetry_daemon.hpp"
#include "telemetry_store.hpp"
#include "udp_sender.hpp"

// The real sensor needs the Linux I2C interface. CMakeLists.txt defines
// TELEMETRY_HAS_LINUX_I2C only when building on Linux.
#ifdef TELEMETRY_HAS_LINUX_I2C
#include "bme280_sensor.hpp"
#include "linux_i2c_bus.hpp"
#endif

namespace {

// ---- Ctrl+C handling ----------------------------------------------------------
//
// A "signal" is how the operating system interrupts a program. Pressing
// Ctrl+C sends SIGINT. `systemctl stop` (used on the Pi) sends SIGTERM. By
// default both kill the program instantly, which would lose any frames not
// yet committed to the database.
//
// Instead we install a handler that only sets a flag. The main loop checks
// the flag once per cycle and shuts down cleanly.
//
// A signal handler can interrupt the program at ANY instruction, so it is
// only allowed to do a tiny set of safe things. Setting a variable of type
// `volatile std::sig_atomic_t` is the one thing the C++ standard guarantees
// is safe. That is why the handler does nothing else.
volatile std::sig_atomic_t g_stop_requested = 0;

void handle_stop_signal(int /*signal_number*/) {
    g_stop_requested = 1;
}

bool should_keep_running() {
    return g_stop_requested == 0;
}

const char* sensor_type_to_text(telemetry::SensorType type) {
    return type == telemetry::SensorType::Simulated ? "simulated" : "bme280";
}

}  // namespace

int main(int argument_count, char* argument_values[]) {
    using namespace telemetry;

    // ---- 1. Read the command line ----
    // argument_values[0] is the program's own name, so start from 1.
    const std::vector<std::string> arguments(argument_values + 1,
                                             argument_values + argument_count);
    const ParseResult parsed = parse_command_line(arguments);

    if (parsed.outcome == ParseOutcome::ShowHelp) {
        std::cout << build_help_text();
        return EXIT_SUCCESS;
    }
    if (parsed.outcome == ParseOutcome::Error) {
        std::cerr << "Error: " << parsed.error_message << "\n";
        return EXIT_FAILURE;
    }
    const DaemonConfig& config = parsed.config;

    // ---- 2. Create the chosen sensor ----
    // std::unique_ptr is a "smart pointer": it owns a heap object and deletes
    // it automatically when it goes out of scope. It is used here because the
    // concrete sensor class is only decided at run time.
    std::unique_ptr<SensorInterface> sensor;

#ifdef TELEMETRY_HAS_LINUX_I2C
    // Declared out here because the bus must stay alive as long as the sensor.
    std::unique_ptr<LinuxI2cBus> i2c_bus;
#endif

    if (config.sensor_type == SensorType::Simulated) {
        SimulatedSensorConfig simulated_config;
        simulated_config.fault_start_read = config.fault_start_frame;
        simulated_config.fault_length_reads = config.fault_length_frames;
        sensor = std::make_unique<SimulatedSensor>(simulated_config);
    } else {
#ifdef TELEMETRY_HAS_LINUX_I2C
        i2c_bus = std::make_unique<LinuxI2cBus>(config.i2c_device_path, config.i2c_address);
        sensor = std::make_unique<Bme280Sensor>(*i2c_bus);
#else
        std::cerr << "Error: the bme280 sensor is only supported on Linux.\n";
        return EXIT_FAILURE;
#endif
    }

    if (!sensor->initialize()) {
        std::cerr << "Error: could not initialize the '" << sensor->sensor_name() << "' sensor.\n";
#ifdef TELEMETRY_HAS_LINUX_I2C
        if (i2c_bus != nullptr && !i2c_bus->last_error().empty()) {
            std::cerr << "       " << i2c_bus->last_error() << "\n";
        }
#endif
        std::cerr << "       To run without hardware, use: --sensor simulated\n";
        return EXIT_FAILURE;
    }

    // ---- 3. Open the UDP sender ----
    UdpSender sender;
    if (!sender.open(config.destination_ip, config.destination_port)) {
        std::cerr << "Error: " << sender.last_error() << "\n";
        return EXIT_FAILURE;
    }

    // ---- 4. Open the SQLite store ----
    TelemetryStore store;
    TelemetryStore* store_pointer = nullptr;  // stays null when recording is off
    if (config.database_enabled) {
        // Commit once per second's worth of frames.
        if (!store.open(config.database_path, config.rate_hz)) {
            std::cerr << "Error: " << store.last_error() << "\n";
            return EXIT_FAILURE;
        }
        store_pointer = &store;
    }

    // ---- 5. Install Ctrl+C handling ----
    std::signal(SIGINT, handle_stop_signal);
    std::signal(SIGTERM, handle_stop_signal);

    // ---- 6. Run ----
    std::cout << "telemetry_daemon starting\n"
              << "  sensor:      " << sensor_type_to_text(config.sensor_type) << "\n"
              << "  rate:        " << config.rate_hz << " Hz\n"
              << "  destination: " << config.destination_ip << ":" << config.destination_port
              << " (UDP)\n"
              << "  database:    "
              << (config.database_enabled ? config.database_path : std::string("disabled")) << "\n"
              << "  frame limit: "
              << (config.frame_limit == 0 ? std::string("none, press Ctrl+C to stop")
                                          : std::to_string(config.frame_limit))
              << std::endl;

    TelemetryDaemon daemon(*sensor, &sender, store_pointer);

    RunOptions run_options;
    run_options.rate_hz = config.rate_hz;
    run_options.frame_limit = config.frame_limit;
    run_options.status_output = config.quiet ? nullptr : &std::cout;

    daemon.run(run_options, should_keep_running);

    // ---- 7. Shut down cleanly and summarise ----
    store.close();  // commits any frames still waiting in the open batch
    sender.close();

    const DaemonStatistics& statistics = daemon.statistics();
    std::cout << "\ntelemetry_daemon stopped"
              << (g_stop_requested != 0 ? " (stop signal received)" : " (frame limit reached)")
              << "\n"
              << "  frames built:         " << statistics.frames_built << "\n"
              << "  frames sent:          " << statistics.frames_sent << "\n"
              << "  sensor read failures: " << statistics.sensor_read_failures << "\n"
              << "  send failures:        " << statistics.send_failures << "\n"
              << "  store failures:       " << statistics.store_failures << "\n"
              << "  missed deadlines:     " << statistics.missed_deadlines << "\n"
              << "  final health state:   " << health_state_to_text(daemon.health_state())
              << std::endl;

    return EXIT_SUCCESS;
}
