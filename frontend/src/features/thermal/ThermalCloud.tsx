import { useEffect, useMemo } from "react";
import { useThree, type ThreeEvent } from "@react-three/fiber";
import * as THREE from "three";
import { boundsSize } from "../../lib/bounds";
import { temperatureCss } from "../../lib/colorScale";
import type { TemperatureModel } from "../../types/api";

function indexFromEvent(event: ThreeEvent<MouseEvent> | ThreeEvent<PointerEvent>): number | null {
  return typeof event.index === "number" ? event.index : null;
}

export function ThermalCloud({
  model,
  selectedIndex,
  onSelect,
  onHover,
}: {
  model: TemperatureModel;
  selectedIndex: number | null;
  onSelect: (index: number) => void;
  onHover: (index: number | null) => void;
}) {
  const geometry = useMemo(() => {
    const positions = new Float32Array(model.nodes.length * 3);
    const colors = new Float32Array(model.nodes.length * 3);
    model.nodes.forEach((node, index) => {
      positions[index * 3] = node.x_m;
      positions[index * 3 + 1] = node.y_m;
      positions[index * 3 + 2] = node.z_m;
      const color = new THREE.Color(
        temperatureCss(node.temperature_C, model.minimum_temperature_C, model.maximum_temperature_C),
      );
      colors[index * 3] = color.r;
      colors[index * 3 + 1] = color.g;
      colors[index * 3 + 2] = color.b;
    });
    const buffer = new THREE.BufferGeometry();
    buffer.setAttribute("position", new THREE.BufferAttribute(positions, 3));
    buffer.setAttribute("color", new THREE.BufferAttribute(colors, 3));
    buffer.computeBoundingSphere();
    return buffer;
  }, [model]);

  const marker = useMemo(() => {
    if (selectedIndex === null) return null;
    const node = model.nodes[selectedIndex];
    if (!node) return null;
    const buffer = new THREE.BufferGeometry();
    buffer.setAttribute(
      "position",
      new THREE.BufferAttribute(new Float32Array([node.x_m, node.y_m, node.z_m]), 3),
    );
    return buffer;
  }, [model, selectedIndex]);

  const span = Math.max(
    boundsSize(model.bounding_box).x,
    boundsSize(model.bounding_box).y,
    boundsSize(model.bounding_box).z,
    1e-4,
  );
  const raycaster = useThree((state) => state.raycaster);

  useEffect(() => {
    raycaster.params.Points.threshold = Math.max(span * 0.08, 1e-4);
  }, [raycaster, span]);

  useEffect(() => {
    return () => geometry.dispose();
  }, [geometry]);

  useEffect(() => {
    return () => marker?.dispose();
  }, [marker]);

  return (
    <group>
      <points
        geometry={geometry}
        onClick={(event) => {
          event.stopPropagation();
          const index = indexFromEvent(event);
          if (index !== null) onSelect(index);
        }}
        onPointerMove={(event) => {
          event.stopPropagation();
          const index = indexFromEvent(event);
          if (index !== null) onHover(index);
        }}
        onPointerOut={() => onHover(null)}
      >
        <pointsMaterial size={16} sizeAttenuation={false} vertexColors toneMapped={false} />
      </points>
      {marker ? (
        <points geometry={marker} raycast={() => undefined}>
          <pointsMaterial
            color="#ffffff"
            size={24}
            sizeAttenuation={false}
            depthTest={false}
            toneMapped={false}
          />
        </points>
      ) : null}
    </group>
  );
}
