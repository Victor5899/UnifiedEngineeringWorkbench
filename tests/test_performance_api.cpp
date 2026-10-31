#include "workbench/server/api_server.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <thread>

#ifndef WORKBENCH_TEST_DIR
#define WORKBENCH_TEST_DIR "tmp_tests"
#endif

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "TEST FAILURE: " << message << '\n';
        ++g_failures;
    }
}

class TestServer {
public:
    explicit TestServer(workbench::ApiServerConfig config) {
        workbench::configure_api_server(server_, config);
        port_ = server_.bind_to_any_port("127.0.0.1");
        if (port_ <= 0) throw std::runtime_error("Could not bind the performance test server.");
        thread_ = std::thread([this] { server_.listen_after_bind(); });
    }

    TestServer(const TestServer&) = delete;
    TestServer& operator=(const TestServer&) = delete;

    ~TestServer() {
        server_.stop();
        if (thread_.joinable()) thread_.join();
    }

    httplib::Result call(const std::function<httplib::Result(httplib::Client&)>& operation) const {
        httplib::Result last;
        for (int attempt = 0; attempt < 25; ++attempt) {
            httplib::Client client("127.0.0.1", port_);
            client.set_connection_timeout(2, 0);
            client.set_read_timeout(5, 0);
            last = operation(client);
            if (last) return last;
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
        return last;
    }

private:
    httplib::Server server_;
    int port_ = 0;
    std::thread thread_;
};

nlohmann::json json_body(const httplib::Result& response) {
    return nlohmann::json::parse(response->body);
}

void test_hardware_and_idle(const TestServer& server) {
    const auto hardware = server.call([](httplib::Client& client) { return client.Get("/api/performance/hardware"); });
    expect(static_cast<bool>(hardware) && hardware->status == 200, "hardware status");
    if (!hardware || hardware->status != 200) return;
    const auto body = json_body(hardware);
    expect(body.contains("cpu_model"), "hardware cpu model field");
    expect(body.contains("physical_cores"), "hardware physical cores field");
    expect(body.contains("logical_cpus"), "hardware logical CPU field");
    expect(body.contains("ram_bytes"), "hardware RAM field");
    expect(body.contains("operating_system"), "hardware OS field");
    expect(body.contains("compiler"), "hardware compiler field");
    expect(body.contains("compiler_version"), "hardware compiler version field");
    expect(body.contains("build_type"), "hardware build type field");
    expect(body.contains("optimization_flags"), "hardware optimization flags field");
    expect(body["maximum_worker_count"].is_number_integer(), "maximum worker count is an integer");
    const int maximum = body["maximum_worker_count"].get<int>();
    expect(maximum >= 1 && maximum <= 4, "maximum worker count is 1 to 4");
    if (!body["logical_cpus"].is_null()) {
        expect(maximum <= body["logical_cpus"].get<int>(), "worker limit does not exceed logical CPUs");
    }

    const auto status = server.call([](httplib::Client& client) { return client.Get("/api/performance/status"); });
    expect(static_cast<bool>(status) && status->status == 200, "idle status request");
    if (status) expect(json_body(status)["state"] == "idle", "status starts idle");

    const auto missing = server.call([](httplib::Client& client) { return client.Get("/api/performance/results"); });
    expect(static_cast<bool>(missing) && missing->status == 404, "results are absent before a benchmark");
    const auto export_missing =
        server.call([](httplib::Client& client) { return client.Get("/api/performance/export"); });
    expect(static_cast<bool>(export_missing) && export_missing->status == 404, "export is absent before a benchmark");
}

void test_invalid(const TestServer& server) {
    const auto reject = [&](const nlohmann::json& payload, const std::string& label) {
        const auto response = server.call([&](httplib::Client& client) {
            return client.Post("/api/performance/benchmark", payload.dump(), "application/json");
        });
        expect(static_cast<bool>(response) && response->status == 400, label);
    };
    reject({{"seed", 1}, {"batch_size", 0}, {"mesh_size", 4}, {"repetitions", 1}}, "zero batch is rejected");
    reject({{"seed", 1}, {"batch_size", 1}, {"mesh_size", 0}, {"repetitions", 1}}, "zero mesh is rejected");
    reject({{"seed", 1}, {"batch_size", 1}, {"mesh_size", 4}, {"repetitions", 0}}, "zero repetitions are rejected");
    reject({{"seed", 1}, {"batch_size", 1}, {"mesh_size", 4}, {"repetitions", 1}, {"worker_counts", {5}}},
           "five workers are rejected");
    reject({{"seed", 1}, {"batch_size", 1}, {"mesh_size", 4}, {"repetitions", 1}, {"worker_counts", {1, 1}}},
           "duplicate workers are rejected");
    reject({{"seed", -1}, {"batch_size", 1}, {"mesh_size", 4}, {"repetitions", 1}}, "negative seed is rejected");
    const auto garbage = server.call([](httplib::Client& client) {
        return client.Post("/api/performance/benchmark", "{", "application/json");
    });
    expect(static_cast<bool>(garbage) && garbage->status == 400, "malformed JSON is rejected");

    const auto status = server.call([](httplib::Client& client) { return client.Get("/api/performance/status"); });
    expect(static_cast<bool>(status) && json_body(status)["state"] == "idle", "invalid requests do not start a job");
}

void test_job(const TestServer& server, int maximum_workers) {
    nlohmann::json request = {
        {"seed", 11},
        {"batch_size", 4},
        {"mesh_size", 96},
        {"repetitions", 5},
        {"worker_counts", nlohmann::json::array({1})},
    };
    if (maximum_workers >= 2) request["worker_counts"].push_back(2);

    const auto started = server.call([&](httplib::Client& client) {
        return client.Post("/api/performance/benchmark", request.dump(), "application/json");
    });
    expect(static_cast<bool>(started) && started->status == 202, "valid benchmark is accepted");
    if (!started || started->status != 202) return;
    const auto accepted = json_body(started);
    expect(accepted["state"] == "running", "accepted benchmark is running");
    expect(accepted["benchmark_id"].is_string(), "accepted benchmark has an id");

    bool saw_running = false;
    bool saw_concurrency = false;
    nlohmann::json finished;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto status = server.call([](httplib::Client& client) { return client.Get("/api/performance/status"); });
        if (!status || status->status != 200) break;
        const auto body = json_body(status);
        if (body["state"] == "running") {
            saw_running = true;
            if (!saw_concurrency) {
                saw_concurrency = true;
                const auto health = server.call([](httplib::Client& client) { return client.Get("/api/health"); });
                expect(static_cast<bool>(health) && health->status == 200, "health responds during a benchmark");
                const auto duplicate = server.call([&](httplib::Client& client) {
                    return client.Post("/api/performance/benchmark", request.dump(), "application/json");
                });
                expect(static_cast<bool>(duplicate) && duplicate->status == 409, "a second benchmark is rejected");
                const auto early = server.call([](httplib::Client& client) {
                    return client.Get("/api/performance/results");
                });
                expect(static_cast<bool>(early) && early->status == 409, "results stay hidden while running");
                if (early) expect(early->body.find("median_execution_time_seconds") == std::string::npos,
                                 "partial timings are not published");
                const auto early_csv = server.call([](httplib::Client& client) {
                    return client.Get("/api/performance/export");
                });
                expect(static_cast<bool>(early_csv) && early_csv->status == 409, "CSV stays hidden while running");
                expect(body["cases_total"] == 4, "status reports the batch size");
            }
        }
        if (body["state"] == "completed" || body["state"] == "failed") {
            finished = body;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }

    expect(saw_running, "status reports a running benchmark");
    expect(finished["state"] == "completed", "benchmark completes");
    if (finished["state"] != "completed") {
        std::cerr << "TEST FAILURE: final status " << finished.dump() << '\n';
        return;
    }

    const auto results = server.call([](httplib::Client& client) { return client.Get("/api/performance/results"); });
    expect(static_cast<bool>(results) && results->status == 200, "completed results");
    if (!results || results->status != 200) return;
    const auto body = json_body(results);
    expect(body["random_seed"] == 11, "result seed");
    expect(body["batch_size"] == 4, "result batch size");
    expect(body["mesh_size"] == 96, "result mesh size");
    expect(body["repetitions"] == 5, "result repetitions");
    expect(body["numerical_validation"] == "passed", "result validation");
    expect(body["maximum_absolute_error_celsius"].get<double>() <= 1e-8, "result numerical error");
    expect(body["configurations"].is_array(), "result configurations");
    const auto& configurations = body["configurations"];
    expect(configurations.size() == request["worker_counts"].size() + 1, "serial and requested workers");
    if (!configurations.empty()) {
        expect(configurations[0]["execution_mode"] == "serial", "first configuration is serial");
        expect(configurations[0]["execution_time_seconds"].size() == 5, "five serial samples");
        expect(configurations[0]["warmup_time_seconds"].is_number(), "serial warm-up is reported separately");
        expect(body.contains("measured_round_orders") && body["measured_round_orders"].size() == 5,
               "measured rounds record their execution order");
        const double serial_median = configurations[0]["median_execution_time_seconds"].get<double>();
        for (std::size_t index = 1; index < configurations.size(); ++index) {
            const double median = configurations[index]["median_execution_time_seconds"].get<double>();
            const double speedup = configurations[index]["speedup"].get<double>();
            const int workers = configurations[index]["worker_count"].get<int>();
            expect(std::abs(speedup - serial_median / median) <= 1e-9, "API speedup matches the medians");
            expect(std::abs(configurations[index]["parallel_efficiency"].get<double>() - speedup / workers) <= 1e-9,
                   "API efficiency uses the worker count");
            expect(configurations[index]["execution_time_seconds"].size() == 5, "five parallel samples");
        }
    }

    const auto csv = server.call([](httplib::Client& client) { return client.Get("/api/performance/export"); });
    expect(static_cast<bool>(csv) && csv->status == 200, "CSV export status");
    if (csv && csv->status == 200) {
        expect(csv->get_header_value("Content-Type").find("text/csv") != std::string::npos, "CSV content type");
        expect(csv->body.find("execution_time_seconds") != std::string::npos, "CSV header row");
        expect(csv->body.find(body["benchmark_id"].get<std::string>()) != std::string::npos, "CSV benchmark id");
        const std::size_t expected_rows = 1 + 5 * configurations.size();
        expect(std::count(csv->body.begin(), csv->body.end(), '\n') == static_cast<std::ptrdiff_t>(expected_rows),
               "CSV row count");
    }

    nlohmann::json follow_up = {{"seed", 4}, {"batch_size", 2}, {"mesh_size", 4}, {"repetitions", 1}};
    const auto again = server.call([&](httplib::Client& client) {
        return client.Post("/api/performance/benchmark", follow_up.dump(), "application/json");
    });
    expect(static_cast<bool>(again) && again->status == 202, "a new benchmark can start after completion");
    const auto follow_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    std::string follow_state = "running";
    while (std::chrono::steady_clock::now() < follow_deadline && follow_state == "running") {
        const auto status = server.call([](httplib::Client& client) { return client.Get("/api/performance/status"); });
        if (!status) break;
        follow_state = json_body(status)["state"].get<std::string>();
        if (follow_state == "running") std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    expect(follow_state == "completed", "default worker benchmark completes");
    const auto follow_results =
        server.call([](httplib::Client& client) { return client.Get("/api/performance/results"); });
    expect(static_cast<bool>(follow_results) && follow_results->status == 200, "default worker results");
    if (follow_results && follow_results->status == 200) {
        const auto follow_body = json_body(follow_results);
        expect(follow_body["configurations"].size() == static_cast<std::size_t>(maximum_workers) + 1,
               "omitted workers run every supported count");
        expect(follow_body["numerical_validation"] == "passed", "default worker validation");
    }
}

}  // namespace

int main() {
    try {
        const auto temp = std::filesystem::path(WORKBENCH_TEST_DIR) / "performance_api_temp";
        std::filesystem::remove_all(temp);
        workbench::ApiServerConfig config;
        config.version = "test";
        config.temp_directory = temp;
        TestServer server(config);
        test_hardware_and_idle(server);
        test_invalid(server);
        const auto hardware = server.call([](httplib::Client& client) { return client.Get("/api/performance/hardware"); });
        int maximum = 1;
        if (hardware && hardware->status == 200) {
            maximum = json_body(hardware)["maximum_worker_count"].get<int>();
        }
        if (maximum < 4) {
            const auto too_many = server.call([](httplib::Client& client) {
                const nlohmann::json payload = {
                    {"seed", 1}, {"batch_size", 1}, {"mesh_size", 4}, {"repetitions", 1}, {"worker_counts", {4}}};
                return client.Post("/api/performance/benchmark", payload.dump(), "application/json");
            });
            expect(static_cast<bool>(too_many) && too_many->status == 400,
                   "four workers are rejected when fewer logical CPUs are available");
        }
        test_job(server, maximum);
    } catch (const std::exception& error) {
        std::cerr << "TEST FAILURE: uncaught exception: " << error.what() << '\n';
        return 1;
    }
    if (g_failures != 0) {
        std::cerr << g_failures << " performance API test failure(s).\n";
        return 1;
    }
    std::cout << "All performance API tests passed.\n";
    return 0;
}
