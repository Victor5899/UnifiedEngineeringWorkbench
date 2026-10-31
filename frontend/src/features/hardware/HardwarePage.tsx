import { PlannedModule } from "../planned/PlannedModule";

export function HardwarePage({ active }: { active: boolean }) {
  return (
    <PlannedModule
      active={active}
      title="Hardware"
      summary="Embedded hardware adapters are planned for a later phase. This page does not detect devices and does not read telemetry."
      items={["Mock embedded adapters", "Device connection status", "Hardware telemetry"]}
    />
  );
}
