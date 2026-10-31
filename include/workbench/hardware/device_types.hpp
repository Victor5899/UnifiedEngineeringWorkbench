#pragma once

#include <optional>
#include <string>
#include <vector>

namespace workbench {

// Host-side view of a simulated board. These names describe the reference
// profile. They do not mean a USB, serial, or JTAG device was enumerated.
enum class DeviceType {
    microcontroller,
    fpga,
    jtag_adapter,
};

enum class ConnectionState {
    disconnected,
    connected,
};

enum class HardwareError {
    ok,
    unknown_device,
    already_connected,
    not_connected,
    simulated_timeout,
    unsupported_operation,
    invalid_request,
};

struct Capability {
    std::string id;
    std::string summary;
};

struct DeviceInfo {
    std::string id;
    DeviceType type = DeviceType::microcontroller;
    std::string display_name;
    std::string reference_profile;
    std::string manufacturer_reference;
    ConnectionState connection = ConnectionState::disconnected;
    bool available = true;
    bool simulated = true;
    std::vector<Capability> capabilities;
};

struct TelemetryReading {
    std::string name;
    std::string value;
    std::string unit;
    bool simulated = true;
};

struct Telemetry {
    std::string device_id;
    bool simulated = true;
    std::string notice;
    std::vector<TelemetryReading> readings;
};

struct CapabilityReport {
    std::string id;
    DeviceType type = DeviceType::microcontroller;
    std::string display_name;
    std::vector<Capability> capabilities;
};

// Explicit result for every hardware operation. Adapters do not throw for
// unknown ids, duplicate connects, or the simulated JTAG timeout.
struct HardwareResult {
    HardwareError error = HardwareError::ok;
    std::string message;
    std::optional<ConnectionState> connection;

    bool ok() const { return error == HardwareError::ok; }
};

const char* to_string(DeviceType type);
const char* to_string(ConnectionState state);
const char* to_string(HardwareError error);

}
