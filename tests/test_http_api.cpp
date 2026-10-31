#include "workbench/server/api_server.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

#ifndef WORKBENCH_SOURCE_DIR
#define WORKBENCH_SOURCE_DIR "."
#endif
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

void expect_near(double actual, double expected, const std::string& message) {
    if (!(std::abs(actual - expected) <= 1e-6)) {
        std::cerr << "TEST FAILURE: " << message << " actual=" << actual
                  << " expected=" << expected << '\n';
        ++g_failures;
    }
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read " + path.string());
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::size_t count_files(const std::filesystem::path& directory) {
    if (!std::filesystem::exists(directory)) return 0;
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file()) ++count;
    }
    return count;
}

class TestServer {
public:
    explicit TestServer(workbench::ApiServerConfig config)
        : temp_directory_(config.temp_directory) {
        workbench::configure_api_server(server_, config);
        port_ = server_.bind_to_any_port("127.0.0.1");
        if (port_ <= 0) throw std::runtime_error("Could not bind the test server.");
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
            client.set_connection_timeout(1, 0);
            client.set_read_timeout(8, 0);
            last = operation(client);
            if (last) return last;
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
        return last;
    }

    const std::filesystem::path& temp_directory() const { return temp_directory_; }

private:
    httplib::Server server_;
    std::filesystem::path temp_directory_;
    int port_ = 0;
    std::thread thread_;
};

httplib::MultipartFormData file_part(const std::string& filename, const std::string& content) {
    httplib::MultipartFormData part;
    part.name = "file";
    part.filename = filename;
    part.content = content;
    part.content_type = "application/octet-stream";
    return part;
}

const char* kTriangleStl =
    "solid wedge\n"
    "  facet normal 0 0 1\n"
    "    outer loop\n"
    "      vertex 0 0 0\n"
    "      vertex 1 0 0\n"
    "      vertex 0 1 0\n"
    "    endloop\n"
    "  endfacet\n"
    "endsolid wedge\n";

void test_health_and_missing_route(const TestServer& server) {
    const auto health = server.call([](httplib::Client& client) { return client.Get("/api/health"); });
    expect(static_cast<bool>(health), "health request connects");
    if (!health) return;
    expect(health->status == 200, "health status");
    expect(health->get_header_value("Content-Type").find("application/json") != std::string::npos,
           "health content type");
    expect(health->get_header_value("Access-Control-Allow-Origin") == "*", "health CORS header");
    const auto body = nlohmann::json::parse(health->body);
    expect(body["status"] == "ok", "health status field");
    expect(body["service"] == "unified-engineering-workbench", "health service field");
    expect(body["version"] == "test", "health version field");

    const auto options = server.call([](httplib::Client& client) {
        return client.Options("/api/geometry/stl");
    });
    expect(static_cast<bool>(options) && options->status == 204, "CORS preflight");

    const auto missing = server.call([](httplib::Client& client) { return client.Get("/api/missing"); });
    expect(static_cast<bool>(missing) && missing->status == 404, "missing route status");
    if (missing) {
        const auto error = nlohmann::json::parse(missing->body);
        expect(error.contains("error"), "missing route JSON error");
    }
}

void test_valid_stl(const TestServer& server) {
    const std::string bytes = read_file(std::filesystem::path(WORKBENCH_SOURCE_DIR) /
                                         "data" / "geometry" / "sample_geometry.stl");
    const auto response = server.call([&](httplib::Client& client) {
        return client.Post("/api/geometry/stl", {file_part("sample_geometry.stl", bytes)});
    });
    expect(static_cast<bool>(response), "STL upload connects");
    if (!response) return;
    expect(response->status == 200, "STL upload status");
    const auto body = nlohmann::json::parse(response->body);
    expect(body["filename"] == "sample_geometry.stl", "STL filename");
    expect(body["solid_name"] == "rectangular_block", "STL solid name");
    expect(body["triangle_count"] == 12, "STL triangle count");
    expect(body["vertex_count"] == 36, "STL vertex count");
    expect(body["triangles"].size() == 12, "STL triangle array");
    expect(body["coordinate_units"] == "file", "STL coordinate units");
    expect_near(body["bounding_box"]["min"][0], 0.0, "STL min x");
    expect_near(body["bounding_box"]["min"][1], 0.0, "STL min y");
    expect_near(body["bounding_box"]["min"][2], 0.0, "STL min z");
    expect_near(body["bounding_box"]["max"][0], 0.10, "STL max x");
    expect_near(body["bounding_box"]["max"][1], 0.05, "STL max y");
    expect_near(body["bounding_box"]["max"][2], 0.02, "STL max z");
    expect_near(body["dimensions"]["x"], 0.10, "STL dimension x");
    expect_near(body["dimensions"]["y"], 0.05, "STL dimension y");
    expect_near(body["dimensions"]["z"], 0.02, "STL dimension z");
    expect_near(body["triangles"][0]["normal"][2], -1.0, "STL first normal z");
    expect_near(body["triangles"][0]["vertices"][0][0], 0.0, "STL first vertex x");
    expect(count_files(server.temp_directory()) == 0, "STL temp file removed");
}

void test_valid_temperature(const TestServer& server) {
    const std::string bytes = read_file(std::filesystem::path(WORKBENCH_SOURCE_DIR) /
                                         "data" / "thermal" / "sample_temperature.csv");
    const auto response = server.call([&](httplib::Client& client) {
        return client.Post("/api/thermal/csv", {file_part("sample_temperature.csv", bytes)});
    });
    expect(static_cast<bool>(response), "CSV upload connects");
    if (!response) return;
    expect(response->status == 200, "CSV upload status");
    const auto body = nlohmann::json::parse(response->body);
    expect(body["filename"] == "sample_temperature.csv", "CSV filename");
    expect(body["node_count"] == 27, "CSV node count");
    expect(body["nodes"].size() == 27, "CSV node array");
    expect(body["nodes"][0]["node_id"] == 1, "CSV first node id");
    expect_near(body["nodes"][0]["temperature_C"], 25.0, "CSV first temperature");
    expect_near(body["nodes"][26]["x_m"], 0.1, "CSV last x");
    expect_near(body["nodes"][26]["temperature_C"], 67.0, "CSV last temperature");
    expect_near(body["minimum_temperature_C"], 25.0, "CSV minimum");
    expect_near(body["maximum_temperature_C"], 67.0, "CSV maximum");
    expect_near(body["average_temperature_C"], 46.0, "CSV average");
    expect_near(body["bounding_box"]["max"][1], 0.05, "CSV max y");
    expect_near(body["dimensions"]["z"], 0.02, "CSV dimension z");
    expect(body["coordinate_units"] == "m", "CSV coordinate units");
    expect(body["temperature_units"] == "C", "CSV temperature units");
    expect(count_files(server.temp_directory()) == 0, "CSV temp file removed");

    const char* constant =
        "node_id,x_m,y_m,z_m,temperature_C\n"
        "1,0,0,0,20\n"
        "2,1,0,0,20\n";
    const auto flat = server.call([&](httplib::Client& client) {
        return client.Post("/api/thermal/csv", {file_part("flat.csv", constant)});
    });
    expect(static_cast<bool>(flat) && flat->status == 200, "constant temperature status");
    if (!flat || flat->status != 200) return;
    expect(flat->body.find("nan") == std::string::npos, "constant temperature has no nan");
    expect(flat->body.find("inf") == std::string::npos, "constant temperature has no inf");
    const auto flat_body = nlohmann::json::parse(flat->body);
    expect_near(flat_body["minimum_temperature_C"], 20.0, "constant minimum");
    expect_near(flat_body["maximum_temperature_C"], 20.0, "constant maximum");
    expect_near(flat_body["average_temperature_C"], 20.0, "constant average");
}

void test_invalid_and_missing(const TestServer& server) {
    const auto bad_stl = server.call([](httplib::Client& client) {
        return client.Post("/api/geometry/stl", {file_part("broken.stl", "this is not an stl file")});
    });
    expect(static_cast<bool>(bad_stl) && bad_stl->status == 400, "invalid STL status");
    if (bad_stl) {
        const auto body = nlohmann::json::parse(bad_stl->body);
        const std::string message = body["error"];
        expect(message.find("STL parse error") != std::string::npos, "invalid STL message");
        expect(message.find("api_secret_temp") == std::string::npos, "invalid STL hides temp path");
    }

    const auto bad_csv = server.call([](httplib::Client& client) {
        return client.Post("/api/thermal/csv", {file_part("broken.csv", "a,b,c\n1,2,3\n")});
    });
    expect(static_cast<bool>(bad_csv) && bad_csv->status == 400, "invalid CSV status");
    if (bad_csv) {
        const auto body = nlohmann::json::parse(bad_csv->body);
        const std::string message = body["error"];
        expect(message.find("Temperature CSV parse error") != std::string::npos, "invalid CSV message");
        expect(message.find("api_secret_temp") == std::string::npos, "invalid CSV hides temp path");
    }

    const auto missing = server.call([](httplib::Client& client) {
        httplib::MultipartFormData note;
        note.name = "note";
        note.content = "hello";
        note.content_type = "text/plain";
        return client.Post("/api/geometry/stl", {note});
    });
    expect(static_cast<bool>(missing) && missing->status == 400, "missing file field status");
    if (missing) {
        const auto body = nlohmann::json::parse(missing->body);
        expect(std::string(body["error"]).find("file") != std::string::npos, "missing file message");
    }

    const auto plain = server.call([](httplib::Client& client) {
        return client.Post("/api/thermal/csv", "text/plain", "hello");
    });
    expect(static_cast<bool>(plain) && plain->status == 400, "non-multipart status");

    const auto empty = server.call([](httplib::Client& client) {
        return client.Post("/api/geometry/stl", {file_part("empty.stl", "")});
    });
    expect(static_cast<bool>(empty) && empty->status == 400, "empty upload status");
    if (empty) {
        const auto body = nlohmann::json::parse(empty->body);
        expect(std::string(body["error"]).find("empty") != std::string::npos, "empty upload message");
    }

    const auto sneaky = server.call([](httplib::Client& client) {
        return client.Post("/api/geometry/stl", {file_part("../../sneaky.stl", kTriangleStl)});
    });
    expect(static_cast<bool>(sneaky) && sneaky->status == 200, "path-like filename still parses");
    if (sneaky && sneaky->status == 200) {
        const auto body = nlohmann::json::parse(sneaky->body);
        expect(body["filename"] == "sneaky.stl", "filename keeps only the base name");
        expect(body["triangle_count"] == 1, "sanitized upload triangle count");
    }
    expect(!std::filesystem::exists(server.temp_directory().parent_path() / "sneaky.stl"),
           "client filename was not used as a path");
    expect(count_files(server.temp_directory()) == 0, "temp directory is empty after errors");
}

void test_oversized() {
    const auto root = std::filesystem::path(WORKBENCH_TEST_DIR) / "api_uploads_small";
    workbench::ApiServerConfig config;
    config.version = "test";
    config.temp_directory = root;
    config.max_upload_bytes = 64;
    config.payload_overhead_bytes = 1024 * 1024;
    TestServer server(config);

    const std::string bulky(200, 'A');
    const auto response = server.call([&](httplib::Client& client) {
        return client.Post("/api/geometry/stl", {file_part("big.stl", bulky)});
    });
    expect(static_cast<bool>(response) && response->status == 413, "oversized STL status");
    if (response) {
        const auto body = nlohmann::json::parse(response->body);
        const std::string message = body["error"];
        expect(message.find("exceeds the limit") != std::string::npos, "oversized STL message");
        expect(message.find("64") != std::string::npos, "oversized STL reports the limit");
    }
    expect(count_files(server.temp_directory()) == 0, "oversized STL is not kept");

    workbench::ApiServerConfig tight = config;
    tight.temp_directory = std::filesystem::path(WORKBENCH_TEST_DIR) / "api_uploads_tight";
    tight.max_upload_bytes = 32;
    tight.payload_overhead_bytes = 16;
    TestServer tight_server(tight);
    const std::string huge(400, 'B');
    const auto rejected = tight_server.call([&](httplib::Client& client) {
        return client.Post("/api/thermal/csv", {file_part("huge.csv", huge)});
    });
    expect(static_cast<bool>(rejected) && rejected->status == 413, "payload limit status");
    if (rejected) {
        const auto body = nlohmann::json::parse(rejected->body);
        expect(body.contains("error"), "payload limit JSON error");
    }
    expect(count_files(tight_server.temp_directory()) == 0, "rejected payload is not kept");
}

}  // namespace

int main() {
    try {
        const auto temp = std::filesystem::path(WORKBENCH_TEST_DIR) / "api_secret_temp";
        std::filesystem::remove_all(temp);
        workbench::ApiServerConfig config;
        config.version = "test";
        config.temp_directory = temp;
        TestServer server(config);
        test_health_and_missing_route(server);
        test_valid_stl(server);
        test_valid_temperature(server);
        test_invalid_and_missing(server);
        test_oversized();
        expect(count_files(temp) == 0, "shared temp directory is empty at the end");
    } catch (const std::exception& error) {
        std::cerr << "TEST FAILURE: uncaught exception: " << error.what() << '\n';
        return 1;
    }
    if (g_failures != 0) {
        std::cerr << g_failures << " HTTP API test failure(s).\n";
        return 1;
    }
    std::cout << "All HTTP API tests passed.\n";
    return 0;
}
