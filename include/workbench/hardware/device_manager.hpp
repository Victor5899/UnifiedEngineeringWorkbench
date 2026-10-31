#pragma once

#include "workbench/hardware/device.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace workbench {

// Registers the three mock adapters and dispatches host operations by id.
// The registry is fixed after construction. Each adapter locks only its own state,
// so a call on one device does not hold a lock that the others need.
class DeviceManager {
public:
    DeviceManager();
    explicit DeviceManager(std::vector<std::unique_ptr<Device>> devices);

    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

    std::vector<DeviceInfo> discover() const;
    std::vector<CapabilityReport> capabilities() const;
    HardwareResult describe(const std::string& id, DeviceInfo& info) const;
    HardwareResult connect(const std::string& id);
    HardwareResult disconnect(const std::string& id);
    HardwareResult read_telemetry(const std::string& id, Telemetry& telemetry) const;
    HardwareResult arm_simulated_failure(const std::string& id, const std::string& failure_id);

private:
    Device* find(const std::string& id) const;
    void index_devices();

    std::vector<std::unique_ptr<Device>> devices_;
    std::unordered_map<std::string, std::size_t> index_;
};

}
