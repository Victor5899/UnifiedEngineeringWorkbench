#pragma once

#include "workbench/hardware/device.hpp"

#include <mutex>

namespace workbench {

// In-process stand-in for a Digilent Arty A7-100-class board. It does not load
// a bitstream and does not claim that FPGA logic is running.
class MockFpgaAdapter final : public Device {
public:
    MockFpgaAdapter() = default;
    MockFpgaAdapter(const MockFpgaAdapter&) = delete;
    MockFpgaAdapter& operator=(const MockFpgaAdapter&) = delete;

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
