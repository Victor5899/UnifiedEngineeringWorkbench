#include "workbench/thermal/temperature_csv_parser.hpp"

#include <algorithm>
#include <cmath>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace workbench {
namespace {

constexpr unsigned char kUtf8Bom0 = 0xEF;
constexpr unsigned char kUtf8Bom1 = 0xBB;
constexpr unsigned char kUtf8Bom2 = 0xBF;

constexpr const char* kRequiredColumns[] = {
    "node_id", "x_m", "y_m", "z_m", "temperature_C"
};
constexpr std::size_t kColumnCount = 5;

std::string file_error(const std::string& path, const std::string& detail) {
    return "Temperature CSV parse error in '" + path + "': " + detail;
}

std::string row_error(const std::string& path, std::size_t row, const std::string& detail) {
    return "Temperature CSV parse error in '" + path + "' at row " +
           std::to_string(row) + ": " + detail;
}

std::string expected_header() {
    return "node_id,x_m,y_m,z_m,temperature_C";
}

void validate_limits(const TemperatureCsvLimits& limits) {
    if (limits.max_file_bytes == 0) {
        throw std::invalid_argument("Temperature CSV max_file_bytes must be positive.");
    }
    if (limits.max_nodes == 0) {
        throw std::invalid_argument("Temperature CSV max_nodes must be positive.");
    }
}

std::uintmax_t checked_file_size(const std::string& path, std::uintmax_t max_bytes) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::file_status status = fs::status(path, ec);
    if (ec || !fs::exists(status)) {
        throw TemperatureCsvParseError(file_error(path, "cannot open the file."));
    }
    if (!fs::is_regular_file(status)) {
        throw TemperatureCsvParseError(file_error(path, "path is not a regular file."));
    }
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec) {
        throw TemperatureCsvParseError(file_error(path, "cannot determine the file size."));
    }
    if (size > max_bytes) {
        throw TemperatureCsvParseError(file_error(
            path,
            "file size is " + std::to_string(static_cast<unsigned long long>(size)) +
                " bytes, which exceeds the limit of " +
                std::to_string(static_cast<unsigned long long>(max_bytes)) + " bytes."));
    }
    return size;
}

std::string trim_copy(std::string_view text) {
    while (!text.empty() &&
           (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return std::string(text);
}

void strip_bom(std::string& line) {
    if (line.size() >= 3 &&
        static_cast<unsigned char>(line[0]) == kUtf8Bom0 &&
        static_cast<unsigned char>(line[1]) == kUtf8Bom1 &&
        static_cast<unsigned char>(line[2]) == kUtf8Bom2) {
        line.erase(0, 3);
    }
}

std::string display_token(const std::string& token) {
    constexpr std::size_t kMax = 64;
    if (token.size() <= kMax) return token;
    return token.substr(0, kMax - 3) + "...";
}

std::vector<std::string> split_csv_fields(const std::string& line) {
    std::vector<std::string> fields;
    fields.reserve(kColumnCount);
    std::string current;
    for (char c : line) {
        if (c == ',') {
            fields.push_back(trim_copy(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    fields.push_back(trim_copy(current));
    return fields;
}

enum class RealStatus { Ok, NotANumber, NotFinite };

bool equals_ignore_case(const std::string& text, const char* literal) {
    const std::size_t size = std::char_traits<char>::length(literal);
    if (text.size() != size) return false;
    for (std::size_t i = 0; i < size; ++i) {
        char c = text[i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != literal[i]) return false;
    }
    return true;
}

bool is_non_finite_token(const std::string& token) {
    std::string body = token;
    if (!body.empty() && (body.front() == '+' || body.front() == '-')) body.erase(body.begin());
    return equals_ignore_case(body, "inf") ||
           equals_ignore_case(body, "infinity") ||
           equals_ignore_case(body, "nan");
}

RealStatus parse_real(const std::string& token, double& value) {
    if (token.empty() || token.find_first_of("xX") != std::string::npos) {
        return RealStatus::NotANumber;
    }
    if (is_non_finite_token(token)) return RealStatus::NotFinite;
    char* end = nullptr;
    const double parsed = std::strtod(token.c_str(), &end);
    if (end == token.c_str() || end == nullptr || *end != '\0') return RealStatus::NotANumber;
    if (!std::isfinite(parsed)) return RealStatus::NotFinite;
    value = parsed;
    return RealStatus::Ok;
}

enum class IdStatus { Ok, NotInteger, OutOfRange };

IdStatus parse_node_id(const std::string& token, std::int64_t& value) {
    if (token.empty()) return IdStatus::NotInteger;
    std::string_view body(token);
    if (body.front() == '+') body.remove_prefix(1);
    if (body.empty()) return IdStatus::NotInteger;

    const char* begin = body.data();
    const char* end = body.data() + body.size();
    std::int64_t parsed = 0;
    const std::from_chars_result result = std::from_chars(begin, end, parsed);
    if (result.ec == std::errc::result_out_of_range) return IdStatus::OutOfRange;
    if (result.ec != std::errc() || result.ptr != end) return IdStatus::NotInteger;
    value = parsed;
    return IdStatus::Ok;
}

struct ColumnMap {
    std::size_t node_id = 0;
    std::size_t x_m = 1;
    std::size_t y_m = 2;
    std::size_t z_m = 3;
    std::size_t temperature_C = 4;
};

ColumnMap parse_header(const std::string& path, const std::string& line) {
    const std::vector<std::string> fields = split_csv_fields(line);
    if (fields.size() != kColumnCount) {
        throw TemperatureCsvParseError(row_error(
            path, 1,
            "expected 5 columns (" + expected_header() + "), found " +
                std::to_string(fields.size()) + "."));
    }

    int assigned[kColumnCount] = {-1, -1, -1, -1, -1};
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const std::string& name = fields[index];
        if (name.empty()) {
            throw TemperatureCsvParseError(row_error(path, 1, "a header column is empty."));
        }
        bool known = false;
        for (std::size_t required = 0; required < kColumnCount; ++required) {
            if (name == kRequiredColumns[required]) {
                known = true;
                if (assigned[required] >= 0) {
                    throw TemperatureCsvParseError(row_error(
                        path, 1, "duplicate column '" + name + "'."));
                }
                assigned[required] = static_cast<int>(index);
                break;
            }
        }
        if (!known) {
            throw TemperatureCsvParseError(row_error(
                path, 1,
                "unrecognized column '" + display_token(name) +
                    "'. Expected header: " + expected_header() + "."));
        }
    }

    for (std::size_t required = 0; required < kColumnCount; ++required) {
        if (assigned[required] < 0) {
            throw TemperatureCsvParseError(row_error(
                path, 1,
                "missing required column '" + std::string(kRequiredColumns[required]) +
                    "'. Expected header: " + expected_header() + "."));
        }
    }

    ColumnMap columns;
    columns.node_id = static_cast<std::size_t>(assigned[0]);
    columns.x_m = static_cast<std::size_t>(assigned[1]);
    columns.y_m = static_cast<std::size_t>(assigned[2]);
    columns.z_m = static_cast<std::size_t>(assigned[3]);
    columns.temperature_C = static_cast<std::size_t>(assigned[4]);
    return columns;
}

double read_finite_field(const std::string& path,
                         std::size_t row,
                         const std::string& text,
                         const char* column) {
    if (text.empty()) {
        throw TemperatureCsvParseError(row_error(
            path, row, std::string(column) + " is missing."));
    }
    double value = 0.0;
    const RealStatus status = parse_real(text, value);
    if (status == RealStatus::NotFinite) {
        throw TemperatureCsvParseError(row_error(
            path, row,
            std::string(column) + " '" + display_token(text) +
                "' is not finite. NaN and infinity are rejected."));
    }
    if (status != RealStatus::Ok) {
        throw TemperatureCsvParseError(row_error(
            path, row,
            std::string(column) + " '" + display_token(text) + "' is not a number."));
    }
    return value;
}

void include_point(BoundingBox& bounds, bool& initialized, double x, double y, double z) {
    if (!initialized) {
        bounds.min = {x, y, z};
        bounds.max = {x, y, z};
        initialized = true;
        return;
    }
    bounds.min.x = std::min(bounds.min.x, x);
    bounds.min.y = std::min(bounds.min.y, y);
    bounds.min.z = std::min(bounds.min.z, z);
    bounds.max.x = std::max(bounds.max.x, x);
    bounds.max.y = std::max(bounds.max.y, y);
    bounds.max.z = std::max(bounds.max.z, z);
}

}  // namespace

TemperatureDataset parse_temperature_csv(const std::string& path,
                                         const TemperatureCsvLimits& limits) {
    validate_limits(limits);
    const std::uintmax_t file_size = checked_file_size(path, limits.max_file_bytes);
    if (file_size == 0) {
        throw TemperatureCsvParseError(file_error(path, "file is empty."));
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw TemperatureCsvParseError(file_error(path, "cannot open the file."));
    }

    TemperatureDataset dataset;
    const std::size_t reserve_count = std::min(
        limits.max_nodes,
        static_cast<std::size_t>(file_size / 8) + 1);
    dataset.nodes.reserve(reserve_count);

    std::string line;
    std::size_t row = 0;
    bool header_done = false;
    ColumnMap columns;
    std::unordered_map<std::int64_t, std::size_t> seen_ids;
    seen_ids.reserve(reserve_count);
    bool stats_ready = false;
    bool bounds_ready = false;
    double temperature_sum = 0.0;

    while (std::getline(input, line)) {
        ++row;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (row == 1) strip_bom(line);
        if (line.find('\0') != std::string::npos) {
            throw TemperatureCsvParseError(row_error(
                path, row, "row contains a NUL byte. Expected a text CSV."));
        }
        if (trim_copy(line).empty()) {
            throw TemperatureCsvParseError(row_error(path, row, "row is empty."));
        }

        if (!header_done) {
            columns = parse_header(path, line);
            header_done = true;
            continue;
        }

        if (dataset.nodes.size() >= limits.max_nodes) {
            throw TemperatureCsvParseError(row_error(
                path, row,
                "node count exceeds the limit of " + std::to_string(limits.max_nodes) +
                    ". Increase TemperatureCsvLimits::max_nodes for a trusted file."));
        }

        const std::vector<std::string> fields = split_csv_fields(line);
        if (fields.size() != kColumnCount) {
            throw TemperatureCsvParseError(row_error(
                path, row,
                "expected 5 values, found " + std::to_string(fields.size()) +
                    ". Each data row needs node_id, x_m, y_m, z_m and temperature_C."));
        }

        TemperatureNode node;
        const IdStatus id_status = parse_node_id(fields[columns.node_id], node.node_id);
        if (id_status == IdStatus::OutOfRange) {
            throw TemperatureCsvParseError(row_error(
                path, row,
                "node_id '" + display_token(fields[columns.node_id]) +
                    "' is outside the signed 64-bit range."));
        }
        if (id_status != IdStatus::Ok) {
            throw TemperatureCsvParseError(row_error(
                path, row,
                "node_id '" + display_token(fields[columns.node_id]) +
                    "' is not an integer."));
        }

        const auto existing = seen_ids.find(node.node_id);
        if (existing != seen_ids.end()) {
            throw TemperatureCsvParseError(row_error(
                path, row,
                "duplicate node_id " + std::to_string(node.node_id) +
                    " (first seen at row " + std::to_string(existing->second) + ")."));
        }
        seen_ids.emplace(node.node_id, row);

        node.x_m = read_finite_field(path, row, fields[columns.x_m], "x_m");
        node.y_m = read_finite_field(path, row, fields[columns.y_m], "y_m");
        node.z_m = read_finite_field(path, row, fields[columns.z_m], "z_m");
        node.temperature_C =
            read_finite_field(path, row, fields[columns.temperature_C], "temperature_C");

        if (!stats_ready) {
            dataset.minimum_temperature_C = node.temperature_C;
            dataset.maximum_temperature_C = node.temperature_C;
            stats_ready = true;
        } else {
            dataset.minimum_temperature_C =
                std::min(dataset.minimum_temperature_C, node.temperature_C);
            dataset.maximum_temperature_C =
                std::max(dataset.maximum_temperature_C, node.temperature_C);
        }
        temperature_sum += node.temperature_C;
        include_point(dataset.bounds, bounds_ready, node.x_m, node.y_m, node.z_m);
        dataset.nodes.push_back(node);
    }

    if (input.bad()) {
        throw TemperatureCsvParseError(file_error(path, "could not read the full file."));
    }
    if (!header_done) {
        throw TemperatureCsvParseError(file_error(path, "file is empty."));
    }
    if (dataset.nodes.empty()) {
        throw TemperatureCsvParseError(file_error(path, "dataset contains no data rows."));
    }

    dataset.average_temperature_C =
        temperature_sum / static_cast<double>(dataset.nodes.size());
    return dataset;
}

}
