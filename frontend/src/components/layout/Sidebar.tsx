import type { ModuleId } from "../../types/workspace";
import { modules } from "../../features/modules";

export function Sidebar({
  current,
  onSelect,
}: {
  current: ModuleId;
  onSelect: (module: ModuleId) => void;
}) {
  return (
    <aside className="flex w-full shrink-0 flex-col border-b border-ink-600 bg-ink-900 md:w-60 md:border-b-0 md:border-r">
      <div className="flex items-center gap-3 px-4 py-4">
        <div className="grid h-10 w-10 shrink-0 place-items-center rounded-md border border-accent/30 bg-accent/10">
          <svg viewBox="0 0 32 32" className="h-5 w-5" aria-hidden="true">
            <path d="M16 5 L27 11 V21 L16 27 L5 21 V11 Z" fill="none" stroke="#3ecfb2" strokeWidth="1.6" />
            <path d="M16 16 L16 27 M16 16 L5 11 M16 16 L27 11" fill="none" stroke="#8fb8c9" strokeWidth="1.2" />
          </svg>
        </div>
        <div className="min-w-0">
          <p className="text-[11px] uppercase tracking-[0.16em] text-mist-500">Workbench</p>
          <p className="truncate text-sm font-semibold text-mist-100">Unified Engineering</p>
        </div>
      </div>
      <nav className="flex gap-1 overflow-x-auto px-2 pb-3 md:flex-col md:px-3" aria-label="Modules">
        {modules.map((entry) => {
          const Icon = entry.icon;
          const active = entry.id === current;
          return (
            <button
              key={entry.id}
              type="button"
              aria-current={active ? "page" : undefined}
              className={`flex min-w-[9.5rem] items-center gap-3 rounded-md px-3 py-2 text-left md:min-w-0 ${
                active ? "bg-ink-800 text-mist-100" : "text-mist-300 hover:bg-ink-800/70"
              }`}
              onClick={() => onSelect(entry.id)}
            >
              <Icon size={16} className={active ? "text-accent" : "text-mist-500"} />
              <span className="min-w-0 flex-1">
                <span className="block text-sm">{entry.label}</span>
                <span className="hidden text-[11px] text-mist-500 md:block">
                  {entry.implemented ? entry.description : "Planned"}
                </span>
              </span>
            </button>
          );
        })}
      </nav>
    </aside>
  );
}
