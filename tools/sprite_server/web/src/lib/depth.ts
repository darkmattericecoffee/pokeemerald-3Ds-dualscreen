// The 3D views spread a picture's planes as the console does: one standard for every picture
// (CtrStereoDepths_Get's MON PLANES, 3 screen pixels of disparity from the farthest plane to the
// nearest). The edit file's trailer still carries a depth byte, which only records the console's
// battle depth; it does not change how a picture is drawn.
export const STANDARD_PX = 3;

/** Sprite pixels of 3D depth per pixel of disparity: the standard spreads the stack over 15 of the 64. */
export const DEPTH_SCALE = 5;

/**
 * Where plane `p` of `planes` sits along z, in sprite pixels: the stack spans px * DEPTH_SCALE
 * (times `exaggerate`), centred on the screen plane as on the console ("planes sit around").
 */
export function planeZ(p: number, planes: number, px = STANDARD_PX, exaggerate = 1): number {
  if (planes <= 1) return 0;
  return (p / (planes - 1) - 0.5) * px * DEPTH_SCALE * exaggerate;
}
