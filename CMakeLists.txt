cmake_minimum_required(VERSION 3.16)
project(UnifiedEngineeringWorkbench VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(workbench_core
    src/core/thermal/thermal_solver.cpp
)
target_include_directories(workbench_core PUBLIC include)

add_executable(workbench src/app/main.cpp)
target_link_libraries(workbench PRIVATE workbench_core)

enable_testing()
add_executable(workbench_tests tests/test_thermal.cpp)
target_link_libraries(workbench_tests PRIVATE workbench_core)
add_test(NAME thermal_tests COMMAND workbench_tests)
