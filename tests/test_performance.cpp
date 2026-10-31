#include "workbench/performance/benchmark_runner.hpp"
#include "workbench/performance/hardware_info.hpp"
#include "workbench/performance/worker_pool.hpp"
#include "workbench/performance/workload_generator.hpp"
#include "workbench/thermal_solver.hpp"
#include "workbench_build_info.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "TEST FAILURE: " << message << '\n';
        ++g_failures;
    }
}

void expect_near(double actual, double expected, double tolerance, const std::string& message) {
    if (!(std::abs(actual - expected) <= tolerance)) {
        std::cerr << "TEST FAILURE: " << message << " actual=" << actual << " expected=" << expected
                  << '\n';
        ++g_failures;
    }
}

bool throws_invalid(const std::function<void()>& action) {
    try {
        action();
    } catch (const std::invalid_argument&) {
        return true;
    } catch (const std::exception& error) {
        std::cerr << "TEST FAILURE: expected invalid_argument, got: " << error.what() << '\n';
        ++g_failures;
        return true;
    }
    return false;
}

double reference_median(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    const std::size_t count = samples.size();
    if (count % 2 == 1) return samples[count / 2];
    return 0.5 * (samples[count / 2 - 1] + samples[count / 2]);
}

bool in_half_open(double value, double minimum, double maximum) {
    return value >= minimum && value < maximum && std::isfinite(value);
}

workbench::Workload sample_workload() {
    workbench::WorkloadConfig config;
    config.seed = 42;
    config.batch_size = 6;
    config.mesh_size = 8;
    return workbench::generate_workload(config);
}

void expect_case_order(const workbench::Workload& workload, const std::vector<workbench::CaseResult>& results,
                       const std::string& label) {
    expect(results.size() == workload.cases.size(), label + " result count");
    for (std::size_t index = 0; index < results.size() && index < workload.cases.size(); ++index) {
        const workbench::ThermalConfig& config = workload.cases[index].config;
        expect(results[index].temperature_C.size() == workload.cases[index].elements + 1,
               label + " node count");
        if (results[index].temperature_C.empty()) continue;
        expect_near(results[index].temperature_C.front(), config.left_temperature_C, 1e-8,
                    label + " left boundary");
        expect_near(results[index].temperature_C.back(), config.right_temperature_C, 1e-8,
                    label + " right boundary");
        for (double temperature : results[index].temperature_C) {
            expect(std::isfinite(temperature), label + " finite temperature");
        }
    }
}

void test_workload() {
    workbench::WorkloadConfig config;
    config.seed = 7;
    config.batch_size = 5;
    config.mesh_size = 4;
    const workbench::Workload first = workbench::generate_workload(config);
    const workbench::Workload second = workbench::generate_workload(config);
    expect(first == second, "same seed produces the same workload");
    expect(first.cases.size() == 5, "batch size is honored");

    config.seed = 8;
    const workbench::Workload other = workbench::generate_workload(config);
    expect(!(first == other), "different seeds produce different workloads");

    for (const workbench::ThermalCase& item : first.cases) {
        expect(item.elements == 4, "mesh size is applied to every case");
        expect(item.config.length_m == workbench::kFixedLengthM, "length stays positive and fixed");
        expect(item.config.area_m2 == workbench::kFixedAreaM2, "area stays positive and fixed");
        expect(item.config.conductivity_W_mK > 0.0, "conductivity is positive");
        expect(item.config.generation_W_m3 > 0.0, "heat generation is positive");
        expect(in_half_open(item.config.conductivity_W_mK, workbench::kConductivityMinW_mK,
                            workbench::kConductivityMaxW_mK),
               "conductivity is inside its documented range");
        expect(in_half_open(item.config.generation_W_m3, workbench::kGenerationMinW_m3,
                            workbench::kGenerationMaxW_m3),
               "heat generation is inside its documented range");
        expect(in_half_open(item.config.left_temperature_C, workbench::kLeftTemperatureMinC,
                            workbench::kLeftTemperatureMaxC),
               "left boundary temperature is inside its documented range");
        expect(in_half_open(item.config.right_temperature_C, workbench::kRightTemperatureMinC,
                            workbench::kRightTemperatureMaxC),
               "right boundary temperature is inside its documented range");
        expect(std::isfinite(item.config.left_temperature_C) && std::isfinite(item.config.right_temperature_C),
               "boundary temperatures are finite");
    }

    expect(throws_invalid([] {
               workbench::WorkloadConfig invalid;
               invalid.batch_size = 0;
               invalid.mesh_size = 4;
               workbench::generate_workload(invalid);
           }),
           "zero batch size is rejected");
    expect(throws_invalid([] {
               workbench::WorkloadConfig invalid;
               invalid.batch_size = 1;
               invalid.mesh_size = 0;
               workbench::generate_workload(invalid);
           }),
           "zero mesh size is rejected");
    expect(throws_invalid([] {
               workbench::WorkloadConfig invalid;
               invalid.batch_size = workbench::kMaximumBatchSize + 1;
               invalid.mesh_size = 1;
               workbench::generate_workload(invalid);
           }),
           "oversized batch is rejected");
    expect(throws_invalid([] {
               workbench::WorkloadConfig invalid;
               invalid.batch_size = 1;
               invalid.mesh_size = workbench::kMaximumMeshSize + 1;
               workbench::generate_workload(invalid);
           }),
           "oversized mesh is rejected");
}

void expect_coverage(std::size_t workers, std::size_t tasks) {
    workbench::WorkerPool pool(workers);
    auto hits = std::make_unique<std::atomic<int>[]>(tasks);
    for (std::size_t index = 0; index < tasks; ++index) hits[index].store(0);
    pool.parallel_for(tasks, [&](std::size_t index) {
        hits[index].fetch_add(1, std::memory_order_relaxed);
    });
    for (std::size_t index = 0; index < tasks; ++index) {
        expect(hits[index].load() == 1, "worker pool visits each index once");
    }
}

void test_pool() {
    const unsigned limit = workbench::maximum_supported_workers();
    expect(limit >= 1 && limit <= 4, "worker limit is from 1 to 4");
    for (std::size_t workers = 1; workers <= limit; ++workers) {
        expect_coverage(workers, 17);
    }
    expect(throws_invalid([] { workbench::WorkerPool pool(0); }), "zero workers are rejected");
    expect(throws_invalid([] { workbench::WorkerPool pool(5); }), "five workers are rejected");
    if (limit < 4) {
        expect(throws_invalid([limit] { workbench::WorkerPool pool(static_cast<std::size_t>(limit) + 1); }),
               "a worker count above the logical CPU limit is rejected");
    }

    workbench::WorkerPool pool(1);
    bool threw = false;
    try {
        pool.parallel_for(6, [](std::size_t index) {
            if (index == 2) throw std::runtime_error("injected failure");
        });
    } catch (const std::runtime_error& error) {
        threw = std::string(error.what()).find("injected failure") != std::string::npos;
    }
    expect(threw, "worker pool rethrows a task exception");
    auto hits = std::make_unique<std::atomic<int>[]>(4);
    for (std::size_t index = 0; index < 4; ++index) hits[index].store(0);
    pool.parallel_for(4, [&](std::size_t index) { hits[index].fetch_add(1); });
    for (std::size_t index = 0; index < 4; ++index) {
        expect(hits[index].load() == 1, "worker pool remains usable after an exception");
    }
}

void test_execution() {
    const workbench::Workload workload = sample_workload();
    const std::vector<workbench::CaseResult> serial = workbench::execute_serial(workload);
    expect_case_order(workload, serial, "serial");
    const workbench::ThermalResult direct =
        workbench::solve_steady_1d(workload.cases.front().config, workload.cases.front().elements);
    expect(serial.front().temperature_C.size() == direct.temperature_C.size(), "serial matches solver size");
    if (serial.front().temperature_C.size() == direct.temperature_C.size()) {
        for (std::size_t node = 0; node < direct.temperature_C.size(); ++node) {
            expect_near(serial.front().temperature_C[node], direct.temperature_C[node], 1e-12,
                        "serial reuses the thermal solver");
        }
    }

    const unsigned limit = workbench::maximum_supported_workers();
    for (std::size_t workers = 1; workers <= limit; ++workers) {
        workbench::WorkerPool pool(workers);
        const std::vector<workbench::CaseResult> parallel = workbench::execute_parallel(workload, pool);
        expect_case_order(workload, parallel, "parallel " + std::to_string(workers));
        const double error = workbench::maximum_absolute_temperature_error(serial, parallel);
        expect(error <= workbench::kNumericalToleranceC, "parallel temperatures agree with serial");
    }

    workbench::Workload broken = workload;
    broken.cases[1].config.conductivity_W_mK = -5.0;
    workbench::WorkerPool pool(std::min<std::size_t>(2, limit));
    bool threw = false;
    try {
        workbench::execute_parallel(broken, pool);
    } catch (const std::runtime_error& error) {
        threw = std::string(error.what()).find("Thermal case") != std::string::npos;
    }
    expect(threw, "parallel execution reports a failed case");
    const std::vector<workbench::CaseResult> recovered = workbench::execute_parallel(workload, pool);
    expect(recovered.size() == workload.cases.size(), "pool recovers after a failed thermal case");
}

void test_benchmark() {
    expect_near(workbench::median_value({5.0, 1.0, 4.0, 2.0, 3.0}), 3.0, 1e-15, "odd median");
    expect_near(workbench::median_value({1.0, 2.0, 3.0, 4.0}), 2.5, 1e-15, "even median");
    // A discarded warm-up is not a median sample. The middle of the five measured values is 3,
    // even when an extra warm-up duration would have changed the sorted middle.
    const std::vector<double> measured_only{9.0, 1.0, 4.0, 2.0, 3.0};
    expect_near(workbench::median_value(measured_only), 3.0, 1e-15, "median uses the measured samples only");
    expect(measured_only.size() == 5, "five measured samples stay five when a warm-up is kept aside");
    expect(throws_invalid([] { workbench::median_value({}); }), "empty median is rejected");

    workbench::BenchmarkConfig config;
    config.seed = 99;
    config.batch_size = 4;
    config.mesh_size = 8;
    config.repetitions = 5;
    const unsigned limit = workbench::maximum_supported_workers();
    for (unsigned workers = 1; workers <= limit; ++workers) config.worker_counts.push_back(workers);

    const workbench::BenchmarkResult result = workbench::run_benchmark(config);
    expect(result.repetitions == 5, "benchmark records five repetitions");
    expect(result.configurations.size() == 1 + config.worker_counts.size(), "serial plus each worker count");
    expect(!result.configurations.empty() && result.configurations.front().execution_mode == "serial",
           "serial configuration is first");
    expect(result.configurations.front().execution_times_s.size() == 5, "serial stores every measured repetition");
    expect(result.configurations.front().execution_times_s.size() == result.repetitions,
           "serial samples exclude the warm-up");
    expect(result.workload_generation_time_s >= 0.0, "generation time is recorded");
    expect(result.warmup_time_s >= 0.0, "warm-up time is recorded");
    expect(result.total_duration_s >= result.warmup_time_s, "total duration includes the warm-up");
    expect(result.numerical_validation_passed, "numerical validation passed");
    expect(result.maximum_absolute_error_C <= workbench::kNumericalToleranceC, "error stays within 1e-8 C");

    const double serial_median = reference_median(result.configurations.front().execution_times_s);
    expect_near(result.configurations.front().median_execution_time_s, serial_median, 1e-12, "serial median");
    expect_near(result.configurations.front().speedup, 1.0, 1e-12, "serial speedup is the reference");
    expect_near(result.configurations.front().parallel_efficiency, 1.0, 1e-12, "serial efficiency");

    for (std::size_t index = 1; index < result.configurations.size(); ++index) {
        const workbench::ConfigurationResult& configuration = result.configurations[index];
        expect(configuration.execution_mode == "parallel", "later configurations are parallel");
        expect(configuration.execution_times_s.size() == 5, "parallel stores every measured repetition");
        expect(configuration.execution_times_s.size() == result.repetitions,
               "parallel samples exclude the warm-up");
        expect(configuration.worker_count == config.worker_counts[index - 1], "worker count is preserved");
        const double median = reference_median(configuration.execution_times_s);
        expect_near(configuration.median_execution_time_s, median, 1e-12, "parallel median");
        expect_near(configuration.speedup, serial_median / median, 1e-9, "speedup uses the serial median");
        expect_near(configuration.parallel_efficiency,
                    configuration.speedup / static_cast<double>(configuration.worker_count), 1e-9,
                    "efficiency divides by the actual worker count");
        expect(configuration.maximum_absolute_error_C <= workbench::kNumericalToleranceC,
               "parallel numerical error");
        std::cout << "workers=" << configuration.worker_count
                  << " median_s=" << configuration.median_execution_time_s
                  << " speedup=" << configuration.speedup
                  << " efficiency=" << configuration.parallel_efficiency
                  << " error_C=" << configuration.maximum_absolute_error_C << '\n';
    }

    double warmup_sum = 0.0;
    for (const workbench::ConfigurationResult& configuration : result.configurations) {
        expect(configuration.warmup_time_s >= 0.0, "each configuration records its own warm-up");
        expect(configuration.execution_times_s.size() == result.repetitions,
               "warm-up duration is not one of the measured repetitions");
        warmup_sum += configuration.warmup_time_s;
        expect_near(configuration.median_execution_time_s,
                    reference_median(configuration.execution_times_s), 1e-12,
                    "median is computed from measured repetitions only");
    }
    expect_near(result.warmup_time_s, warmup_sum, 1e-9, "aggregate warm-up sums the excluded per-configuration warm-ups");
    expect(result.measured_round_orders.size() == result.repetitions, "one execution order per measured round");
    if (!result.measured_round_orders.empty()) {
        const std::vector<std::string>& base = result.measured_round_orders.front();
        expect(base.size() == result.configurations.size(), "each round visits every configuration");
        expect(!base.empty() && base.front() == "serial", "the first measured round starts with the serial baseline");
        for (std::size_t round = 0; round < result.measured_round_orders.size(); ++round) {
            const std::vector<std::string>& order = result.measured_round_orders[round];
            expect(order.size() == base.size(), "a rotated round still runs every configuration once");
            for (std::size_t index = 0; index < base.size(); ++index) {
                expect(order[index] == base[(index + round) % base.size()],
                       "measured rounds rotate the configuration order");
            }
        }
    }

    const std::string csv = workbench::benchmark_result_csv(result);
    expect(csv.find("execution_time_seconds") != std::string::npos, "CSV header");
    expect(csv.find("configuration_warmup_time_seconds") != std::string::npos,
           "CSV keeps each excluded warm-up beside the measured rows");
    expect(csv.find(std::to_string(result.seed)) != std::string::npos, "CSV contains the seed");
    std::size_t rows = 0;
    for (char character : csv) {
        if (character == '\n') ++rows;
    }
    expect(rows == 1 + result.repetitions * result.configurations.size(), "CSV keeps every repetition");

    expect(throws_invalid([] {
               workbench::BenchmarkConfig invalid;
               invalid.repetitions = 0;
               invalid.batch_size = 1;
               invalid.mesh_size = 2;
               invalid.worker_counts = {1};
               workbench::validate_benchmark_config(invalid);
           }),
           "zero repetitions are rejected");
    expect(throws_invalid([] {
               workbench::BenchmarkConfig invalid;
               invalid.repetitions = 5;
               invalid.batch_size = 1;
               invalid.mesh_size = 2;
               invalid.worker_counts = {1, 1};
               workbench::validate_benchmark_config(invalid);
           }),
           "duplicate worker counts are rejected");
    expect(throws_invalid([] {
               workbench::BenchmarkConfig invalid;
               invalid.repetitions = 20;
               invalid.batch_size = workbench::kMaximumBatchSize;
               invalid.mesh_size = workbench::kMaximumMeshSize;
               invalid.worker_counts = {1};
               workbench::validate_benchmark_config(invalid);
           }),
           "an oversized schedule is rejected");

    workbench::BenchmarkConfig defaults;
    defaults.seed = 3;
    defaults.batch_size = 2;
    defaults.mesh_size = 4;
    defaults.repetitions = 1;
    const workbench::BenchmarkResult filled = workbench::run_benchmark(defaults);
    expect(filled.configurations.size() == 1 + limit, "empty worker list uses every supported count");
}

void test_hardware() {
    const workbench::HardwareInfo info = workbench::collect_hardware_info();
    const std::string configured_build = WORKBENCH_BUILD_TYPE;
    if (configured_build == "unavailable") expect(info.build_type.empty(), "unknown build type stays unavailable");
    else expect(info.build_type == configured_build, "build type comes from the CMake configuration");
    const std::string configured_flags = WORKBENCH_OPT_FLAGS;
    if (configured_flags.empty() || std::string(configured_flags) == "unavailable") {
        expect(info.optimization_flags.empty(), "missing optimization flags stay unavailable");
    } else {
        expect(info.optimization_flags == configured_flags, "optimization flags come from the compiler configuration");
    }
    expect(!info.compiler.empty(), "compiler name is available from the preprocessor");
    const std::optional<unsigned> logical = workbench::logical_cpu_count();
    if (logical) {
        expect(*logical >= 1, "logical CPU count is positive when reported");
        expect(workbench::maximum_supported_workers() == std::min(4u, *logical),
               "worker limit follows the logical CPU count");
        if (info.physical_cores) {
            expect(*info.physical_cores >= 1, "physical core count is positive when reported");
        }
    } else {
        expect(workbench::maximum_supported_workers() == 1, "unknown CPU count allows one worker");
    }
    if (info.ram_bytes) {
        expect(*info.ram_bytes > 1024ull * 1024ull, "reported RAM is a plausible byte count");
    }
    std::cout << "CPU: " << (info.cpu_model.empty() ? "unavailable" : info.cpu_model) << '\n'
              << "Logical CPUs: " << (logical ? std::to_string(*logical) : "unavailable") << '\n'
              << "Build: " << (info.build_type.empty() ? "unavailable" : info.build_type) << '\n'
              << "Flags: " << (info.optimization_flags.empty() ? "unavailable" : info.optimization_flags)
              << '\n';
}

}  // namespace

int main() {
    try {
        test_workload();
        test_pool();
        test_execution();
        test_benchmark();
        test_hardware();
    } catch (const std::exception& error) {
        std::cerr << "TEST FAILURE: uncaught exception: " << error.what() << '\n';
        return 1;
    }
    if (g_failures != 0) {
        std::cerr << g_failures << " performance test failure(s).\n";
        return 1;
    }
    std::cout << "All performance tests passed.\n";
    return 0;
}
