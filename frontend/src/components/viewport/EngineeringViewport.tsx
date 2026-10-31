import { Grid, GizmoHelper, GizmoViewport, OrbitControls } from "@react-three/drei";
import { Canvas, useThree } from "@react-three/fiber";
import { useEffect, useRef, type ReactNode } from "react";
import * as THREE from "three";
import { boundsCenter, boundsSize, frameCamera, type FrameControls } from "../../lib/bounds";
import type { BoundingBox } from "../../types/api";

function readControls(value: unknown): FrameControls | null {
  if (typeof value !== "object" || value === null) return null;
  if (!("target" in value) || !("update" in value)) return null;
  const target = (value as { target: unknown }).target;
  const update = (value as { update: unknown }).update;
  if (!(target instanceof THREE.Vector3) || typeof update !== "function") return null;
  return {
    target,
    update: () => {
      (update as () => void).call(value);
    },
  };
}

function ZUpCamera() {
  const camera = useThree((state) => state.camera);
  const ready = useRef(false);
  if (!ready.current) {
    camera.up.set(0, 0, 1);
    camera.lookAt(0, 0, 0);
    ready.current = true;
  }
  return null;
}

function CameraRig({
  bounds,
  fitRequest,
  resetRequest,
}: {
  bounds: BoundingBox | null;
  fitRequest: number;
  resetRequest: number;
}) {
  const camera = useThree((state) => state.camera);
  const get = useThree((state) => state.get);
  const boundsRef = useRef(bounds);
  boundsRef.current = bounds;

  useEffect(() => {
    const current = boundsRef.current;
    if (!current) return;
    let frame = 0;
    let attempts = 0;
    const run = () => {
      const perspective = camera instanceof THREE.PerspectiveCamera ? camera : null;
      const controls = readControls(get().controls);
      if (!perspective || (!controls && attempts < 8)) {
        attempts += 1;
        frame = window.requestAnimationFrame(run);
        return;
      }
      frameCamera(perspective, controls, current, "reset");
    };
    frame = window.requestAnimationFrame(run);
    return () => window.cancelAnimationFrame(frame);
  }, [bounds, camera, get, resetRequest]);

  useEffect(() => {
    const current = boundsRef.current;
    if (!current || fitRequest === 0) return;
    const perspective = camera instanceof THREE.PerspectiveCamera ? camera : null;
    if (!perspective) return;
    frameCamera(perspective, readControls(get().controls), current, "fit");
  }, [camera, fitRequest, get]);

  return null;
}

function SceneEnvironment({ bounds }: { bounds: BoundingBox | null }) {
  const span = bounds ? Math.max(boundsSize(bounds).x, boundsSize(bounds).y, boundsSize(bounds).z, 1e-3) : 1;
  const center = bounds ? boundsCenter(bounds) : new THREE.Vector3();
  const floorZ = bounds ? bounds.min[2] : 0;
  const cell = Math.max(span / 5, 1e-4);
  return (
    <>
      <ambientLight intensity={0.62} />
      <hemisphereLight args={["#d5e2ef", "#121820", 0.45]} />
      <directionalLight position={[center.x + span, center.y - span * 0.4, center.z + span * 1.5]} intensity={1.35} />
      <directionalLight position={[center.x - span, center.y + span * 0.8, center.z + span * 0.4]} intensity={0.4} />
      <Grid
        args={[span * 6, span * 6]}
        position={[center.x, center.y, floorZ - Math.max(span * 0.02, 1e-5)]}
        cellSize={cell}
        cellThickness={0.7}
        cellColor="#3c5166"
        sectionSize={Math.max(span, cell)}
        sectionThickness={1.35}
        sectionColor="#7f9bb4"
        fadeDistance={span * 12}
        fadeStrength={1}
        infiniteGrid={false}
        side={THREE.DoubleSide}
      />
      <axesHelper
        args={[Math.max(span * 0.45, 1e-4)]}
        position={bounds ? [bounds.min[0], bounds.min[1], bounds.min[2]] : [0, 0, 0]}
      />
    </>
  );
}

export function EngineeringViewport({
  bounds,
  fitRequest,
  resetRequest,
  children,
}: {
  bounds: BoundingBox | null;
  fitRequest: number;
  resetRequest: number;
  children?: ReactNode;
}) {
  return (
    <div className="absolute inset-0 bg-[#0b0f14]">
      <Canvas
        className="h-full w-full"
        dpr={[1, 2]}
        camera={{ position: [1.35, -1.45, 1.05], fov: 40, near: 0.001, far: 200 }}
        gl={{ antialias: true, logarithmicDepthBuffer: true }}
        onCreated={({ gl }) => {
          gl.outputColorSpace = THREE.SRGBColorSpace;
        }}
      >
        <color attach="background" args={["#0b0f14"]} />
        <ZUpCamera />
        <SceneEnvironment bounds={bounds} />
        <OrbitControls makeDefault enableDamping dampingFactor={0.14} enablePan enableRotate enableZoom screenSpacePanning />
        <CameraRig bounds={bounds} fitRequest={fitRequest} resetRequest={resetRequest} />
        {children}
        <GizmoHelper alignment="bottom-right" margin={[72, 72]}>
          <GizmoViewport axisColors={["#e06c75", "#7dce8a", "#6cb6ff"]} labelColor="#10151c" />
        </GizmoHelper>
      </Canvas>
      <div className="pointer-events-none absolute right-3 top-3 z-10 rounded-md border border-ink-600 bg-ink-900/85 px-2 py-1 font-mono text-[11px]">
        <span className="text-[#e06c75]">X</span>
        <span className="mx-1.5 text-ink-600">/</span>
        <span className="text-[#7dce8a]">Y</span>
        <span className="mx-1.5 text-ink-600">/</span>
        <span className="text-[#6cb6ff]">Z</span>
      </div>
    </div>
  );
}
