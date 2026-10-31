import type { GeometryModel, HealthResponse, TemperatureModel } from "../types/api";
import type {
  BenchmarkAccepted,
  BenchmarkRequest,
  BenchmarkResult,
  BenchmarkStatus,
  HardwareInfo,
} from "../types/performance";

export class ApiError extends Error {
  readonly status: number;

  constructor(message: string, status: number) {
    super(message);
    this.name = "ApiError";
    this.status = status;
  }
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function isFiniteNumber(value: unknown): value is number {
  return typeof value === "number" && Number.isFinite(value);
}

function isVec3(value: unknown): value is [number, number, number] {
  return Array.isArray(value) && value.length === 3 && value.every(isFiniteNumber);
}

function isBoundingBox(value: unknown): boolean {
  return isRecord(value) && isVec3(value.min) && isVec3(value.max);
}

function isDimensions(value: unknown): boolean {
  return isRecord(value) && isFiniteNumber(value.x) && isFiniteNumber(value.y) && isFiniteNumber(value.z);
}

function isGeometryModel(value: unknown): value is GeometryModel {
  if (!isRecord(value)) return false;
  if (typeof value.filename !== "string" || typeof value.solid_name !== "string") return false;
  if (typeof value.coordinate_units !== "string") return false;
  if (!isFiniteNumber(value.triangle_count) || !isFiniteNumber(value.vertex_count)) return false;
  if (!isBoundingBox(value.bounding_box) || !isDimensions(value.dimensions)) return false;
  if (!Array.isArray(value.triangles)) return false;
  if (value.triangles.length !== value.triangle_count) return false;
  if (value.vertex_count !== value.triangle_count * 3) return false;
  return value.triangles.every((triangle) => {
    if (!isRecord(triangle) || !isVec3(triangle.normal) || !Array.isArray(triangle.vertices)) return false;
    return triangle.vertices.length === 3 && triangle.vertices.every(isVec3);
  });
}

function isTemperatureModel(value: unknown): value is TemperatureModel {
  if (!isRecord(value)) return false;
  if (typeof value.filename !== "string") return false;
  if (typeof value.coordinate_units !== "string" || typeof value.temperature_units !== "string") return false;
  if (!isFiniteNumber(value.node_count)) return false;
  if (!isFiniteNumber(value.minimum_temperature_C)) return false;
  if (!isFiniteNumber(value.maximum_temperature_C)) return false;
  if (!isFiniteNumber(value.average_temperature_C)) return false;
  if (value.minimum_temperature_C > value.maximum_temperature_C) return false;
  if (!isBoundingBox(value.bounding_box) || !isDimensions(value.dimensions)) return false;
  if (!Array.isArray(value.nodes) || value.nodes.length !== value.node_count || value.node_count < 1) {
    return false;
  }
  return value.nodes.every((node) => {
    if (!isRecord(node)) return false;
    return (
      isFiniteNumber(node.node_id) &&
      isFiniteNumber(node.x_m) &&
      isFiniteNumber(node.y_m) &&
      isFiniteNumber(node.z_m) &&
      isFiniteNumber(node.temperature_C)
    );
  });
}

function isHealth(value: unknown): value is HealthResponse {
  return (
    isRecord(value) &&
    typeof value.status === "string" &&
    typeof value.service === "string" &&
    typeof value.version === "string"
  );
}

async function errorMessage(response: Response): Promise<string> {
  const text = await response.text();
  if (!text) return `Request failed (${response.status}).`;
  try {
    const body: unknown = JSON.parse(text);
    if (isRecord(body) && typeof body.error === "string" && body.error.trim()) return body.error;
  } catch {
    // Fall through to a short plain-text message.
  }
  const compact = text.replace(/\s+/g, " ").trim();
  if (!compact || compact.length > 240) return `Request failed (${response.status}).`;
  return compact;
}

async function postFile<T>(path: string, file: File, accept: (value: unknown) => value is T): Promise<T> {
  const form = new FormData();
  form.append("file", file, file.name);
  let response: Response;
  try {
    response = await fetch(path, { method: "POST", body: form });
  } catch {
    throw new ApiError("Could not reach the backend. Start workbench_server and try again.", 0);
  }
  if (!response.ok) throw new ApiError(await errorMessage(response), response.status);
  let body: unknown;
  try {
    body = await response.json();
  } catch {
    throw new ApiError("The backend returned a response that is not JSON.", response.status);
  }
  if (!accept(body)) throw new ApiError("The backend response did not match the expected data.", response.status);
  return body;
}

export async function fetchHealth(): Promise<HealthResponse | null> {
  try {
    const response = await fetch("/api/health", { cache: "no-store" });
    if (!response.ok) return null;
    const body: unknown = await response.json();
    return isHealth(body) && body.status === "ok" ? body : null;
  } catch {
    return null;
  }
}

export function uploadStl(file: File): Promise<GeometryModel> {
  return postFile("/api/geometry/stl", file, isGeometryModel);
}

export function uploadTemperatureCsv(file: File): Promise<TemperatureModel> {
  return postFile("/api/thermal/csv", file, isTemperatureModel);
}

function nullableString(value: unknown): string | null | undefined {
  if (value === null) return null;
  if (typeof value === "string") return value;
  return undefined;
}

function nullableNumber(value: unknown): number | null | undefined {
  if (value === null) return null;
  if (isFiniteNumber(value)) return value;
  return undefined;
}

function isHardwareInfo(value: unknown): value is HardwareInfo {
  if (!isRecord(value)) return false;
  if (nullableString(value.cpu_model) === undefined) return false;
  if (nullableNumber(value.physical_cores) === undefined) return false;
  if (nullableNumber(value.logical_cpus) === undefined) return false;
  if (nullableNumber(value.ram_bytes) === undefined) return false;
  if (nullableString(value.operating_system) === undefined) return false;
  if (nullableString(value.compiler) === undefined) return false;
  if (nullableString(value.compiler_version) === undefined) return false;
  if (nullableString(value.build_type) === undefined) return false;
  if (nullableString(value.optimization_flags) === undefined) return false;
  return isFiniteNumber(value.maximum_worker_count) && value.maximum_worker_count >= 1;
}

function isNumberArray(value: unknown): value is number[] {
  return Array.isArray(value) && value.every(isFiniteNumber);
}

function isBenchmarkResult(value: unknown): value is BenchmarkResult {
  if (!isRecord(value)) return false;
  if (typeof value.benchmark_id !== "string" || typeof value.timestamp_utc !== "string") return false;
  if (!isFiniteNumber(value.random_seed) || !isFiniteNumber(value.batch_size)) return false;
  if (!isFiniteNumber(value.mesh_size) || !isFiniteNumber(value.repetitions)) return false;
  if (!isFiniteNumber(value.workload_generation_time_seconds)) return false;
  if (!isFiniteNumber(value.warmup_time_seconds) || !isFiniteNumber(value.total_duration_seconds)) return false;
  if (typeof value.speedup_reference !== "string") return false;
  if (!isFiniteNumber(value.numerical_tolerance_celsius)) return false;
  if (!isFiniteNumber(value.maximum_absolute_error_celsius)) return false;
  if (typeof value.numerical_validation !== "string") return false;
  if (!isHardwareInfo(value.hardware) || !Array.isArray(value.configurations)) return false;
  return value.configurations.every((entry) => {
    if (!isRecord(entry)) return false;
    return (
      typeof entry.execution_mode === "string" &&
      isFiniteNumber(entry.worker_count) &&
      isFiniteNumber(entry.warmup_time_seconds) &&
      isNumberArray(entry.execution_time_seconds) &&
      isFiniteNumber(entry.median_execution_time_seconds) &&
      isFiniteNumber(entry.speedup) &&
      isFiniteNumber(entry.parallel_efficiency) &&
      isFiniteNumber(entry.maximum_absolute_error_celsius) &&
      typeof entry.numerical_validation === "string"
    );
  });
}

function isBenchmarkStatus(value: unknown): value is BenchmarkStatus {
  if (!isRecord(value)) return false;
  if (typeof value.state !== "string" || typeof value.phase !== "string" || typeof value.message !== "string") {
    return false;
  }
  if (!(value.benchmark_id === null || typeof value.benchmark_id === "string")) return false;
  if (!(value.error === null || typeof value.error === "string")) return false;
  return (
    isFiniteNumber(value.worker_count) &&
    isFiniteNumber(value.repetition) &&
    isFiniteNumber(value.repetitions) &&
    isFiniteNumber(value.cases_completed) &&
    isFiniteNumber(value.cases_total) &&
    isFiniteNumber(value.completed_steps) &&
    isFiniteNumber(value.total_steps)
  );
}

function isBenchmarkAccepted(value: unknown): value is BenchmarkAccepted {
  return isRecord(value) && value.state === "running" && typeof value.benchmark_id === "string";
}

async function requestJson<T>(path: string, init: RequestInit | undefined, accept: (value: unknown) => value is T): Promise<T> {
  let response: Response;
  try {
    response = await fetch(path, { cache: "no-store", ...init });
  } catch {
    throw new ApiError("Could not reach the backend. Start workbench_server and try again.", 0);
  }
  if (!response.ok) throw new ApiError(await errorMessage(response), response.status);
  let body: unknown;
  try {
    body = await response.json();
  } catch {
    throw new ApiError("The backend returned a response that is not JSON.", response.status);
  }
  if (!accept(body)) throw new ApiError("The backend response did not match the expected data.", response.status);
  return body;
}

export function fetchHardware(): Promise<HardwareInfo> {
  return requestJson("/api/performance/hardware", undefined, isHardwareInfo);
}

export function fetchBenchmarkStatus(): Promise<BenchmarkStatus> {
  return requestJson("/api/performance/status", undefined, isBenchmarkStatus);
}

export function fetchBenchmarkResults(): Promise<BenchmarkResult> {
  return requestJson("/api/performance/results", undefined, isBenchmarkResult);
}

export function startBenchmark(request: BenchmarkRequest): Promise<BenchmarkAccepted> {
  return requestJson(
    "/api/performance/benchmark",
    {
      method: "POST",
      headers: { "Content-Type": "application/json", Accept: "application/json" },
      body: JSON.stringify(request),
    },
    isBenchmarkAccepted,
  );
}

export async function downloadBenchmarkCsv(): Promise<{ filename: string; csv: string }> {
  let response: Response;
  try {
    response = await fetch("/api/performance/export", { cache: "no-store" });
  } catch {
    throw new ApiError("Could not reach the backend. Start workbench_server and try again.", 0);
  }
  if (!response.ok) throw new ApiError(await errorMessage(response), response.status);
  const csv = await response.text();
  if (!csv.includes("execution_time_seconds")) {
    throw new ApiError("The export did not contain a benchmark table.", response.status);
  }
  const disposition = response.headers.get("Content-Disposition") ?? "";
  const match = /filename="([^"]+)"/.exec(disposition);
  return { filename: match?.[1] ?? "benchmark-results.csv", csv };
}
