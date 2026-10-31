import { useEffect, useMemo, useState } from "react";
import { Download, Play } from "lucide-react";
import { Field, InspectorPanel, InspectorSection, Note } from "../../components/inspector/InspectorPanel";
import { usePublishWorkspace } from "../../hooks/useWorkspace";
import { ApiError, downloadBenchmarkCsv, fetchBenchmarkResults, fetchBenchmarkStatus, fetchHardware, startBenchmark } from "../../services/api";
import type { BenchmarkResult, BenchmarkStatus, HardwareInfo } from "../../types/performance";
import { BenchmarkChart } from "./BenchmarkCharts";

const inputClass =
  "mt-1 w-full rounded-md border border-ink-600 bg-ink-800 px-3 py-1.5 text-sm text-mist-100 outline-none focus:border-accent";

export function PerformancePage({ active }: { active: boolean }) {
  const [hardware, setHardware] = useState<HardwareInfo | null>(null);
  const [status, setStatus] = useState<BenchmarkStatus | null>(null);
  const [result, setResult] = useState<BenchmarkResult | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [watching, setWatching] = useState(false);
  const [submitting, setSubmitting] = useState(false);
  const [seed, setSeed] = useState("42");
  const [batchSize, setBatchSize] = useState("8");
  const [meshSize, setMeshSize] = useState("64");
  const [repetitions, setRepetitions] = useState("5");
  const [workers, setWorkers] = useState<number[]>([]);

  const running = watching || status?.state === "running" || submitting;
  const phase = error ? "error" : running ? "working" : result ? "success" : "idle";
  const message = error
    ? error
    : running
      ? status?.message || "Benchmark is running on the C++ backend."
      : result
        ? `Benchmark ${result.benchmark_id} completed.`
        : "Configure a thermal batch and start a benchmark.";
  usePublishWorkspace(active, result?.benchmark_id ?? null, phase, message);

  useEffect(() => {
    if (!active) return;
    let cancelled = false;
    const load = async () => {
      try {
        const [nextHardware, nextStatus] = await Promise.all([fetchHardware(), fetchBenchmarkStatus()]);
        if (cancelled) return;
        setHardware(nextHardware);
        setStatus(nextStatus);
        setWorkers((current) =>
          current.length > 0
            ? current.filter((count) => count <= nextHardware.maximum_worker_count)
            : Array.from({ length: nextHardware.maximum_worker_count }, (_, index) => index + 1),
        );
        if (nextStatus.state === "running") setWatching(true);
        if (nextStatus.state === "completed") {
          setResult(await fetchBenchmarkResults());
        }
        if (nextStatus.state === "failed" && nextStatus.error) setError(nextStatus.error);
      } catch (caught) {
        if (!cancelled) setError(caught instanceof ApiError ? caught.message : "Could not reach the backend.");
      }
    };
    void load();
    return () => {
      cancelled = true;
    };
  }, [active]);

  useEffect(() => {
    if (!watching) return;
    let cancelled = false;
    const tick = async () => {
      try {
        const next = await fetchBenchmarkStatus();
        if (cancelled) return;
        setStatus(next);
        if (next.state === "completed") {
          setResult(await fetchBenchmarkResults());
          setError(null);
          setWatching(false);
        } else if (next.state === "failed") {
          setError(next.error ?? "The benchmark failed.");
          setWatching(false);
        }
      } catch (caught) {
        if (!cancelled) setError(caught instanceof ApiError ? caught.message : "Could not reach the backend.");
      }
    };
    void tick();
    const timer = window.setInterval(() => void tick(), 400);
    return () => {
      cancelled = true;
      window.clearInterval(timer);
    };
  }, [watching]);

  const progress = useMemo(() => {
    if (!status || status.total_steps <= 0) return 0;
    const caseFraction = status.cases_total > 0 ? Math.min(1, status.cases_completed / status.cases_total) : 0;
    const current = status.state === "running" ? caseFraction : 0;
    return Math.max(0, Math.min(1, (status.completed_steps + current) / status.total_steps));
  }, [status]);

  const summary = useMemo(() => summarize(result), [result]);

  const toggleWorker = (count: number) => {
    setWorkers((current) =>
      current.includes(count) ? current.filter((value) => value !== count) : [...current, count].sort((a, b) => a - b),
    );
  };

  const onStart = async () => {
    const request = readRequest(seed, batchSize, meshSize, repetitions, workers);
    if (typeof request === "string") {
      setError(request);
      return;
    }
    setSubmitting(true);
    setError(null);
    setResult(null);
    try {
      await startBenchmark(request);
      setWatching(true);
    } catch (caught) {
      setError(caught instanceof ApiError ? caught.message : "Could not reach the backend.");
    } finally {
      setSubmitting(false);
    }
  };

  const onExport = async () => {
    try {
      const file = await downloadBenchmarkCsv();
      const blob = new Blob([file.csv], { type: "text/csv;charset=utf-8" });
      const url = URL.createObjectURL(blob);
      const link = document.createElement("a");
      link.href = url;
      link.download = file.filename;
      link.click();
      URL.revokeObjectURL(url);
    } catch (caught) {
      setError(caught instanceof ApiError ? caught.message : "Could not download the CSV.");
    }
  };

  return (
    <div className={active ? "flex min-h-0 flex-1 flex-col overflow-hidden lg:flex-row" : "hidden"}>
      <section className="min-w-0 flex-1 overflow-y-auto p-6">
        <p className="text-xs font-semibold uppercase tracking-[0.16em] text-mist-500">Performance lab</p>
        <h2 className="mt-2 text-2xl font-semibold text-mist-100">Thermal batch benchmark</h2>
        <p className="mt-3 max-w-3xl text-sm leading-6 text-mist-300">
          Each case is the existing 1D steady thermal solver. One seeded workload is solved serially and again
          on a worker pool. Speedup is the serial median divided by the parallel median. The page polls the
          backend, so this view stays responsive while the solves run.
        </p>

        {error ? (
          <p className="mt-4 max-w-3xl rounded-md border border-danger/40 bg-ink-900 px-3 py-2 text-sm text-danger" role="alert">
            {error}
          </p>
        ) : null}

        <div className="mt-6 grid max-w-4xl gap-3 sm:grid-cols-2 xl:grid-cols-4">
          <label className="text-xs text-mist-500">
            Batch size
            <input className={inputClass} inputMode="numeric" value={batchSize} disabled={running} onChange={(event) => setBatchSize(event.target.value)} />
          </label>
          <label className="text-xs text-mist-500">
            Mesh size (elements)
            <input className={inputClass} inputMode="numeric" value={meshSize} disabled={running} onChange={(event) => setMeshSize(event.target.value)} />
          </label>
          <label className="text-xs text-mist-500">
            Random seed
            <input className={inputClass} inputMode="numeric" value={seed} disabled={running} onChange={(event) => setSeed(event.target.value)} />
          </label>
          <label className="text-xs text-mist-500">
            Repetitions
            <input className={inputClass} inputMode="numeric" value={repetitions} disabled={running} onChange={(event) => setRepetitions(event.target.value)} />
          </label>
        </div>

        <fieldset className="mt-4 max-w-4xl" disabled={running || !hardware}>
          <legend className="text-xs text-mist-500">Worker configurations</legend>
          <div className="mt-2 flex flex-wrap gap-2">
            {[1, 2, 3, 4].map((count) => {
              const allowed = hardware ? count <= hardware.maximum_worker_count : false;
              return (
                <label
                  key={count}
                  className={`inline-flex items-center gap-2 rounded-md border px-3 py-1.5 text-sm ${
                    allowed ? "border-ink-600 text-mist-100" : "border-ink-700 text-mist-500"
                  }`}
                >
                  <input
                    type="checkbox"
                    checked={workers.includes(count)}
                    disabled={!allowed}
                    onChange={() => toggleWorker(count)}
                  />
                  {count} {count === 1 ? "worker" : "workers"}
                </label>
              );
            })}
          </div>
        </fieldset>

        <div className="mt-4 flex flex-wrap items-center gap-3">
          <button type="button" className="btn btn-primary" disabled={running || !hardware} onClick={() => void onStart()}>
            <Play size={15} />
            Start benchmark
          </button>
          <span className="text-sm text-mist-300" role="status">
            {running ? status?.message || "Starting the benchmark." : status?.state === "completed" ? "Last benchmark completed." : "Idle"}
          </span>
        </div>

        {running ? (
          <div className="mt-4 max-w-4xl">
            <div className="h-2 overflow-hidden rounded-full bg-ink-700">
              <div className="h-full bg-accent transition-all" style={{ width: `${Math.round(progress * 100)}%` }} />
            </div>
            <p className="mt-2 text-xs text-mist-500">
              {status?.cases_total
                ? `Cases ${status.cases_completed} / ${status.cases_total} in the current batch.`
                : "Waiting for the first solver batch."}
            </p>
          </div>
        ) : null}

        {summary && result ? (
          <>
            <div className="mt-6 grid max-w-5xl gap-3 sm:grid-cols-2 xl:grid-cols-3">
              <Metric label="Serial median" value={formatSeconds(summary.serialMedian)} />
              <Metric label="Best parallel median" value={formatSeconds(summary.bestParallelMedian)} />
              <Metric label="Best speedup" value={formatRatio(summary.bestSpeedup)} />
              <Metric label="Best parallel efficiency" value={formatRatio(summary.bestEfficiency)} />
              <Metric label="Maximum numerical error" value={`${formatError(result.maximum_absolute_error_celsius)} °C`} />
              <Metric label="Numerical validation" value={result.numerical_validation} />
            </div>
            <p className="mt-3 max-w-3xl text-sm leading-6 text-mist-300">
              A parallel efficiency slightly above 1 (100%) can come from measurement variation. It shows up most
              easily with one worker, where the pool does almost the same work as the serial baseline and the two
              medians can trade places by a small amount.
            </p>
            {summary.bestSpeedup < 1 ? (
              <p className="mt-3 max-w-3xl text-sm leading-6 text-mist-300">
                Parallel execution was slower than the serial reference for this workload. The plotted speedup is
                the measured ratio. Thread scheduling can cost more than it saves when each thermal system is small.
              </p>
            ) : null}
            <div className="mt-6 grid max-w-5xl gap-3 xl:grid-cols-3">
              <BenchmarkChart title="Execution time" yLabel="Median seconds" points={summary.timePoints} />
              <BenchmarkChart title="Speedup" yLabel="T serial / T parallel" points={summary.speedupPoints} reference={1} />
              <BenchmarkChart
                title="Parallel efficiency"
                yLabel="Speedup / workers"
                points={summary.efficiencyPoints}
                reference={1}
              />
            </div>
            <p className="mt-6 max-w-3xl text-sm leading-6 text-mist-300">
              The table lists the measured repetitions only. Each configuration was warmed up once on the same
              workload, and those warm-up times are excluded from this table, the medians, and the speedups.
            </p>
            <div className="mt-3 max-w-5xl overflow-x-auto rounded-md border border-ink-600">
              <table className="w-full min-w-[36rem] text-left text-sm">
                <thead className="bg-ink-900 text-xs uppercase tracking-[0.12em] text-mist-500">
                  <tr>
                    <th className="px-3 py-2 font-semibold">Mode</th>
                    <th className="px-3 py-2 font-semibold">Workers</th>
                    <th className="px-3 py-2 font-semibold">Repetition</th>
                    <th className="px-3 py-2 font-semibold">Time (s)</th>
                  </tr>
                </thead>
                <tbody>
                  {result.configurations.flatMap((configuration) =>
                    configuration.execution_time_seconds.map((sample, index) => (
                      <tr key={`${configuration.execution_mode}-${configuration.worker_count}-${index}`} className="border-t border-ink-700">
                        <td className="px-3 py-2 text-mist-100">{configuration.execution_mode}</td>
                        <td className="mono px-3 py-2 text-mist-100">{configuration.worker_count}</td>
                        <td className="mono px-3 py-2 text-mist-100">{index + 1}</td>
                        <td className="mono px-3 py-2 text-mist-100">{formatSeconds(sample)}</td>
                      </tr>
                    )),
                  )}
                </tbody>
              </table>
            </div>
          </>
        ) : (
          <p className="mt-6 max-w-2xl text-sm leading-6 text-mist-500">
            Charts and timings appear after a benchmark finishes. No example numbers are stored in this page.
          </p>
        )}

        <HardwareGrid hardware={hardware} />
      </section>
      <InspectorPanel title="Benchmark">
        <InspectorSection title="Timing">
          <Field label="Generation" value={result ? formatSeconds(result.workload_generation_time_seconds) : "unavailable"} />
          <Field
            label="Warm-up (excluded)"
            value={result ? formatSeconds(result.warmup_time_seconds) : "unavailable"}
          />
          <Field label="Total duration" value={result ? formatSeconds(result.total_duration_seconds) : "unavailable"} />
          <Field label="State" value={status?.state ?? "unavailable"} />
        </InspectorSection>
        <InspectorSection title="Validation">
          <Field label="Status" value={result?.numerical_validation ?? "unavailable"} />
          <Field label="Max error" value={result ? `${formatError(result.maximum_absolute_error_celsius)} °C` : "unavailable"} />
          <Field label="Tolerance" value={result ? `${formatError(result.numerical_tolerance_celsius)} °C` : "1e-8 °C"} />
          <Field label="Reference" value={result?.speedup_reference ?? "serial median"} />
        </InspectorSection>
        <div className="px-4 py-3">
          <button type="button" className="btn" disabled={!result || running} onClick={() => void onExport()}>
            <Download size={15} />
            Download CSV
          </button>
        </div>
        <Note>
          Conductivity is drawn from [0.2, 400) W/(m·K), heat generation from [1, 500000) W/m³, and the end
          temperatures from finite Dirichlet ranges. Length is 1 m and area is 1e-4 m². The same cases are reused
          for every serial and parallel repetition.
        </Note>
      </InspectorPanel>
    </div>
  );
}

function Metric({ label, value }: { label: string; value: string }) {
  return (
    <article className="rounded-md border border-ink-600 bg-ink-900 px-4 py-3">
      <p className="text-[11px] uppercase tracking-[0.14em] text-mist-500">{label}</p>
      <p className="mono mt-1 text-lg text-mist-100">{value}</p>
    </article>
  );
}

function HardwareGrid({ hardware }: { hardware: HardwareInfo | null }) {
  const rows = [
    ["CPU", hardware?.cpu_model ?? "unavailable"],
    ["Physical cores", hardware?.physical_cores?.toString() ?? "unavailable"],
    ["Logical CPUs", hardware?.logical_cpus?.toString() ?? "unavailable"],
    ["RAM", formatRam(hardware?.ram_bytes ?? null)],
    ["Operating system", hardware?.operating_system ?? "unavailable"],
    ["Compiler", hardware ? joinPresent(hardware.compiler, hardware.compiler_version) : "unavailable"],
    ["Build", hardware?.build_type ?? "unavailable"],
    ["Optimization flags", hardware?.optimization_flags ?? "unavailable"],
  ];
  return (
    <div className="mt-6 max-w-5xl">
      <h3 className="text-sm font-semibold text-mist-100">Hardware and build</h3>
      <div className="mt-3 grid gap-3 sm:grid-cols-2">
        {rows.map(([label, value]) => (
          <article key={label} className="rounded-md border border-ink-600 bg-ink-900 px-4 py-3">
            <p className="text-[11px] uppercase tracking-[0.14em] text-mist-500">{label}</p>
            <p className="mono mt-1 break-all text-sm text-mist-100">{value || "unavailable"}</p>
          </article>
        ))}
      </div>
    </div>
  );
}

function summarize(result: BenchmarkResult | null) {
  if (!result) return null;
  const serial = result.configurations.find((configuration) => configuration.execution_mode === "serial");
  const parallel = result.configurations.filter((configuration) => configuration.execution_mode === "parallel");
  if (!serial || parallel.length === 0) return null;
  const bestParallel = parallel.reduce((best, configuration) =>
    configuration.median_execution_time_seconds < best.median_execution_time_seconds ? configuration : best,
  );
  const bestSpeedup = parallel.reduce((best, configuration) => Math.max(best, configuration.speedup), parallel[0].speedup);
  const bestEfficiency = parallel.reduce(
    (best, configuration) => Math.max(best, configuration.parallel_efficiency),
    parallel[0].parallel_efficiency,
  );
  const label = (configuration: BenchmarkResult["configurations"][number]) =>
    configuration.execution_mode === "serial" ? "Serial" : String(configuration.worker_count);
  return {
    serialMedian: serial.median_execution_time_seconds,
    bestParallelMedian: bestParallel.median_execution_time_seconds,
    bestSpeedup,
    bestEfficiency,
    timePoints: result.configurations.map((configuration) => ({
      label: label(configuration),
      value: configuration.median_execution_time_seconds,
    })),
    speedupPoints: parallel.map((configuration) => ({
      label: String(configuration.worker_count),
      value: configuration.speedup,
    })),
    efficiencyPoints: parallel.map((configuration) => ({
      label: String(configuration.worker_count),
      value: configuration.parallel_efficiency,
    })),
  };
}

function readRequest(seed: string, batchSize: string, meshSize: string, repetitions: string, workers: number[]) {
  const parsedSeed = readInteger(seed, 0, 4294967295, "Random seed");
  const parsedBatch = readInteger(batchSize, 1, 256, "Batch size");
  const parsedMesh = readInteger(meshSize, 1, 320, "Mesh size");
  const parsedRepetitions = readInteger(repetitions, 1, 20, "Repetitions");
  if (typeof parsedSeed === "string") return parsedSeed;
  if (typeof parsedBatch === "string") return parsedBatch;
  if (typeof parsedMesh === "string") return parsedMesh;
  if (typeof parsedRepetitions === "string") return parsedRepetitions;
  if (workers.length === 0) return "Select at least one worker count.";
  return {
    seed: parsedSeed,
    batch_size: parsedBatch,
    mesh_size: parsedMesh,
    repetitions: parsedRepetitions,
    worker_counts: [...workers].sort((a, b) => a - b),
  };
}

function readInteger(text: string, minimum: number, maximum: number, label: string): number | string {
  if (!/^\d+$/.test(text)) return `${label} must be an integer.`;
  const value = Number(text);
  if (!Number.isSafeInteger(value) || value < minimum || value > maximum) {
    return `${label} must be from ${minimum} to ${maximum}.`;
  }
  return value;
}

function formatSeconds(value: number): string {
  if (!Number.isFinite(value)) return "unavailable";
  if (value !== 0 && Math.abs(value) < 0.001) return `${value.toExponential(3)} s`;
  return `${value.toFixed(6)} s`;
}

function formatRatio(value: number): string {
  if (!Number.isFinite(value)) return "unavailable";
  return value.toFixed(3);
}

function formatError(value: number): string {
  if (!Number.isFinite(value)) return "unavailable";
  if (value === 0) return "0";
  if (value < 1e-4) return value.toExponential(2);
  return value.toFixed(6);
}

function formatRam(bytes: number | null): string {
  if (bytes === null) return "unavailable";
  const gibibytes = bytes / (1024 * 1024 * 1024);
  return `${gibibytes.toFixed(2)} GiB (${bytes} bytes)`;
}

function joinPresent(left: string | null, right: string | null): string {
  const parts = [left, right].filter((part): part is string => Boolean(part));
  return parts.length > 0 ? parts.join(" ") : "unavailable";
}
