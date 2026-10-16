// =============================================================================
// telemetry_daemon.cpp
//
// Implementation of TelemetryDaemon. The cycle diagram and the design
// reasoning are in telemetry_daemon.hpp.
// =============================================================================
#include "telemetry_daemon.hpp"

#include <chrono>
#include <cstdio>
#include <optional>
#include <ostream>

#include "fixed_rate_timer.hpp"

namespace telemetry {

std::uint64_t current_unix_time_milliseconds() {
    // system_clock is the wall clock. It is the right clock for a timestamp
    // people will read. (The loop timing uses steady_clock instead. See
    // fixed_rate_timer.hpp for why they differ.)
    const auto now = std::chrono::system_clock::now();
    const auto since_epoch = now.time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch).count());
}

TelemetryDaemon::TelemetryDaemon(SensorInterface& sensor,
                                 UdpSender* sender,
                                 TelemetryStore* store,
                                 const HealthMonitorConfig& health_config)
    : sensor_(sensor), sender_(sender), store_(store), health_monitor_(health_config) {}

TelemetryFrame TelemetryDaemon::execute_cycle(std::uint64_t timestamp_milliseconds) {
    // ---- Step 1: read the sensor ----
    const std::optional<SensorReading> reading = sensor_.read_sensor();
    const bool read_succeeded = reading.has_value();

    // ---- Step 2: update the health state machine ----
    if (read_succeeded) {
        last_good_reading_ = *reading;
        health_monitor_.record_read_success();
    } else {
        ++statistics_.sensor_read_failures;
        health_monitor_.record_read_failure();
    }

    // ---- Step 3: build the frame ----
    TelemetryFrame frame;
    frame.health_state = health_monitor_.current_state();
    frame.reading_is_fresh = read_succeeded;
    frame.consecutive_read_failures = health_monitor_.consecutive_failures();
    frame.sequence_number = next_sequence_number_;
    frame.timestamp_milliseconds = timestamp_milliseconds;
    // On failure this repeats the last good values, flagged as not fresh.
    frame.reading = last_good_reading_;

    // The sequence number is an unsigned 32-bit integer, so after about 4.29
    // billion frames it wraps around to 0. At 50 Hz that takes 2.7 years.
    ++next_sequence_number_;
    ++statistics_.frames_built;

    // ---- Step 4: send it over the network ----
    if (sender_ != nullptr) {
        const EncodedFrame encoded = encode_frame(frame);
        if (sender_->send_bytes(encoded.data(), encoded.size())) {
            ++statistics_.frames_sent;
        } else {
            ++statistics_.send_failures;
        }
    }

    // ---- Step 5: record it locally ----
    // Done after sending so that a slow disk write delays the frame on the
    // network as little as possible.
    if (store_ != nullptr) {
        if (!store_->store_frame(frame)) {
            ++statistics_.store_failures;
        }
    }

    return frame;
}

void TelemetryDaemon::run(const RunOptions& options,
                          const std::function<bool()>& should_keep_running) {
    FixedRateTimer timer(static_cast<double>(options.rate_hz));
    timer.start();

    const auto loop_start_time = std::chrono::steady_clock::now();
    HealthState previously_printed_state = health_monitor_.current_state();
    std::uint64_t frames_this_run = 0;

    while (should_keep_running()) {
        const TelemetryFrame frame = execute_cycle(current_unix_time_milliseconds());
        ++frames_this_run;
        statistics_.missed_deadlines = timer.missed_deadline_count();

        if (options.status_output != nullptr) {
            // Print once per second, and also immediately whenever the health
            // state changes so a fault is never hidden between status lines.
            const bool one_second_has_passed = (frames_this_run % options.rate_hz) == 0;
            const bool state_changed = frame.health_state != previously_printed_state;
            if (one_second_has_passed || state_changed) {
                const std::chrono::duration<double> elapsed =
                    std::chrono::steady_clock::now() - loop_start_time;
                print_status_line(*options.status_output, frame, elapsed.count());
                previously_printed_state = frame.health_state;
            }
        }

        if (options.frame_limit != 0 && frames_this_run >= options.frame_limit) {
            break;
        }

        timer.wait_for_next_tick();
    }

    statistics_.missed_deadlines = timer.missed_deadline_count();
}

void TelemetryDaemon::print_status_line(std::ostream& output,
                                        const TelemetryFrame& frame,
                                        double elapsed_seconds) const {
    // snprintf formats text into a fixed-size buffer. It is used here instead
    // of stream formatting because column-aligned numbers are much shorter to
    // write this way. The buffer is larger than the longest possible line.
    char line[256];
    std::snprintf(
        line, sizeof(line),
        "[%7.1f s] seq=%-8u state=%-8s fresh=%-3s temp=%6.2f C  pressure=%6u Pa  "
        "humidity=%5.2f %%  read_failures=%llu  missed_deadlines=%llu",
        elapsed_seconds,
        static_cast<unsigned>(frame.sequence_number),
        health_state_to_text(frame.health_state),
        frame.reading_is_fresh ? "yes" : "no",
        frame.reading.temperature_centi_celsius / 100.0,
        static_cast<unsigned>(frame.reading.pressure_pascals),
        frame.reading.humidity_centi_percent / 100.0,
        static_cast<unsigned long long>(statistics_.sensor_read_failures),
        static_cast<unsigned long long>(statistics_.missed_deadlines));
    // std::endl ends the line AND flushes, so the line appears immediately
    // even when output is being piped to a file or another program.
    output << line << std::endl;
}

}  // namespace telemetry
