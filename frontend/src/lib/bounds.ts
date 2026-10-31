import * as THREE from "three";
import type { BoundingBox } from "../types/api";

export function boundsSize(bounds: BoundingBox): THREE.Vector3 {
  return new THREE.Vector3(
    bounds.max[0] - bounds.min[0],
    bounds.max[1] - bounds.min[1],
    bounds.max[2] - bounds.min[2],
  );
}

export function boundsCenter(bounds: BoundingBox): THREE.Vector3 {
  return new THREE.Vector3(
    (bounds.min[0] + bounds.max[0]) / 2,
    (bounds.min[1] + bounds.max[1]) / 2,
    (bounds.min[2] + bounds.max[2]) / 2,
  );
}

export function boundsRadius(bounds: BoundingBox): number {
  const size = boundsSize(bounds);
  return Math.max(0.5 * Math.hypot(size.x, size.y, size.z), 1e-4);
}

export interface FrameControls {
  target: THREE.Vector3;
  update: () => void;
}

export function frameCamera(
  camera: THREE.PerspectiveCamera,
  controls: FrameControls | null,
  bounds: BoundingBox,
  mode: "fit" | "reset",
): void {
  const center = boundsCenter(bounds);
  const radius = boundsRadius(bounds);
  const fov = (camera.fov * Math.PI) / 180;
  const distance = (radius / Math.sin(fov / 2)) * 1.55;
  const direction = new THREE.Vector3();
  if (mode === "fit") {
    const anchor = controls ? controls.target : center;
    direction.copy(camera.position).sub(anchor);
  }
  if (direction.lengthSq() < 1e-12) {
    direction.set(1, -1, 0.82);
  }
  direction.normalize();
  camera.up.set(0, 0, 1);
  camera.position.copy(center).add(direction.multiplyScalar(distance));
  camera.near = Math.max(distance / 10000, 1e-6);
  camera.far = Math.max(distance * 200, 10);
  camera.updateProjectionMatrix();
  camera.lookAt(center);
  if (controls) {
    controls.target.copy(center);
    controls.update();
  }
}
