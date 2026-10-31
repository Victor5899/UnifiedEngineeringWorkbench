#pragma once

#include "workbench/hardware/device.hpp"

#include <mutex>

namespace workbench {

// In-process stand-in for an Arduino Uno Rev3-class board. It does not open a
// serial port or a USB device.
class MockMcuAdapter final : public Device {
public:
    MockMcuAdapter() = default;
    MockMcuAdapter(const MockMcuAdapter&) = delete;
    MockMcuAdapter& operator=(const MockMcuAdapter&) = delete;

    DeviceInfo describe() const override;
    HardwareResult connect() override;
    HardwareResult disconnect() override;
    HardwareResult read_telemetry(Telemetry& telemetry) const override;

    static const char* id();

private:
    mutable std::mutex mutex_;
    ConnectionState state_ = ConnectionState::disconnected;
};

}
