import { RotateCcw, Scan, Upload } from "lucide-react";

export function ViewportToolbar({
  importLabel,
  disabled,
  viewDisabled,
  onImport,
  onFit,
  onReset,
}: {
  importLabel: string;
  disabled: boolean;
  viewDisabled: boolean;
  onImport: () => void;
  onFit: () => void;
  onReset: () => void;
}) {
  return (
    <div className="pointer-events-none absolute left-3 top-3 z-10 flex max-w-[calc(100%-1.5rem)] flex-wrap items-center gap-2">
      <button type="button" className="btn btn-primary pointer-events-auto" disabled={disabled} onClick={onImport}>
        <Upload size={15} />
        {importLabel}
      </button>
      <button type="button" className="btn pointer-events-auto" disabled={disabled || viewDisabled} onClick={onFit}>
        <Scan size={15} />
        Fit
      </button>
      <button type="button" className="btn pointer-events-auto" disabled={disabled || viewDisabled} onClick={onReset}>
        <RotateCcw size={15} />
        Reset
      </button>
      <p className="pointer-events-none hidden text-[11px] text-mist-500 lg:block">
        Left drag orbit · right drag pan · scroll zoom
      </p>
    </div>
  );
}
