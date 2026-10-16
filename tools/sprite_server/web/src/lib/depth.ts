import type { Depth } from "./types";

// A version's depth sweet spot (byte 9 of the edit file's trailer): how far apart the console
// spreads its planes, in screen pixels of disparity from the farthest plane to the nearest.
export const DEPTHS: Record<Depth, { label: string; px: number; short: string }> = {
  1: { label: "Shallow", px: 2, short: "S" },
  2: { label: "Standard", px: 3, short: "M" },
  3: { label: "Deep", px: 4.5, short: "D" },
};

/** Versions that do not say (older editors) are shown at the standard depth. */
export const DEFAULT_DEPTH: Depth = 2;

/** Sprite pixels of 3D depth per pixel of disparity: Standard spreads the stack over 15 of the 64. */
export const DEPTH_SCALE = 5;

export function depthPx(depth: Depth | null | undefined): number {
  return DEPTHS[depth ?? DEFAULT_DEPTH].px;
}

export function depthLabel(depth: Depth | null | undefined): string {
  return depth ? DEPTHS[depth].label : `${DEPTHS[DEFAULT_DEPTH].label} (not set)`;
}

/**
 * Where plane `p` of `planes` sits along z, in sprite pixels: the stack spans depthPx * DEPTH_SCALE
 * (times `exaggerate`), centred on the screen plane as on the console ("planes sit around").
 */
export function planeZ(p: number, planes: number, px: number, exaggerate = 1): number {
  if (planes <= 1) return 0;
  return (p / (planes - 1) - 0.5) * px * DEPTH_SCALE * exaggerate;
}
