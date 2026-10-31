#include "workbench/server/api_server.hpp"

#include "performance_api.hpp"
#include "workbench/geometry/stl_parser.hpp"
#include "workbench/thermal/temperature_csv_parser.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#ifndef WORKBENCH_VERSION
#define WORKBENCH_VERSION "0.2.0"
#endif

namespace {

class ApiRequestError : public std::runtime_error {
public:
    ApiRequestError(int status, const std::string& message)
        : std::runtime_error(message), status_(status) {}

    int status() const { return status_; }

private:
    int status_;
};

void replace_all(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    std::size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos) {
        text.replace(position, from.size(), to);
        position += to.size();
    }
}

std::string sanitize_upload_name(const std::string& raw) {
    std::string name = raw;
    for (char& character : name) {
        if (character == '\\') character = '/';
    }
    const auto slash = name.find_last_of('/');
    if (slash != std::string::npos) name = name.substr(slash + 1);

    std::string clean;
    clean.reserve(name.size());
    for (unsigned char character : name) {
        if (character < 32 || character == 127) continue;
        clean.push_back(static_cast<char>(character));
    }
    if (clean.empty() || clean == "." || clean == "..") return "upload";
    if (clean.size() > 180) clean.resize(180);
    return clean;
}

std::string public_error(std::string message,
                         const std::filesystem::path& stored_path,
                         const std::string& display_name) {
    if (!stored_path.empty()) {
        replace_all(message, stored_path.string(), display_name);
        replace_all(message, stored_path.generic_string(), display_name);
    }
    if (message.size() > 2000) message.resize(2000);
    return message;
}

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

void send_json_text(httplib::Response& response, int status, std::string body) {
    response.status = status;
    response.set_content(std::move(body), "application/json");
    response.set_header("Cache-Control", "no-store");
    apply_cors(response);
}

void send_error(httplib::Response& response, int status, const std::string& message) {
    send_json(response, status, {{"error", message}});
}

nlohmann::json vec3_json(const workbench::Vec3& value) {
    return nlohmann::json::array({value.x, value.y, value.z});
}

nlohmann::json bounds_json(const workbench::BoundingBox& bounds) {
    return {
        {"min", vec3_json(bounds.min)},
        {"max", vec3_json(bounds.max)},
    };
}

nlohmann::json dimensions_json(const workbench::BoundingBox& bounds) {
    return {
        {"x", bounds.dimension_x()},
        {"y", bounds.dimension_y()},
        {"z", bounds.dimension_z()},
    };
}

// One binary record is 50 bytes. The JSON contract writes every coordinate as
// text, and a DOM of nested arrays would be several times larger again. The
// geometry response is one pre-sized string. Above this budget the upload is
// refused after a successful parse, before that string is allocated.
constexpr std::size_t kGeometryJsonBudgetBytes = 384ull * 1024ull * 1024ull;
constexpr std::size_t kGeometryJsonBytesPerTriangle = 448;

void append_json_number(std::string& out, double value) {
    char buffer[64];
    const auto result = std::to_chars(
        buffer,
        buffer + sizeof(buffer),
        value,
        std::chars_format::general,
        std::numeric_limits<double>::max_digits10);
    if (result.ec != std::errc{}) {
        throw ApiRequestError(500, "The server could not encode a geometry coordinate.");
    }
    out.append(buffer, static_cast<std::size_t>(result.ptr - buffer));
}

void append_json_vec3(std::string& out, const workbench::Vec3& value) {
    out.push_back('[');
    append_json_number(out, value.x);
    out.push_back(',');
    append_json_number(out, value.y);
    out.push_back(',');
    append_json_number(out, value.z);
    out.push_back(']');
}

std::string geometry_json_text(const workbench::StlMesh& mesh, const std::string& filename) {
    const std::size_t count = mesh.triangle_count();
    const std::size_t max_triangles = kGeometryJsonBudgetBytes / kGeometryJsonBytesPerTriangle;
    if (count > max_triangles) {
        const auto estimated_mib =
            (static_cast<unsigned long long>(count) * kGeometryJsonBytesPerTriangle) / (1024ull * 1024ull);
        throw ApiRequestError(
            413,
            "This STL contains " + std::to_string(count) +
                " triangles. The JSON viewer response would be about " +
                std::to_string(estimated_mib) + " MiB, above the " +
                std::to_string(kGeometryJsonBudgetBytes / (1024ull * 1024ull)) +
                " MiB response budget. The file is within the upload limit, but expanding every triangle into JSON uses much more memory than the STL. Decimate the mesh to " +
                std::to_string(max_triangles) + " triangles or fewer and try again.");
    }

    std::string out;
    const std::size_t per_triangle = 160;
    if (count > (std::numeric_limits<std::size_t>::max() - 1024) / per_triangle) {
        throw ApiRequestError(413, "This STL is too large to encode as a JSON response.");
    }
    out.reserve(1024 + count * per_triangle);
    out += "{\"filename\":";
    out += nlohmann::json(filename).dump();
    out += ",\"solid_name\":";
    out += nlohmann::json(mesh.solid_name).dump();
    out += ",\"triangle_count\":";
    out += std::to_string(count);
    out += ",\"vertex_count\":";
    out += std::to_string(mesh.vertex_count());
    out += ",\"triangles\":[";
    for (std::size_t index = 0; index < count; ++index) {
        if (index != 0) out.push_back(',');
        const workbench::Triangle& triangle = mesh.triangles[index];
        out += "{\"normal\":";
        append_json_vec3(out, triangle.normal);
        out += ",\"vertices\":[";
        append_json_vec3(out, triangle.vertices[0]);
        out.push_back(',');
        append_json_vec3(out, triangle.vertices[1]);
        out.push_back(',');
        append_json_vec3(out, triangle.vertices[2]);
        out += "]}";
    }
    out += "],\"bounding_box\":{\"min\":";
    append_json_vec3(out, mesh.bounds.min);
    out += ",\"max\":";
    append_json_vec3(out, mesh.bounds.max);
    out += "},\"dimensions\":{\"x\":";
    append_json_number(out, mesh.bounds.dimension_x());
    out += ",\"y\":";
    append_json_number(out, mesh.bounds.dimension_y());
    out += ",\"z\":";
    append_json_number(out, mesh.bounds.dimension_z());
    out += "},\"coordinate_units\":\"file\"}";
    return out;
}

nlohmann::json temperature_json(const workbench::TemperatureDataset& dataset,
                                const std::string& filename) {
    nlohmann::json nodes = nlohmann::json::array();
    auto& node_array = nodes.get_ref<nlohmann::json::array_t&>();
    node_array.reserve(dataset.nodes.size());
    for (const workbench::TemperatureNode& node : dataset.nodes) {
        nlohmann::json record = {
            {"node_id", node.node_id},
            {"x_m", node.x_m},
            {"y_m", node.y_m},
            {"z_m", node.z_m},
            {"temperature_C", node.temperature_C},
        };
        node_array.push_back(std::move(record));
    }
    return {
        {"filename", filename},
        {"node_count", dataset.node_count()},
        {"minimum_temperature_C", dataset.minimum_temperature_C},
        {"maximum_temperature_C", dataset.maximum_temperature_C},
        {"average_temperature_C", dataset.average_temperature_C},
        {"bounding_box", bounds_json(dataset.bounds)},
        {"dimensions", dimensions_json(dataset.bounds)},
        {"coordinate_units", "m"},
        {"temperature_units", "C"},
        {"nodes", std::move(nodes)},
    };
}

std::string make_token() {
    static std::atomic<std::uint64_t> counter{0};
    std::random_device device;
    std::mt19937_64 engine(device());
    std::uniform_int_distribution<std::uint64_t> distribution;
    std::ostringstream stream;
    stream << std::hex << distribution(engine) << '-' << counter.fetch_add(1);
    return stream.str();
}

class ScopedTempFile {
public:
    ScopedTempFile(const ScopedTempFile&) = delete;
    ScopedTempFile& operator=(const ScopedTempFile&) = delete;
    ScopedTempFile(ScopedTempFile&& other) noexcept : path_(std::move(other.path_)) {
        other.path_.clear();
    }
    ScopedTempFile& operator=(ScopedTempFile&&) = delete;

    static ScopedTempFile create(const std::filesystem::path& directory, const std::string& extension) {
        const std::filesystem::path path = directory / (make_token() + extension);
        std::ofstream output(path, std::ios::binary);
        if (!output) {
            std::error_code error;
            std::filesystem::remove(path, error);
            throw ApiRequestError(500, "Could not store the upload.");
        }
        return ScopedTempFile(path);
    }

    ~ScopedTempFile() {
        if (path_.empty()) return;
        std::error_code error;
        std::filesystem::remove(path_, error);
        if (error) std::cerr << "Could not remove a temporary upload.\n";
    }

    const std::filesystem::path& path() const { return path_; }

    void write(const std::string& bytes) {
        std::ofstream output(path_, std::ios::binary | std::ios::trunc);
        if (!output) throw ApiRequestError(500, "Could not store the upload.");
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) throw ApiRequestError(500, "Could not store the upload.");
    }

private:
    explicit ScopedTempFile(std::filesystem::path path) : path_(std::move(path)) {}

    std::filesystem::path path_;
};

void log_result(const char* route, int status) {
    std::cout << "POST " << route << ' ' << status << '\n';
}

template <typename Parse, typename Publish>
void handle_upload(const httplib::Request& request,
                   httplib::Response& response,
                   const workbench::ApiServerConfig& config,
                   const char* route,
                   const char* extension,
                   Parse parse,
                   Publish publish) {
    const std::string content_type = request.get_header_value("Content-Type");
    const bool multipart = content_type.find("multipart/form-data") != std::string::npos;
    if (!multipart || !request.has_file("file")) {
        send_error(response,
                   400,
                   "No file was uploaded. Send multipart/form-data with a file field named \"file\".");
        log_result(route, 400);
        return;
    }

    const httplib::MultipartFormData& file = request.get_file_value("file");
    const std::string display_name = sanitize_upload_name(file.filename);
    if (file.content.empty()) {
        send_error(response, 400, "The uploaded file is empty.");
        log_result(route, 400);
        return;
    }
    if (file.content.size() > config.max_upload_bytes) {
        send_error(response,
                   413,
                   "The uploaded file is " + std::to_string(file.content.size()) +
                       " bytes, which exceeds the limit of " +
                       std::to_string(static_cast<unsigned long long>(config.max_upload_bytes)) +
                       " bytes.");
        log_result(route, 413);
        return;
    }

    std::filesystem::path stored_path;
    try {
        // The client filename is only a display label. The stored name is generated.
        ScopedTempFile temp = ScopedTempFile::create(config.temp_directory, extension);
        temp.write(file.content);
        stored_path = temp.path();
        publish(response, parse(stored_path.string()), display_name);
        log_result(route, response.status);
    } catch (const workbench::StlParseError& error) {
        send_error(response, 400, public_error(error.what(), stored_path, display_name));
        log_result(route, 400);
    } catch (const workbench::TemperatureCsvParseError& error) {
        send_error(response, 400, public_error(error.what(), stored_path, display_name));
        log_result(route, 400);
    } catch (const ApiRequestError& error) {
        send_error(response, error.status(), error.what());
        log_result(route, error.status());
    } catch (const std::invalid_argument& error) {
        send_error(response, 400, error.what());
        log_result(route, 400);
    } catch (const std::exception& error) {
        std::cerr << "API exception on " << route << ": " << error.what() << '\n';
        send_error(response, 500, "The server could not process the upload.");
        log_result(route, 500);
    }
}

std::size_t payload_limit(const workbench::ApiServerConfig& config) {
    const auto file_limit = static_cast<std::size_t>(std::min<std::uintmax_t>(
        config.max_upload_bytes, std::numeric_limits<std::size_t>::max()));
    if (config.payload_overhead_bytes > std::numeric_limits<std::size_t>::max() - file_limit) {
        return std::numeric_limits<std::size_t>::max();
    }
    return file_limit + config.payload_overhead_bytes;
}

}  // namespace

namespace workbench {

void configure_api_server(httplib::Server& server, const ApiServerConfig& config) {
    if (config.max_upload_bytes == 0) {
        throw std::invalid_argument("Maximum upload size must be positive.");
    }

    ApiServerConfig active = config;
    if (active.version.empty()) active.version = WORKBENCH_VERSION;
    if (active.temp_directory.empty()) {
        active.temp_directory =
            std::filesystem::temp_directory_path() / "unified-engineering-workbench";
    }

    std::error_code directory_error;
    std::filesystem::create_directories(active.temp_directory, directory_error);
    if (directory_error || !std::filesystem::is_directory(active.temp_directory)) {
        std::cerr << "Temporary directory error: "
                  << (directory_error ? directory_error.message() : "not a directory") << '\n';
        throw std::runtime_error("Could not prepare temporary storage for uploads.");
    }

    server.set_payload_max_length(payload_limit(active));
    server.set_read_timeout(60, 0);
    server.set_write_timeout(60, 0);

    server.set_error_handler([](const httplib::Request& request, httplib::Response& response) {
        if (!response.body.empty()) return;
        int status = response.status;
        if (status < 400) status = 404;
        const char* message = "The request could not be completed.";
        if (status == 404) message = "That API route does not exist.";
        else if (status == 413) message = "The upload exceeds the configured size limit.";
        else if (status == 400) message = "The request is invalid.";
        else if (status == 405) message = "That method is not supported.";
        send_error(response, status, message);
        std::cout << request.method << ' ' << request.path << ' ' << status << '\n';
    });

    server.set_exception_handler([](const httplib::Request&, httplib::Response& response,
                                    std::exception_ptr pointer) {
        try {
            if (pointer) std::rethrow_exception(pointer);
        } catch (const std::exception& error) {
            std::cerr << "API exception: " << error.what() << '\n';
        } catch (...) {
            std::cerr << "API exception: unknown\n";
        }
        send_error(response, 500, "The server could not process the request.");
    });

    // Routes are regular expressions. Dots in the path are escaped so they stay literal.
    server.Options(R"(/api/.*)", [](const httplib::Request&, httplib::Response& response) {
        response.status = 204;
        apply_cors(response);
    });

    const std::string version = active.version;
    server.Get("/api/health", [version](const httplib::Request&, httplib::Response& response) {
        send_json(response,
                  200,
                  {{"status", "ok"},
                   {"service", "unified-engineering-workbench"},
                   {"version", version}});
    });

    server.Post(R"(/api/geometry/stl)", [active](const httplib::Request& request, httplib::Response& response) {
        handle_upload(
            request,
            response,
            active,
            "/api/geometry/stl",
            ".stl",
            [active](const std::string& path) {
                StlImportLimits limits;
                limits.max_file_bytes = active.max_upload_bytes;
                return parse_stl(path, limits);
            },
            [](httplib::Response& response, const StlMesh& mesh, const std::string& filename) {
                send_json_text(response, 200, geometry_json_text(mesh, filename));
            });
    });

    register_performance_routes(server);

    server.Post(R"(/api/thermal/csv)", [active](const httplib::Request& request, httplib::Response& response) {
        handle_upload(
            request,
            response,
            active,
            "/api/thermal/csv",
            ".csv",
            [active](const std::string& path) {
                TemperatureCsvLimits limits;
                limits.max_file_bytes = active.max_upload_bytes;
                return parse_temperature_csv(path, limits);
            },
            [](httplib::Response& response, const TemperatureDataset& dataset, const std::string& filename) {
                send_json(response, 200, temperature_json(dataset, filename));
            });
    });

    if (!active.static_directory.empty()) {
        if (!std::filesystem::is_directory(active.static_directory)) {
            throw std::runtime_error("The static frontend directory does not exist.");
        }
        if (!server.set_mount_point("/", active.static_directory.string())) {
            throw std::runtime_error("Could not serve the static frontend directory.");
        }
    }
}

}  // namespace workbench
