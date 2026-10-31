import { useEffect, useMemo } from "react";
import * as THREE from "three";
import type { GeometryModel, GeometryTriangle } from "../../types/api";

/** Edge extraction builds a map of every edge. Large binary meshes skip it. */
export const edgeOverlayTriangleLimit = 100_000;

function writeNormal(target: Float32Array, offset: number, triangle: GeometryTriangle) {
  let nx = triangle.normal[0];
  let ny = triangle.normal[1];
  let nz = triangle.normal[2];
  let lengthSquared = nx * nx + ny * ny + nz * nz;
  if (lengthSquared <= 1e-16) {
    const [a, b, c] = triangle.vertices;
    const abx = b[0] - a[0];
    const aby = b[1] - a[1];
    const abz = b[2] - a[2];
    const acx = c[0] - a[0];
    const acy = c[1] - a[1];
    const acz = c[2] - a[2];
    nx = aby * acz - abz * acy;
    ny = abz * acx - abx * acz;
    nz = abx * acy - aby * acx;
    lengthSquared = nx * nx + ny * ny + nz * nz;
    if (lengthSquared <= 1e-16) {
      nx = 0;
      ny = 0;
      nz = 1;
      lengthSquared = 1;
    }
  }
  const inverse = 1 / Math.sqrt(lengthSquared);
  nx *= inverse;
  ny *= inverse;
  nz *= inverse;
  target[offset] = nx;
  target[offset + 1] = ny;
  target[offset + 2] = nz;
  target[offset + 3] = nx;
  target[offset + 4] = ny;
  target[offset + 5] = nz;
  target[offset + 6] = nx;
  target[offset + 7] = ny;
  target[offset + 8] = nz;
}

function buildGeometry(model: GeometryModel): THREE.BufferGeometry {
  const positions = new Float32Array(model.triangles.length * 9);
  const normals = new Float32Array(model.triangles.length * 9);
  for (let index = 0; index < model.triangles.length; index += 1) {
    const triangle = model.triangles[index];
    const offset = index * 9;
    for (let vertexIndex = 0; vertexIndex < 3; vertexIndex += 1) {
      const vertex = triangle.vertices[vertexIndex];
      const at = offset + vertexIndex * 3;
      positions[at] = vertex[0];
      positions[at + 1] = vertex[1];
      positions[at + 2] = vertex[2];
    }
    writeNormal(normals, offset, triangle);
  }
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute("position", new THREE.BufferAttribute(positions, 3));
  geometry.setAttribute("normal", new THREE.BufferAttribute(normals, 3));
  geometry.computeBoundingSphere();
  return geometry;
}

export function GeometryMesh({ model }: { model: GeometryModel }) {
  const geometry = useMemo(() => buildGeometry(model), [model]);
  const showEdges = model.triangle_count <= edgeOverlayTriangleLimit;
  const edges = useMemo(() => (showEdges ? new THREE.EdgesGeometry(geometry) : null), [geometry, showEdges]);

  useEffect(() => {
    return () => {
      edges?.dispose();
      geometry.dispose();
    };
  }, [edges, geometry]);

  return (
    <group>
      <mesh geometry={geometry}>
        <meshStandardMaterial
          color="#c5d0dc"
          metalness={0.58}
          roughness={0.36}
          side={THREE.DoubleSide}
          polygonOffset
          polygonOffsetFactor={1}
          polygonOffsetUnits={1}
        />
      </mesh>
      {edges ? (
        <lineSegments geometry={edges}>
          <lineBasicMaterial color="#9fd7e8" transparent opacity={0.72} />
        </lineSegments>
      ) : null}
    </group>
  );
}
