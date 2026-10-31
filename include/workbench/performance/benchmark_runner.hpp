#pragma once

#include "workbench/performance/hardware_info.hpp"
#include "workbench/performance/worker_pool.hpp"
#include "workbench/performance/workload_generator.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace workbench {

inline constexpr double kNumericalToleranceC = 1.0e-8;
inline constexpr std::size_t kMinimumRepetitions = 1;
inline constexpr std::size_t kMaximumRepetitions = 20;
// executions * batch_size * mesh_size^3. Caps a single benchmark request.
inline constexpr std::uint64_t kMaximumScheduleCost = 80000000000ull;

struct CaseResult {
    double maximum_temperature_C = 0.0;
    double midpoint_temperature_C = 0.0;
    std::vector<double> temperature_C;
};

struct BenchmarkConfig {
    std::string benchmark_id;
    std::uint32_t seed = 1;
    std::size_t batch_size = 8;
    std::size_t mesh_size = 32;
    std::size_t repetitions = 5;
    // Empty means every worker count from 1 through maximum_supported_workers().
    std::vector<std::size_t> worker_counts;
};

enum class BenchmarkPhase {
    Workload,
    Warmup,
    Serial,
    Parallel,
};

struct BenchmarkProgress {
    BenchmarkPhase phase = BenchmarkPhase::Workload;
    std::size_t worker_count = 0;
    std::size_t repetition = 0;
    std::size_t repetitions = 0;
    std::size_t completed_steps = 0;
    std::size_t total_steps = 0;
};

struct ConfigurationResult {
    std::string execution_mode;
    std::size_t worker_count = 0;
    // One discarded solve. This time is not part of execution_times_s or the median.
    double warmup_time_s = 0.0;
    std::vector<double> execution_times_s;
    double median_execution_time_s = 0.0;
    double speedup = 0.0;
    double parallel_efficiency = 0.0;
    double maximum_absolute_error_C = 0.0;
    bool numerical_validation_passed = false;
};

struct BenchmarkResult {
    std::string benchmark_id;
    std::uint32_t seed = 0;
    std::size_t batch_size = 0;
    std::size_t mesh_size = 0;
    std::size_t repetitions = 0;
    double workload_generation_time_s = 0.0;
    // Sum of the per-configuration warm-up solves. Excluded from every median.
    double warmup_time_s = 0.0;
    double total_duration_s = 0.0;
    std::string timestamp_utc;
    HardwareInfo hardware;
    // Serial stays at configurations[0]. Parallel entries follow in worker-count order.
    std::vector<ConfigurationResult> configurations;
    // One entry per measured round. Each entry lists "serial" or "parallel:N" in the
    // order that round actually ran. Round r is round 0 rotated left by r positions.
    std::vector<std::vector<std::string>> measured_round_orders;
    double maximum_absolute_error_C = 0.0;
    bool numerical_validation_passed = false;
};

using BenchmarkProgressCallback = std::function<void(const BenchmarkProgress&)>;

// Fills an empty worker_counts list. Throws std::invalid_argument for a count
// outside 1..4, a count above the logical CPU limit, a duplicate, or a
// schedule whose estimated dense-factorization cost exceeds kMaximumScheduleCost.
void validate_benchmark_config(BenchmarkConfig& config);

double median_value(std::vector<double> samples);

// Solves every case in order on the calling thread. The optional counter is
// incremented after each successful solve and is not required for correctness.
std::vector<CaseResult> execute_serial(const Workload& workload,
                                       std::atomic<std::size_t>* cases_completed = nullptr);

// Solves every case exactly once on the pool. Result slot i belongs only to
// case i. The pool is not created here; the caller owns its lifetime so thread
// startup can stay outside a measured interval.
std::vector<CaseResult> execute_parallel(const Workload& workload,
                                         WorkerPool& pool,
                                         std::atomic<std::size_t>* cases_completed = nullptr);

// Largest absolute nodal temperature difference, in degrees Celsius.
double maximum_absolute_temperature_error(const std::vector<CaseResult>& reference,
                                          const std::vector<CaseResult>& candidate);

// Generates one workload. Every measured configuration, the serial baseline and
// each worker pool, then runs one untimed warm-up on that same workload. The
// warm-up durations are retained separately and are not part of the medians.
// Measured rounds rotate the configuration order so the same configuration is
// not always first. Speedup(p) = serial_median / parallel_median.
// Efficiency(p) = speedup / p. Compute timers use steady_clock and cover solver
// execution only.
BenchmarkResult run_benchmark(const BenchmarkConfig& config,
                              const BenchmarkProgressCallback& progress = {},
                              std::atomic<std::size_t>* cases_completed = nullptr);

std::string benchmark_result_csv(const BenchmarkResult& result);

}
