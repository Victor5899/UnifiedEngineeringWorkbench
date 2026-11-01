#include "workbench/geometry/stl_parser.hpp"
#include "workbench/learning/backpropagation.hpp"
#include "workbench/performance/benchmark_runner.hpp"
#include "workbench/thermal/temperature_csv_parser.hpp"
#include "workbench/thermal_solver.hpp"

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <optional>
#include <string>
#include <vector>

namespace {

void print_vec3(const workbench::Vec3& value) {
    std::cout << value.x << ' ' << value.y << ' ' << value.z << '\n';
}

void inspect_stl(const std::string& path) {
    const workbench::StlMesh mesh = workbench::parse_stl(path);
    std::cout << std::setprecision(10);
    std::cout << "STL inspection\n"
              << "File: " << path << '\n'
              << "Solid: " << (mesh.solid_name.empty() ? "(unnamed)" : mesh.solid_name) << '\n'
              << "Triangles: " << mesh.triangle_count() << '\n'
              << "Vertices: " << mesh.vertex_count() << '\n'
              << "Bounding box minimum: ";
    print_vec3(mesh.bounds.min);
    std::cout << "Bounding box maximum: ";
    print_vec3(mesh.bounds.max);
    std::cout << "Dimensions: " << mesh.bounds.dimension_x() << ' '
              << mesh.bounds.dimension_y() << ' '
              << mesh.bounds.dimension_z() << '\n'
              << "Coordinate units: file units\n";
}

std::size_t parse_positive(const std::string& text, const std::string& label) {
    if (text.empty() || text.size() > 9) {
        throw std::invalid_argument(label + " must be a positive integer.");
    }
    std::size_t value = 0;
    for (char character : text) {
        if (character < '0' || character > '9') {
            throw std::invalid_argument(label + " must be a positive integer.");
        }
        value = value * 10 + static_cast<std::size_t>(character - '0');
    }
    if (value == 0) throw std::invalid_argument(label + " must be a positive integer.");
    return value;
}

std::uint32_t parse_seed(const std::string& text) {
    if (text.empty() || text.size() > 10) {
        throw std::invalid_argument("Seed must be an integer from 0 to 4294967295.");
    }
    std::uint64_t value = 0;
    for (char character : text) {
        if (character < '0' || character > '9') {
            throw std::invalid_argument("Seed must be an integer from 0 to 4294967295.");
        }
        value = value * 10 + static_cast<std::uint64_t>(character - '0');
    }
    if (value > 4294967295ull) {
        throw std::invalid_argument("Seed must be an integer from 0 to 4294967295.");
    }
    return static_cast<std::uint32_t>(value);
}

std::vector<std::size_t> parse_worker_list(const std::string& text) {
    std::vector<std::size_t> workers;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::string token = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        workers.push_back(parse_positive(token, "Worker count"));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return workers;
}

std::string text_or_unavailable(const std::string& value) {
    return value.empty() ? "unavailable" : value;
}

template <typename T>
std::string number_or_unavailable(const std::optional<T>& value) {
    if (!value) return "unavailable";
    return std::to_string(*value);
}

int run_benchmark_command(int argc, char** argv) {
    workbench::BenchmarkConfig config;
    config.seed = 1;
    config.batch_size = 8;
    config.mesh_size = 32;
    config.repetitions = 5;
    std::string output;
    for (int index = 2; index < argc; ++index) {
        const std::string argument = argv[index];
        auto require_value = [&](const std::string& flag) {
            if (index + 1 >= argc) throw std::invalid_argument(flag + " needs a value.");
            return std::string(argv[++index]);
        };
        if (argument == "--seed") config.seed = parse_seed(require_value(argument));
        else if (argument == "--batch") config.batch_size = parse_positive(require_value(argument), "Batch size");
        else if (argument == "--mesh") config.mesh_size = parse_positive(require_value(argument), "Mesh size");
        else if (argument == "--repetitions") {
            config.repetitions = parse_positive(require_value(argument), "Repetitions");
        } else if (argument == "--workers") {
            config.worker_counts = parse_worker_list(require_value(argument));
        } else if (argument == "--output") {
            output = require_value(argument);
        } else {
            throw std::invalid_argument("Unknown benchmark argument: " + argument);
        }
    }

    const workbench::BenchmarkResult result = workbench::run_benchmark(config);
    std::cout << std::setprecision(10);
    std::cout << "Benchmark " << result.benchmark_id << '\n'
              << "Seed: " << result.seed << '\n'
              << "Batch size: " << result.batch_size << '\n'
              << "Mesh size: " << result.mesh_size << '\n'
              << "Repetitions: " << result.repetitions << '\n'
              << "CPU: " << text_or_unavailable(result.hardware.cpu_model) << '\n'
              << "Physical cores: " << number_or_unavailable(result.hardware.physical_cores) << '\n'
              << "Logical CPUs: " << number_or_unavailable(result.hardware.logical_cpus) << '\n'
              << "RAM bytes: " << number_or_unavailable(result.hardware.ram_bytes) << '\n'
              << "Operating system: " << text_or_unavailable(result.hardware.operating_system) << '\n'
              << "Compiler: " << text_or_unavailable(result.hardware.compiler) << ' '
              << text_or_unavailable(result.hardware.compiler_version) << '\n'
              << "Build type: " << text_or_unavailable(result.hardware.build_type) << '\n'
              << "Optimization flags: " << text_or_unavailable(result.hardware.optimization_flags) << '\n'
              << "Workload generation time (s): " << result.workload_generation_time_s << '\n'
              << "Warm-up time excluded from medians (s): " << result.warmup_time_s << '\n'
              << "Total duration (s): " << result.total_duration_s << '\n';
    for (std::size_t round = 0; round < result.measured_round_orders.size(); ++round) {
        std::cout << "Measured round " << (round + 1) << " order:";
        for (const std::string& label : result.measured_round_orders[round]) std::cout << ' ' << label;
        std::cout << '\n';
    }

    for (const workbench::ConfigurationResult& configuration : result.configurations) {
        std::cout << configuration.execution_mode << " workers=" << configuration.worker_count
                  << " warmup_excluded_s=" << configuration.warmup_time_s
                  << " median_s=" << configuration.median_execution_time_s
                  << " speedup=" << configuration.speedup
                  << " efficiency=" << configuration.parallel_efficiency
                  << " max_abs_error_C=" << configuration.maximum_absolute_error_C
                  << " validation=" << (configuration.numerical_validation_passed ? "passed" : "failed")
                  << '\n';
        std::cout << "  repetition_times_s:";
        for (double sample : configuration.execution_times_s) std::cout << ' ' << sample;
        std::cout << '\n';
    }
    std::cout << "Maximum absolute error (C): " << result.maximum_absolute_error_C << '\n'
              << "Numerical validation: " << (result.numerical_validation_passed ? "passed" : "failed")
              << '\n';
    if (!output.empty()) {
        std::ofstream file(output);
        if (!file) throw std::runtime_error("Cannot open benchmark CSV: " + output);
        file << workbench::benchmark_result_csv(result);
        if (!file) throw std::runtime_error("Cannot write benchmark CSV: " + output);
        std::cout << "CSV written to: " << output << '\n';
    }
    return result.numerical_validation_passed ? 0 : 1;
}

void print_backprop_report(std::ostream& out, const workbench::BackpropExperiment& experiment) {
    const auto previous_flags = out.flags();
    const auto previous_precision = out.precision();
    const auto previous_locale = out.getloc();
    out.imbue(std::locale::classic());
    out << std::defaultfloat << std::setprecision(8);

    const workbench::BackpropForward& forward = experiment.initial_forward;
    out << "Backpropagation\n"
        << "Model: one training sample, one hidden neuron\n"
        << "Activation: tanh\n"
        << "Loss: 0.5*(y_hat-y)^2\n"
        << "Input x: " << experiment.sample.x << '\n'
        << "Target y: " << experiment.sample.y << '\n'
        << "Learning rate: " << experiment.learning_rate << '\n'
        << "Finite-difference epsilon: " << experiment.epsilon << '\n'
        << "Gradient tolerance: " << experiment.gradient_tolerance << '\n';
    out << std::setprecision(17);
    out << "\nInitial parameters\n"
        << "w = " << experiment.initial_parameters.w << '\n'
        << "b = " << experiment.initial_parameters.b << '\n'
        << "v = " << experiment.initial_parameters.v << '\n'
        << "c = " << experiment.initial_parameters.c << '\n'
        << "\nInitial forward pass\n"
        << "z = " << forward.z << '\n'
        << "h = " << forward.h << '\n'
        << "y_hat = " << forward.y_hat << '\n'
        << "error = " << forward.error << '\n'
        << "loss = " << forward.loss << '\n'
        << "\nAnalytical gradients\n"
        << "dJ/dw = " << experiment.analytical.dJ_dw << '\n'
        << "dJ/db = " << experiment.analytical.dJ_db << '\n'
        << "dJ/dv = " << experiment.analytical.dJ_dv << '\n'
        << "dJ/dc = " << experiment.analytical.dJ_dc << '\n'
        << "\nNumerical gradient verification\n";
    out << std::left << std::setw(14) << "Parameter"
        << std::setw(28) << "Analytical gradient"
        << std::setw(28) << "Numerical gradient"
        << std::setw(28) << "Absolute error"
        << "Result\n";
    for (const workbench::BackpropGradientCheck& check : experiment.checks) {
        out << std::left << std::setw(14) << workbench::backprop_parameter_name(check.parameter)
            << std::setw(28) << check.analytical
            << std::setw(28) << check.numerical
            << std::setw(28) << check.absolute_error
            << (check.passed ? "PASS" : "FAIL") << '\n';
    }
    out << "Gradient verification: " << (experiment.gradient_check_passed ? "passed" : "failed") << '\n'
        << "\nFirst simultaneous update\n"
        << "w = " << experiment.after_one_update.w << '\n'
        << "b = " << experiment.after_one_update.b << '\n'
        << "v = " << experiment.after_one_update.v << '\n'
        << "c = " << experiment.after_one_update.c << '\n'
        << "\nTraining\n"
        << "Steps: " << experiment.training_steps << '\n'
        << "History rows: " << experiment.history.size() << '\n';
    if (!experiment.history.empty()) {
        out << "Initial loss: " << experiment.history.front().loss << '\n'
            << "Final loss: " << experiment.history.back().loss << '\n'
            << "Final parameters\n"
            << "w = " << experiment.history.back().parameters.w << '\n'
            << "b = " << experiment.history.back().parameters.b << '\n'
            << "v = " << experiment.history.back().parameters.v << '\n'
            << "c = " << experiment.history.back().parameters.c << '\n';
    }

    out.flags(previous_flags);
    out.precision(previous_precision);
    out.imbue(previous_locale);
}

int run_backprop_command(int argc, char** argv) {
    std::string output = "results/backpropagation_training.csv";
    for (int index = 2; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--output") {
            if (index + 1 >= argc) {
                throw std::invalid_argument(
                    "--output needs a value.\nUsage: workbench backprop [--output results/backpropagation_training.csv]");
            }
            output = argv[++index];
        } else {
            throw std::invalid_argument(
                "Unknown backprop argument: " + argument +
                "\nUsage: workbench backprop [--output results/backpropagation_training.csv]");
        }
    }

    const workbench::BackpropExperiment experiment = workbench::run_backpropagation();
    print_backprop_report(std::cout, experiment);
    workbench::write_backprop_history_csv(experiment.history, output);
    std::cout << "CSV written to: " << output << '\n';
    if (!experiment.gradient_check_passed) {
        std::cerr << "Gradient verification failed.\n";
        return 1;
    }
    return 0;
}

void inspect_temperature(const std::string& path) {
    const workbench::TemperatureDataset dataset = workbench::parse_temperature_csv(path);
    std::cout << std::setprecision(10);
    std::cout << "Temperature CSV inspection\n"
              << "File: " << path << '\n'
              << "Nodes: " << dataset.node_count() << '\n'
              << "Minimum temperature (C): " << dataset.minimum_temperature_C << '\n'
              << "Maximum temperature (C): " << dataset.maximum_temperature_C << '\n'
              << "Average temperature (C): " << dataset.average_temperature_C << '\n'
              << "Bounding box minimum (m): ";
    print_vec3(dataset.bounds.min);
    std::cout << "Bounding box maximum (m): ";
    print_vec3(dataset.bounds.max);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc > 1) {
            const std::string command = argv[1];
            if (command == "benchmark") return run_benchmark_command(argc, argv);
            if (command == "backprop") return run_backprop_command(argc, argv);
            if (command == "inspect-stl") {
                if (argc != 3) {
                    std::cerr << "Usage: workbench inspect-stl <file.stl>\n";
                    return 1;
                }
                inspect_stl(argv[2]);
                return 0;
            }
            if (command == "inspect-temperature") {
                if (argc != 3) {
                    std::cerr << "Usage: workbench inspect-temperature <file.csv>\n";
                    return 1;
                }
                inspect_temperature(argv[2]);
                return 0;
            }
        }

        std::size_t elements = 10;
        std::string output = "thermal_result.csv";
        if (argc > 1) elements = static_cast<std::size_t>(std::stoul(argv[1]));
        if (argc > 2) output = argv[2];
        const auto result = workbench::solve_steady_1d({}, elements);
        workbench::write_result_csv(result, output);
        std::cout << "1D steady thermal FEA completed\n"
                  << "Elements: " << result.elements << '\n'
                  << "Maximum midpoint absolute error: "
                  << result.max_midpoint_error_C << " C\n"
                  << "CSV written to: " << output << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
