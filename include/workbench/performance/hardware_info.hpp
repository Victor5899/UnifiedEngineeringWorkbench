#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace workbench {

// Fields that cannot be read stay empty or disengaged. Nothing here is guessed.
struct HardwareInfo {
    std::string cpu_model;
    std::optional<unsigned> physical_cores;
    std::optional<unsigned> logical_cpus;
    std::optional<std::uint64_t> ram_bytes;
    std::string operating_system;
    std::string compiler;
    std::string compiler_version;
    std::string build_type;
    std::string optimization_flags;
};

HardwareInfo collect_hardware_info();

// Logical CPUs available to this process. Disengaged when the platform does
// not report a positive count.
std::optional<unsigned> logical_cpu_count();

// The benchmark pool never uses more than four workers and never uses more
// workers than logical_cpu_count(). When the logical count is unknown, the
// limit is 1 so the run does not assume extra cores.
unsigned maximum_supported_workers();

}
