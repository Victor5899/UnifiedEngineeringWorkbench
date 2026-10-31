import type { ReactNode } from "react";

export function EmptyState({
  title,
  detail,
  action,
}: {
  title: string;
  detail: string;
  action: ReactNode;
}) {
  return (
    <div className="pointer-events-none absolute inset-0 z-10 flex items-center justify-center p-6">
      <div className="pointer-events-auto max-w-md rounded-lg border border-ink-600 bg-ink-900/95 px-6 py-5 text-center">
        <h2 className="text-base font-semibold text-mist-100">{title}</h2>
        <p className="mt-2 text-sm leading-6 text-mist-300">{detail}</p>
        <div className="mt-4 flex justify-center">{action}</div>
      </div>
    </div>
  );
}

export function LoadingOverlay({ label }: { label: string }) {
  return (
    <div className="absolute inset-0 z-20 flex items-center justify-center bg-ink-950/55" role="status">
      <div className="flex items-center gap-3 rounded-md border border-ink-600 bg-ink-900 px-4 py-3">
        <span className="h-4 w-4 animate-spin rounded-full border-2 border-mist-500 border-t-accent" />
        <span className="text-sm text-mist-100">{label}</span>
      </div>
    </div>
  );
}

export function ErrorBanner({ message }: { message: string }) {
  return (
    <div className="pointer-events-none absolute inset-x-3 top-16 z-10" role="alert">
      <p className="rounded-md border border-danger/40 bg-ink-900/95 px-3 py-2 text-sm leading-5 text-danger">
        {message}
      </p>
    </div>
  );
}
