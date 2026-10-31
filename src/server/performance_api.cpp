#include "performance_api.hpp"

#include "workbench/performance/benchmark_runner.hpp"
#include "workbench/performance/hardware_info.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace {

void apply_cors(httplib::Response& response) {
    response.set_header("Access-Control-Allow-Origin", "*");
    response.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response.set_header("Access-Control-Allow-Headers", "Content-Type, Accept");
    response.set_header("Access-Control-Max-Age", "600");
}

void send_json(httplib::Response& response, int status, const nlohmann::json& body) {
    response.status = status;
    response.set_content(body.dump(), "application/json");
    response.set_header("Cache-Control", "no-store");
    apply_cors(response);
}

void send_error(httplib::Response& response, int status, const std::string& message) {
    send_json(response, status, {{"error", message}});
}

bool is_strict_integer(const nlohmann::json& value) {
    const auto type = value.type();
    return type == nlohmann::json::value_t::number_integer ||
           type == nlohmann::json::value_t::number_unsigned;
}

nlohmann::json nullable_text(const std::string& value) {
    if (value.empty()) return nullptr;
    return value;
}

nlohmann::json nullable_unsigned(const std::optional<unsigned>& value) {
    if (!value) return nullptr;
    return *value;
}

nlohmann::json nullable_u64(const std::optional<std::uint64_t>& value) {
    if (!value) return nullptr;
    return *value;
}

std::string phase_name(workbench::BenchmarkPhase phase) {
    switch (phase) {
    case workbench::BenchmarkPhase::Workload:
        return "workload";
    case workbench::BenchmarkPhase::Warmup:
        return "warmup";
    case workbench::BenchmarkPhase::Serial:
        return "serial";
    case workbench::BenchmarkPhase::Parallel:
        return "parallel";
    }
    return "running";
}

std::string progress_message(const workbench::BenchmarkProgress& progress) {
    switch (progress.phase) {
    case workbench::BenchmarkPhase::Workload:
        return "Generating the thermal workload.";
    case workbench::BenchmarkPhase::Warmup:
        if (progress.worker_count == 0) {
            return "Running the serial warm-up. This time is excluded from the measured results.";
        }
        return "Running the " + std::to_string(progress.worker_count) +
               "-worker warm-up. This time is excluded from the measured results.";
    case workbench::BenchmarkPhase::Serial:
        return "Running the serial reference, repetition " + std::to_string(progress.repetition) +
               " of " + std::to_string(progress.repetitions) + ".";
    case workbench::BenchmarkPhase::Parallel:
        return "Running " + std::to_string(progress.worker_count) + " workers, repetition " +
               std::to_string(progress.repetition) + " of " + std::to_string(progress.repetitions) +
               ".";
    }
    return "Running the benchmark.";
}

nlohmann::json hardware_json(const workbench::HardwareInfo& info) {
    return {
        {"cpu_model", nullable_text(info.cpu_model)},
        {"physical_cores", nullable_unsigned(info.physical_cores)},
        {"logical_cpus", nullable_unsigned(info.logical_cpus)},
        {"ram_bytes", nullable_u64(info.ram_bytes)},
        {"operating_system", nullable_text(info.operating_system)},
        {"compiler", nullable_text(info.compiler)},
        {"compiler_version", nullable_text(info.compiler_version)},
        {"build_type", nullable_text(info.build_type)},
        {"optimization_flags", nullable_text(info.optimization_flags)},
        {"maximum_worker_count", workbench::maximum_supported_workers()},
    };
}

nlohmann::json configuration_json(const workbench::ConfigurationResult& configuration) {
    return {
        {"execution_mode", configuration.execution_mode},
        {"worker_count", configuration.worker_count},
        {"warmup_time_seconds", configuration.warmup_time_s},
        {"execution_time_seconds", configuration.execution_times_s},
        {"median_execution_time_seconds", configuration.median_execution_time_s},
        {"speedup", configuration.speedup},
        {"parallel_efficiency", configuration.parallel_efficiency},
        {"maximum_absolute_error_celsius", configuration.maximum_absolute_error_C},
        {"numerical_validation", configuration.numerical_validation_passed ? "passed" : "failed"},
    };
}

std::string make_job_id() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    static std::atomic<unsigned> counter{0};
    return "bench-" + std::to_string(seconds) + "-" + std::to_string(counter.fetch_add(1));
}

nlohmann::json result_json(const workbench::BenchmarkResult& result) {
    nlohmann::json configurations = nlohmann::json::array();
    for (const workbench::ConfigurationResult& configuration : result.configurations) {
        configurations.push_back(configuration_json(configuration));
    }
    return {
        {"benchmark_id", result.benchmark_id},
        {"timestamp_utc", result.timestamp_utc},
        {"random_seed", result.seed},
        {"batch_size", result.batch_size},
        {"mesh_size", result.mesh_size},
        {"repetitions", result.repetitions},
        {"workload_generation_time_seconds", result.workload_generation_time_s},
        {"warmup_time_seconds", result.warmup_time_s},
        {"total_duration_seconds", result.total_duration_s},
        {"speedup_reference", "serial_median"},
        {"numerical_tolerance_celsius", workbench::kNumericalToleranceC},
        {"maximum_absolute_error_celsius", result.maximum_absolute_error_C},
        {"numerical_validation", result.numerical_validation_passed ? "passed" : "failed"},
        {"hardware", hardware_json(result.hardware)},
        {"measured_round_orders", result.measured_round_orders},
        {"configurations", std::move(configurations)},
    };
}

class BenchmarkJobService {
public:
    BenchmarkJobService() = default;
    BenchmarkJobService(const BenchmarkJobService&) = delete;
    BenchmarkJobService& operator=(const BenchmarkJobService&) = delete;

    ~BenchmarkJobService() {
        if (worker_.joinable()) worker_.join();
    }

    nlohmann::json hardware() const { return hardware_json(workbench::collect_hardware_info()); }

    int start(const nlohmann::json& body, nlohmann::json& response, std::string& error) {
        workbench::BenchmarkConfig config;
        try {
            config = parse_config(body);
            workbench::validate_benchmark_config(config);
        } catch (const std::invalid_argument& failure) {
            error = failure.what();
            return 400;
        } catch (const std::exception&) {
            error = "Benchmark configuration must be a JSON object with numeric fields.";
            return 400;
        }

        std::thread previous;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (state_ == "running") {
                error = "A benchmark is already running.";
                return 409;
            }
            if (worker_.joinable()) previous = std::move(worker_);
            state_ = "running";
            phase_ = "starting";
            message_ = "Starting the benchmark.";
            error_.clear();
            result_.reset();
            benchmark_id_ = config.benchmark_id;
            worker_count_ = 0;
            repetition_ = 0;
            repetitions_ = config.repetitions;
            cases_total_ = config.batch_size;
            completed_steps_ = 0;
            const std::size_t configurations = 1 + config.worker_counts.size();
            total_steps_ = 1 + configurations * (1 + config.repetitions);
            cases_completed_.store(0, std::memory_order_relaxed);
        }
        if (previous.joinable()) previous.join();

        try {
            worker_ = std::thread([this, config] { execute_job(config); });
        } catch (const std::exception& failure) {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = "failed";
            phase_ = "failed";
            error_ = failure.what();
            message_ = error_;
            error = error_;
            return 500;
        }

        response = {{"state", "running"}, {"benchmark_id", config.benchmark_id}};
        return 202;
    }

    nlohmann::json status() const {
        std::lock_guard<std::mutex> lock(mutex_);
        nlohmann::json body = {
            {"state", state_},
            {"benchmark_id", benchmark_id_.empty() ? nlohmann::json(nullptr) : nlohmann::json(benchmark_id_)},
            {"phase", phase_},
            {"message", message_},
            {"worker_count", worker_count_},
            {"repetition", repetition_},
            {"repetitions", repetitions_},
            {"cases_completed", cases_completed_.load(std::memory_order_relaxed)},
            {"cases_total", cases_total_},
            {"completed_steps", completed_steps_},
            {"total_steps", total_steps_},
            {"error", error_.empty() ? nlohmann::json(nullptr) : nlohmann::json(error_)},
        };
        return body;
    }

    int results(nlohmann::json& body, std::string& error) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == "running") {
            error = "A benchmark is still running.";
            return 409;
        }
        if (state_ == "failed") {
            error = error_.empty() ? "The benchmark failed." : error_;
            return 500;
        }
        if (!result_) {
            error = "No benchmark result is available.";
            return 404;
        }
        body = result_json(*result_);
        return 200;
    }

    int csv(std::string& body, std::string& filename, std::string& error) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == "running") {
            error = "A benchmark is still running.";
            return 409;
        }
        if (!result_) {
            error = "No benchmark result is available.";
            return 404;
        }
        body = workbench::benchmark_result_csv(*result_);
        filename = "benchmark-" + result_->benchmark_id + ".csv";
        return 200;
    }

private:
    static workbench::BenchmarkConfig parse_config(const nlohmann::json& body) {
        if (!body.is_object()) {
            throw std::invalid_argument("Benchmark configuration must be a JSON object.");
        }
        for (auto it = body.begin(); it != body.end(); ++it) {
            const std::string key = it.key();
            if (key != "seed" && key != "batch_size" && key != "mesh_size" && key != "repetitions" &&
                key != "worker_counts") {
                throw std::invalid_argument("Unrecognized benchmark field \"" + key + "\".");
            }
        }

        workbench::BenchmarkConfig config;
        config.benchmark_id = make_job_id();
        config.seed = read_u32(body, "seed");
        config.batch_size = read_size(body, "batch_size");
        config.mesh_size = read_size(body, "mesh_size");
        config.repetitions = read_size(body, "repetitions");
        if (body.contains("worker_counts")) {
            const nlohmann::json& workers = body.at("worker_counts");
            if (!workers.is_array() || workers.empty()) {
                throw std::invalid_argument("worker_counts must be a non-empty array of 1, 2, 3, or 4.");
            }
            for (const nlohmann::json& entry : workers) {
                if (!is_strict_integer(entry) || entry.get<std::int64_t>() <= 0) {
                    throw std::invalid_argument("worker_counts must contain positive integers.");
                }
                config.worker_counts.push_back(static_cast<std::size_t>(entry.get<std::uint64_t>()));
            }
        }
        return config;
    }

    static std::uint32_t read_u32(const nlohmann::json& body, const char* key) {
        if (!body.contains(key) || !is_strict_integer(body.at(key))) {
            throw std::invalid_argument(std::string(key) + " must be an integer.");
        }
        const nlohmann::json& value = body.at(key);
        if (value.is_number_unsigned()) {
            const auto number = value.get<std::uint64_t>();
            if (number > 4294967295ull) {
                throw std::invalid_argument(std::string(key) + " is outside the uint32 range.");
            }
            return static_cast<std::uint32_t>(number);
        }
        const auto number = value.get<std::int64_t>();
        if (number < 0) {
            throw std::invalid_argument(std::string(key) + " must be zero or positive.");
        }
        return static_cast<std::uint32_t>(number);
    }

    static std::size_t read_size(const nlohmann::json& body, const char* key) {
        const std::uint32_t value = read_u32(body, key);
        return static_cast<std::size_t>(value);
    }

    void execute_job(workbench::BenchmarkConfig config) {
        try {
            workbench::BenchmarkResult result = workbench::run_benchmark(
                config,
                [this](const workbench::BenchmarkProgress& progress) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    phase_ = phase_name(progress.phase);
                    message_ = progress_message(progress);
                    worker_count_ = progress.worker_count;
                    repetition_ = progress.repetition;
                    repetitions_ = progress.repetitions;
                    completed_steps_ = progress.completed_steps;
                    total_steps_ = progress.total_steps;
                },
                &cases_completed_);
            std::lock_guard<std::mutex> lock(mutex_);
            benchmark_id_ = result.benchmark_id;
            message_ = "Benchmark completed.";
            phase_ = "completed";
            state_ = "completed";
            error_.clear();
            completed_steps_ = total_steps_;
            cases_completed_.store(cases_total_, std::memory_order_relaxed);
            result_ = std::move(result);
        } catch (const std::exception& failure) {
            std::lock_guard<std::mutex> lock(mutex_);
            result_.reset();
            state_ = "failed";
            phase_ = "failed";
            error_ = failure.what();
            message_ = error_;
        }
    }

    mutable std::mutex mutex_;
    std::thread worker_;
    std::string state_ = "idle";
    std::string phase_ = "idle";
    std::string message_ = "No benchmark has been started.";
    std::string error_;
    std::string benchmark_id_;
    std::size_t worker_count_ = 0;
    std::size_t repetition_ = 0;
    std::size_t repetitions_ = 0;
    std::size_t cases_total_ = 0;
    std::size_t completed_steps_ = 0;
    std::size_t total_steps_ = 0;
    std::atomic<std::size_t> cases_completed_{0};
    std::optional<workbench::BenchmarkResult> result_;
};

}  // namespace

namespace workbench {

void register_performance_routes(httplib::Server& server) {
    const auto jobs = std::make_shared<BenchmarkJobService>();

    server.Get("/api/performance/hardware", [jobs](const httplib::Request&, httplib::Response& response) {
        send_json(response, 200, jobs->hardware());
        std::cout << "GET /api/performance/hardware 200\n";
    });

    server.Get("/api/performance/status", [jobs](const httplib::Request&, httplib::Response& response) {
        send_json(response, 200, jobs->status());
        std::cout << "GET /api/performance/status 200\n";
    });

    server.Get("/api/performance/results", [jobs](const httplib::Request&, httplib::Response& response) {
        nlohmann::json body;
        std::string error;
        const int status = jobs->results(body, error);
        if (status == 200) send_json(response, 200, body);
        else send_error(response, status, error);
        std::cout << "GET /api/performance/results " << status << '\n';
    });

    server.Get("/api/performance/export", [jobs](const httplib::Request&, httplib::Response& response) {
        std::string body;
        std::string filename;
        std::string error;
        const int status = jobs->csv(body, filename, error);
        if (status == 200) {
            response.status = 200;
            response.set_content(std::move(body), "text/csv; charset=utf-8");
            response.set_header("Cache-Control", "no-store");
            response.set_header("Content-Disposition", "attachment; filename=\"" + filename + "\"");
            apply_cors(response);
        } else {
            send_error(response, status, error);
        }
        std::cout << "GET /api/performance/export " << status << '\n';
    });

    server.Post("/api/performance/benchmark", [jobs](const httplib::Request& request, httplib::Response& response) {
        nlohmann::json parsed;
        try {
            parsed = nlohmann::json::parse(request.body);
        } catch (const nlohmann::json::exception&) {
            send_error(response, 400, "Benchmark configuration must be a JSON object.");
            std::cout << "POST /api/performance/benchmark 400\n";
            return;
        }

        nlohmann::json body;
        std::string error;
        const int status = jobs->start(parsed, body, error);
        if (status == 202) send_json(response, 202, body);
        else send_error(response, status, error);
        std::cout << "POST /api/performance/benchmark " << status << '\n';
    });
}

}  // namespace workbench
