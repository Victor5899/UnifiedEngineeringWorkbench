import { InspectorPanel } from "../../components/inspector/InspectorPanel";
import { usePublishWorkspace } from "../../hooks/useWorkspace";

export function PlannedModule({
  active,
  title,
  summary,
  items,
}: {
  active: boolean;
  title: string;
  summary: string;
  items: readonly string[];
}) {
  usePublishWorkspace(active, null, "idle", `${title} is not implemented in this phase.`);

  return (
    <div className={active ? "flex min-h-0 flex-1 flex-col overflow-hidden lg:flex-row" : "hidden"}>
      <section className="min-w-0 flex-1 overflow-y-auto p-6">
        <p className="text-xs font-semibold uppercase tracking-[0.16em] text-warn">Not implemented</p>
        <h2 className="mt-2 text-2xl font-semibold text-mist-100">{title}</h2>
        <p className="mt-3 max-w-2xl text-sm leading-6 text-mist-300">{summary}</p>
        <ul className="mt-6 max-w-2xl space-y-2">
          {items.map((item) => (
            <li key={item} className="rounded-md border border-ink-600 bg-ink-900 px-4 py-3 text-sm text-mist-300">
              <span className="mr-2 font-semibold text-warn">Planned</span>
              {item}
            </li>
          ))}
        </ul>
      </section>
      <InspectorPanel title="Module status">
        <p className="px-4 py-4 text-sm leading-6 text-mist-300">
          This module is a placeholder. It does not run analysis, connect to a device, or report results.
        </p>
      </InspectorPanel>
    </div>
  );
}
