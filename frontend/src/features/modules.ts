import { Box, Cpu, Gauge, Info, Thermometer, type LucideIcon } from "lucide-react";
import type { ModuleId } from "../types/workspace";

export interface ModuleDefinition {
  id: ModuleId;
  label: string;
  description: string;
  icon: LucideIcon;
  implemented: boolean;
}

export const modules: readonly ModuleDefinition[] = [
  {
    id: "geometry",
    label: "Geometry",
    description: "ASCII STL viewer",
    icon: Box,
    implemented: true,
  },
  {
    id: "thermal",
    label: "Thermal Analysis",
    description: "Temperature point cloud",
    icon: Thermometer,
    implemented: true,
  },
  {
    id: "performance",
    label: "Performance",
    description: "Planned batch benchmarks",
    icon: Gauge,
    implemented: false,
  },
  {
    id: "hardware",
    label: "Hardware",
    description: "Planned device adapters",
    icon: Cpu,
    implemented: false,
  },
  {
    id: "about",
    label: "About",
    description: "Workbench information",
    icon: Info,
    implemented: true,
  },
];

export function moduleById(id: ModuleId): ModuleDefinition {
  const found = modules.find((entry) => entry.id === id);
  if (!found) return modules[0];
  return found;
}
