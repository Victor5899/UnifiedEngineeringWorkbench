import { createContext, useContext, useEffect, useState, type ReactNode } from "react";
import { fetchHealth } from "../services/api";
import type { BackendConnection, StatusPhase, WorkspaceSummary } from "../types/workspace";

const WorkspaceDispatchContext = createContext<(summary: WorkspaceSummary) => void>(() => {});

export function WorkspaceDispatchProvider({
  children,
  publish,
}: {
  children: ReactNode;
  publish: (summary: WorkspaceSummary) => void;
}) {
  return <WorkspaceDispatchContext.Provider value={publish}>{children}</WorkspaceDispatchContext.Provider>;
}

export function usePublishWorkspace(
  active: boolean,
  filename: string | null,
  phase: StatusPhase,
  message: string,
): void {
  const publish = useContext(WorkspaceDispatchContext);
  useEffect(() => {
    if (!active) return;
    publish({ filename, phase, message });
  }, [active, filename, phase, message, publish]);
}

export function useBackendHealth(): { connection: BackendConnection; version: string | null } {
  const [connection, setConnection] = useState<BackendConnection>("checking");
  const [version, setVersion] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;

    const poll = async () => {
      const health = await fetchHealth();
      if (cancelled) return;
      if (health) {
        setConnection("connected");
        setVersion(health.version);
      } else {
        setConnection("disconnected");
        setVersion(null);
      }
    };

    void poll();
    const timer = window.setInterval(() => void poll(), 4000);
    return () => {
      cancelled = true;
      window.clearInterval(timer);
    };
  }, []);

  return { connection, version };
}
