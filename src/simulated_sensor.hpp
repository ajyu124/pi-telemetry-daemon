// =============================================================================
// simulated_sensor.hpp
//
// PURPOSE
//   A fake sensor that needs no hardware. It produces realistic, slowly
//   changing temperature, pressure and humidity values, and can pretend to
//   fail for a chosen stretch of reads so the fault handling can be seen
//   working.
//
// WHY IT EXISTS
//   - Lets the whole program run on a laptop with no Raspberry Pi.
//   - Lets a fault be triggered on demand. With real hardware you would have
//     to physically unplug the sensor at the right moment.
//
// DETERMINISTIC ON PURPOSE
//   The values are generated from a fixed random seed, so two runs produce
//   the same sequence. Repeatable behaviour is what makes automated tests
//   trustworthy. A "seed" is the starting number for a random number
//   generator. The same seed always gives the same sequence.
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <random>
#include <string>

#include "sensor_interface.hpp"

namespace telemetry {

struct SimulatedSensorConfig {
    // Simulated fault window. Reads are numbered from 0. Reads numbered
    //   fault_start_read  up to (but not including)
    //   fault_start_read + fault_length_reads
    // will fail. A fault_length_reads of 0 means "never fail".
    std::uint64_t fault_start_read = 0;
    std::uint64_t fault_length_reads = 0;

    // Starting number for the random noise generator.
    std::uint32_t random_seed = 12345;
};

class SimulatedSensor : public SensorInterface {
public:
    SimulatedSensor() = default;
    explicit SimulatedSensor(const SimulatedSensorConfig& config);

    // The `override` keyword asks the compiler to confirm that each of these
    // really matches a function in SensorInterface. It catches typos.
    bool initialize() override;
    std::optional<SensorReading> read_sensor() override;
    std::string sensor_name() const override { return "simulated"; }

    // How many times read_sensor() has been called so far.
    std::uint64_t reads_attempted() const { return reads_attempted_; }

private:
    bool is_inside_fault_window(std::uint64_t read_number) const;

    SimulatedSensorConfig config_;
    std::uint64_t reads_attempted_ = 0;
    // std::mt19937 is the standard library's general purpose random number
    // generator (the "Mersenne Twister").
    std::mt19937 random_generator_{12345};
};

}  // namespace telemetry
