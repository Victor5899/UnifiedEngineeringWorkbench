#include "workbench/hardware/mock_mcu_adapter.hpp"

namespace workbench {
namespace {

DeviceInfo profile() {
    DeviceInfo info;
    info.id = MockMcuAdapter::id();
    info.type = DeviceType::microcontroller;
    info.display_name = "Arduino Uno Rev3 class microcontroller";
    info.reference_profile = "Arduino Uno Rev3, ATmega328P, 5 V logic";
    info.manufacturer_reference =
        "Arduino Uno Rev3 product specification and A000066 datasheet. "
        "Operating voltage 5 V. Digital I/O operates at 5 V. "
        "14 digital I/O pins, of which 6 provide PWM. 6 analog inputs. "
        "Recommended DC current 20 mA per I/O pin. 16 MHz clock. "
        "32 KB flash, of which 0.5 KB is used by the bootloader, 2 KB SRAM, 1 KB EEPROM.";
    info.available = true;
    info.simulated = true;
    info.capabilities = {
        {"usb_serial",
         "Host link on the reference board is USB serial through the onboard USB-to-serial bridge. "
         "This adapter does not open a serial port or a USB device."},
        {"logic_voltage_5v",
         "Documented operating voltage is 5 V, and the digital pins operate at 5 V. "
         "The board also has a 3.3 V output pin rated 50 mA. That pin is a supply output, not the logic domain. "
         "IOREF on the Uno Rev3 header is tied to 5 V."},
        {"gpio_and_analog",
         "Reference capacity is 14 digital I/O pins and 6 analog inputs. "
         "Recommended pin current is 20 mA. Arduino's board text says 40 mA on an I/O pin must not be exceeded."},
        {"no_jtag_tap",
         "This ATmega328P-class profile has no IEEE 1149.1 JTAG port. "
         "The Uno ICSP header is an SPI programming header and is not interchangeable with the FPGA JTAG header."},
    };
    return info;
}

HardwareResult finish(HardwareError error, std::string message, ConnectionState state) {
    return HardwareResult{error, std::move(message), state};
}

}  // namespace

const char* MockMcuAdapter::id() { return "mcu-uno-rev3"; }

DeviceInfo MockMcuAdapter::describe() const {
    DeviceInfo info = profile();
    std::lock_guard<std::mutex> lock(mutex_);
    info.connection = state_;
    return info;
}

HardwareResult MockMcuAdapter::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == ConnectionState::connected) {
        return finish(HardwareError::already_connected,
                      "The simulated microcontroller is already connected.",
                      state_);
    }
    state_ = ConnectionState::connected;
    return finish(HardwareError::ok,
                  "Simulated microcontroller connection established. No physical board was contacted.",
                  state_);
}

HardwareResult MockMcuAdapter::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == ConnectionState::disconnected) {
        return finish(HardwareError::not_connected,
                      "The simulated microcontroller is already disconnected.",
                      state_);
    }
    state_ = ConnectionState::disconnected;
    return finish(HardwareError::ok,
                  "Simulated microcontroller disconnected. No physical board was contacted.",
                  state_);
}

HardwareResult MockMcuAdapter::read_telemetry(Telemetry& telemetry) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != ConnectionState::connected) {
        telemetry = Telemetry{};
        return finish(HardwareError::not_connected,
                      "Telemetry is available only while the simulated microcontroller is connected.",
                      state_);
    }
    telemetry.device_id = id();
    telemetry.simulated = true;
    telemetry.notice =
        "Simulated telemetry. No physical microcontroller is attached. "
        "28.5 C is an illustrative constant, not a sensor specification and not a measurement. "
        "5.0 V repeats the documented operating voltage. It is not an ADC reading.";
    telemetry.readings = {
        {"Temperature", "28.5", "C", true},
        {"Supply voltage", "5.0", "V", true},
        {"Status", "Online", "", true},
    };
    return finish(HardwareError::ok, "Simulated microcontroller telemetry.", state_);
}

}  // namespace workbench
