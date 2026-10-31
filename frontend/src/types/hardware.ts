export type EmbeddedDeviceType = "microcontroller" | "fpga" | "jtag_adapter";
export type ConnectionState = "connected" | "disconnected";

export interface DeviceCapability {
  id: string;
  summary: string;
}

export interface EmbeddedDevice {
  id: string;
  device_type: EmbeddedDeviceType;
  display_name: string;
  reference_profile: string;
  manufacturer_reference: string;
  connection_state: ConnectionState;
  available: boolean;
  simulated: true;
  physical_hardware_connected: false;
  capabilities: DeviceCapability[];
}

export interface EmbeddedDeviceList {
  simulated: true;
  physical_hardware_connected: false;
  devices: EmbeddedDevice[];
}

export interface CapabilityDevice {
  id: string;
  device_type: EmbeddedDeviceType;
  display_name: string;
  capabilities: DeviceCapability[];
}

export interface CapabilityList {
  simulated: true;
  physical_hardware_connected: false;
  devices: CapabilityDevice[];
}

export interface HardwareCommandResult {
  simulated: true;
  physical_hardware_connected: false;
  device_id: string;
  connection_state: ConnectionState;
  message: string;
}

export interface TelemetryReading {
  name: string;
  value: string;
  unit: string;
  simulated: boolean;
}

export interface TelemetryReport {
  simulated: true;
  physical_hardware_connected: false;
  device_id: string;
  notice: string;
  readings: TelemetryReading[];
}

export interface SimulatedFailureArmed {
  simulated: true;
  physical_hardware_connected: false;
  device_id: string;
  failure: string;
  armed: true;
  connection_state: ConnectionState;
  message: string;
}
