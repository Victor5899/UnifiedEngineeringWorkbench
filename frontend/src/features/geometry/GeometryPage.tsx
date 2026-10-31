import { useMemo, useRef, useState } from "react";
import { Upload } from "lucide-react";
import { ViewportToolbar } from "../../components/controls/ViewportToolbar";
import { Field, InspectorPanel, InspectorSection, Note } from "../../components/inspector/InspectorPanel";
import { EmptyState, ErrorBanner, LoadingOverlay } from "../../components/viewport/ViewportChrome";
import { EngineeringViewport } from "../../components/viewport/EngineeringViewport";
import { useFilePicker } from "../../hooks/useFilePicker";
import { usePublishWorkspace } from "../../hooks/useWorkspace";
import { formatNumber, formatVec3 } from "../../lib/format";
import { ApiError, uploadStl } from "../../services/api";
import type { GeometryModel } from "../../types/api";
import { edgeOverlayTriangleLimit, GeometryMesh } from "./GeometryMesh";

export function GeometryPage({ active }: { active: boolean }) {
  const [model, setModel] = useState<GeometryModel | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const [fitRequest, setFitRequest] = useState(0);
  const [resetRequest, setResetRequest] = useState(0);
  const requestId = useRef(0);

  const phase = loading ? "working" : error ? "error" : model ? "success" : "idle";
  const message = loading
    ? "Parsing STL on the C++ backend."
    : error
      ? error
      : model
        ? `Parsed ${model.triangle_count} triangles.`
        : "No geometry loaded.";
  usePublishWorkspace(active, model?.filename ?? null, phase, message);

  const bounds = useMemo(() => model?.bounding_box ?? null, [model]);

  const handleFile = async (file: File) => {
    const id = ++requestId.current;
    setLoading(true);
    setError(null);
    try {
      const next = await uploadStl(file);
      if (requestId.current !== id) return;
      setModel(next);
      setResetRequest((value) => value + 1);
    } catch (caught) {
      if (requestId.current !== id) return;
      setError(caught instanceof ApiError ? caught.message : "Could not reach the backend. Start workbench_server and try again.");
    } finally {
      if (requestId.current === id) setLoading(false);
    }
  };

  const picker = useFilePicker(".stl,model/stl", (file) => {
    void handleFile(file);
  });

  return (
    <div className={active ? "flex min-h-0 flex-1 flex-col overflow-hidden lg:flex-row" : "hidden"}>
      {picker.input}
      <section className="relative min-h-0 min-w-0 flex-1">
        <EngineeringViewport bounds={bounds} fitRequest={fitRequest} resetRequest={resetRequest}>
          {model ? <GeometryMesh model={model} /> : null}
        </EngineeringViewport>
        <ViewportToolbar
          importLabel="Import STL"
          disabled={loading}
          viewDisabled={!model}
          onImport={picker.open}
          onFit={() => setFitRequest((value) => value + 1)}
          onReset={() => setResetRequest((value) => value + 1)}
        />
        {!model && !loading ? (
          <EmptyState
            title="Import an STL"
            detail="ASCII and binary STL are parsed by the C++ geometry engine. Triangle coordinates are displayed in the file's own units."
            action={
              <button type="button" className="btn btn-primary" onClick={picker.open}>
                <Upload size={15} />
                Choose STL
              </button>
            }
          />
        ) : null}
        {error ? <ErrorBanner message={error} /> : null}
        {loading ? <LoadingOverlay label="Parsing geometry" /> : null}
      </section>
      <GeometryInspector model={model} />
    </div>
  );
}

function GeometryInspector({ model }: { model: GeometryModel | null }) {
  return (
    <InspectorPanel title="Geometry inspector">
      {model ? (
        <>
          <InspectorSection title="File">
            <Field label="Name" value={model.filename} />
            <Field label="Solid" value={model.solid_name || "(unnamed)"} />
            <Field label="Triangles" value={formatNumber(model.triangle_count, 0)} />
            <Field label="Vertices" value={formatNumber(model.vertex_count, 0)} />
            <Field label="Units" value={model.coordinate_units} />
          </InspectorSection>
          <InspectorSection title="Bounding box">
            <Field label="Minimum" value={formatVec3(model.bounding_box.min)} />
            <Field label="Maximum" value={formatVec3(model.bounding_box.max)} />
          </InspectorSection>
          <InspectorSection title="Dimensions">
            <Field label="X" value={formatNumber(model.dimensions.x)} />
            <Field label="Y" value={formatNumber(model.dimensions.y)} />
            <Field label="Z" value={formatNumber(model.dimensions.z)} />
          </InspectorSection>
          <Note>
            STL does not store a unit. These coordinates are file units. The sample block was authored in metres.
            {model.triangle_count > edgeOverlayTriangleLimit
              ? " Edge lines are omitted above 100,000 triangles so the shaded surface stays responsive."
              : ""}
          </Note>
        </>
      ) : (
        <Note>Statistics appear after a file is parsed.</Note>
      )}
    </InspectorPanel>
  );
}
