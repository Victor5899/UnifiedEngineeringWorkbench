#include "workbench/hardware/device_manager.hpp"
#include "workbench/hardware/mock_fpga_adapter.hpp"
#include "workbench/hardware/mock_jtag_adapter.hpp"
#include "workbench/hardware/mock_mcu_adapter.hpp"

#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "TEST FAILURE: " << message << '\n';
        ++g_failures;
    }
}

workbench::DeviceInfo require_device(workbench::DeviceManager& manager, const std::string& id) {
    workbench::DeviceInfo info;
    const workbench::HardwareResult result = manager.describe(id, info);
    expect(result.ok(), "describe " + id);
    return info;
}

bool has_capability(const workbench::DeviceInfo& info, const std::string& capability_id) {
    for (const workbench::Capability& capability : info.capabilities) {
        if (capability.id == capability_id && !capability.summary.empty()) return true;
    }
    return false;
}

const workbench::TelemetryReading* find_reading(const workbench::Telemetry& telemetry, const std::string& name) {
    for (const workbench::TelemetryReading& reading : telemetry.readings) {
        if (reading.name == name) return &reading;
    }
    return nullptr;
}

class NamedDevice final : public workbench::Device {
public:
    explicit NamedDevice(std::string id) : id_(std::move(id)) {}

    workbench::DeviceInfo describe() const override {
        workbench::DeviceInfo info;
        info.id = id_;
        info.display_name = id_;
        info.type = workbench::DeviceType::microcontroller;
        info.simulated = true;
        return info;
    }

    workbench::HardwareResult connect() override { return {}; }
    workbench::HardwareResult disconnect() override { return {}; }
    workbench::HardwareResult read_telemetry(workbench::Telemetry&) const override { return {}; }

private:
    std::string id_;
};

void test_discovery() {
    workbench::DeviceManager manager;
    const std::vector<workbench::DeviceInfo> devices = manager.discover();
    expect(devices.size() == 3, "discovery returns three devices");
    if (devices.size() != 3) return;

    expect(devices[0].id == workbench::MockMcuAdapter::id(), "first device is the microcontroller");
    expect(devices[1].id == workbench::MockFpgaAdapter::id(), "second device is the FPGA board");
    expect(devices[2].id == workbench::MockJtagAdapter::id(), "third device is the JTAG adapter");

    expect(devices[0].type == workbench::DeviceType::microcontroller, "MCU type");
    expect(devices[1].type == workbench::DeviceType::fpga, "FPGA type");
    expect(devices[2].type == workbench::DeviceType::jtag_adapter, "JTAG type");

    for (const workbench::DeviceInfo& device : devices) {
        expect(device.simulated, device.id + " is labelled simulated");
        expect(device.available, device.id + " is available as a mock profile");
        expect(device.connection == workbench::ConnectionState::disconnected, device.id + " starts disconnected");
        expect(!device.display_name.empty(), device.id + " has a display name");
        expect(!device.reference_profile.empty(), device.id + " has a reference profile");
        expect(!device.capabilities.empty(), device.id + " reports capabilities");
    }

    expect(has_capability(devices[0], "logic_voltage_5v"), "MCU reports the 5 V domain");
    expect(has_capability(devices[0], "no_jtag_tap"), "MCU reports that it has no JTAG TAP");
    expect(has_capability(devices[1], "io_voltage_domain_3v3"), "FPGA reports the 3.3 V domain");
    expect(has_capability(devices[1], "catalog_logic_resources"), "FPGA reports catalog resources");
    expect(has_capability(devices[2], "debug_transport"), "JTAG reports a debug transport");
    expect(has_capability(devices[2], "simulated_connection_timeout"), "JTAG reports the scripted timeout");

    const std::vector<workbench::CapabilityReport> reports = manager.capabilities();
    expect(reports.size() == 3, "capability report covers three devices");
    if (reports.size() == 3) {
        expect(reports[0].id == devices[0].id && reports[0].capabilities.size() == devices[0].capabilities.size(),
               "MCU capability report matches discovery");
        expect(reports[2].type == workbench::DeviceType::jtag_adapter, "JTAG capability report keeps its type");
    }
}

void test_connect_disconnect_and_telemetry() {
    workbench::DeviceManager manager;
    const char* ids[] = {
        workbench::MockMcuAdapter::id(),
        workbench::MockFpgaAdapter::id(),
        workbench::MockJtagAdapter::id(),
    };
    for (const char* id : ids) {
        workbench::Telemetry early;
        const workbench::HardwareResult denied = manager.read_telemetry(id, early);
        expect(denied.error == workbench::HardwareError::not_connected, std::string(id) + " telemetry while disconnected");
        expect(early.readings.empty(), std::string(id) + " disconnected telemetry has no readings");
        expect(denied.connection == workbench::ConnectionState::disconnected, std::string(id) + " stays disconnected");

        const workbench::HardwareResult connected = manager.connect(id);
        expect(connected.ok(), std::string(id) + " connects");
        expect(connected.connection == workbench::ConnectionState::connected, std::string(id) + " reports connected");
        expect(require_device(manager, id).connection == workbench::ConnectionState::connected,
               std::string(id) + " describe agrees");

        const workbench::HardwareResult duplicate = manager.connect(id);
        expect(duplicate.error == workbench::HardwareError::already_connected, std::string(id) + " duplicate connect");
        expect(duplicate.connection == workbench::ConnectionState::connected, std::string(id) + " duplicate leaves it connected");

        workbench::Telemetry telemetry;
        const workbench::HardwareResult reading = manager.read_telemetry(id, telemetry);
        expect(reading.ok(), std::string(id) + " telemetry while connected");
        expect(telemetry.simulated, std::string(id) + " telemetry is simulated");
        expect(telemetry.device_id == id, std::string(id) + " telemetry id");
        expect(telemetry.notice.find("Simulated") != std::string::npos, std::string(id) + " notice says simulated");
        expect(!telemetry.readings.empty(), std::string(id) + " has readings");
        for (const workbench::TelemetryReading& sample : telemetry.readings) {
            expect(sample.simulated, std::string(id) + " reading " + sample.name + " is simulated");
        }

        const workbench::HardwareResult disconnected = manager.disconnect(id);
        expect(disconnected.ok(), std::string(id) + " disconnects");
        expect(require_device(manager, id).connection == workbench::ConnectionState::disconnected,
               std::string(id) + " returns to disconnected");
        const workbench::HardwareResult again = manager.disconnect(id);
        expect(again.error == workbench::HardwareError::not_connected, std::string(id) + " second disconnect");
    }
}

void test_profile_readings() {
    workbench::DeviceManager manager;
    expect(manager.connect(workbench::MockMcuAdapter::id()).ok(), "connect MCU for readings");
    workbench::Telemetry mcu;
    expect(manager.read_telemetry(workbench::MockMcuAdapter::id(), mcu).ok(), "read MCU telemetry");
    const workbench::TelemetryReading* temperature = find_reading(mcu, "Temperature");
    const workbench::TelemetryReading* supply = find_reading(mcu, "Supply voltage");
    const workbench::TelemetryReading* status = find_reading(mcu, "Status");
    expect(temperature != nullptr && temperature->value == "28.5" && temperature->unit == "C", "MCU temperature");
    expect(supply != nullptr && supply->value == "5.0" && supply->unit == "V", "MCU supply");
    expect(status != nullptr && status->value == "Online", "MCU status");
    expect(mcu.notice.find("not a measurement") != std::string::npos, "MCU notice denies a measurement");

    expect(manager.connect(workbench::MockFpgaAdapter::id()).ok(), "connect FPGA for readings");
    workbench::Telemetry fpga;
    expect(manager.read_telemetry(workbench::MockFpgaAdapter::id(), fpga).ok(), "read FPGA telemetry");
    const workbench::TelemetryReading* configuration = find_reading(fpga, "Configuration");
    const workbench::TelemetryReading* luts = find_reading(fpga, "Catalog look-up tables");
    expect(configuration != nullptr && configuration->value.find("No bitstream") != std::string::npos,
           "FPGA telemetry does not claim a bitstream");
    expect(luts != nullptr && luts->value == "15850", "FPGA catalog LUT count");
    expect(fpga.notice.find("does not mean a bitstream") != std::string::npos, "FPGA notice denies programming");

    expect(manager.connect(workbench::MockJtagAdapter::id()).ok(), "connect JTAG for readings");
    workbench::Telemetry jtag;
    expect(manager.read_telemetry(workbench::MockJtagAdapter::id(), jtag).ok(), "read JTAG telemetry");
    const workbench::TelemetryReading* role = find_reading(jtag, "Role");
    const workbench::TelemetryReading* programming = find_reading(jtag, "Programming performed");
    expect(role != nullptr && role->value.find("transport") != std::string::npos, "JTAG role is transport");
    expect(programming != nullptr && programming->value == "No", "JTAG programming was not performed");
    expect(jtag.notice.find("not a processing device") != std::string::npos, "JTAG notice denies compute");
}

void test_invalid_identifier() {
    workbench::DeviceManager manager;
    workbench::DeviceInfo info;
    const workbench::HardwareResult missing = manager.describe("missing-device", info);
    expect(missing.error == workbench::HardwareError::unknown_device, "unknown describe");
    expect(!missing.connection.has_value(), "unknown describe has no connection state");
    expect(manager.connect("").error == workbench::HardwareError::invalid_request, "empty connect id");
    expect(manager.connect("missing-device").error == workbench::HardwareError::unknown_device, "unknown connect");
    expect(manager.disconnect("missing-device").error == workbench::HardwareError::unknown_device, "unknown disconnect");
    workbench::Telemetry telemetry;
    expect(manager.read_telemetry("missing-device", telemetry).error == workbench::HardwareError::unknown_device,
           "unknown telemetry");
    expect(telemetry.readings.empty(), "unknown telemetry writes no readings");
    expect(manager.arm_simulated_failure("", "connection_timeout").error == workbench::HardwareError::invalid_request,
           "empty failure device id");
    expect(manager.arm_simulated_failure(workbench::MockJtagAdapter::id(), "").error ==
               workbench::HardwareError::invalid_request,
           "empty failure id");
    expect(manager.arm_simulated_failure(workbench::MockMcuAdapter::id(), "connection_timeout").error ==
               workbench::HardwareError::unsupported_operation,
           "MCU rejects the JTAG failure");
}

void test_simulated_timeout_and_independence() {
    workbench::DeviceManager manager;
    expect(manager.connect(workbench::MockMcuAdapter::id()).ok(), "MCU connects before the timeout");
    expect(manager.connect(workbench::MockFpgaAdapter::id()).ok(), "FPGA connects before the timeout");

    const workbench::HardwareResult armed =
        manager.arm_simulated_failure(workbench::MockJtagAdapter::id(), workbench::MockJtagAdapter::timeout_failure_id());
    expect(armed.ok(), "JTAG timeout arms");
    expect(armed.connection == workbench::ConnectionState::disconnected, "arming does not connect the adapter");

    const workbench::HardwareResult timed_out = manager.connect(workbench::MockJtagAdapter::id());
    expect(timed_out.error == workbench::HardwareError::simulated_timeout, "JTAG connect times out");
    expect(timed_out.message.find("Simulated JTAG connection timeout") != std::string::npos, "timeout message");
    expect(timed_out.connection == workbench::ConnectionState::disconnected, "timeout leaves JTAG disconnected");
    expect(require_device(manager, workbench::MockJtagAdapter::id()).connection == workbench::ConnectionState::disconnected,
           "describe confirms the timeout state");

    workbench::Telemetry denied;
    expect(manager.read_telemetry(workbench::MockJtagAdapter::id(), denied).error == workbench::HardwareError::not_connected,
           "timed-out adapter has no telemetry");

    expect(require_device(manager, workbench::MockMcuAdapter::id()).connection == workbench::ConnectionState::connected,
           "MCU stays connected through the JTAG timeout");
    expect(require_device(manager, workbench::MockFpgaAdapter::id()).connection == workbench::ConnectionState::connected,
           "FPGA stays connected through the JTAG timeout");
    workbench::Telemetry mcu;
    expect(manager.read_telemetry(workbench::MockMcuAdapter::id(), mcu).ok(), "MCU telemetry survives the timeout");

    const workbench::HardwareResult recovered = manager.connect(workbench::MockJtagAdapter::id());
    expect(recovered.ok(), "JTAG connects after the simulated timeout");
    expect(recovered.connection == workbench::ConnectionState::connected, "recovery reports connected");
    workbench::Telemetry jtag;
    expect(manager.read_telemetry(workbench::MockJtagAdapter::id(), jtag).ok(), "JTAG telemetry after recovery");
    expect(find_reading(jtag, "Programming performed") != nullptr &&
               find_reading(jtag, "Programming performed")->value == "No",
           "recovery still does not claim programming");

    expect(manager.disconnect(workbench::MockFpgaAdapter::id()).ok(), "FPGA disconnects on its own");
    expect(require_device(manager, workbench::MockMcuAdapter::id()).connection == workbench::ConnectionState::connected,
           "MCU remains connected after FPGA disconnect");
    expect(require_device(manager, workbench::MockJtagAdapter::id()).connection == workbench::ConnectionState::connected,
           "JTAG remains connected after FPGA disconnect");
    expect(require_device(manager, workbench::MockFpgaAdapter::id()).connection == workbench::ConnectionState::disconnected,
           "FPGA is the only device disconnected");
}

void test_concurrent_connections() {
    workbench::DeviceManager manager;
    std::atomic<int> connected{0};
    std::atomic<int> duplicates{0};
    auto attempt = [&] {
        const workbench::HardwareResult result = manager.connect(workbench::MockMcuAdapter::id());
        if (result.ok()) connected.fetch_add(1);
        else if (result.error == workbench::HardwareError::already_connected) duplicates.fetch_add(1);
    };
    std::thread first(attempt);
    std::thread second(attempt);
    first.join();
    second.join();
    expect(connected.load() == 1, "one of two concurrent connects succeeds");
    expect(duplicates.load() == 1, "the other concurrent connect is rejected");
    expect(require_device(manager, workbench::MockMcuAdapter::id()).connection == workbench::ConnectionState::connected,
           "MCU is connected after the race");

    std::atomic<int> others{0};
    std::thread fpga([&] {
        if (manager.connect(workbench::MockFpgaAdapter::id()).ok()) others.fetch_add(1);
    });
    std::thread jtag([&] {
        if (manager.connect(workbench::MockJtagAdapter::id()).ok()) others.fetch_add(1);
    });
    fpga.join();
    jtag.join();
    expect(others.load() == 2, "FPGA and JTAG connect concurrently");
    expect(require_device(manager, workbench::MockFpgaAdapter::id()).connection == workbench::ConnectionState::connected,
           "FPGA connected from its own thread");
    expect(require_device(manager, workbench::MockJtagAdapter::id()).connection == workbench::ConnectionState::connected,
           "JTAG connected from its own thread");
}

void test_registration_guards() {
    bool duplicate = false;
    try {
        std::vector<std::unique_ptr<workbench::Device>> devices;
        devices.push_back(std::make_unique<NamedDevice>("same-id"));
        devices.push_back(std::make_unique<NamedDevice>("same-id"));
        workbench::DeviceManager manager(std::move(devices));
    } catch (const std::invalid_argument&) {
        duplicate = true;
    }
    expect(duplicate, "duplicate device ids are rejected");

    bool empty = false;
    try {
        std::vector<std::unique_ptr<workbench::Device>> devices;
        devices.push_back(nullptr);
        workbench::DeviceManager manager(std::move(devices));
    } catch (const std::invalid_argument&) {
        empty = true;
    }
    expect(empty, "a null device registration is rejected");
}

}  // namespace

int main() {
    try {
        test_discovery();
        test_connect_disconnect_and_telemetry();
        test_profile_readings();
        test_invalid_identifier();
        test_simulated_timeout_and_independence();
        test_concurrent_connections();
        test_registration_guards();
    } catch (const std::exception& error) {
        std::cerr << "TEST FAILURE: uncaught exception: " << error.what() << '\n';
        return 1;
    }
    if (g_failures != 0) {
        std::cerr << g_failures << " hardware test failure(s).\n";
        return 1;
    }
    std::cout << "All hardware tests passed.\n";
    return 0;
}
