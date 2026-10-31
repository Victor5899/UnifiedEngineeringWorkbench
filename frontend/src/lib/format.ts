import type { Vec3 } from "../types/api";

export function formatNumber(value: number, digits = 6): string {
  if (!Number.isFinite(value)) return "n/a";
  const fixed = value.toFixed(digits);
  const trimmed = fixed.replace(/(\.\d*?)0+$/, "$1").replace(/\.$/, "");
  return trimmed === "-0" ? "0" : trimmed;
}

export function formatVec3(value: Vec3, digits = 4): string {
  return value.map((part) => formatNumber(part, digits)).join(", ");
}

export function formatNodeId(value: number): string {
  if (!Number.isFinite(value)) return "n/a";
  if (Number.isSafeInteger(value)) return String(value);
  return formatNumber(value, 0);
}
