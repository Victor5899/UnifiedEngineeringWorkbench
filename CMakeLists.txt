cmake_minimum_required(VERSION 3.16)
project(UnifiedEngineeringWorkbench VERSION 0.2.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
endif()
# CMake 4 rejects dependency projects that declare a minimum older than 3.5.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING
        "Policy floor for FetchContent dependencies" FORCE)
endif()

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

# Local HTTP API. The CLI executable above is unchanged.
# cpp-httplib v0.18.3 and nlohmann/json v3.11.3 are fetched on the first configure.
option(WORKBENCH_BUILD_SERVER "Build the local HTTP API and its tests" ON)
if(WORKBENCH_BUILD_SERVER)
    include(FetchContent)
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    set(JSON_Install OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_REQUIRE_ZLIB OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_REQUIRE_BROTLI OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG v0.18.3
        GIT_SHALLOW TRUE
    )
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(httplib nlohmann_json)

    find_package(Threads REQUIRED)

    add_library(workbench_server_lib src/server/api_server.cpp)
    target_include_directories(workbench_server_lib PUBLIC include)
    target_compile_definitions(workbench_server_lib
        PUBLIC "WORKBENCH_VERSION=\"${PROJECT_VERSION}\"")
    target_link_libraries(workbench_server_lib
        PUBLIC
            workbench_core
            httplib::httplib
            nlohmann_json::nlohmann_json
            Threads::Threads
    )

    add_executable(workbench_server src/server/main.cpp)
    target_link_libraries(workbench_server PRIVATE workbench_server_lib)

    add_executable(workbench_api_tests tests/test_http_api.cpp)
    target_link_libraries(workbench_api_tests PRIVATE workbench_server_lib)
    target_compile_definitions(workbench_api_tests PRIVATE
        WORKBENCH_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        WORKBENCH_TEST_DIR="${CMAKE_BINARY_DIR}/tmp_tests")
    add_test(NAME http_api_tests COMMAND workbench_api_tests)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    foreach(target workbench_core workbench workbench_tests
            workbench_geometry_tests workbench_temperature_tests)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
    if(TARGET workbench_server_lib)
        foreach(target workbench_server_lib workbench_server workbench_api_tests)
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        endforeach()
    endif()
endif()
