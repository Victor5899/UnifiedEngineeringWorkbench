import type { GeometryModel, HealthResponse, TemperatureModel } from "../types/api";

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
