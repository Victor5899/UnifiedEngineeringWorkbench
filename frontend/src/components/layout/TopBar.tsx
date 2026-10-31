import type { BackendConnection, ModuleId, WorkspaceSummary } from "../../types/workspace";
import { moduleById } from "../../features/modules";

function connectionLabel(connection: BackendConnection): string {
  if (connection === "connected") return "Backend connected";
  if (connection === "disconnected") return "Backend disconnected";
  return "Checking backend";
}

function phaseLabel(phase: WorkspaceSummary["phase"]): string {
  if (phase === "working") return "Working";
  if (phase === "success") return "Ready";
  if (phase === "error") return "Error";
  return "Idle";
}

export function TopBar({
  module,
  summary,
  connection,
}: {
  module: ModuleId;
  summary: WorkspaceSummary;
  connection: BackendConnection;
}) {
  const dot =
    connection === "connected" ? "bg-ok" : connection === "disconnected" ? "bg-danger" : "bg-warn";
  return (
    <header className="flex h-14 shrink-0 items-center justify-between gap-4 border-b border-ink-600 bg-ink-900 px-4">
      <div className="min-w-0">
        <h1 className="truncate text-sm font-semibold text-mist-100">{moduleById(module).label}</h1>
        <p className="truncate text-xs text-mist-500">{summary.filename ?? "No file loaded"}</p>
      </div>
      <div className="flex shrink-0 items-center gap-4">
        <p className="hidden text-xs text-mist-300 sm:block">{phaseLabel(summary.phase)}</p>
        <p className="flex items-center gap-2 text-xs text-mist-300" aria-live="polite">
          <span className={`h-2 w-2 rounded-full ${dot}`} />
          {connectionLabel(connection)}
        </p>
      </div>
    </header>
  );
}
