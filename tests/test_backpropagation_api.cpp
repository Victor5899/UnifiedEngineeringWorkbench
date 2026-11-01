#include "workbench/server/api_server.hpp"

#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "TEST FAILURE: " << message << '\n';
        ++g_failures;
    }
}

void expect_close(double actual, double expected, double tolerance, const std::string& message) {
    if (!std::isfinite(actual) || !std::isfinite(expected) || std::fabs(actual - expected) > tolerance) {
        std::cerr << std::setprecision(17);
        std::cerr << "TEST FAILURE: " << message << " actual=" << actual << " expected=" << expected << '\n';
        ++g_failures;
    }
}

class TestServer {
public:
    TestServer() {
        workbench::configure_api_server(server_, {});
        port_ = server_.bind_to_any_port("127.0.0.1");
        if (port_ <= 0) throw std::runtime_error("Could not bind the backpropagation test server.");
        thread_ = std::thread([this] { server_.listen_after_bind(); });
    }

    TestServer(const TestServer&) = delete;
    TestServer& operator=(const TestServer&) = delete;

    ~TestServer() {
        server_.stop();
        if (thread_.joinable()) thread_.join();
    }

    httplib::Result get(const std::string& path) const {
        httplib::Result last;
        for (int attempt = 0; attempt < 25; ++attempt) {
            httplib::Client client("127.0.0.1", port_);
            client.set_connection_timeout(2, 0);
            client.set_read_timeout(5, 0);
            last = client.Get(path);
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

int count_data_rows(const std::string& csv) {
    int rows = 0;
    std::istringstream stream(csv);
    std::string line;
    bool header = true;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        if (header) {
            header = false;
            continue;
        }
        ++rows;
    }
    return rows;
}

}  // namespace

int main() {
    try {
        TestServer server;
        const auto health = server.get("/api/health");
        expect(static_cast<bool>(health) && health->status == 200, "health route still responds");

        const auto report = server.get("/api/learning/backpropagation");
        expect(static_cast<bool>(report) && report->status == 200, "backpropagation route status");
        if (!report || report->status != 200) {
            throw std::runtime_error("backpropagation route did not return a report");
        }
        const nlohmann::json body = nlohmann::json::parse(report->body);
        expect(body["training_samples"] == 1, "one training sample");
        expect(body["hidden_neurons"] == 1, "one hidden neuron");
        expect(body["activation"] == "tanh", "tanh activation");
        expect(body["gradient_check_passed"] == true, "gradient check passed");
        expect(body["training_steps"] == 100, "100 training steps");
        expect(body["history"].is_array() && body["history"].size() == 101, "JSON history has 101 rows");
        expect(body["gradient_checks"].is_array() && body["gradient_checks"].size() == 4, "four JSON gradient checks");
        expect_close(body["initial_forward"]["h"].get<double>(), 0.46211715726000974, 1e-12, "API hidden activation");
        expect_close(body["initial_forward"]["y_hat"].get<double>(), 0.46211715726000974, 1e-12, "API prediction");
        expect(std::fabs(body["initial_loss"].get<double>() - 1.182545) <= 5e-6, "API initial loss approximation");
        expect(body["final_loss"].get<double>() < body["initial_loss"].get<double>(), "API loss decreases overall");
        expect_close(body["parameters_after_one_update"]["w"].get<double>(), 0.6209464475240061, 1e-12, "API first w");
        expect_close(body["parameters_after_one_update"]["b"].get<double>(), 0.12094644752400613, 1e-12, "API first b");
        expect_close(body["parameters_after_one_update"]["v"].get<double>(), 1.0710682047485947, 1e-12, "API first v");
        expect_close(body["parameters_after_one_update"]["c"].get<double>(), 0.15378828427399904, 1e-12, "API first c");
        if (body["gradient_checks"].is_array()) {
            for (const auto& check : body["gradient_checks"]) {
                expect(check["passed"] == true, "API gradient row passed");
                expect(check["absolute_error"].get<double>() <= 1e-5, "API absolute error bound");
            }
        }
        if (body["history"].is_array() && body["history"].size() == 101) {
            expect(body["history"].front()["step"] == 0, "JSON history starts at step 0");
            expect(body["history"].back()["step"] == 100, "JSON history ends at step 100");
        }

        const auto csv = server.get("/api/learning/backpropagation/csv");
        expect(static_cast<bool>(csv) && csv->status == 200, "CSV route status");
        if (csv && csv->status == 200) {
            expect(csv->body.rfind("step,loss,w,b,v,c\n", 0) == 0, "CSV header");
            expect(count_data_rows(csv->body) == 101, "CSV contains 101 data rows");
        }

        const auto devices = server.get("/api/hardware/devices");
        expect(static_cast<bool>(devices) && devices->status == 200, "hardware route still responds");
    } catch (const std::exception& error) {
        std::cerr << "TEST FAILURE: " << error.what() << '\n';
        return 1;
    }
    if (g_failures != 0) {
        std::cerr << g_failures << " backpropagation API checks failed.\n";
        return 1;
    }
    std::cout << "All backpropagation API tests passed.\n";
    return 0;
}
