#pragma once

#include "workbench/thermal_solver.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace workbench {

// One independent steady 1D thermal case. Every case uses the existing
// ThermalConfig and is solved by solve_steady_1d. The element count is the
// benchmark mesh size; it is not drawn from the random generator.
struct ThermalCase {
    ThermalConfig config;
    std::size_t elements = 0;
};

struct WorkloadConfig {
    std::uint32_t seed = 1;
    std::size_t batch_size = 1;
    std::size_t mesh_size = 1;
};

struct Workload {
    WorkloadConfig config;
    std::vector<ThermalCase> cases;
};

// Inclusive lower bound and exclusive upper bound of each uniform draw.
inline constexpr double kConductivityMinW_mK = 0.2;
inline constexpr double kConductivityMaxW_mK = 400.0;
inline constexpr double kGenerationMinW_m3 = 1.0;
inline constexpr double kGenerationMaxW_m3 = 500000.0;
inline constexpr double kLeftTemperatureMinC = -20.0;
inline constexpr double kLeftTemperatureMaxC = 500.0;
inline constexpr double kRightTemperatureMinC = -20.0;
inline constexpr double kRightTemperatureMaxC = 400.0;
inline constexpr double kFixedLengthM = 1.0;
inline constexpr double kFixedAreaM2 = 1.0e-4;

inline constexpr std::size_t kMinimumBatchSize = 1;
inline constexpr std::size_t kMaximumBatchSize = 256;
inline constexpr std::size_t kMinimumMeshSize = 1;
inline constexpr std::size_t kMaximumMeshSize = 320;

// Throws std::invalid_argument when the seed configuration cannot produce a
// valid batch. Seed itself may be any uint32 value, including zero.
void validate_workload_config(const WorkloadConfig& config);

// Generates the batch exactly once for this call. The same seed and config
// produce the same cases in the same order. Each case draws four values from
// std::mt19937, in this order:
//   conductivity, heat generation, left temperature, right temperature.
// The generator output u = mt19937() / 2^32 is in [0, 1). Each physical value
// is min + (max - min) * u, which lands in [min, max). Length and area are the
// fixed positive constants above. Conductivity and heat generation are
// therefore strictly positive, and both Dirichlet temperatures are finite.
Workload generate_workload(const WorkloadConfig& config);

bool operator==(const ThermalCase& left, const ThermalCase& right);
bool operator==(const Workload& left, const Workload& right);

}
