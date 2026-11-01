import { useCallback, useState } from "react";
import { AppShell } from "./components/layout/AppShell";
import { AboutPage } from "./features/about/AboutPage";
import { BackpropPage } from "./features/backprop/BackpropPage";
import { GeometryPage } from "./features/geometry/GeometryPage";
import { HardwarePage } from "./features/hardware/HardwarePage";
import { PerformancePage } from "./features/performance/PerformancePage";
import { ThermalPage } from "./features/thermal/ThermalPage";
import { useBackendHealth, WorkspaceDispatchProvider } from "./hooks/useWorkspace";
import type { ModuleId, WorkspaceSummary } from "./types/workspace";

export function App() {
  const [module, setModule] = useState<ModuleId>("geometry");
  const health = useBackendHealth();
  const [summary, setSummary] = useState<WorkspaceSummary>({
    filename: null,
    phase: "idle",
    message: "No geometry loaded.",
  });
  const publish = useCallback((next: WorkspaceSummary) => {
    setSummary((current) =>
      current.filename === next.filename && current.phase === next.phase && current.message === next.message
        ? current
        : next,
    );
  }, []);

  return (
    <WorkspaceDispatchProvider publish={publish}>
      <AppShell module={module} onModule={setModule} summary={summary} connection={health.connection}>
        <GeometryPage active={module === "geometry"} />
        <ThermalPage active={module === "thermal"} />
        <PerformancePage active={module === "performance"} />
        <HardwarePage active={module === "hardware"} />
        <BackpropPage active={module === "backprop"} />
        <AboutPage active={module === "about"} connection={health.connection} version={health.version} />
      </AppShell>
    </WorkspaceDispatchProvider>
  );
}
