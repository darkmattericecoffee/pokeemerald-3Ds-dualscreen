import * as React from "react";
import type { Look } from "@/lib/render";

/** How the 3D views draw: shared by every view on a page. */
export interface ViewSettings {
  /** 0, 1, ... or "anim" (the frames in turn, as the game animates). */
  frame: number | "anim";
  look: Look;
  /** Solid: every plane's pixels reach back to the farthest plane, so turning shows no gaps. Off:
   *  flat cards at their depths, as the planes really are. */
  solid: boolean;
  sway: boolean;
  /** Times the version's own depth (1 = as on the console). */
  exaggerate: number;
}

export const defaultSettings: ViewSettings = { frame: 0, look: "colour", solid: true, sway: true, exaggerate: 1 };

/** The frame to draw now: a fixed one, or the frames in turn every 0.5 s. */
export function useFrameIndex(frame: number | "anim", frames: number): number {
  const [tick, setTick] = React.useState(0);
  React.useEffect(() => {
    if (frame !== "anim") return;
    const t = setInterval(() => setTick((n) => n + 1), 500);
    return () => clearInterval(t);
  }, [frame]);
  if (frame === "anim") return tick % Math.max(1, frames);
  return Math.min(frame, Math.max(0, frames - 1));
}
