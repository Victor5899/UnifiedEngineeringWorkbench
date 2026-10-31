import { PlannedModule } from "../planned/PlannedModule";

export function PerformancePage({ active }: { active: boolean }) {
  return (
    <PlannedModule
      active={active}
      title="Performance"
      summary="Serial and parallel batch benchmarks are planned for a later phase. This page does not time the solver and does not display performance results."
      items={[
        "Batch study over element counts",
        "Serial and parallel timing comparison",
        "Export of benchmark tables",
      ]}
    />
  );
}
