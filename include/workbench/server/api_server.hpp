#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

// cpp-httplib is header-only. The server target supplies the include path.
#include <httplib.h>

namespace workbench {

// Local HTTP adapter. Parsing remains in parse_ascii_stl and parse_temperature_csv.
// Uploaded files are written under generated names and removed before the handler returns.
struct ApiServerConfig {
    std::uintmax_t max_upload_bytes = 64ull * 1024ull * 1024ull;
    // Extra room for multipart framing above the file-size limit.
    std::size_t payload_overhead_bytes = 1024ull * 1024ull;
    std::filesystem::path temp_directory;
    std::filesystem::path static_directory;
    std::string version = "0.2.0";
};

// Registers the API routes on an existing server. Binds nothing and does not listen.
// Throws std::invalid_argument when the upload limit is zero, and std::runtime_error
// when temporary storage or an optional static directory cannot be prepared.
void configure_api_server(httplib::Server& server, const ApiServerConfig& config);

}
