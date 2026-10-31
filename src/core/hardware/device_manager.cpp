#include "workbench/hardware/device_manager.hpp"

#include "workbench/hardware/mock_fpga_adapter.hpp"
#include "workbench/hardware/mock_jtag_adapter.hpp"
#include "workbench/hardware/mock_mcu_adapter.hpp"

#include <stdexcept>
#include <utility>

namespace workbench {
namespace {

HardwareResult unknown_device() {
    return HardwareResult{
        HardwareError::unknown_device,
        "No simulated device has that identifier.",
        std::nullopt};
}

HardwareResult invalid_id() {
    return HardwareResult{
        HardwareError::invalid_request,
        "A device identifier is required.",
        std::nullopt};
}

}  // namespace

DeviceManager::DeviceManager() {
    devices_.push_back(std::make_unique<MockMcuAdapter>());
    devices_.push_back(std::make_unique<MockFpgaAdapter>());
    devices_.push_back(std::make_unique<MockJtagAdapter>());
    index_devices();
}

DeviceManager::DeviceManager(std::vector<std::unique_ptr<Device>> devices) : devices_(std::move(devices)) {
    index_devices();
}

void DeviceManager::index_devices() {
    index_.clear();
    for (std::size_t index = 0; index < devices_.size(); ++index) {
        if (!devices_[index]) {
            throw std::invalid_argument("Hardware device registration is empty.");
        }
        const std::string id = devices_[index]->describe().id;
        if (id.empty()) {
            throw std::invalid_argument("Hardware device id is empty.");
        }
        if (!index_.emplace(id, index).second) {
            throw std::invalid_argument("Duplicate hardware device id \"" + id + "\".");
        }
    }
}

Device* DeviceManager::find(const std::string& id) const {
    const auto found = index_.find(id);
    if (found == index_.end()) return nullptr;
    return devices_[found->second].get();
}

std::vector<DeviceInfo> DeviceManager::discover() const {
    std::vector<DeviceInfo> devices;
    devices.reserve(devices_.size());
    for (const std::unique_ptr<Device>& device : devices_) {
        devices.push_back(device->describe());
    }
    return devices;
}

std::vector<CapabilityReport> DeviceManager::capabilities() const {
    std::vector<CapabilityReport> reports;
    reports.reserve(devices_.size());
    for (const DeviceInfo& info : discover()) {
        CapabilityReport report;
        report.id = info.id;
        report.type = info.type;
        report.display_name = info.display_name;
        report.capabilities = info.capabilities;
        reports.push_back(std::move(report));
    }
    return reports;
}

HardwareResult DeviceManager::describe(const std::string& id, DeviceInfo& info) const {
    if (id.empty()) return invalid_id();
    Device* device = find(id);
    if (device == nullptr) return unknown_device();
    info = device->describe();
    return HardwareResult{HardwareError::ok, "Simulated device.", info.connection};
}

HardwareResult DeviceManager::connect(const std::string& id) {
    if (id.empty()) return invalid_id();
    Device* device = find(id);
    if (device == nullptr) return unknown_device();
    return device->connect();
}

HardwareResult DeviceManager::disconnect(const std::string& id) {
    if (id.empty()) return invalid_id();
    Device* device = find(id);
    if (device == nullptr) return unknown_device();
    return device->disconnect();
}

HardwareResult DeviceManager::read_telemetry(const std::string& id, Telemetry& telemetry) const {
    telemetry = Telemetry{};
    if (id.empty()) return invalid_id();
    Device* device = find(id);
    if (device == nullptr) return unknown_device();
    return device->read_telemetry(telemetry);
}

HardwareResult DeviceManager::arm_simulated_failure(const std::string& id, const std::string& failure_id) {
    if (id.empty()) return invalid_id();
    if (failure_id.empty()) {
        return HardwareResult{
            HardwareError::invalid_request,
            "A simulated failure identifier is required.",
            std::nullopt};
    }
    Device* device = find(id);
    if (device == nullptr) return unknown_device();
    return device->arm_simulated_failure(failure_id);
}

}  // namespace workbench
