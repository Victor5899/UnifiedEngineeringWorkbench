import { useEffect, useMemo } from "react";
import * as THREE from "three";
import type { GeometryModel, GeometryTriangle } from "../../types/api";

function faceNormal(triangle: GeometryTriangle): THREE.Vector3 {
  const normal = new THREE.Vector3(triangle.normal[0], triangle.normal[1], triangle.normal[2]);
  if (normal.lengthSq() > 1e-16) return normal.normalize();
  const [a, b, c] = triangle.vertices;
  const ab = new THREE.Vector3(b[0] - a[0], b[1] - a[1], b[2] - a[2]);
  const ac = new THREE.Vector3(c[0] - a[0], c[1] - a[1], c[2] - a[2]);
  normal.crossVectors(ab, ac);
  if (normal.lengthSq() < 1e-16) normal.set(0, 0, 1);
  return normal.normalize();
}

function buildGeometry(model: GeometryModel): THREE.BufferGeometry {
  const positions = new Float32Array(model.triangles.length * 9);
  const normals = new Float32Array(model.triangles.length * 9);
  model.triangles.forEach((triangle, index) => {
    const normal = faceNormal(triangle);
    triangle.vertices.forEach((vertex, vertexIndex) => {
      const offset = index * 9 + vertexIndex * 3;
      positions[offset] = vertex[0];
      positions[offset + 1] = vertex[1];
      positions[offset + 2] = vertex[2];
      normals[offset] = normal.x;
      normals[offset + 1] = normal.y;
      normals[offset + 2] = normal.z;
    });
  });
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute("position", new THREE.BufferAttribute(positions, 3));
  geometry.setAttribute("normal", new THREE.BufferAttribute(normals, 3));
  geometry.computeBoundingSphere();
  return geometry;
}

export function GeometryMesh({ model }: { model: GeometryModel }) {
  const geometry = useMemo(() => buildGeometry(model), [model]);
  const edges = useMemo(() => new THREE.EdgesGeometry(geometry), [geometry]);

  useEffect(() => {
    return () => {
      edges.dispose();
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
      <lineSegments geometry={edges}>
        <lineBasicMaterial color="#9fd7e8" transparent opacity={0.72} />
      </lineSegments>
    </group>
  );
}
