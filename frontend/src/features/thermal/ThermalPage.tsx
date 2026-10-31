import { useMemo, useRef, useState } from "react";
import { Upload } from "lucide-react";
import { ViewportToolbar } from "../../components/controls/ViewportToolbar";
import { Field, InspectorPanel, InspectorSection, Note } from "../../components/inspector/InspectorPanel";
import { EngineeringViewport } from "../../components/viewport/EngineeringViewport";
import { EmptyState, ErrorBanner, LoadingOverlay } from "../../components/viewport/ViewportChrome";
import { useFilePicker } from "../../hooks/useFilePicker";
import { usePublishWorkspace } from "../../hooks/useWorkspace";
import { temperatureCss, temperatureGradientCss } from "../../lib/colorScale";
import { formatNodeId, formatNumber, formatVec3 } from "../../lib/format";
import { ApiError, uploadTemperatureCsv } from "../../services/api";
import type { TemperatureModel, TemperatureNode } from "../../types/api";
import { ThermalCloud } from "./ThermalCloud";

export function ThermalPage({ active }: { active: boolean }) {
  const [model, setModel] = useState<TemperatureModel | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const [fitRequest, setFitRequest] = useState(0);
  const [resetRequest, setResetRequest] = useState(0);
  const [selectedIndex, setSelectedIndex] = useState<number | null>(null);
  const [hoverIndex, setHoverIndex] = useState<number | null>(null);
  const requestId = useRef(0);

  const phase = loading ? "working" : error ? "error" : model ? "success" : "idle";
  const message = loading
    ? "Parsing the temperature CSV on the C++ backend."
    : error
      ? error
      : model
        ? `Parsed ${model.node_count} nodes.`
        : "No temperature data loaded.";
  usePublishWorkspace(active, model?.filename ?? null, phase, message);

  const bounds = useMemo(() => model?.bounding_box ?? null, [model]);
  const selected = selectedIndex !== null ? model?.nodes[selectedIndex] ?? null : null;
  const hovered = hoverIndex !== null ? model?.nodes[hoverIndex] ?? null : null;

  const handleFile = async (file: File) => {
    const id = ++requestId.current;
    setLoading(true);
    setError(null);
    try {
      const next = await uploadTemperatureCsv(file);
      if (requestId.current !== id) return;
      setModel(next);
      setSelectedIndex(null);
      setHoverIndex(null);
      setResetRequest((value) => value + 1);
    } catch (caught) {
      if (requestId.current !== id) return;
      setError(caught instanceof ApiError ? caught.message : "Could not reach the backend. Start workbench_server and try again.");
    } finally {
      if (requestId.current === id) setLoading(false);
    }
  };

  const picker = useFilePicker(".csv,text/csv", (file) => {
    void handleFile(file);
  });

  return (
    <div className={active ? "flex min-h-0 flex-1 flex-col overflow-hidden lg:flex-row" : "hidden"}>
      {picker.input}
      <section className="relative min-h-0 min-w-0 flex-1">
        <EngineeringViewport bounds={bounds} fitRequest={fitRequest} resetRequest={resetRequest}>
          {model ? (
            <ThermalCloud
              model={model}
              selectedIndex={selectedIndex}
              onSelect={setSelectedIndex}
              onHover={setHoverIndex}
            />
          ) : null}
        </EngineeringViewport>
        <ViewportToolbar
          importLabel="Import CSV"
          disabled={loading}
          viewDisabled={!model}
          onImport={picker.open}
          onFit={() => setFitRequest((value) => value + 1)}
          onReset={() => setResetRequest((value) => value + 1)}
        />
        {!model && !loading ? (
          <EmptyState
            title="Import a temperature CSV"
            detail="Nodes are parsed by the C++ importer. Each point is coloured from the temperature range in the file. The cloud is not registered to an STL mesh."
            action={
              <button type="button" className="btn btn-primary" onClick={picker.open}>
                <Upload size={15} />
                Choose CSV
              </button>
            }
          />
        ) : null}
        {model ? <TemperatureLegend model={model} /> : null}
        {hovered ? <HoverReadout node={hovered} /> : null}
        {error ? <ErrorBanner message={error} /> : null}
        {loading ? <LoadingOverlay label="Parsing temperatures" /> : null}
      </section>
      <ThermalInspector model={model} selected={selected} onClear={() => setSelectedIndex(null)} />
    </div>
  );
}

function HoverReadout({ node }: { node: TemperatureNode }) {
  return (
    <div className="pointer-events-none absolute bottom-3 left-3 z-10 rounded-md border border-ink-600 bg-ink-900/90 px-3 py-2 text-xs text-mist-100">
      <span className="mono">Node {formatNodeId(node.node_id)}</span>
      <span className="mx-2 text-mist-500">·</span>
      <span className="mono">{formatNumber(node.temperature_C, 2)} °C</span>
    </div>
  );
}

function TemperatureLegend({ model }: { model: TemperatureModel }) {
  const midpoint = (model.minimum_temperature_C + model.maximum_temperature_C) / 2;
  return (
    <div className="pointer-events-none absolute right-3 top-14 z-10 w-28 rounded-md border border-ink-600 bg-ink-900/92 p-3">
      <p className="mb-2 text-[10px] uppercase tracking-[0.14em] text-mist-500">Temperature °C</p>
      <div className="flex gap-2">
        <div className="h-36 w-3 rounded-sm" style={{ background: temperatureGradientCss() }} />
        <div className="mono flex h-36 flex-col justify-between text-[11px] text-mist-100">
          <span>{formatNumber(model.maximum_temperature_C, 2)}</span>
          <span>{formatNumber(midpoint, 2)}</span>
          <span>{formatNumber(model.minimum_temperature_C, 2)}</span>
        </div>
      </div>
      <p className="mono mt-2 text-[11px] text-mist-300">avg {formatNumber(model.average_temperature_C, 2)}</p>
    </div>
  );
}

function ThermalInspector({
  model,
  selected,
  onClear,
}: {
  model: TemperatureModel | null;
  selected: TemperatureNode | null;
  onClear: () => void;
}) {
  const swatch = model && selected
    ? temperatureCss(selected.temperature_C, model.minimum_temperature_C, model.maximum_temperature_C)
    : null;
  return (
    <InspectorPanel title="Thermal inspector">
      {model ? (
        <>
          <InspectorSection title="File">
            <Field label="Name" value={model.filename} />
            <Field label="Nodes" value={formatNumber(model.node_count, 0)} />
          </InspectorSection>
          <InspectorSection title="Temperature">
            <Field label="Minimum (°C)" value={formatNumber(model.minimum_temperature_C, 4)} />
            <Field label="Maximum (°C)" value={formatNumber(model.maximum_temperature_C, 4)} />
            <Field label="Average (°C)" value={formatNumber(model.average_temperature_C, 4)} />
          </InspectorSection>
          <InspectorSection title="Bounding box (m)">
            <Field label="Minimum" value={formatVec3(model.bounding_box.min)} />
            <Field label="Maximum" value={formatVec3(model.bounding_box.max)} />
            <Field label="X" value={formatNumber(model.dimensions.x)} />
            <Field label="Y" value={formatNumber(model.dimensions.y)} />
            <Field label="Z" value={formatNumber(model.dimensions.z)} />
          </InspectorSection>
          <InspectorSection title="Selected node">
            {selected ? (
              <>
                <Field label="Node ID" value={formatNodeId(selected.node_id)} />
                <Field label="X (m)" value={formatNumber(selected.x_m)} />
                <Field label="Y (m)" value={formatNumber(selected.y_m)} />
                <Field label="Z (m)" value={formatNumber(selected.z_m)} />
                <Field label="Temperature (°C)" value={formatNumber(selected.temperature_C, 4)} />
              </>
            ) : (
              <p className="py-2 text-xs text-mist-500">Click a node to inspect it. Hover shows a short readout.</p>
            )}
          </InspectorSection>
          {selected && swatch ? (
            <div className="flex items-center justify-between px-4 py-3">
              <span className="flex items-center gap-2 text-xs text-mist-300">
                <span className="h-3 w-3 rounded-sm border border-ink-600" style={{ background: swatch }} />
                Scale colour
              </span>
              <button type="button" className="btn" onClick={onClear}>
                Clear
              </button>
            </div>
          ) : null}
          <Note>
            Coordinates are metres and temperatures are degrees Celsius. This point cloud is not registered to an STL mesh.
          </Note>
        </>
      ) : (
        <Note>Node statistics appear after a CSV is parsed.</Note>
      )}
    </InspectorPanel>
  );
}
