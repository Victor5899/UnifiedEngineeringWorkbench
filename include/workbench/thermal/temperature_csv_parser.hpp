#pragma once

#include "workbench/bounding_box.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace workbench {

inline constexpr std::uintmax_t kDefaultMaxTemperatureCsvBytes = 64ull * 1024ull * 1024ull;
inline constexpr std::size_t kDefaultMaxTemperatureNodes = 5000000;

struct TemperatureCsvLimits {
    std::uintmax_t max_file_bytes = kDefaultMaxTemperatureCsvBytes;
    std::size_t max_nodes = kDefaultMaxTemperatureNodes;
};

struct TemperatureNode {
    std::int64_t node_id = 0;
    double x_m = 0.0;
    double y_m = 0.0;
    double z_m = 0.0;
    double temperature_C = 0.0;
};

// Nodes stay in file order. Statistics and bounds describe that set.
// This table is not registered to an STL mesh.
struct TemperatureDataset {
    std::vector<TemperatureNode> nodes;
    double minimum_temperature_C = 0.0;
    double maximum_temperature_C = 0.0;
    double average_temperature_C = 0.0;
    BoundingBox bounds{};

    std::size_t node_count() const { return nodes.size(); }
};

class TemperatureCsvParseError : public std::runtime_error {
public:
    explicit TemperatureCsvParseError(const std::string& message)
        : std::runtime_error(message) {}
};

// Expected header: node_id,x_m,y_m,z_m,temperature_C
// Column order may vary. Invalid limits throw std::invalid_argument.
// File and row failures throw TemperatureCsvParseError.
TemperatureDataset parse_temperature_csv(const std::string& path,
                                         const TemperatureCsvLimits& limits = {});

}
