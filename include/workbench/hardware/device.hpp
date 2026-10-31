#pragma once

#include "workbench/hardware/device_types.hpp"

namespace workbench {

// Common host interface for a microcontroller, an FPGA board, and a JTAG
// adapter. Each concrete adapter owns its connection state.
class Device {
public:
    virtual ~Device() = default;

    virtual DeviceInfo describe() const = 0;
    virtual HardwareResult connect() = 0;
    virtual HardwareResult disconnect() = 0;
    virtual HardwareResult read_telemetry(Telemetry& telemetry) const = 0;

    // Default: this device has no scripted failure. The JTAG adapter overrides it.
    virtual HardwareResult arm_simulated_failure(const std::string&) {
        return HardwareResult{
            HardwareError::unsupported_operation,
            "This simulated device has no recoverable failure. Only the JTAG adapter implements a "
            "simulated connection timeout.",
            std::nullopt};
    }
};

}
