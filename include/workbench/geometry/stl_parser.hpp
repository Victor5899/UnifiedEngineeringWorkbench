#pragma once

#include "workbench/bounding_box.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace workbench {

// ASCII STL is not an indexed mesh. vertex_count() is the number of vertex
// records stored in the file, which is three per triangle.
inline constexpr std::uintmax_t kDefaultMaxStlFileBytes = 64ull * 1024ull * 1024ull;
inline constexpr std::size_t kDefaultMaxStlTriangles = 2000000;

struct StlImportLimits {
    std::uintmax_t max_file_bytes = kDefaultMaxStlFileBytes;
    std::size_t max_triangles = kDefaultMaxStlTriangles;
};

struct Triangle {
    Vec3 normal{};
    std::array<Vec3, 3> vertices{};
};

struct StlMesh {
    std::string solid_name;
    std::vector<Triangle> triangles;
    BoundingBox bounds{};

    std::size_t triangle_count() const { return triangles.size(); }
    std::size_t vertex_count() const { return triangles.size() * 3; }
};

class StlParseError : public std::runtime_error {
public:
    explicit StlParseError(const std::string& message)
        : std::runtime_error(message) {}
};

// Reads one ASCII STL solid. Binary STL is rejected.
// Invalid limits throw std::invalid_argument. File and syntax failures throw StlParseError.
StlMesh parse_ascii_stl(const std::string& path, const StlImportLimits& limits = {});

// Reads one ASCII or binary STL solid. A file whose size is exactly
// 84 + triangle_count * 50 bytes is binary, including when its 80-byte header
// begins with "solid". Text that starts with "solid" and contains facet
// structure stays on the ASCII parser. Limits and errors match parse_ascii_stl.
StlMesh parse_stl(const std::string& path, const StlImportLimits& limits = {});

}
