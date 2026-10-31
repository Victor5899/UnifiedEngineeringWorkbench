#include "workbench/hardware/mock_jtag_adapter.hpp"

namespace workbench {
namespace {

DeviceInfo profile() {
    DeviceInfo info;
    info.id = MockJtagAdapter::id();
    info.type = DeviceType::jtag_adapter;
    info.display_name = "Digilent JTAG-HS2 class debug adapter";
    info.reference_profile = "Digilent JTAG-HS2 programming cable, USB to target TAP";
    info.manufacturer_reference =
        "Digilent JTAG-HS2 Reference Manual. The cable is USB bus-powered and is a programming and "
        "debug adapter for a Xilinx target. It is not a processor. Operating VREF is 1.65 V to 5.5 V. "
        "Absolute maximum VREF is -0.5 V to 6 V. The cable drives TMS, TCK, and TDI through series resistors "
        "and receives TDO. VREF must be the same supply that drives the target JTAG port. "
        "The 6-pin 100-mil header mates with Digilent programming headers such as Arty A7 J8. "
        "An included adaptor fits the Xilinx 2x7 2 mm header. That adaptor is a different connector.";
    info.available = true;
    info.simulated = true;
    info.capabilities = {
        {"debug_transport",
         "JTAG is a debugging and programming transport for a target test access port. "
         "This adapter does not execute application logic, and a simulated connection does not scan or program a device."},
        {"vref_referenced_buffers",
         "Documented operating VREF is 1.65 V to 5.5 V. Output levels for TMS, TCK, and TDI follow that reference. "
         "TDO is an input to the adapter. The reference is supplied by the target. The cable does not power the FPGA from VREF."},
        {"connector",
         "Mates with Digilent's 6-pin 100-mil programming header, or with Xilinx's 2x7 2 mm header through the included adaptor. "
         "Neither connector is the Arduino Uno ICSP header. A matching pin count is not electrical compatibility."},
        {"simulated_connection_timeout",
         "Arming failure id connection_timeout makes the next connect() return a simulated timeout. "
         "The adapter stays disconnected. The following connect() can succeed. Other devices are not changed."},
    };
    return info;
}

HardwareResult finish(HardwareError error, std::string message, ConnectionState state) {
    return HardwareResult{error, std::move(message), state};
}

}  // namespace

const char* MockJtagAdapter::id() { return "jtag-hs2"; }

const char* MockJtagAdapter::timeout_failure_id() { return "connection_timeout"; }

DeviceInfo MockJtagAdapter::describe() const {
    DeviceInfo info = profile();
    std::lock_guard<std::mutex> lock(mutex_);
    info.connection = state_;
    return info;
}

HardwareResult MockJtagAdapter::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == ConnectionState::connected) {
        return finish(HardwareError::already_connected,
                      "The simulated JTAG adapter is already connected.",
                      state_);
    }
    if (timeout_armed_) {
        timeout_armed_ = false;
        state_ = ConnectionState::disconnected;
        return finish(
            HardwareError::simulated_timeout,
            "Simulated JTAG connection timeout. No cable, target, or debug session was contacted. "
            "The adapter remains disconnected, and a later connection attempt can succeed.",
            state_);
    }
    state_ = ConnectionState::connected;
    return finish(
        HardwareError::ok,
        "Simulated JTAG adapter session established. No boundary scan, programming, or debug operation was performed.",
        state_);
}

HardwareResult MockJtagAdapter::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == ConnectionState::disconnected) {
        return finish(HardwareError::not_connected,
                      "The simulated JTAG adapter is already disconnected.",
                      state_);
    }
    state_ = ConnectionState::disconnected;
    return finish(HardwareError::ok,
                  "Simulated JTAG adapter session closed. No target was left in a debug state because none was accessed.",
                  state_);
}

HardwareResult MockJtagAdapter::read_telemetry(Telemetry& telemetry) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != ConnectionState::connected) {
        telemetry = Telemetry{};
        return finish(HardwareError::not_connected,
                      "Telemetry is available only while the simulated JTAG adapter is connected.",
                      state_);
    }
    telemetry.device_id = id();
    telemetry.simulated = true;
    telemetry.notice =
        "Simulated telemetry for a debug adapter, not a processing device. "
        "No boundary scan, programming, or processor halt was performed. "
        "3.3 V is the documented Arty A7 J8 reference rail (schematic net VCC3V3), repeated here as a label. "
        "It is not a measurement from a connected target.";
    telemetry.readings = {
        {"Role", "Debug and programming transport", "", true},
        {"Target voltage reference", "3.3", "V", true},
        {"Scan session", "None", "", true},
        {"Programming performed", "No", "", true},
        {"Status", "Online", "", true},
    };
    return finish(HardwareError::ok, "Simulated JTAG adapter telemetry.", state_);
}

HardwareResult MockJtagAdapter::arm_simulated_failure(const std::string& failure_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (failure_id != timeout_failure_id()) {
        return finish(HardwareError::invalid_request,
                      "The only simulated JTAG failure is connection_timeout.",
                      state_);
    }
    timeout_armed_ = true;
    return finish(
        HardwareError::ok,
        "Simulated JTAG connection timeout armed. The next connection attempt will fail and leave the adapter disconnected.",
        state_);
}

}  // namespace workbench
