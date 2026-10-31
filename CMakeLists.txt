cmake_minimum_required(VERSION 3.16)
project(UnifiedEngineeringWorkbench VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(workbench_core
    src/core/thermal/thermal_solver.cpp
    src/core/geometry/stl_parser.cpp
    src/core/thermal/temperature_csv_parser.cpp
)
target_include_directories(workbench_core PUBLIC include)

add_executable(workbench src/app/main.cpp)
target_link_libraries(workbench PRIVATE workbench_core)

enable_testing()

add_executable(workbench_tests tests/test_thermal.cpp)
target_link_libraries(workbench_tests PRIVATE workbench_core)
add_test(NAME thermal_tests COMMAND workbench_tests)

add_executable(workbench_geometry_tests tests/test_stl_parser.cpp)
target_link_libraries(workbench_geometry_tests PRIVATE workbench_core)
target_compile_definitions(workbench_geometry_tests PRIVATE
    WORKBENCH_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    WORKBENCH_TEST_DIR="${CMAKE_BINARY_DIR}/tmp_tests")
add_test(NAME geometry_tests COMMAND workbench_geometry_tests)

add_executable(workbench_temperature_tests tests/test_temperature_csv.cpp)
target_link_libraries(workbench_temperature_tests PRIVATE workbench_core)
target_compile_definitions(workbench_temperature_tests PRIVATE
    WORKBENCH_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    WORKBENCH_TEST_DIR="${CMAKE_BINARY_DIR}/tmp_tests")
add_test(NAME temperature_csv_tests COMMAND workbench_temperature_tests)

add_test(NAME thermal_cli
    COMMAND workbench 4 ${CMAKE_BINARY_DIR}/thermal_cli_smoke.csv)
set_tests_properties(thermal_cli PROPERTIES
    PASS_REGULAR_EXPRESSION "1D steady thermal FEA completed")

add_test(NAME inspect_sample_stl
    COMMAND workbench inspect-stl ${CMAKE_SOURCE_DIR}/data/geometry/sample_geometry.stl)
set_tests_properties(inspect_sample_stl PROPERTIES
    PASS_REGULAR_EXPRESSION "Triangles: 12[^0-9]")

add_test(NAME inspect_sample_temperature
    COMMAND workbench inspect-temperature ${CMAKE_SOURCE_DIR}/data/thermal/sample_temperature.csv)
set_tests_properties(inspect_sample_temperature PROPERTIES
    PASS_REGULAR_EXPRESSION "Nodes: 27[^0-9]")

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    foreach(target workbench_core workbench workbench_tests
            workbench_geometry_tests workbench_temperature_tests)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
endif()
