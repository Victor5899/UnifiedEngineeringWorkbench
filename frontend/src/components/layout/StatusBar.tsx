import type { BackendConnection, WorkspaceSummary } from "../../types/workspace";

function connectionLabel(connection: BackendConnection): string {
  if (connection === "connected") return "Connected";
  if (connection === "disconnected") return "Disconnected";
  return "Checking";
}

function phaseLabel(phase: WorkspaceSummary["phase"]): string {
  if (phase === "working") return "Parsing";
  if (phase === "success") return "Parsed";
  if (phase === "error") return "Failed";
  return "Idle";
}

export function StatusBar({
  summary,
  connection,
}: {
  summary: WorkspaceSummary;
  connection: BackendConnection;
}) {
  return (
    <footer className="flex h-9 shrink-0 items-center gap-4 overflow-hidden border-t border-ink-600 bg-ink-950 px-4 text-[11px] text-mist-500">
      <span className="shrink-0">Backend {connectionLabel(connection)}</span>
      <span className="hidden shrink-0 sm:inline">Dataset {summary.filename ?? "none"}</span>
      <span className="shrink-0">Parse {phaseLabel(summary.phase)}</span>
      <span className="min-w-0 truncate text-mist-300" role="status">
        {summary.message}
      </span>
    </footer>
  );
}
