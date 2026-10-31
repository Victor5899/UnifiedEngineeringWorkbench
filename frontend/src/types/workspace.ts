export type ModuleId = "geometry" | "thermal" | "performance" | "hardware" | "about";

export type StatusPhase = "idle" | "working" | "success" | "error";

export interface WorkspaceSummary {
  filename: string | null;
  phase: StatusPhase;
  message: string;
}

export type BackendConnection = "checking" | "connected" | "disconnected";
