#include "workbench/performance/hardware_info.hpp"

#include "workbench_build_info.hpp"

#include <algorithm>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/utsname.h>
#else
#include <fstream>
#include <set>
#include <unistd.h>
#include <sys/utsname.h>
#endif

namespace workbench {
namespace {

std::string trimmed(std::string value) {
    while (!value.empty() && (value.back() == '\0' || value.back() == '\n' || value.back() == '\r' ||
                              value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    std::size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) ++start;
    if (start > 0) value.erase(0, start);
    return value;
}

std::string configured_text(const char* value) {
    if (value == nullptr) return {};
    const std::string text = trimmed(value);
    if (text.empty() || text == "unavailable") return {};
    return text;
}

void collect_toolchain(HardwareInfo& info) {
#if defined(__apple_build_version__)
    info.compiler = "AppleClang";
    info.compiler_version = __clang_version__;
#elif defined(__clang__)
    info.compiler = "Clang";
    info.compiler_version = __clang_version__;
#elif defined(__GNUC__)
    info.compiler = "GCC";
    info.compiler_version = std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." +
                            std::to_string(__GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
    info.compiler = "MSVC";
    info.compiler_version = std::to_string(_MSC_VER);
#else
    info.compiler.clear();
    info.compiler_version.clear();
#endif
    info.build_type = configured_text(WORKBENCH_BUILD_TYPE);
    info.optimization_flags = configured_text(WORKBENCH_OPT_FLAGS);
}

#if defined(__APPLE__)

template <typename T>
bool sysctl_value(const char* name, T& value) {
    std::size_t length = sizeof(T);
    return sysctlbyname(name, &value, &length, nullptr, 0) == 0 && length == sizeof(T);
}

std::string sysctl_string(const char* name) {
    std::size_t length = 0;
    if (sysctlbyname(name, nullptr, &length, nullptr, 0) != 0 || length == 0) return {};
    std::string value(length, '\0');
    if (sysctlbyname(name, value.data(), &length, nullptr, 0) != 0) return {};
    return trimmed(value);
}

void collect_platform(HardwareInfo& info) {
    const std::string model = sysctl_string("machdep.cpu.brand_string");
    if (!model.empty()) info.cpu_model = model;

    int physical = 0;
    if (sysctl_value("hw.physicalcpu", physical) && physical > 0) {
        info.physical_cores = static_cast<unsigned>(physical);
    }
    int logical = 0;
    if (sysctl_value("hw.logicalcpu", logical) && logical > 0) {
        info.logical_cpus = static_cast<unsigned>(logical);
    }
    std::uint64_t memory = 0;
    if (sysctl_value("hw.memsize", memory) && memory > 0) {
        info.ram_bytes = memory;
    }

    utsname system{};
    if (uname(&system) == 0) {
        info.operating_system = trimmed(system.sysname) + " " + trimmed(system.release) + " " +
                                trimmed(system.machine);
    }
}

#elif defined(_WIN32)

void collect_platform(HardwareInfo& info) {
    SYSTEM_INFO system;
    GetSystemInfo(&system);
    if (system.dwNumberOfProcessors > 0) {
        info.logical_cpus = static_cast<unsigned>(system.dwNumberOfProcessors);
    }

    DWORD length = 0;
    GetLogicalProcessorInformation(nullptr, &length);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && length > 0) {
        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer(
            length / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
        if (!buffer.empty() && GetLogicalProcessorInformation(buffer.data(), &length)) {
            unsigned physical = 0;
            for (const SYSTEM_LOGICAL_PROCESSOR_INFORMATION& entry : buffer) {
                if (entry.Relationship == RelationProcessorCore) ++physical;
            }
            if (physical > 0) info.physical_cores = physical;
        }
    }

    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory) && memory.ullTotalPhys > 0) {
        info.ram_bytes = static_cast<std::uint64_t>(memory.ullTotalPhys);
    }

    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0,
                      KEY_QUERY_VALUE,
                      &key) == ERROR_SUCCESS) {
        char model[256] = {};
        DWORD model_size = sizeof(model);
        if (RegGetValueA(key, nullptr, "ProcessorNameString", RRF_RT_REG_SZ, nullptr, model,
                         &model_size) == ERROR_SUCCESS) {
            const std::string text = trimmed(model);
            if (!text.empty()) info.cpu_model = text;
        }
        RegCloseKey(key);
    }

    using RtlGetVersionPtr = LONG(WINAPI*)(OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    auto rtl_get_version =
        ntdll == nullptr ? nullptr
                         : reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(ntdll, "RtlGetVersion"));
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (rtl_get_version != nullptr && rtl_get_version(&version) == 0) {
        info.operating_system = "Windows " + std::to_string(version.dwMajorVersion) + "." +
                                std::to_string(version.dwMinorVersion) + "." +
                                std::to_string(version.dwBuildNumber);
    }
}

#else

void collect_platform(HardwareInfo& info) {
    std::ifstream cpuinfo("/proc/cpuinfo");
    if (cpuinfo) {
        std::string line;
        std::set<std::pair<std::string, std::string>> cores;
        std::string physical_id;
        std::string core_id;
        bool have_pair = false;
        auto flush_pair = [&] {
            if (!have_pair) return;
            cores.insert({physical_id, core_id});
            physical_id.clear();
            core_id.clear();
            have_pair = false;
        };
        while (std::getline(cpuinfo, line)) {
            const auto separator = line.find(':');
            if (separator == std::string::npos) {
                flush_pair();
                continue;
            }
            const std::string key = trimmed(line.substr(0, separator));
            const std::string value = trimmed(line.substr(separator + 1));
            if (info.cpu_model.empty() && key == "model name" && !value.empty()) {
                info.cpu_model = value;
            } else if (key == "physical id") {
                physical_id = value;
                have_pair = true;
            } else if (key == "core id") {
                core_id = value;
                have_pair = true;
            }
        }
        flush_pair();
        if (!cores.empty()) info.physical_cores = static_cast<unsigned>(cores.size());
    }

    const long online = sysconf(_SC_NPROCESSORS_ONLN);
    if (online > 0) info.logical_cpus = static_cast<unsigned>(online);

    const long pages = sysconf(_SC_PHYS_PAGES);
    const long page_size = sysconf(_SC_PAGESIZE);
    if (pages > 0 && page_size > 0) {
        info.ram_bytes = static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(page_size);
    }

    utsname system{};
    if (uname(&system) == 0) {
        info.operating_system = trimmed(system.sysname) + " " + trimmed(system.release) + " " +
                                trimmed(system.machine);
    }
}

#endif

}  // namespace

std::optional<unsigned> logical_cpu_count() {
    HardwareInfo info;
    collect_platform(info);
    if (info.logical_cpus && *info.logical_cpus > 0) return info.logical_cpus;
    const unsigned runtime = std::thread::hardware_concurrency();
    if (runtime > 0) return runtime;
    return std::nullopt;
}

unsigned maximum_supported_workers() {
    const std::optional<unsigned> logical = logical_cpu_count();
    if (!logical || *logical == 0) return 1;
    return std::min(4u, *logical);
}

HardwareInfo collect_hardware_info() {
    HardwareInfo info;
    collect_toolchain(info);
    collect_platform(info);
    if (!info.logical_cpus || *info.logical_cpus == 0) {
        const unsigned runtime = std::thread::hardware_concurrency();
        if (runtime > 0) info.logical_cpus = runtime;
        else info.logical_cpus.reset();
    }
    return info;
}

}
