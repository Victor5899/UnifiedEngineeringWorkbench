#pragma once

#include "workbench/hardware/device.hpp"

#include <mutex>

namespace workbench {

// In-process stand-in for a Digilent JTAG-HS2-class debug adapter. Connection
// only changes software state. It does not shift a scan chain or program a target.
class MockJtagAdapter final : public Device {
public:
    MockJtagAdapter() = default;
    MockJtagAdapter(const MockJtagAdapter&) = delete;
    MockJtagAdapter& operator=(const MockJtagAdapter&) = delete;

    DeviceInfo describe() const override;
    HardwareResult connect() override;
    HardwareResult disconnect() override;
    HardwareResult read_telemetry(Telemetry& telemetry) const override;
    HardwareResult arm_simulated_failure(const std::string& failure_id) override;

    static const char* id();
    static const char* timeout_failure_id();

private:
    mutable std::mutex mutex_;
    ConnectionState state_ = ConnectionState::disconnected;
    bool timeout_armed_ = false;
};

}
