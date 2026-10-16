// =============================================================================
// telemetry_daemon.hpp
//
// PURPOSE
//   The heart of the program. TelemetryDaemon ties every other piece together
//   and runs the main loop.
//
// WHAT IS A DAEMON?
//   A program that runs continuously in the background with no user
//   interaction, such as a web server or this telemetry sender.
//
// ONE CYCLE OF THE LOOP (execute_cycle)
//
//      +-------------+   +----------------+   +-------------+   +----------+
//      | read sensor |-->| update health  |-->| build frame |-->| send UDP |
//      +-------------+   | state machine  |   | + encode    |   +----------+
//                        +----------------+   +-------------+        |
//                                                                     v
//                                                              +-------------+
//                                                              | store in DB |
//                                                              +-------------+
//
//   run() repeats that cycle at a fixed rate using FixedRateTimer.
//
// WHAT HAPPENS WHEN A SENSOR READ FAILS?
//   The daemon still sends a frame on schedule. The frame repeats the last
//   good measurement, has its "fresh" flag cleared, and carries the current
//   health state and failure count. So the receiver can always tell the
//   difference between "the sensor died" (frames keep arriving, marked stale)
//   and "the link died" (frames stop arriving). A sender that goes silent on
//   error makes those two cases look identical.
//
// HOW THE PIECES ARE CONNECTED (DEPENDENCY INJECTION)
//   The daemon does not create its sensor, sender or store. They are handed
//   in through the constructor. This is called dependency injection. It is
//   why tests can pass in a scripted fake sensor and no network at all
//   (tests/test_telemetry_daemon.cpp), and why switching from the simulated
//   sensor to the real one changes nothing in this file.
// =============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <iosfwd>

#include "health_monitor.hpp"
#include "sensor_interface.hpp"
#include "telemetry_frame.hpp"
#include "telemetry_store.hpp"
#include "udp_sender.hpp"

namespace telemetry {

// Running totals, printed in the status line and the final summary.
struct DaemonStatistics {
    std::uint64_t frames_built = 0;
    std::uint64_t frames_sent = 0;
    std::uint64_t sensor_read_failures = 0;
    std::uint64_t send_failures = 0;
    std::uint64_t store_failures = 0;
    std::uint64_t missed_deadlines = 0;
};

// Settings for run().
struct RunOptions {
    std::uint32_t rate_hz = 50;
    // Stop after this many frames. 0 means no limit.
    std::uint64_t frame_limit = 0;
    // Where to print status lines. nullptr means print nothing.
    std::ostream* status_output = nullptr;
};

class TelemetryDaemon {
public:
    //   sensor:        where measurements come from. Required.
    //   sender:        where frames are sent. May be nullptr (send nothing).
    //   store:         where frames are recorded. May be nullptr (record nothing).
    //   health_config: thresholds for the health state machine.
    // The daemon does not own these objects. They must outlive it.
    TelemetryDaemon(SensorInterface& sensor,
                    UdpSender* sender,
                    TelemetryStore* store,
                    const HealthMonitorConfig& health_config = HealthMonitorConfig{});

    // Performs exactly one cycle (see the diagram above) and returns the
    // frame that was produced. `timestamp_milliseconds` is passed in instead
    // of read from the clock so that tests are repeatable.
    TelemetryFrame execute_cycle(std::uint64_t timestamp_milliseconds);

    // Runs cycles at a fixed rate until either the frame limit is reached or
    // `should_keep_running()` returns false. That function is how main.cpp
    // tells the loop that Ctrl+C was pressed.
    void run(const RunOptions& options, const std::function<bool()>& should_keep_running);

    const DaemonStatistics& statistics() const { return statistics_; }
    HealthState health_state() const { return health_monitor_.current_state(); }

private:
    void print_status_line(std::ostream& output,
                           const TelemetryFrame& frame,
                           double elapsed_seconds) const;

    SensorInterface& sensor_;
    UdpSender* sender_;
    TelemetryStore* store_;
    HealthMonitor health_monitor_;
    DaemonStatistics statistics_;

    std::uint32_t next_sequence_number_ = 0;
    // The most recent successful measurement, repeated in frames while the
    // sensor is failing. All zeros until the first successful read.
    SensorReading last_good_reading_;
};

// Returns the current wall-clock time as milliseconds since 1970-01-01 UTC
// (known as "Unix time"). Used for the timestamp inside each frame.
std::uint64_t current_unix_time_milliseconds();

}  // namespace telemetry
