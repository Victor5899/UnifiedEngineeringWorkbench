#include "workbench/hardware/device_types.hpp"

namespace workbench {

const char* to_string(DeviceType type) {
    switch (type) {
    case DeviceType::microcontroller:
        return "microcontroller";
    case DeviceType::fpga:
        return "fpga";
    case DeviceType::jtag_adapter:
        return "jtag_adapter";
    }
    return "microcontroller";
}

const char* to_string(ConnectionState state) {
    switch (state) {
    case ConnectionState::disconnected:
        return "disconnected";
    case ConnectionState::connected:
        return "connected";
    }
    return "disconnected";
}

const char* to_string(HardwareError error) {
    switch (error) {
    case HardwareError::ok:
        return "ok";
    case HardwareError::unknown_device:
        return "unknown_device";
    case HardwareError::already_connected:
        return "already_connected";
    case HardwareError::not_connected:
        return "not_connected";
    case HardwareError::simulated_timeout:
        return "simulated_timeout";
    case HardwareError::unsupported_operation:
        return "unsupported_operation";
    case HardwareError::invalid_request:
        return "invalid_request";
    }
    return "invalid_request";
}

}
