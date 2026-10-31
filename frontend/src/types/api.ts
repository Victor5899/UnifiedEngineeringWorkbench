export type Vec3 = [number, number, number];

export interface BoundingBox {
  min: Vec3;
  max: Vec3;
}

export interface Dimensions {
  x: number;
  y: number;
  z: number;
}

export interface GeometryTriangle {
  normal: Vec3;
  vertices: [Vec3, Vec3, Vec3];
}

export interface GeometryModel {
  filename: string;
  solid_name: string;
  triangle_count: number;
  vertex_count: number;
  triangles: GeometryTriangle[];
  bounding_box: BoundingBox;
  dimensions: Dimensions;
  coordinate_units: string;
}

export interface TemperatureNode {
  node_id: number;
  x_m: number;
  y_m: number;
  z_m: number;
  temperature_C: number;
}

export interface TemperatureModel {
  filename: string;
  node_count: number;
  minimum_temperature_C: number;
  maximum_temperature_C: number;
  average_temperature_C: number;
  bounding_box: BoundingBox;
  dimensions: Dimensions;
  coordinate_units: string;
  temperature_units: string;
  nodes: TemperatureNode[];
}

export interface HealthResponse {
  status: string;
  service: string;
  version: string;
}
