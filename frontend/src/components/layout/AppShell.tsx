import type { ReactNode } from "react";
import type { BackendConnection, ModuleId, WorkspaceSummary } from "../../types/workspace";
import { Sidebar } from "./Sidebar";
import { StatusBar } from "./StatusBar";
import { TopBar } from "./TopBar";

export function AppShell({
  module,
  onModule,
  summary,
  connection,
  children,
}: {
  module: ModuleId;
  onModule: (module: ModuleId) => void;
  summary: WorkspaceSummary;
  connection: BackendConnection;
  children: ReactNode;
}) {
  return (
    <div className="flex h-screen flex-col overflow-hidden bg-ink-950 text-mist-100 md:flex-row">
      <Sidebar current={module} onSelect={onModule} />
      <div className="flex min-h-0 min-w-0 flex-1 flex-col">
        <TopBar module={module} summary={summary} connection={connection} />
        <div className="flex min-h-0 flex-1 flex-col">{children}</div>
        <StatusBar summary={summary} connection={connection} />
      </div>
    </div>
  );
}
