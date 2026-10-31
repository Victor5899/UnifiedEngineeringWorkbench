#include "workbench/geometry/stl_parser.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace workbench {
namespace {

constexpr unsigned char kUtf8Bom0 = 0xEF;
constexpr unsigned char kUtf8Bom1 = 0xBB;
constexpr unsigned char kUtf8Bom2 = 0xBF;

bool is_horizontal_space(char c) {
    return c == ' ' || c == '\t' || c == '\v' || c == '\f';
}

bool is_whitespace(char c) {
    return is_horizontal_space(c) || c == '\n' || c == '\r';
}

std::string trim_copy(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() && is_horizontal_space(text[begin])) ++begin;
    std::size_t end = text.size();
    while (end > begin && is_horizontal_space(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

std::string display_token(const std::string& token) {
    constexpr std::size_t kMax = 64;
    if (token.size() <= kMax) return token;
    return token.substr(0, kMax - 3) + "...";
}

std::size_t skip_bom(const std::string& bytes) {
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == kUtf8Bom0 &&
        static_cast<unsigned char>(bytes[1]) == kUtf8Bom1 &&
        static_cast<unsigned char>(bytes[2]) == kUtf8Bom2) {
        return 3;
    }
    return 0;
}

std::size_t find_whole_word(const std::string& line, const std::string& word) {
    std::size_t pos = 0;
    while ((pos = line.find(word, pos)) != std::string::npos) {
        const bool left_ok = pos == 0 || is_horizontal_space(line[pos - 1]);
        const std::size_t after = pos + word.size();
        const bool right_ok = after == line.size() || is_horizontal_space(line[after]);
        if (left_ok && right_ok) return pos;
        pos += word.size();
    }
    return std::string::npos;
}

bool starts_with_keyword(const std::string& bytes, std::size_t index, const char* keyword) {
    const std::string word(keyword);
    if (index + word.size() > bytes.size()) return false;
    if (bytes.compare(index, word.size(), word) != 0) return false;
    const std::size_t after = index + word.size();
    return after == bytes.size() || is_whitespace(bytes[after]);
}

bool contains_non_text(const std::string& bytes) {
    for (std::size_t i = skip_bom(bytes); i < bytes.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(bytes[i]);
        if (c == '\t' || c == '\n' || c == '\r' || (c >= 32 && c <= 126)) continue;
        return true;
    }
    return false;
}

bool matches_binary_stl_size(const std::string& bytes) {
    if (bytes.size() < 84) return false;
    const auto* raw = reinterpret_cast<const unsigned char*>(bytes.data());
    const std::uint32_t count =
        static_cast<std::uint32_t>(raw[80]) |
        (static_cast<std::uint32_t>(raw[81]) << 8) |
        (static_cast<std::uint32_t>(raw[82]) << 16) |
        (static_cast<std::uint32_t>(raw[83]) << 24);
    constexpr std::uint64_t kHeaderBytes = 84;
    constexpr std::uint64_t kRecordBytes = 50;
    if (static_cast<std::uint64_t>(count) >
        (std::numeric_limits<std::uint64_t>::max() - kHeaderBytes) / kRecordBytes) {
        return false;
    }
    const std::uint64_t expected = kHeaderBytes + kRecordBytes * static_cast<std::uint64_t>(count);
    return expected == bytes.size();
}

enum class StlEncoding { Empty, Ascii, Binary, NotAscii };

// Binary STL is an 80-byte header, a little-endian triangle count, and 50 bytes
// per triangle. A binary header may legally begin with the letters "solid", so
// size, NUL/non-text bytes, and the ASCII keywords are used together.
StlEncoding classify_stl(const std::string& bytes) {
    std::size_t index = skip_bom(bytes);
    while (index < bytes.size() && is_whitespace(bytes[index])) ++index;
    if (index >= bytes.size()) return StlEncoding::Empty;

    const bool solid = starts_with_keyword(bytes, index, "solid");
    const bool binary_size = matches_binary_stl_size(bytes);
    const bool non_text = contains_non_text(bytes);
    const bool has_structure =
        bytes.find("facet") != std::string::npos || bytes.find("endsolid") != std::string::npos;

    if (binary_size && (!solid || non_text || !has_structure)) return StlEncoding::Binary;
    if (!solid) return non_text ? StlEncoding::Binary : StlEncoding::NotAscii;
    if (non_text) return StlEncoding::Binary;
    return StlEncoding::Ascii;
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

std::string line_error(const std::string& path, std::size_t line, const std::string& detail) {
    return "STL parse error in '" + path + "' at line " + std::to_string(line) + ": " + detail;
}

std::string file_error(const std::string& path, const std::string& detail) {
    return "STL parse error in '" + path + "': " + detail;
}

void validate_limits(const StlImportLimits& limits) {
    if (limits.max_file_bytes == 0) {
        throw std::invalid_argument("STL max_file_bytes must be positive.");
    }
    if (limits.max_triangles == 0) {
        throw std::invalid_argument("STL max_triangles must be positive.");
    }
}

std::string read_file_bytes(const std::string& path, std::uintmax_t max_bytes) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::file_status status = fs::status(path, ec);
    if (ec || !fs::exists(status)) {
        throw StlParseError(file_error(path, "cannot open the file."));
    }
    if (!fs::is_regular_file(status)) {
        throw StlParseError(file_error(path, "path is not a regular file."));
    }
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec) {
        throw StlParseError(file_error(path, "cannot determine the file size."));
    }
    if (size > max_bytes) {
        throw StlParseError(file_error(
            path,
            "file size is " + std::to_string(static_cast<unsigned long long>(size)) +
                " bytes, which exceeds the limit of " +
                std::to_string(static_cast<unsigned long long>(max_bytes)) + " bytes."));
    }
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()) ||
        size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        throw StlParseError(file_error(path, "file is too large to address."));
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) throw StlParseError(file_error(path, "cannot open the file."));

    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (size == 0) return bytes;
    input.read(bytes.data(), static_cast<std::streamsize>(size));
    const auto read = input.gcount();
    if (read < 0 || static_cast<std::uintmax_t>(read) != size) {
        throw StlParseError(file_error(path, "could not read the full file."));
    }
    return bytes;
}

void include_vertex(BoundingBox& bounds, bool& initialized, const Vec3& point) {
    if (!initialized) {
        bounds.min = point;
        bounds.max = point;
        initialized = true;
        return;
    }
    bounds.min.x = std::min(bounds.min.x, point.x);
    bounds.min.y = std::min(bounds.min.y, point.y);
    bounds.min.z = std::min(bounds.min.z, point.z);
    bounds.max.x = std::max(bounds.max.x, point.x);
    bounds.max.y = std::max(bounds.max.y, point.y);
    bounds.max.z = std::max(bounds.max.z, point.z);
}

class Scanner {
public:
    Scanner(const std::string& text, const std::string& path)
        : text_(text), path_(path) {
        pos_ = skip_bom(text_);
    }

    bool next() {
        skip_whitespace();
        if (pos_ >= text_.size()) {
            token_.clear();
            return false;
        }
        token_line_ = line_;
        const std::size_t start = pos_;
        while (pos_ < text_.size() && !is_whitespace(text_[pos_])) ++pos_;
        token_.assign(text_, start, pos_ - start);
        return true;
    }

    const std::string& token() const { return token_; }
    std::size_t line() const { return token_line_; }
    std::size_t last_line() const { return token_line_; }
    const std::string& path() const { return path_; }

    std::string consume_solid_name() {
        const std::size_t line_begin = pos_;
        std::size_t line_end = pos_;
        while (line_end < text_.size() && text_[line_end] != '\n' && text_[line_end] != '\r') {
            ++line_end;
        }
        const std::string rest = text_.substr(line_begin, line_end - line_begin);
        const std::size_t facet_at = find_whole_word(rest, "facet");
        const std::size_t endsolid_at = find_whole_word(rest, "endsolid");
        std::size_t cut = std::string::npos;
        if (facet_at != std::string::npos) cut = facet_at;
        if (endsolid_at != std::string::npos && (cut == std::string::npos || endsolid_at < cut)) {
            cut = endsolid_at;
        }
        if (cut == std::string::npos) {
            pos_ = line_end;
            return trim_copy(rest);
        }
        pos_ = line_begin + cut;
        return trim_copy(rest.substr(0, cut));
    }

    void skip_rest_of_line() {
        while (pos_ < text_.size() && text_[pos_] != '\n' && text_[pos_] != '\r') ++pos_;
    }

private:
    const std::string& text_;
    const std::string& path_;
    std::size_t pos_ = 0;
    std::size_t line_ = 1;
    std::size_t token_line_ = 1;
    std::string token_;

    void skip_whitespace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == '\n') {
                ++line_;
                ++pos_;
            } else if (c == '\r') {
                ++line_;
                ++pos_;
                if (pos_ < text_.size() && text_[pos_] == '\n') ++pos_;
            } else if (is_horizontal_space(c)) {
                ++pos_;
            } else {
                break;
            }
        }
    }
};

void expect_keyword(Scanner& scanner, const char* keyword) {
    if (!scanner.next()) {
        throw StlParseError(line_error(
            scanner.path(), scanner.last_line(),
            std::string("unexpected end of file; expected '") + keyword + "'."));
    }
    if (scanner.token() != keyword) {
        throw StlParseError(line_error(
            scanner.path(), scanner.line(),
            std::string("expected '") + keyword + "', found '" + display_token(scanner.token()) + "'."));
    }
}

double read_number(Scanner& scanner, const char* role) {
    if (!scanner.next()) {
        throw StlParseError(line_error(
            scanner.path(), scanner.last_line(),
            std::string("unexpected end of file; expected a ") + role + "."));
    }
    double value = 0.0;
    const RealStatus status = parse_real(scanner.token(), value);
    if (status == RealStatus::NotFinite) {
        throw StlParseError(line_error(
            scanner.path(), scanner.line(),
            std::string(role) + " '" + display_token(scanner.token()) +
                "' is not finite. NaN and infinity are rejected."));
    }
    if (status != RealStatus::Ok) {
        throw StlParseError(line_error(
            scanner.path(), scanner.line(),
            std::string(role) + " '" + display_token(scanner.token()) +
                "' is not a floating-point number."));
    }
    return value;
}

Vec3 read_vec3(Scanner& scanner, const char* role) {
    Vec3 point;
    point.x = read_number(scanner, role);
    point.y = read_number(scanner, role);
    point.z = read_number(scanner, role);
    return point;
}

StlMesh parse_ascii_bytes(const std::string& bytes,
                          const std::string& path,
                          const StlImportLimits& limits) {
    const StlEncoding encoding = classify_stl(bytes);
    if (encoding == StlEncoding::Empty) {
        throw StlParseError(file_error(path, "file is empty."));
    }
    if (encoding == StlEncoding::Binary) {
        throw StlParseError(file_error(
            path,
            "unsupported binary STL input. This importer reads ASCII STL only; "
            "re-export the model as ASCII STL."));
    }
    if (encoding == StlEncoding::NotAscii) {
        throw StlParseError(file_error(
            path,
            "unsupported STL input. Expected an ASCII STL file starting with 'solid'."));
    }

    Scanner scanner(bytes, path);
    if (!scanner.next() || scanner.token() != "solid") {
        throw StlParseError(file_error(path, "expected the file to start with 'solid'."));
    }

    StlMesh mesh;
    mesh.solid_name = scanner.consume_solid_name();
    const std::size_t reserve_count = std::min(
        limits.max_triangles,
        bytes.size() / 64 + 1);
    mesh.triangles.reserve(reserve_count);

    bool bounds_ready = false;
    while (true) {
        if (!scanner.next()) {
            throw StlParseError(line_error(
                path, scanner.last_line(),
                "unexpected end of file; expected 'facet' or 'endsolid'."));
        }
        if (scanner.token() == "endsolid") {
            const std::size_t end_line = scanner.line();
            scanner.skip_rest_of_line();
            if (scanner.next()) {
                throw StlParseError(line_error(
                    path, scanner.line(),
                    "unexpected content '" + display_token(scanner.token()) +
                        "' after endsolid. Only one solid is supported."));
            }
            if (mesh.triangles.empty()) {
                throw StlParseError(line_error(path, end_line, "solid contains no facets."));
            }
            break;
        }
        if (scanner.token() != "facet") {
            throw StlParseError(line_error(
                path, scanner.line(),
                "expected 'facet' or 'endsolid', found '" + display_token(scanner.token()) + "'."));
        }
        if (mesh.triangles.size() >= limits.max_triangles) {
            throw StlParseError(line_error(
                path, scanner.line(),
                "triangle count exceeds the limit of " + std::to_string(limits.max_triangles) +
                    ". Increase StlImportLimits::max_triangles for a trusted file."));
        }

        expect_keyword(scanner, "normal");
        Triangle triangle;
        triangle.normal = read_vec3(scanner, "facet normal");
        expect_keyword(scanner, "outer");
        expect_keyword(scanner, "loop");
        for (std::size_t vertex = 0; vertex < triangle.vertices.size(); ++vertex) {
            expect_keyword(scanner, "vertex");
            triangle.vertices[vertex] = read_vec3(scanner, "vertex coordinate");
            include_vertex(mesh.bounds, bounds_ready, triangle.vertices[vertex]);
        }
        expect_keyword(scanner, "endloop");
        expect_keyword(scanner, "endfacet");
        mesh.triangles.push_back(triangle);
    }
    return mesh;
}

}  // namespace

StlMesh parse_ascii_stl(const std::string& path, const StlImportLimits& limits) {
    validate_limits(limits);
    const std::string bytes = read_file_bytes(path, limits.max_file_bytes);
    return parse_ascii_bytes(bytes, path, limits);
}

}
