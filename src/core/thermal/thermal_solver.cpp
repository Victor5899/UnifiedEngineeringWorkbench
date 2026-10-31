#include "workbench/thermal_solver.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace workbench {
namespace {
void validate(const ThermalConfig& c, std::size_t n) {
    if (n == 0) throw std::invalid_argument("Element count must be positive.");
    if (!(c.length_m > 0.0) || !(c.area_m2 > 0.0) ||
        !(c.conductivity_W_mK > 0.0))
        throw std::invalid_argument("Length, area and conductivity must be positive.");
    if (!std::isfinite(c.generation_W_m3) ||
        !std::isfinite(c.left_temperature_C) ||
        !std::isfinite(c.right_temperature_C))
        throw std::invalid_argument("Thermal inputs must be finite.");
}
}

double exact_temperature(const ThermalConfig& c, double x) {
    return c.left_temperature_C
        + (c.right_temperature_C - c.left_temperature_C) * x / c.length_m
        + (c.generation_W_m3 / (2.0 * c.conductivity_W_mK)) * x * (c.length_m - x);
}

ThermalResult solve_steady_1d(const ThermalConfig& c, std::size_t n) {
    validate(c, n);
    const std::size_t nodes = n + 1;
    const double h = c.length_m / static_cast<double>(n);
    const double factor = c.conductivity_W_mK * c.area_m2 / h;
    const double element_load = c.generation_W_m3 * c.area_m2 * h / 2.0;

    // Assemble the global tridiagonal stiffness matrix and load vector.
    std::vector<std::vector<double>> K(nodes, std::vector<double>(nodes, 0.0));
    std::vector<double> F(nodes, 0.0);
    for (std::size_t e = 0; e < n; ++e) {
        const std::size_t i = e, j = e + 1;
        K[i][i] += factor; K[i][j] -= factor;
        K[j][i] -= factor; K[j][j] += factor;
        F[i] += element_load;
        F[j] += element_load;
    }

    // Apply prescribed temperatures by eliminating their columns from free equations.
    const double prescribed[2] = {c.left_temperature_C, c.right_temperature_C};
    const std::size_t fixed_nodes[2] = {0, nodes - 1};
    for (int b = 0; b < 2; ++b) {
        const std::size_t p = fixed_nodes[b];
        for (std::size_t i = 0; i < nodes; ++i) {
            if (i != p) {
                F[i] -= K[i][p] * prescribed[b];
                K[i][p] = 0.0;
                K[p][i] = 0.0;
            }
        }
        K[p][p] = 1.0;
        F[p] = prescribed[b];
    }

    // Gaussian elimination with partial pivoting (small systems: at most 81 nodes).
    for (std::size_t col = 0; col < nodes; ++col) {
        std::size_t pivot = col;
        for (std::size_t r = col + 1; r < nodes; ++r)
            if (std::abs(K[r][col]) > std::abs(K[pivot][col])) pivot = r;
        if (std::abs(K[pivot][col]) < 1e-14)
            throw std::runtime_error("Singular thermal stiffness matrix.");
        std::swap(K[col], K[pivot]);
        std::swap(F[col], F[pivot]);
        for (std::size_t r = col + 1; r < nodes; ++r) {
            const double m = K[r][col] / K[col][col];
            for (std::size_t j = col; j < nodes; ++j) K[r][j] -= m * K[col][j];
            F[r] -= m * F[col];
        }
    }
    std::vector<double> T(nodes, 0.0);
    for (std::size_t ii = nodes; ii-- > 0;) {
        double rhs = F[ii];
        for (std::size_t j = ii + 1; j < nodes; ++j) rhs -= K[ii][j] * T[j];
        T[ii] = rhs / K[ii][ii];
    }

    ThermalResult result;
    result.elements = n;
    result.x_m.resize(nodes);
    result.temperature_C = T;
    for (std::size_t i = 0; i < nodes; ++i) result.x_m[i] = i * h;

    // Evaluate error at element midpoints using FE linear interpolation.
    for (std::size_t e = 0; e < n; ++e) {
        const double xmid = (e + 0.5) * h;
        const double fe_mid = 0.5 * (T[e] + T[e + 1]);
        result.max_midpoint_error_C =
            std::max(result.max_midpoint_error_C,
                     std::abs(fe_mid - exact_temperature(c, xmid)));
    }
    return result;
}

void write_result_csv(const ThermalResult& r, const std::string& path) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Cannot open output CSV: " + path);
    out << "x_m,temperature_C\n" << std::setprecision(12);
    for (std::size_t i = 0; i < r.x_m.size(); ++i)
        out << r.x_m[i] << ',' << r.temperature_C[i] << '\n';
}
}
