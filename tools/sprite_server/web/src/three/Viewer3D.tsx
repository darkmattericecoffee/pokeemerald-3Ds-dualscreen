import { Canvas, useFrame, useThree } from "@react-three/fiber";
import { Loader2, RotateCcw, ZoomIn, ZoomOut } from "lucide-react";
import * as React from "react";
import * as THREE from "three";
import type { EditFile, Picture } from "@/lib/editfile";
import { renderPixels } from "@/lib/render";
import { cn } from "@/lib/utils";
import { createOrbit, OrbitContext, reset, swayAt, turn, zoomBy, type Orbit } from "./orbit";
import { PlaneStack } from "./PlaneStack";
import type { ViewSettings } from "./settings";

const DISTANCE = 150;
const IDLE_MS = 2500;

function Rig({ orbit, sway, children }: { orbit: Orbit; sway: boolean; children: React.ReactNode }) {
  const group = React.useRef<THREE.Group>(null);
  const shown = React.useRef({ yaw: 0, pitch: 0, zoom: 1 });
  const { camera } = useThree();
  useFrame(() => {
    const now = performance.now();
    const idle = sway && now - orbit.lastInput > IDLE_MS;
    const s = idle ? swayAt(now) : { yaw: 0, pitch: 0 };
    const k = now - orbit.lastInput < 100 ? 0.45 : 0.08; // follow a drag closely, ease otherwise
    shown.current.yaw += (orbit.yaw + s.yaw - shown.current.yaw) * k;
    shown.current.pitch += (orbit.pitch + s.pitch - shown.current.pitch) * k;
    shown.current.zoom += (orbit.zoom - shown.current.zoom) * 0.2;
    if (group.current) {
      group.current.rotation.y = shown.current.yaw;
      group.current.rotation.x = shown.current.pitch;
    }
    camera.position.set(0, 0, DISTANCE / shown.current.zoom);
    camera.lookAt(0, 0, 0);
  });
  return <group ref={group}>{children}</group>;
}

/** The flat picture, when WebGL is not there. */
function Flat({ edit, picture, frame, look }: { edit: EditFile; picture: Picture | null; frame: number; look: ViewSettings["look"] }) {
  const ref = React.useRef<HTMLCanvasElement>(null);
  React.useEffect(() => {
    const ctx = ref.current?.getContext("2d");
    if (!ctx) return;
    ctx.putImageData(new ImageData(renderPixels(edit, picture, frame, look) as Uint8ClampedArray<ArrayBuffer>, 64, 64), 0, 0);
  }, [edit, picture, frame, look]);
  return <canvas ref={ref} width={64} height={64} className="pixelated m-auto h-3/4 w-auto" />;
}

class GlBoundary extends React.Component<{ fallback: React.ReactNode; children: React.ReactNode }, { failed: boolean }> {
  state = { failed: false };
  static getDerivedStateFromError() {
    return { failed: true };
  }
  render() {
    return this.state.failed ? this.props.fallback : this.props.children;
  }
}

/** Keeps a view drawing only while it is on screen. */
function useOnScreen(ref: React.RefObject<HTMLElement | null>) {
  const [on, setOn] = React.useState(true);
  React.useEffect(() => {
    const el = ref.current;
    if (!el || typeof IntersectionObserver === "undefined") return;
    const io = new IntersectionObserver(([e]) => setOn(e.isIntersecting), { rootMargin: "100px" });
    io.observe(el);
    return () => io.disconnect();
  }, [ref]);
  return on;
}

export function Viewer3D({
  edit,
  picture,
  frame,
  settings,
  loading,
  className,
  children,
  label,
}: {
  edit: EditFile | undefined;
  picture: Picture | null;
  frame: number;
  settings: ViewSettings;
  loading?: boolean;
  className?: string;
  children?: React.ReactNode;
  label: string;
}) {
  const shared = React.useContext(OrbitContext);
  const own = React.useRef<Orbit>(createOrbit());
  const orbit = shared ?? own.current;
  const box = React.useRef<HTMLDivElement>(null);
  const onScreen = useOnScreen(box);
  const drag = React.useRef<{ id: number; x: number; y: number; touch: boolean } | null>(null);
  const pinch = React.useRef<Map<number, { x: number; y: number }>>(new Map());
  const reduced = React.useMemo(
    () => typeof matchMedia !== "undefined" && matchMedia("(prefers-reduced-motion: reduce)").matches,
    [],
  );

  // Pinch on a trackpad (or Ctrl + wheel) zooms; a plain wheel still scrolls the page.
  React.useEffect(() => {
    const el = box.current;
    if (!el) return;
    const wheel = (e: WheelEvent) => {
      if (!e.ctrlKey && !e.metaKey) return;
      e.preventDefault();
      zoomBy(orbit, Math.exp(-e.deltaY * 0.0015));
    };
    el.addEventListener("wheel", wheel, { passive: false });
    return () => el.removeEventListener("wheel", wheel);
  }, [orbit]);

  const onPointerDown = (e: React.PointerEvent) => {
    pinch.current.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (!drag.current) {
      drag.current = { id: e.pointerId, x: e.clientX, y: e.clientY, touch: e.pointerType === "touch" };
      (e.currentTarget as HTMLElement).setPointerCapture?.(e.pointerId);
    }
  };
  const onPointerMove = (e: React.PointerEvent) => {
    const pts = pinch.current;
    if (pts.has(e.pointerId) && pts.size === 2) {
      const [a, b] = [...pts.values()];
      const before = Math.hypot(a.x - b.x, a.y - b.y);
      pts.set(e.pointerId, { x: e.clientX, y: e.clientY });
      const [c, d] = [...pts.values()];
      const after = Math.hypot(c.x - d.x, c.y - d.y);
      if (before > 0) zoomBy(orbit, after / before);
      return;
    }
    const d = drag.current;
    if (!d || d.id !== e.pointerId) return;
    // On touch a vertical drag scrolls the page (touch-action: pan-y), so only turn sideways.
    turn(orbit, e.clientX - d.x, d.touch ? 0 : e.clientY - d.y);
    d.x = e.clientX;
    d.y = e.clientY;
  };
  const onPointerUp = (e: React.PointerEvent) => {
    pinch.current.delete(e.pointerId);
    if (drag.current?.id === e.pointerId) drag.current = null;
  };

  return (
    <div
      ref={box}
      className={cn("screen relative isolate aspect-square w-full select-none overflow-hidden", className)}
      style={{ touchAction: "pan-y" }}
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerUp}
      onPointerCancel={onPointerUp}
      onDoubleClick={() => reset(orbit)}
      role="img"
      aria-label={label}
    >
      {/* Scanlines over the "screen" */}
      <div className="pointer-events-none absolute inset-0 z-10 bg-[repeating-linear-gradient(0deg,rgb(255_255_255/0.025)_0_1px,transparent_1px_3px)]" />
      {edit ? (
        <GlBoundary fallback={<Flat edit={edit} picture={picture} frame={frame} look={settings.look} />}>
          <Canvas
            frameloop={onScreen ? "always" : "never"}
            dpr={[1, 2]}
            camera={{ fov: 30, position: [0, 0, DISTANCE], near: 1, far: 1000 }}
            gl={{ antialias: true, alpha: true, powerPreference: "low-power" }}
            fallback={<Flat edit={edit} picture={picture} frame={frame} look={settings.look} />}
          >
            <Rig orbit={orbit} sway={settings.sway && !reduced}>
              <PlaneStack
                edit={edit}
                picture={picture}
                frame={frame}
                look={settings.look}
                exaggerate={settings.exaggerate}
                solid={settings.solid}
              />
            </Rig>
          </Canvas>
        </GlBoundary>
      ) : (
        <div className="absolute inset-0 grid place-items-center text-white/60">
          {loading ? <Loader2 className="size-8 animate-spin" /> : <span className="text-sm font-bold">Nothing to show</span>}
        </div>
      )}
      <div className="absolute bottom-2 right-2 z-20 flex gap-1" onPointerDown={(e) => e.stopPropagation()}>
        {[
          { icon: ZoomOut, label: "Zoom out", act: () => zoomBy(orbit, 1 / 1.25) },
          { icon: ZoomIn, label: "Zoom in", act: () => zoomBy(orbit, 1.25) },
          { icon: RotateCcw, label: "Straight on again (or double-click)", act: () => reset(orbit) },
        ].map(({ icon: Icon, label: l, act }) => (
          <button
            key={l}
            type="button"
            onClick={act}
            className="grid size-8 place-items-center rounded-md bg-white/10 text-white/70 backdrop-blur hover:bg-white/20 hover:text-white"
            title={l}
            aria-label={l}
          >
            <Icon className="size-4" />
          </button>
        ))}
      </div>
      {children}
    </div>
  );
}
