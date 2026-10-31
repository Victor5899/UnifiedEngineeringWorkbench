#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace workbench {
struct ThermalConfig {
    double length_m = 1.0;
    double area_m2 = 1.0e-4;
    double conductivity_W_mK = 10.0;
    double generation_W_m3 = 10000.0;
    double left_temperature_C = 100.0;
    double right_temperature_C = 20.0;
};

struct ThermalResult {
    std::size_t elements = 0;
    std::vector<double> x_m;
    std::vector<double> temperature_C;
    double max_midpoint_error_C = 0.0;
};

double exact_temperature(const ThermalConfig& config, double x_m);
ThermalResult solve_steady_1d(const ThermalConfig& config, std::size_t elements);
void write_result_csv(const ThermalResult& result, const std::string& path);
}
