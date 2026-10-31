import type { ReactNode } from "react";

export function InspectorPanel({ title, children }: { title: string; children: ReactNode }) {
  return (
    <aside className="flex h-52 w-full shrink-0 flex-col overflow-hidden border-t border-ink-600 bg-ink-900 lg:h-auto lg:min-h-0 lg:w-[22rem] lg:border-l lg:border-t-0">
      <div className="border-b border-ink-600 px-4 py-3">
        <h2 className="text-sm font-semibold text-mist-100">{title}</h2>
      </div>
      <div className="min-h-0 flex-1 overflow-y-auto">{children}</div>
    </aside>
  );
}

export function InspectorSection({ title, children }: { title: string; children: ReactNode }) {
  return (
    <section className="border-b border-ink-700 px-4 py-3">
      <h3 className="mb-1 text-[11px] font-semibold uppercase tracking-[0.16em] text-mist-500">{title}</h3>
      <dl>{children}</dl>
    </section>
  );
}

export function Field({ label, value }: { label: string; value: string }) {
  return (
    <div className="grid grid-cols-[minmax(0,1fr)_auto] items-baseline gap-3 border-b border-ink-700/70 py-2 last:border-b-0">
      <dt className="text-xs text-mist-500">{label}</dt>
      <dd className="mono text-right text-sm text-mist-100 break-all">{value}</dd>
    </div>
  );
}

export function Note({ children }: { children: ReactNode }) {
  return <p className="px-4 py-3 text-xs leading-5 text-mist-500">{children}</p>;
}
