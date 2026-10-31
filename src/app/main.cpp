#include "workbench/thermal_solver.hpp"
#include <exception>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    try {
        std::size_t elements = 10;
        std::string output = "thermal_result.csv";
        if (argc > 1) elements = static_cast<std::size_t>(std::stoul(argv[1]));
        if (argc > 2) output = argv[2];
        const auto result = workbench::solve_steady_1d({}, elements);
        workbench::write_result_csv(result, output);
        std::cout << "1D steady thermal FEA completed\n"
                  << "Elements: " << result.elements << '\n'
                  << "Maximum midpoint absolute error: "
                  << result.max_midpoint_error_C << " C\n"
                  << "CSV written to: " << output << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
