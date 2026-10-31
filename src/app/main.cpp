#include "workbench/geometry/stl_parser.hpp"
#include "workbench/thermal/temperature_csv_parser.hpp"
#include "workbench/thermal_solver.hpp"

#include <exception>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

void print_vec3(const workbench::Vec3& value) {
    std::cout << value.x << ' ' << value.y << ' ' << value.z << '\n';
}

void inspect_stl(const std::string& path) {
    const workbench::StlMesh mesh = workbench::parse_ascii_stl(path);
    std::cout << std::setprecision(10);
    std::cout << "ASCII STL inspection\n"
              << "File: " << path << '\n'
              << "Solid: " << (mesh.solid_name.empty() ? "(unnamed)" : mesh.solid_name) << '\n'
              << "Triangles: " << mesh.triangle_count() << '\n'
              << "Vertices: " << mesh.vertex_count() << '\n'
              << "Bounding box minimum: ";
    print_vec3(mesh.bounds.min);
    std::cout << "Bounding box maximum: ";
    print_vec3(mesh.bounds.max);
    std::cout << "Dimensions: " << mesh.bounds.dimension_x() << ' '
              << mesh.bounds.dimension_y() << ' '
              << mesh.bounds.dimension_z() << '\n'
              << "Coordinate units: file units\n";
}

void inspect_temperature(const std::string& path) {
    const workbench::TemperatureDataset dataset = workbench::parse_temperature_csv(path);
    std::cout << std::setprecision(10);
    std::cout << "Temperature CSV inspection\n"
              << "File: " << path << '\n'
              << "Nodes: " << dataset.node_count() << '\n'
              << "Minimum temperature (C): " << dataset.minimum_temperature_C << '\n'
              << "Maximum temperature (C): " << dataset.maximum_temperature_C << '\n'
              << "Average temperature (C): " << dataset.average_temperature_C << '\n'
              << "Bounding box minimum (m): ";
    print_vec3(dataset.bounds.min);
    std::cout << "Bounding box maximum (m): ";
    print_vec3(dataset.bounds.max);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc > 1) {
            const std::string command = argv[1];
            if (command == "inspect-stl") {
                if (argc != 3) {
                    std::cerr << "Usage: workbench inspect-stl <file.stl>\n";
                    return 1;
                }
                inspect_stl(argv[2]);
                return 0;
            }
            if (command == "inspect-temperature") {
                if (argc != 3) {
                    std::cerr << "Usage: workbench inspect-temperature <file.csv>\n";
                    return 1;
                }
                inspect_temperature(argv[2]);
                return 0;
            }
        }

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
