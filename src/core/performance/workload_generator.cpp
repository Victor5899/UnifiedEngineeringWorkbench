#include "workbench/performance/workload_generator.hpp"

#include <cmath>
#include <random>
#include <stdexcept>

namespace workbench {
namespace {

double uniform_draw(std::mt19937& generator, double minimum, double maximum) {
    // mt19937::operator() is specified to return values in [0, 2^32). Dividing
    // by 2^32 maps that onto [0, 1) without using the implementation-defined
    // algorithm inside std::uniform_real_distribution.
    constexpr double kScale = 4294967296.0;
    const double unit = static_cast<double>(generator()) / kScale;
    return minimum + (maximum - minimum) * unit;
}

void check_generated_case(const ThermalCase& item) {
    const ThermalConfig& config = item.config;
    const bool valid = item.elements > 0 &&
                       config.length_m > 0.0 &&
                       config.area_m2 > 0.0 &&
                       config.conductivity_W_mK > 0.0 &&
                       config.generation_W_m3 > 0.0 &&
                       std::isfinite(config.left_temperature_C) &&
                       std::isfinite(config.right_temperature_C);
    if (!valid) {
        throw std::logic_error("Workload generator produced an invalid thermal case.");
    }
}

bool same_config(const ThermalConfig& left, const ThermalConfig& right) {
    return left.length_m == right.length_m &&
           left.area_m2 == right.area_m2 &&
           left.conductivity_W_mK == right.conductivity_W_mK &&
           left.generation_W_m3 == right.generation_W_m3 &&
           left.left_temperature_C == right.left_temperature_C &&
           left.right_temperature_C == right.right_temperature_C;
}

}  // namespace

void validate_workload_config(const WorkloadConfig& config) {
    if (config.batch_size < kMinimumBatchSize || config.batch_size > kMaximumBatchSize) {
        throw std::invalid_argument(
            "Batch size must be from " + std::to_string(kMinimumBatchSize) + " to " +
            std::to_string(kMaximumBatchSize) + ".");
    }
    if (config.mesh_size < kMinimumMeshSize || config.mesh_size > kMaximumMeshSize) {
        throw std::invalid_argument(
            "Mesh size must be from " + std::to_string(kMinimumMeshSize) + " to " +
            std::to_string(kMaximumMeshSize) + " elements.");
    }
}

Workload generate_workload(const WorkloadConfig& config) {
    validate_workload_config(config);

    std::mt19937 generator(config.seed);
    Workload workload;
    workload.config = config;
    workload.cases.reserve(config.batch_size);
    for (std::size_t index = 0; index < config.batch_size; ++index) {
        ThermalCase item;
        item.elements = config.mesh_size;
        item.config.length_m = kFixedLengthM;
        item.config.area_m2 = kFixedAreaM2;
        item.config.conductivity_W_mK =
            uniform_draw(generator, kConductivityMinW_mK, kConductivityMaxW_mK);
        item.config.generation_W_m3 =
            uniform_draw(generator, kGenerationMinW_m3, kGenerationMaxW_m3);
        item.config.left_temperature_C =
            uniform_draw(generator, kLeftTemperatureMinC, kLeftTemperatureMaxC);
        item.config.right_temperature_C =
            uniform_draw(generator, kRightTemperatureMinC, kRightTemperatureMaxC);
        check_generated_case(item);
        workload.cases.push_back(item);
    }
    return workload;
}

bool operator==(const ThermalCase& left, const ThermalCase& right) {
    return left.elements == right.elements && same_config(left.config, right.config);
}

bool operator==(const Workload& left, const Workload& right) {
    return left.config.seed == right.config.seed &&
           left.config.batch_size == right.config.batch_size &&
           left.config.mesh_size == right.config.mesh_size &&
           left.cases == right.cases;
}

}
