#include "workbench/thermal_solver.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

int main() {
    using namespace workbench;
    try {
        const auto r10 = solve_steady_1d({}, 10);
        const auto r20 = solve_steady_1d({}, 20);
        const auto r40 = solve_steady_1d({}, 40);
        const auto r80 = solve_steady_1d({}, 80);
        if (r10.temperature_C.size() != 11) throw std::runtime_error("Wrong node count.");
        if (std::abs(r10.temperature_C.front() - 100.0) > 1e-10) throw std::runtime_error("Left BC failed.");
        if (std::abs(r10.temperature_C.back() - 20.0) > 1e-10) throw std::runtime_error("Right BC failed.");
        if (!(r20.max_midpoint_error_C < r10.max_midpoint_error_C &&
              r40.max_midpoint_error_C < r20.max_midpoint_error_C &&
              r80.max_midpoint_error_C < r40.max_midpoint_error_C))
            throw std::runtime_error("Mesh convergence failed.");
        bool rejected = false;
        try { solve_steady_1d({}, 0); } catch (const std::invalid_argument&) { rejected = true; }
        if (!rejected) throw std::runtime_error("Zero elements were not rejected.");
        std::cout << "All thermal tests passed.\n";
        std::cout << "Errors (N=10,20,40,80): "
                  << r10.max_midpoint_error_C << ", "
                  << r20.max_midpoint_error_C << ", "
                  << r40.max_midpoint_error_C << ", "
                  << r80.max_midpoint_error_C << " C\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "TEST FAILURE: " << e.what() << '\n';
        return 1;
    }
}
