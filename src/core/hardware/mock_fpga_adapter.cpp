#include "workbench/hardware/mock_fpga_adapter.hpp"

namespace workbench {
namespace {

DeviceInfo profile() {
    DeviceInfo info;
    info.id = MockFpgaAdapter::id();
    info.type = DeviceType::fpga;
    info.display_name = "Digilent Arty A7-100 class FPGA board";
    info.reference_profile = "Digilent Arty A7-100, XC7A100TCSG324-1, 3.3 V FPGA I/O";
    info.manufacturer_reference =
        "Digilent Arty A7 Reference Manual. Part XC7A100TCSG324-1. "
        "Published purchasing table: 15850 look-up tables, 126800 flip-flops, 607.5 KB block RAM, "
        "6 clock-management tiles. FPGA I/O, Pmods, LEDs, buttons, and switches are on the 3.3 V rail. "
        "Digilent warns that shield or Pmod pins must not be driven above 3.4 V. "
        "Board power is 5 V from USB port J10, or 7 V to 15 V on jack J13 or header J7 pin 8. "
        "External JTAG uses header J8, for example a Digilent JTAG-HS2. "
        "Xilinx DS181 lists 15850 slices for the XC7A100T and publishes block RAM as 4860 Kb, "
        "which is the same 607.5 KB. Digilent's look-up-table label matches the slice count, not a separate CLB LUT total.";
    info.available = true;
    info.simulated = true;
    info.capabilities = {
        {"catalog_logic_resources",
         "Digilent purchasing table for the XC7A100TCSG324-1: 15850 look-up tables, 126800 flip-flops, "
         "607.5 KB block RAM, and 6 clock-management tiles. These are catalog figures. "
         "This adapter does not configure the FPGA and does not count placed logic."},
        {"io_voltage_domain_3v3",
         "Documented FPGA I/O domain is 3.3 V. The reference manual assigns the 3.3 V rail to FPGA I/O, "
         "clocks, flash, Pmods, LEDs, buttons, switches, USB, and Ethernet. "
         "Shield pin IOREF is connected to 3.3 V. The 5V0 shield pin is a power rail, not a 5 V logic level."},
        {"host_usb",
         "The reference board uses micro-USB J10 for onboard USB-JTAG and USB-UART. "
         "This mock does not open that port and does not load a bitstream."},
        {"external_jtag_header",
         "Header J8 is the external programming header named in the reference manual for a JTAG-HS2-class cable. "
         "That cable is a separate device. The manual specifies board power from J10 or a 7 V to 15 V supply, "
         "not from the JTAG header. Schematic net names for J8 are listed in docs/embedded_integration.md."},
    };
    return info;
}

HardwareResult finish(HardwareError error, std::string message, ConnectionState state) {
    return HardwareResult{error, std::move(message), state};
}

}  // namespace

const char* MockFpgaAdapter::id() { return "fpga-arty-a7-100"; }

DeviceInfo MockFpgaAdapter::describe() const {
    DeviceInfo info = profile();
    std::lock_guard<std::mutex> lock(mutex_);
    info.connection = state_;
    return info;
}

HardwareResult MockFpgaAdapter::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == ConnectionState::connected) {
        return finish(HardwareError::already_connected,
                      "The simulated FPGA board is already connected.",
                      state_);
    }
    state_ = ConnectionState::connected;
    return finish(HardwareError::ok,
                  "Simulated FPGA board session established. No bitstream was loaded and no FPGA logic is running.",
                  state_);
}

HardwareResult MockFpgaAdapter::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == ConnectionState::disconnected) {
        return finish(HardwareError::not_connected,
                      "The simulated FPGA board is already disconnected.",
                      state_);
    }
    state_ = ConnectionState::disconnected;
    return finish(HardwareError::ok,
                  "Simulated FPGA board session closed. No hardware was programmed.",
                  state_);
}

HardwareResult MockFpgaAdapter::read_telemetry(Telemetry& telemetry) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != ConnectionState::connected) {
        telemetry = Telemetry{};
        return finish(HardwareError::not_connected,
                      "Telemetry is available only while the simulated FPGA board is connected.",
                      state_);
    }
    telemetry.device_id = id();
    telemetry.simulated = true;
    telemetry.notice =
        "Simulated telemetry. No physical FPGA is attached. "
        "The 3.3 V figure repeats the documented I/O rail. It is not an XADC sample. "
        "Catalog counts are the Digilent purchasing table, not a measurement of configured logic. "
        "This message does not mean a bitstream was programmed or that FPGA logic is executing.";
    telemetry.readings = {
        {"I/O voltage domain", "3.3", "V", true},
        {"Configuration", "Unconfigured. No bitstream loaded.", "", true},
        {"Catalog look-up tables", "15850", "count", true},
        {"Catalog flip-flops", "126800", "count", true},
        {"Catalog block RAM", "607.5", "KB", true},
        {"Status", "Online", "", true},
    };
    return finish(HardwareError::ok, "Simulated FPGA board telemetry.", state_);
}

}  // namespace workbench
