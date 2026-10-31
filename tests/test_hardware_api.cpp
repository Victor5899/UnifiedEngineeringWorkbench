#include "workbench/server/api_server.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>
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

class TestServer {
public:
    explicit TestServer(workbench::ApiServerConfig config) {
        workbench::configure_api_server(server_, config);
        port_ = server_.bind_to_any_port("127.0.0.1");
        if (port_ <= 0) throw std::runtime_error("Could not bind the hardware test server.");
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

httplib::Result post_json(const TestServer& server, const std::string& path, const nlohmann::json& body) {
    return server.call([&](httplib::Client& client) {
        return client.Post(path, body.dump(), "application/json");
    });
}

void expect_simulation(const nlohmann::json& body, const std::string& label) {
    expect(body["simulated"] == true, label + " is simulated");
    expect(body["physical_hardware_connected"] == false, label + " reports no physical hardware");
}

void test_discovery_and_capabilities(const TestServer& server) {
    const auto health = server.call([](httplib::Client& client) { return client.Get("/api/health"); });
    expect(static_cast<bool>(health) && health->status == 200, "health route still responds");

    const auto geometry = server.call([](httplib::Client& client) {
        return client.Post("/api/geometry/stl", "", "text/plain");
    });
    expect(static_cast<bool>(geometry) && geometry->status == 400, "geometry route is still registered");

    const auto thermal = server.call([](httplib::Client& client) {
        return client.Post("/api/thermal/csv", "", "text/plain");
    });
    expect(static_cast<bool>(thermal) && thermal->status == 400, "thermal route is still registered");

    const auto performance =
        server.call([](httplib::Client& client) { return client.Get("/api/performance/status"); });
    expect(static_cast<bool>(performance) && performance->status == 200, "performance route is still registered");

    const auto devices = server.call([](httplib::Client& client) { return client.Get("/api/hardware/devices"); });
    expect(static_cast<bool>(devices) && devices->status == 200, "device list status");
    if (!devices || devices->status != 200) return;
    const auto list = json_body(devices);
    expect_simulation(list, "device list");
    expect(list["devices"].is_array() && list["devices"].size() == 3, "three devices");
    if (!list["devices"].is_array() || list["devices"].size() != 3) return;

    const auto& mcu = list["devices"][0];
    const auto& fpga = list["devices"][1];
    const auto& jtag = list["devices"][2];
    expect(mcu["id"] == "mcu-uno-rev3" && mcu["device_type"] == "microcontroller", "MCU identity");
    expect(fpga["id"] == "fpga-arty-a7-100" && fpga["device_type"] == "fpga", "FPGA identity");
    expect(jtag["id"] == "jtag-hs2" && jtag["device_type"] == "jtag_adapter", "JTAG identity");
    expect(mcu["connection_state"] == "disconnected", "MCU starts disconnected");
    expect(fpga["connection_state"] == "disconnected", "FPGA starts disconnected");
    expect(jtag["connection_state"] == "disconnected", "JTAG starts disconnected");
    expect(mcu["display_name"].is_string() && !mcu["display_name"].get<std::string>().empty(), "MCU name");
    expect(mcu["capabilities"].is_array() && !mcu["capabilities"].empty(), "MCU capabilities");
    expect(fpga["capabilities"].is_array() && !fpga["capabilities"].empty(), "FPGA capabilities");
    expect(jtag["capabilities"].is_array() && !jtag["capabilities"].empty(), "JTAG capabilities");

    const auto one = server.call([](httplib::Client& client) { return client.Get("/api/hardware/devices/fpga-arty-a7-100"); });
    expect(static_cast<bool>(one) && one->status == 200, "single device status");
    if (one && one->status == 200) {
        const auto body = json_body(one);
        expect(body["reference_profile"].is_string(), "single device profile");
        expect(body["connection_state"] == "disconnected", "single device disconnected");
    }

    const auto missing =
        server.call([](httplib::Client& client) { return client.Get("/api/hardware/devices/missing-device"); });
    expect(static_cast<bool>(missing) && missing->status == 404, "unknown device status");
    if (missing && missing->status == 404) {
        const auto body = json_body(missing);
        expect(body["error"].is_string(), "unknown device error");
        expect(body["code"] == "unknown_device", "unknown device code");
    }

    const auto capabilities =
        server.call([](httplib::Client& client) { return client.Get("/api/hardware/capabilities"); });
    expect(static_cast<bool>(capabilities) && capabilities->status == 200, "capabilities status");
    if (capabilities && capabilities->status == 200) {
        const auto body = json_body(capabilities);
        expect_simulation(body, "capabilities");
        expect(body["devices"].size() == 3, "capabilities cover three devices");
        expect(body["devices"][2]["id"] == "jtag-hs2", "JTAG capabilities id");
        expect(body["devices"][2]["capabilities"].size() >= 1, "JTAG capability entries");
    }
}

void test_validation(const TestServer& server) {
    const auto malformed = server.call([](httplib::Client& client) {
        return client.Post("/api/hardware/connect", "{", "application/json");
    });
    expect(static_cast<bool>(malformed) && malformed->status == 400, "malformed connect JSON");

    const auto null_body = server.call([](httplib::Client& client) {
        return client.Post("/api/hardware/connect", "null", "application/json");
    });
    expect(static_cast<bool>(null_body) && null_body->status == 400, "null connect body");

    const auto missing_id = post_json(server, "/api/hardware/connect", nlohmann::json::object());
    expect(static_cast<bool>(missing_id) && missing_id->status == 400, "connect without device_id");

    const auto extra = post_json(server, "/api/hardware/connect", {{"device_id", "mcu-uno-rev3"}, {"force", true}});
    expect(static_cast<bool>(extra) && extra->status == 400, "connect rejects an unknown field");

    const auto numeric = post_json(server, "/api/hardware/disconnect", {{"device_id", 4}});
    expect(static_cast<bool>(numeric) && numeric->status == 400, "disconnect rejects a numeric id");

    const auto unknown = post_json(server, "/api/hardware/connect", {{"device_id", "missing-device"}});
    expect(static_cast<bool>(unknown) && unknown->status == 404, "connect unknown id");
    if (unknown && unknown->status == 404) expect(json_body(unknown)["code"] == "unknown_device", "connect unknown code");

    const auto telemetry = server.call([](httplib::Client& client) { return client.Get("/api/hardware/telemetry"); });
    expect(static_cast<bool>(telemetry) && telemetry->status == 400, "telemetry without device_id");

    const auto offline = server.call([](httplib::Client& client) {
        return client.Get("/api/hardware/telemetry?device_id=mcu-uno-rev3");
    });
    expect(static_cast<bool>(offline) && offline->status == 409, "telemetry while disconnected");
    if (offline && offline->status == 409) {
        const auto body = json_body(offline);
        expect(body["code"] == "not_connected", "disconnected telemetry code");
        expect(body["connection_state"] == "disconnected", "disconnected telemetry state");
        expect(offline->body.find("28.5") == std::string::npos, "disconnected telemetry hides the sample");
    }

    const auto unsupported = post_json(
        server, "/api/hardware/simulate-failure", {{"device_id", "mcu-uno-rev3"}, {"failure", "connection_timeout"}});
    expect(static_cast<bool>(unsupported) && unsupported->status == 400, "MCU failure is rejected");
    if (unsupported && unsupported->status == 400) {
        expect(json_body(unsupported)["code"] == "unsupported_operation", "MCU failure code");
    }
}

void test_session(const TestServer& server) {
    const auto connected = post_json(server, "/api/hardware/connect", {{"device_id", "mcu-uno-rev3"}});
    expect(static_cast<bool>(connected) && connected->status == 200, "MCU connect");
    if (connected && connected->status == 200) {
        const auto body = json_body(connected);
        expect_simulation(body, "MCU connect");
        expect(body["connection_state"] == "connected", "MCU connect state");
        expect(body["message"].get<std::string>().find("Simulated") != std::string::npos, "MCU connect message");
    }

    const auto duplicate = post_json(server, "/api/hardware/connect", {{"device_id", "mcu-uno-rev3"}});
    expect(static_cast<bool>(duplicate) && duplicate->status == 409, "duplicate MCU connect");
    if (duplicate && duplicate->status == 409) {
        expect(json_body(duplicate)["code"] == "already_connected", "duplicate code");
        expect(json_body(duplicate)["connection_state"] == "connected", "duplicate keeps the connection");
    }

    const auto reading = server.call([](httplib::Client& client) {
        return client.Get("/api/hardware/telemetry?device_id=mcu-uno-rev3");
    });
    expect(static_cast<bool>(reading) && reading->status == 200, "MCU telemetry");
    if (reading && reading->status == 200) {
        const auto body = json_body(reading);
        expect_simulation(body, "MCU telemetry");
        expect(body["notice"].get<std::string>().find("Simulated") != std::string::npos, "MCU telemetry notice");
        bool found_temperature = false;
        for (const auto& sample : body["readings"]) {
            expect(sample["simulated"] == true, "MCU reading flag");
            if (sample["name"] == "Temperature") {
                found_temperature = sample["value"] == "28.5" && sample["unit"] == "C";
            }
        }
        expect(found_temperature, "MCU temperature reading");
    }

    const auto fpga = post_json(server, "/api/hardware/connect", {{"device_id", "fpga-arty-a7-100"}});
    expect(static_cast<bool>(fpga) && fpga->status == 200, "FPGA connect");
    const auto fpga_reading = server.call([](httplib::Client& client) {
        return client.Get("/api/hardware/telemetry?device_id=fpga-arty-a7-100");
    });
    expect(static_cast<bool>(fpga_reading) && fpga_reading->status == 200, "FPGA telemetry");
    if (fpga_reading && fpga_reading->status == 200) {
        expect(fpga_reading->body.find("No bitstream") != std::string::npos, "FPGA response denies a bitstream");
        expect(fpga_reading->body.find("does not mean a bitstream") != std::string::npos, "FPGA notice");
    }

    const auto closed = post_json(server, "/api/hardware/disconnect", {{"device_id", "mcu-uno-rev3"}});
    expect(static_cast<bool>(closed) && closed->status == 200, "MCU disconnect");
    if (closed && closed->status == 200) expect(json_body(closed)["connection_state"] == "disconnected", "MCU closed");

    const auto again = post_json(server, "/api/hardware/disconnect", {{"device_id", "mcu-uno-rev3"}});
    expect(static_cast<bool>(again) && again->status == 409, "second MCU disconnect");

    const auto fpga_state =
        server.call([](httplib::Client& client) { return client.Get("/api/hardware/devices/fpga-arty-a7-100"); });
    expect(static_cast<bool>(fpga_state) && json_body(fpga_state)["connection_state"] == "connected",
           "FPGA stays connected after the MCU disconnects");
    const auto jtag_state =
        server.call([](httplib::Client& client) { return client.Get("/api/hardware/devices/jtag-hs2"); });
    expect(static_cast<bool>(jtag_state) && json_body(jtag_state)["connection_state"] == "disconnected",
           "JTAG is still disconnected");
}

void test_timeout_recovery(const TestServer& server) {
    const auto armed = post_json(
        server, "/api/hardware/simulate-failure", {{"device_id", "jtag-hs2"}, {"failure", "connection_timeout"}});
    expect(static_cast<bool>(armed) && armed->status == 200, "arm JTAG timeout");
    if (armed && armed->status == 200) {
        const auto body = json_body(armed);
        expect(body["armed"] == true, "timeout is armed");
        expect(body["failure"] == "connection_timeout", "timeout failure id");
        expect(body["message"].get<std::string>().find("Simulated") != std::string::npos, "arm message");
    }

    const auto timed_out = post_json(server, "/api/hardware/connect", {{"device_id", "jtag-hs2"}});
    expect(static_cast<bool>(timed_out) && timed_out->status == 504, "simulated timeout status");
    if (timed_out && timed_out->status == 504) {
        const auto body = json_body(timed_out);
        expect(body["code"] == "simulated_timeout", "timeout code");
        expect(body["connection_state"] == "disconnected", "timeout state");
        expect(body["error"].get<std::string>().find("Simulated JTAG connection timeout") != std::string::npos,
               "timeout error text");
        expect(body["simulated"] == true, "timeout is labelled simulated");
    }

    const auto during =
        server.call([](httplib::Client& client) { return client.Get("/api/hardware/devices/jtag-hs2"); });
    expect(static_cast<bool>(during) && json_body(during)["connection_state"] == "disconnected",
           "JTAG remains disconnected after the timeout");
    const auto fpga =
        server.call([](httplib::Client& client) { return client.Get("/api/hardware/devices/fpga-arty-a7-100"); });
    expect(static_cast<bool>(fpga) && json_body(fpga)["connection_state"] == "connected",
           "FPGA state survives the JTAG timeout");

    const auto recovered = post_json(server, "/api/hardware/connect", {{"device_id", "jtag-hs2"}});
    expect(static_cast<bool>(recovered) && recovered->status == 200, "JTAG recovery connect");
    if (recovered && recovered->status == 200) {
        expect(json_body(recovered)["connection_state"] == "connected", "JTAG recovered");
    }
    const auto reading = server.call([](httplib::Client& client) {
        return client.Get("/api/hardware/telemetry?device_id=jtag-hs2");
    });
    expect(static_cast<bool>(reading) && reading->status == 200, "JTAG telemetry after recovery");
    if (reading && reading->status == 200) {
        expect(reading->body.find("No boundary scan") != std::string::npos ||
                   reading->body.find("not a processing device") != std::string::npos,
               "recovered JTAG telemetry stays a transport");
        expect(reading->body.find("\"Programming performed\"") != std::string::npos, "programming field present");
    }

    const auto bad_failure =
        post_json(server, "/api/hardware/simulate-failure", {{"device_id", "jtag-hs2"}, {"failure", "power_loss"}});
    expect(static_cast<bool>(bad_failure) && bad_failure->status == 400, "unknown JTAG failure is rejected");
    const auto still =
        server.call([](httplib::Client& client) { return client.Get("/api/hardware/devices/jtag-hs2"); });
    expect(static_cast<bool>(still) && json_body(still)["connection_state"] == "connected",
           "rejected failure does not drop the JTAG session");
}

}  // namespace

int main() {
    try {
        const auto temp = std::filesystem::temp_directory_path() / "workbench-hardware-api-tests";
        std::filesystem::remove_all(temp);
        workbench::ApiServerConfig config;
        config.version = "test";
        config.temp_directory = temp;
        TestServer server(config);
        test_discovery_and_capabilities(server);
        test_validation(server);
        test_session(server);
        test_timeout_recovery(server);
    } catch (const std::exception& error) {
        std::cerr << "TEST FAILURE: uncaught exception: " << error.what() << '\n';
        return 1;
    }
    if (g_failures != 0) {
        std::cerr << g_failures << " hardware API test failure(s).\n";
        return 1;
    }
    std::cout << "All hardware API tests passed.\n";
    return 0;
}
