import type { BackpropReport } from "../types/backpropagation";
import type { GeometryModel, HealthResponse, TemperatureModel } from "../types/api";
import type {
  CapabilityList,
  EmbeddedDevice,
  EmbeddedDeviceList,
  HardwareCommandResult,
  SimulatedFailureArmed,
  TelemetryReport,
} from "../types/hardware";
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

function isDeviceType(value: unknown): value is EmbeddedDevice["device_type"] {
  return value === "microcontroller" || value === "fpga" || value === "jtag_adapter";
}

function isConnectionState(value: unknown): value is EmbeddedDevice["connection_state"] {
  return value === "connected" || value === "disconnected";
}

function isCapability(value: unknown): value is EmbeddedDevice["capabilities"][number] {
  return isRecord(value) && typeof value.id === "string" && value.id.trim().length > 0 && typeof value.summary === "string";
}

function isSimulatedEnvelope(value: Record<string, unknown>): boolean {
  return value.simulated === true && value.physical_hardware_connected === false;
}

function isEmbeddedDevice(value: unknown): value is EmbeddedDevice {
  if (!isRecord(value) || !isSimulatedEnvelope(value)) return false;
  if (typeof value.id !== "string" || typeof value.display_name !== "string") return false;
  if (typeof value.reference_profile !== "string" || typeof value.manufacturer_reference !== "string") return false;
  if (!isDeviceType(value.device_type) || !isConnectionState(value.connection_state)) return false;
  if (typeof value.available !== "boolean" || !Array.isArray(value.capabilities)) return false;
  return value.capabilities.every(isCapability);
}

function isEmbeddedDeviceList(value: unknown): value is EmbeddedDeviceList {
  return isRecord(value) && isSimulatedEnvelope(value) && Array.isArray(value.devices) && value.devices.every(isEmbeddedDevice);
}

function isCapabilityList(value: unknown): value is CapabilityList {
  if (!isRecord(value) || !isSimulatedEnvelope(value) || !Array.isArray(value.devices)) return false;
  return value.devices.every((device) => {
    if (!isRecord(device) || typeof device.id !== "string" || typeof device.display_name !== "string") return false;
    if (!isDeviceType(device.device_type) || !Array.isArray(device.capabilities)) return false;
    return device.capabilities.every(isCapability);
  });
}

function isHardwareCommand(value: unknown): value is HardwareCommandResult {
  return (
    isRecord(value) &&
    isSimulatedEnvelope(value) &&
    typeof value.device_id === "string" &&
    isConnectionState(value.connection_state) &&
    typeof value.message === "string"
  );
}

function isTelemetryReport(value: unknown): value is TelemetryReport {
  if (!isRecord(value) || !isSimulatedEnvelope(value)) return false;
  if (typeof value.device_id !== "string" || typeof value.notice !== "string" || !Array.isArray(value.readings)) return false;
  return value.readings.every(
    (reading) =>
      isRecord(reading) &&
      typeof reading.name === "string" &&
      typeof reading.value === "string" &&
      typeof reading.unit === "string" &&
      reading.simulated === true,
  );
}

function isFailureArmed(value: unknown): value is SimulatedFailureArmed {
  return (
    isRecord(value) &&
    isSimulatedEnvelope(value) &&
    typeof value.device_id === "string" &&
    typeof value.failure === "string" &&
    value.armed === true &&
    isConnectionState(value.connection_state) &&
    typeof value.message === "string"
  );
}

function postJson<T>(path: string, body: unknown, accept: (value: unknown) => value is T): Promise<T> {
  return requestJson(
    path,
    {
      method: "POST",
      headers: { "Content-Type": "application/json", Accept: "application/json" },
      body: JSON.stringify(body),
    },
    accept,
  );
}

export function fetchEmbeddedDevices(): Promise<EmbeddedDeviceList> {
  return requestJson("/api/hardware/devices", undefined, isEmbeddedDeviceList);
}

export function fetchEmbeddedCapabilities(): Promise<CapabilityList> {
  return requestJson("/api/hardware/capabilities", undefined, isCapabilityList);
}

export function connectEmbeddedDevice(deviceId: string): Promise<HardwareCommandResult> {
  return postJson("/api/hardware/connect", { device_id: deviceId }, isHardwareCommand);
}

export function disconnectEmbeddedDevice(deviceId: string): Promise<HardwareCommandResult> {
  return postJson("/api/hardware/disconnect", { device_id: deviceId }, isHardwareCommand);
}

export function fetchEmbeddedTelemetry(deviceId: string): Promise<TelemetryReport> {
  const query = new URLSearchParams({ device_id: deviceId });
  return requestJson(`/api/hardware/telemetry?${query.toString()}`, undefined, isTelemetryReport);
}

export function armSimulatedHardwareFailure(deviceId: string, failure: string): Promise<SimulatedFailureArmed> {
  return postJson("/api/hardware/simulate-failure", { device_id: deviceId, failure }, isFailureArmed);
}

const backpropParameters = ["w", "b", "v", "c"] as const;

function isBackpropParameters(value: unknown): value is BackpropReport["initial_parameters"] {
  return isRecord(value) && backpropParameters.every((name) => isFiniteNumber(value[name]));
}

function isBackpropReport(value: unknown): value is BackpropReport {
  if (!isRecord(value)) return false;
  if (value.training_samples !== 1 || value.hidden_neurons !== 1 || value.activation !== "tanh") return false;
  if (typeof value.loss_definition !== "string") return false;
  if (!isRecord(value.sample) || !isFiniteNumber(value.sample.x) || !isFiniteNumber(value.sample.y)) return false;
  if (!isFiniteNumber(value.learning_rate) || !isFiniteNumber(value.epsilon) || !isFiniteNumber(value.gradient_tolerance)) {
    return false;
  }
  if (value.training_steps !== 100) return false;
  if (!isBackpropParameters(value.initial_parameters) || !isBackpropParameters(value.parameters_after_one_update)) {
    return false;
  }
  if (!isBackpropParameters(value.final_parameters)) return false;
  const forward = value.initial_forward;
  if (!isRecord(forward) || !["z", "h", "y_hat", "error", "loss"].every((name) => isFiniteNumber(forward[name]))) {
    return false;
  }
  const gradients = value.analytical_gradients;
  if (!isRecord(gradients) || !["dJ_dw", "dJ_db", "dJ_dv", "dJ_dc"].every((name) => isFiniteNumber(gradients[name]))) {
    return false;
  }
  if (!Array.isArray(value.gradient_checks) || value.gradient_checks.length !== 4) return false;
  const checksPass = value.gradient_checks.every((check, index) => {
    if (!isRecord(check) || typeof check.passed !== "boolean") return false;
    return (
      check.parameter === backpropParameters[index] &&
      isFiniteNumber(check.analytical_gradient) &&
      isFiniteNumber(check.numerical_gradient) &&
      isFiniteNumber(check.absolute_error) &&
      check.absolute_error >= 0
    );
  });
  if (!checksPass || typeof value.gradient_check_passed !== "boolean") return false;
  const everyRowPassed = value.gradient_checks.every((check) => isRecord(check) && check.passed === true);
  if (value.gradient_check_passed !== everyRowPassed) return false;
  if (!isFiniteNumber(value.initial_loss) || !isFiniteNumber(value.final_loss)) return false;
  if (!Array.isArray(value.history) || value.history.length !== 101) return false;
  return value.history.every((row, index) => {
    if (!isRecord(row) || row.step !== index || !isFiniteNumber(row.loss)) return false;
    return backpropParameters.every((name) => isFiniteNumber(row[name]));
  });
}

export function fetchBackpropagation(): Promise<BackpropReport> {
  return requestJson("/api/learning/backpropagation", undefined, isBackpropReport);
}

export async function downloadBackpropCsv(): Promise<{ filename: string; csv: string }> {
  let response: Response;
  try {
    response = await fetch("/api/learning/backpropagation/csv", { cache: "no-store" });
  } catch {
    throw new ApiError("Could not reach the backend. Start workbench_server and try again.", 0);
  }
  if (!response.ok) throw new ApiError(await errorMessage(response), response.status);
  const csv = await response.text();
  if (!csv.startsWith("step,loss,w,b,v,c")) {
    throw new ApiError("The export did not contain a backpropagation table.", response.status);
  }
  const disposition = response.headers.get("Content-Disposition") ?? "";
  const match = /filename="([^"]+)"/.exec(disposition);
  return { filename: match?.[1] ?? "backpropagation_training.csv", csv };
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
