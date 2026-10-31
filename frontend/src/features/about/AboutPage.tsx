import { InspectorPanel, InspectorSection, Field } from "../../components/inspector/InspectorPanel";
import { usePublishWorkspace } from "../../hooks/useWorkspace";
import type { BackendConnection } from "../../types/workspace";

export function AboutPage({
  active,
  connection,
  version,
}: {
  active: boolean;
  connection: BackendConnection;
  version: string | null;
}) {
  const connected = connection === "connected";
  usePublishWorkspace(
    active,
    null,
    connected ? "success" : "idle",
    connected ? "Backend connected." : "Backend is not connected.",
  );

  return (
    <div className={active ? "flex min-h-0 flex-1 flex-col overflow-hidden lg:flex-row" : "hidden"}>
      <section className="min-w-0 flex-1 overflow-y-auto p-6">
        <p className="text-xs font-semibold uppercase tracking-[0.16em] text-mist-500">About</p>
        <h2 className="mt-2 text-2xl font-semibold text-mist-100">Unified Engineering Workbench</h2>
        <p className="mt-3 max-w-2xl text-sm leading-6 text-mist-300">
          Geometry and temperature files are parsed by the C++17 engines. This interface uploads the file and
          draws the JSON result. It does not reimplement the STL or CSV parsers.
        </p>
        <div className="mt-6 grid max-w-3xl gap-3 md:grid-cols-2">
          <article className="rounded-md border border-ink-600 bg-ink-900 p-4">
            <h3 className="text-sm font-semibold text-mist-100">Geometry</h3>
            <p className="mt-2 text-sm leading-6 text-mist-300">
              ASCII STL is posted to <span className="mono text-mist-100">/api/geometry/stl</span>. The viewer
              draws the returned triangles with their face normals.
            </p>
          </article>
          <article className="rounded-md border border-ink-600 bg-ink-900 p-4">
            <h3 className="text-sm font-semibold text-mist-100">Thermal analysis</h3>
            <p className="mt-2 text-sm leading-6 text-mist-300">
              Nodal CSV is posted to <span className="mono text-mist-100">/api/thermal/csv</span>. Point colour
              uses the minimum and maximum temperature in that file.
            </p>
          </article>
          <article className="rounded-md border border-ink-600 bg-ink-900 p-4">
            <h3 className="text-sm font-semibold text-mist-100">Coordinates</h3>
            <p className="mt-2 text-sm leading-6 text-mist-300">
              STL coordinates are file units. Temperature coordinates are metres and temperatures are degrees
              Celsius. The point cloud is not registered to the STL triangles.
            </p>
          </article>
          <article className="rounded-md border border-ink-600 bg-ink-900 p-4">
            <h3 className="text-sm font-semibold text-mist-100">Local API</h3>
            <p className="mt-2 text-sm leading-6 text-mist-300">
              The development server proxies <span className="mono text-mist-100">/api</span> to
              127.0.0.1:8080. The backend binds to localhost unless another host is configured.
            </p>
          </article>
        </div>
      </section>
      <InspectorPanel title="Session">
        <InspectorSection title="Backend">
          <Field label="Connection" value={connection} />
          <Field label="Version" value={version ?? "unavailable"} />
          <Field label="Health" value="/api/health" />
        </InspectorSection>
        <p className="px-4 py-3 text-xs leading-5 text-mist-500">
          A Windows 11 x64 Launch.exe is still required for final delivery. This development build does not
          package that executable.
        </p>
      </InspectorPanel>
    </div>
  );
}
