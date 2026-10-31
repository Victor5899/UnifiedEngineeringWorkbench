#include "workbench/server/api_server.hpp"

#include <cstdlib>

#ifndef WORKBENCH_VERSION
#define WORKBENCH_VERSION "0.2.0"
#endif
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

const char* kUsage =
    "Unified Engineering Workbench API\n"
    "\n"
    "Usage: workbench_server [options]\n"
    "  --host <address>            Bind address (default 127.0.0.1)\n"
    "  --port <port>               TCP port (default 8080)\n"
    "  --static <directory>        Also serve a built frontend directory\n"
    "  --temp <directory>          Scratch directory for uploads\n"
    "  --max-upload-bytes <bytes>  Maximum uploaded file size (default 67108864)\n"
    "  --help                      Show this help\n"
    "\n"
    "Environment variables are used when the matching option is omitted:\n"
    "  WORKBENCH_HOST, WORKBENCH_PORT, WORKBENCH_STATIC, WORKBENCH_TEMP,\n"
    "  WORKBENCH_MAX_UPLOAD_BYTES\n"
    "\n"
    "The process binds to 127.0.0.1 unless --host or WORKBENCH_HOST is set.\n";

std::string env_or(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') return fallback;
    return value;
}

int parse_port(const std::string& text) {
    if (text.empty() || text.size() > 5) {
        throw std::invalid_argument("Port must be an integer from 1 to 65535.");
    }
    int port = 0;
    for (char character : text) {
        if (character < '0' || character > '9') {
            throw std::invalid_argument("Port must be an integer from 1 to 65535.");
        }
        port = port * 10 + (character - '0');
    }
    if (port < 1 || port > 65535) {
        throw std::invalid_argument("Port must be an integer from 1 to 65535.");
    }
    return port;
}

std::uintmax_t parse_size(const std::string& text) {
    if (text.empty() || text.size() > 18) {
        throw std::invalid_argument("Upload limit must be a positive integer number of bytes.");
    }
    std::uintmax_t size = 0;
    for (char character : text) {
        if (character < '0' || character > '9') {
            throw std::invalid_argument("Upload limit must be a positive integer number of bytes.");
        }
        const std::uintmax_t digit = static_cast<std::uintmax_t>(character - '0');
        if (size > (static_cast<std::uintmax_t>(-1) - digit) / 10) {
            throw std::invalid_argument("Upload limit is too large.");
        }
        size = size * 10 + digit;
    }
    if (size == 0) {
        throw std::invalid_argument("Upload limit must be a positive integer number of bytes.");
    }
    return size;
}

bool take_value(int& index, int argc, char** argv, const std::string& flag, std::string& value) {
    const std::string argument = argv[index];
    const std::string prefix = flag + "=";
    if (argument == flag) {
        if (index + 1 >= argc) throw std::invalid_argument(flag + " needs a value.");
        value = argv[++index];
        if (value.empty()) throw std::invalid_argument(flag + " needs a value.");
        return true;
    }
    if (argument.compare(0, prefix.size(), prefix) == 0) {
        value = argument.substr(prefix.size());
        if (value.empty()) throw std::invalid_argument(flag + " needs a value.");
        return true;
    }
    return false;
}

struct LaunchOptions {
    std::string host = "127.0.0.1";
    int port = 8080;
    std::filesystem::path static_directory;
    std::filesystem::path temp_directory;
    std::uintmax_t max_upload_bytes = 64ull * 1024ull * 1024ull;
    bool help = false;
};

LaunchOptions parse_args(int argc, char** argv) {
    LaunchOptions options;
    options.host = env_or("WORKBENCH_HOST", options.host);
    options.port = parse_port(env_or("WORKBENCH_PORT", std::to_string(options.port)));
    options.static_directory = env_or("WORKBENCH_STATIC", "");
    options.temp_directory = env_or("WORKBENCH_TEMP", "");
    const std::string upload_env = env_or("WORKBENCH_MAX_UPLOAD_BYTES", "");
    if (!upload_env.empty()) options.max_upload_bytes = parse_size(upload_env);

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        std::string value;
        if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else if (take_value(index, argc, argv, "--host", value)) {
            options.host = value;
        } else if (take_value(index, argc, argv, "--port", value)) {
            options.port = parse_port(value);
        } else if (take_value(index, argc, argv, "--static", value)) {
            options.static_directory = value;
        } else if (take_value(index, argc, argv, "--temp", value)) {
            options.temp_directory = value;
        } else if (take_value(index, argc, argv, "--max-upload-bytes", value)) {
            options.max_upload_bytes = parse_size(value);
        } else {
            throw std::invalid_argument("Unknown argument: " + argument);
        }
    }

    if (options.host.empty()) {
        throw std::invalid_argument("Host must not be empty. The default is 127.0.0.1.");
    }
    return options;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const LaunchOptions options = parse_args(argc, argv);
        if (options.help) {
            std::cout << kUsage;
            return 0;
        }

        workbench::ApiServerConfig config;
        config.max_upload_bytes = options.max_upload_bytes;
        config.temp_directory = options.temp_directory;
        config.static_directory = options.static_directory;
        config.version = WORKBENCH_VERSION;

        httplib::Server server;
        workbench::configure_api_server(server, config);

        std::cout << "Unified Engineering Workbench API\n"
                  << "Listening on http://" << options.host << ':' << options.port << '\n'
                  << "Health: http://" << options.host << ':' << options.port << "/api/health\n";
        if (!options.static_directory.empty()) {
            std::cout << "Static frontend: " << options.static_directory.string() << '\n';
        }
        std::cout << std::flush;
        if (!server.listen(options.host, options.port)) {
            std::cerr << "Could not bind " << options.host << ':' << options.port << ".\n";
            return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n' << kUsage;
        return 1;
    }
}
