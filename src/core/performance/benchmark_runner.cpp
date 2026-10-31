#include "workbench/performance/benchmark_runner.hpp"

#include "workbench/thermal_solver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace workbench {
namespace {

std::string utc_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm calendar{};
#if defined(_WIN32)
    gmtime_s(&calendar, &time);
#else
    gmtime_r(&time, &calendar);
#endif
    char buffer[32];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &calendar) == 0) return {};
    return buffer;
}

std::string make_benchmark_id() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    static std::atomic<unsigned> counter{0};
    return "bench-" + std::to_string(seconds) + "-" + std::to_string(counter.fetch_add(1));
}

double elapsed_seconds(std::chrono::steady_clock::time_point start,
                       std::chrono::steady_clock::time_point finish) {
    return std::chrono::duration<double>(finish - start).count();
}

CaseResult solve_case(const ThermalCase& item) {
    ThermalResult solved = solve_steady_1d(item.config, item.elements);
    CaseResult result;
    result.temperature_C = std::move(solved.temperature_C);
    if (result.temperature_C.empty()) {
        throw std::runtime_error("Thermal solver returned no nodal temperatures.");
    }
    result.maximum_temperature_C =
        *std::max_element(result.temperature_C.begin(), result.temperature_C.end());
    result.midpoint_temperature_C = result.temperature_C[result.temperature_C.size() / 2];
    return result;
}

std::string failure_message(std::size_t index, const std::exception& error) {
    return "Thermal case " + std::to_string(index) + " failed: " + error.what();
}

void notify(const BenchmarkProgressCallback& progress, BenchmarkProgress state) {
    if (progress) progress(state);
}

std::string csv_escape(const std::string& value) {
    const bool quoted = value.find_first_of(",\"\r\n") != std::string::npos;
    if (!quoted) return value;
    std::string escaped = "\"";
    for (char character : value) {
        if (character == '"') escaped += "\"\"";
        else escaped.push_back(character);
    }
    escaped.push_back('"');
    return escaped;
}

std::string csv_double(double value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return stream.str();
}

std::string csv_optional_unsigned(const std::optional<unsigned>& value) {
    if (!value) return {};
    return std::to_string(*value);
}

std::string csv_optional_u64(const std::optional<std::uint64_t>& value) {
    if (!value) return {};
    return std::to_string(*value);
}

std::uint64_t schedule_cost(const BenchmarkConfig& config) {
    const std::uint64_t mesh = static_cast<std::uint64_t>(config.mesh_size);
    // One warm-up plus the measured repetitions, for serial and every worker count.
    const std::uint64_t configurations = 1ull + static_cast<std::uint64_t>(config.worker_counts.size());
    const std::uint64_t executions = configurations * (1ull + static_cast<std::uint64_t>(config.repetitions));
    return executions * static_cast<std::uint64_t>(config.batch_size) * mesh * mesh * mesh;
}

std::string configuration_label(const std::string& mode, std::size_t worker_count) {
    if (mode == "serial") return "serial";
    return "parallel:" + std::to_string(worker_count);
}

ConfigurationResult summarize(const std::string& mode,
                              std::size_t worker_count,
                              std::vector<double> times,
                              double serial_median,
                              double maximum_error) {
    ConfigurationResult result;
    result.execution_mode = mode;
    result.worker_count = worker_count;
    result.execution_times_s = times;
    result.median_execution_time_s = median_value(times);
    if (!(result.median_execution_time_s > 0.0) || !std::isfinite(result.median_execution_time_s)) {
        throw std::runtime_error("Measured execution time must be positive and finite.");
    }
    if (!(serial_median > 0.0) || !std::isfinite(serial_median)) {
        throw std::runtime_error("Serial reference time must be positive and finite.");
    }
    result.speedup = serial_median / result.median_execution_time_s;
    result.parallel_efficiency = result.speedup / static_cast<double>(worker_count);
    result.maximum_absolute_error_C = maximum_error;
    result.numerical_validation_passed = maximum_error <= kNumericalToleranceC;
    return result;
}

}  // namespace

void validate_benchmark_config(BenchmarkConfig& config) {
    WorkloadConfig workload;
    workload.seed = config.seed;
    workload.batch_size = config.batch_size;
    workload.mesh_size = config.mesh_size;
    validate_workload_config(workload);

    if (config.repetitions < kMinimumRepetitions || config.repetitions > kMaximumRepetitions) {
        throw std::invalid_argument(
            "Repetitions must be from " + std::to_string(kMinimumRepetitions) + " to " +
            std::to_string(kMaximumRepetitions) + ".");
    }

    if (config.worker_counts.empty()) {
        const unsigned limit = maximum_supported_workers();
        config.worker_counts.clear();
        for (unsigned count = 1; count <= limit; ++count) config.worker_counts.push_back(count);
    } else {
        std::vector<std::size_t> unique_counts;
        unique_counts.reserve(config.worker_counts.size());
        const unsigned limit = maximum_supported_workers();
        for (std::size_t count : config.worker_counts) {
            if (count < 1 || count > 4) {
                throw std::invalid_argument("Worker count must be 1, 2, 3, or 4.");
            }
            if (count > limit) {
                throw std::invalid_argument(
                    "Worker count of " + std::to_string(count) +
                    " exceeds the " + std::to_string(limit) +
                    " worker limit for the logical CPUs available to this process.");
            }
            if (std::find(unique_counts.begin(), unique_counts.end(), count) != unique_counts.end()) {
                throw std::invalid_argument("Worker counts must be unique.");
            }
            unique_counts.push_back(count);
        }
        std::sort(unique_counts.begin(), unique_counts.end());
        config.worker_counts = std::move(unique_counts);
    }

    const std::uint64_t cost = schedule_cost(config);
    if (cost > kMaximumScheduleCost) {
        throw std::invalid_argument(
            "This benchmark schedules about " + std::to_string(cost) +
            " cubic-mesh operations, above the limit of " + std::to_string(kMaximumScheduleCost) +
            ". Reduce the batch size, mesh size, repetitions, or worker counts.");
    }
}

double median_value(std::vector<double> samples) {
    if (samples.empty()) {
        throw std::invalid_argument("Median of an empty set is undefined.");
    }
    for (double sample : samples) {
        if (!std::isfinite(sample)) {
            throw std::invalid_argument("Median samples must be finite.");
        }
    }
    std::sort(samples.begin(), samples.end());
    const std::size_t count = samples.size();
    if (count % 2 == 1) return samples[count / 2];
    return 0.5 * (samples[count / 2 - 1] + samples[count / 2]);
}

std::vector<CaseResult> execute_serial(const Workload& workload,
                                       std::atomic<std::size_t>* cases_completed) {
    std::vector<CaseResult> results(workload.cases.size());
    for (std::size_t index = 0; index < workload.cases.size(); ++index) {
        try {
            results[index] = solve_case(workload.cases[index]);
        } catch (const std::exception& error) {
            throw std::runtime_error(failure_message(index, error));
        }
        if (cases_completed != nullptr) {
            cases_completed->fetch_add(1, std::memory_order_relaxed);
        }
    }
    return results;
}

std::vector<CaseResult> execute_parallel(const Workload& workload,
                                         WorkerPool& pool,
                                         std::atomic<std::size_t>* cases_completed) {
    std::vector<CaseResult> results(workload.cases.size());
    pool.parallel_for(workload.cases.size(), [&](std::size_t index) {
        try {
            results[index] = solve_case(workload.cases[index]);
        } catch (const std::exception& error) {
            throw std::runtime_error(failure_message(index, error));
        }
        if (cases_completed != nullptr) {
            cases_completed->fetch_add(1, std::memory_order_relaxed);
        }
    });
    return results;
}

double maximum_absolute_temperature_error(const std::vector<CaseResult>& reference,
                                          const std::vector<CaseResult>& candidate) {
    if (reference.size() != candidate.size()) {
        throw std::runtime_error("Serial and parallel batches returned different case counts.");
    }
    double maximum = 0.0;
    for (std::size_t index = 0; index < reference.size(); ++index) {
        const std::vector<double>& left = reference[index].temperature_C;
        const std::vector<double>& right = candidate[index].temperature_C;
        if (left.size() != right.size()) {
            throw std::runtime_error("Serial and parallel cases returned different node counts.");
        }
        for (std::size_t node = 0; node < left.size(); ++node) {
            maximum = std::max(maximum, std::abs(left[node] - right[node]));
        }
    }
    return maximum;
}

BenchmarkResult run_benchmark(const BenchmarkConfig& input,
                              const BenchmarkProgressCallback& progress,
                              std::atomic<std::size_t>* cases_completed) {
    BenchmarkConfig config = input;
    validate_benchmark_config(config);

    const auto benchmark_start = std::chrono::steady_clock::now();
    BenchmarkResult result;
    result.benchmark_id = config.benchmark_id.empty() ? make_benchmark_id() : config.benchmark_id;
    result.seed = config.seed;
    result.batch_size = config.batch_size;
    result.mesh_size = config.mesh_size;
    result.repetitions = config.repetitions;
    result.timestamp_utc = utc_timestamp();
    result.hardware = collect_hardware_info();

    struct MeasuredSlot {
        std::string mode;
        std::size_t worker_count = 1;
        // Created before this configuration's warm-up so thread startup stays outside both timers.
        std::unique_ptr<WorkerPool> pool;
        double warmup_time_s = 0.0;
        std::vector<double> times;
        double error = 0.0;
    };

    std::vector<MeasuredSlot> slots;
    slots.push_back(MeasuredSlot{"serial", 1, nullptr, 0.0, {}, 0.0});
    for (std::size_t worker_count : config.worker_counts) {
        slots.push_back(MeasuredSlot{
            "parallel", worker_count, std::make_unique<WorkerPool>(worker_count), 0.0, {}, 0.0});
    }
    for (MeasuredSlot& slot : slots) slot.times.reserve(config.repetitions);

    // One generation step, then one warm-up and the measured repetitions for every configuration.
    const std::size_t total_steps = 1 + slots.size() * (1 + config.repetitions);
    std::size_t completed_steps = 0;

    BenchmarkProgress state;
    state.repetitions = config.repetitions;
    state.total_steps = total_steps;
    state.phase = BenchmarkPhase::Workload;
    state.completed_steps = completed_steps;
    notify(progress, state);

    WorkloadConfig workload_config;
    workload_config.seed = config.seed;
    workload_config.batch_size = config.batch_size;
    workload_config.mesh_size = config.mesh_size;
    const auto generation_start = std::chrono::steady_clock::now();
    const Workload workload = generate_workload(workload_config);
    result.workload_generation_time_s =
        elapsed_seconds(generation_start, std::chrono::steady_clock::now());
    ++completed_steps;

    auto time_solve = [&](MeasuredSlot& slot) -> std::pair<double, std::vector<CaseResult>> {
        if (cases_completed != nullptr) cases_completed->store(0, std::memory_order_relaxed);
        const auto start = std::chrono::steady_clock::now();
        std::vector<CaseResult> solved = slot.pool
                                              ? execute_parallel(workload, *slot.pool, cases_completed)
                                              : execute_serial(workload, cases_completed);
        return {elapsed_seconds(start, std::chrono::steady_clock::now()), std::move(solved)};
    };

    // Warm every configuration on the same generated cases. These durations are stored
    // separately and are never appended to the measured samples.
    std::vector<std::vector<CaseResult>> warmup_results(slots.size());
    for (std::size_t index = 0; index < slots.size(); ++index) {
        MeasuredSlot& slot = slots[index];
        state.phase = BenchmarkPhase::Warmup;
        state.worker_count = slot.mode == "serial" ? 0 : slot.worker_count;
        state.repetition = 0;
        state.completed_steps = completed_steps;
        notify(progress, state);
        auto timed = time_solve(slot);
        slot.warmup_time_s = timed.first;
        result.warmup_time_s += timed.first;
        if (timed.second.size() != workload.cases.size()) {
            throw std::runtime_error("Warm-up did not return one result per case.");
        }
        warmup_results[index] = std::move(timed.second);
        ++completed_steps;
    }

    std::vector<CaseResult> reference;
    bool have_reference = false;
    result.measured_round_orders.reserve(config.repetitions);
    for (std::size_t round = 0; round < config.repetitions; ++round) {
        std::vector<std::string> order;
        order.reserve(slots.size());
        for (std::size_t offset = 0; offset < slots.size(); ++offset) {
            // Round r is the canonical order rotated left by r, so no configuration
            // stays in the same position for every measured round.
            const std::size_t index = (offset + round) % slots.size();
            MeasuredSlot& slot = slots[index];
            state.phase = slot.mode == "serial" ? BenchmarkPhase::Serial : BenchmarkPhase::Parallel;
            state.worker_count = slot.worker_count;
            state.repetition = round + 1;
            state.completed_steps = completed_steps;
            notify(progress, state);
            auto timed = time_solve(slot);
            slot.times.push_back(timed.first);
            if (slot.mode == "serial" && !have_reference) {
                reference = std::move(timed.second);
                have_reference = true;
            } else {
                slot.error = std::max(slot.error, maximum_absolute_temperature_error(reference, timed.second));
            }
            order.push_back(configuration_label(slot.mode, slot.worker_count));
            ++completed_steps;
        }
        result.measured_round_orders.push_back(std::move(order));
    }
    if (!have_reference) {
        throw std::runtime_error("The benchmark finished without a measured serial baseline.");
    }

    for (std::size_t index = 0; index < slots.size(); ++index) {
        slots[index].error = std::max(
            slots[index].error, maximum_absolute_temperature_error(reference, warmup_results[index]));
    }
    warmup_results.clear();

    const double serial_median = median_value(slots.front().times);
    double maximum_error = 0.0;
    for (MeasuredSlot& slot : slots) {
        maximum_error = std::max(maximum_error, slot.error);
        ConfigurationResult configuration = summarize(
            slot.mode, slot.worker_count, std::move(slot.times), serial_median, slot.error);
        configuration.warmup_time_s = slot.warmup_time_s;
        result.configurations.push_back(std::move(configuration));
    }

    result.maximum_absolute_error_C = maximum_error;
    result.numerical_validation_passed = result.maximum_absolute_error_C <= kNumericalToleranceC;
    result.total_duration_s = elapsed_seconds(benchmark_start, std::chrono::steady_clock::now());
    if (completed_steps != total_steps) {
        throw std::runtime_error("Benchmark progress did not account for every scheduled step.");
    }
    return result;
}

std::string benchmark_result_csv(const BenchmarkResult& result) {
    std::ostringstream csv;
    csv << "benchmark_id,timestamp_utc,random_seed,batch_size,mesh_size,repetitions,"
           "execution_mode,worker_count,repetition_index,execution_time_seconds,"
           "median_execution_time_seconds,speedup,parallel_efficiency,"
           "maximum_absolute_error_celsius,numerical_validation,"
           "workload_generation_time_seconds,warmup_time_seconds,"
           "configuration_warmup_time_seconds,total_duration_seconds,"
           "cpu_model,physical_cpu_cores,logical_cpus,ram_bytes,operating_system,"
           "compiler,compiler_version,build_type,optimization_flags\n";

    for (const ConfigurationResult& configuration : result.configurations) {
        const std::string configuration_validation =
            configuration.numerical_validation_passed ? "passed" : "failed";
        for (std::size_t index = 0; index < configuration.execution_times_s.size(); ++index) {
            csv << csv_escape(result.benchmark_id) << ','
                << csv_escape(result.timestamp_utc) << ','
                << result.seed << ','
                << result.batch_size << ','
                << result.mesh_size << ','
                << result.repetitions << ','
                << configuration.execution_mode << ','
                << configuration.worker_count << ','
                << (index + 1) << ','
                << csv_double(configuration.execution_times_s[index]) << ','
                << csv_double(configuration.median_execution_time_s) << ','
                << csv_double(configuration.speedup) << ','
                << csv_double(configuration.parallel_efficiency) << ','
                << csv_double(configuration.maximum_absolute_error_C) << ','
                << configuration_validation << ','
                << csv_double(result.workload_generation_time_s) << ','
                << csv_double(result.warmup_time_s) << ','
                << csv_double(configuration.warmup_time_s) << ','
                << csv_double(result.total_duration_s) << ','
                << csv_escape(result.hardware.cpu_model) << ','
                << csv_optional_unsigned(result.hardware.physical_cores) << ','
                << csv_optional_unsigned(result.hardware.logical_cpus) << ','
                << csv_optional_u64(result.hardware.ram_bytes) << ','
                << csv_escape(result.hardware.operating_system) << ','
                << csv_escape(result.hardware.compiler) << ','
                << csv_escape(result.hardware.compiler_version) << ','
                << csv_escape(result.hardware.build_type) << ','
                << csv_escape(result.hardware.optimization_flags) << '\n';
        }
    }
    return csv.str();
}

}
