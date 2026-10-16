// =============================================================================
// simulated_sensor.cpp
//
// Implementation of SimulatedSensor. See simulated_sensor.hpp for the purpose.
//
// Each value is a baseline, plus a slow sine wave (so the numbers drift the
// way real room conditions do), plus a little random noise (so they jitter
// the way a real sensor does).
// =============================================================================
#include "simulated_sensor.hpp"

#include <cmath>

namespace telemetry {

namespace {

constexpr double kTwoPi = 6.283185307179586;

// Baselines: an ordinary room at sea level.
constexpr double kBaseTemperatureCentiCelsius = 2200.0;  // 22.00 C
constexpr double kBasePressurePascals = 101325.0;        // standard sea level
constexpr double kBaseHumidityCentiPercent = 4500.0;     // 45.00 %

// How far each value swings above and below its baseline.
constexpr double kTemperatureSwing = 200.0;  // +/- 2.00 C
constexpr double kPressureSwing = 50.0;      // +/- 50 Pa
constexpr double kHumiditySwing = 500.0;     // +/- 5.00 %

// How many reads one full up-and-down wave takes. At 50 reads per second,
// 1500 reads is 30 seconds. The three differ so the values do not move in
// lockstep.
constexpr double kTemperaturePeriodReads = 1500.0;
constexpr double kPressurePeriodReads = 3000.0;
constexpr double kHumidityPeriodReads = 2250.0;

// Maximum random jitter added to each value, in that value's own unit.
constexpr int kTemperatureNoise = 5;
constexpr int kPressureNoise = 3;
constexpr int kHumidityNoise = 10;

}  // namespace

SimulatedSensor::SimulatedSensor(const SimulatedSensorConfig& config)
    : config_(config), random_generator_(config.random_seed) {}

bool SimulatedSensor::initialize() {
    // Nothing to open. Reset so that initialize() gives a clean restart.
    reads_attempted_ = 0;
    random_generator_.seed(config_.random_seed);
    return true;
}

bool SimulatedSensor::is_inside_fault_window(std::uint64_t read_number) const {
    if (config_.fault_length_reads == 0) {
        return false;
    }
    const std::uint64_t fault_end_read = config_.fault_start_read + config_.fault_length_reads;
    return read_number >= config_.fault_start_read && read_number < fault_end_read;
}

std::optional<SensorReading> SimulatedSensor::read_sensor() {
    const std::uint64_t read_number = reads_attempted_;
    ++reads_attempted_;

    if (is_inside_fault_window(read_number)) {
        return std::nullopt;  // pretend the sensor did not answer
    }

    const double n = static_cast<double>(read_number);

    // uniform_int_distribution(a, b) returns a whole number from a to b.
    std::uniform_int_distribution<int> temperature_noise(-kTemperatureNoise, kTemperatureNoise);
    std::uniform_int_distribution<int> pressure_noise(-kPressureNoise, kPressureNoise);
    std::uniform_int_distribution<int> humidity_noise(-kHumidityNoise, kHumidityNoise);

    const double temperature =
        kBaseTemperatureCentiCelsius +
        kTemperatureSwing * std::sin(kTwoPi * n / kTemperaturePeriodReads) +
        temperature_noise(random_generator_);

    const double pressure =
        kBasePressurePascals +
        kPressureSwing * std::sin(kTwoPi * n / kPressurePeriodReads) +
        pressure_noise(random_generator_);

    const double humidity =
        kBaseHumidityCentiPercent +
        kHumiditySwing * std::sin(kTwoPi * n / kHumidityPeriodReads) +
        humidity_noise(random_generator_);

    SensorReading reading;
    reading.temperature_centi_celsius = static_cast<std::int32_t>(std::lround(temperature));
    reading.pressure_pascals = static_cast<std::uint32_t>(std::lround(pressure));
    reading.humidity_centi_percent = static_cast<std::uint32_t>(std::lround(humidity));
    return reading;
}

}  // namespace telemetry
