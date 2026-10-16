import { DIM, NONE, type EditFile, type Picture } from "./editfile";

/** How pixels are coloured: the game's colours, mixed with their plane's colour, or plane colours only. */
export type Look = "colour" | "tint" | "planes";

// The editor's plane colours (far to near), the same as on the console.
const EDITOR_COLOURS: [number, number, number][] = [
  [255, 0, 0],
  [255, 255, 0],
  [0, 255, 255],
  [255, 0, 255],
];

export function planeColours(planes: number): [number, number, number][] {
  if (planes <= EDITOR_COLOURS.length) return EDITOR_COLOURS.slice(0, planes);
  const out: [number, number, number][] = [];
  for (let p = 0; p < planes; p++) {
    const t = (p * (EDITOR_COLOURS.length - 1)) / (planes - 1);
    const i = Math.min(Math.floor(t), EDITOR_COLOURS.length - 2);
    const f = t - i;
    const a = EDITOR_COLOURS[i];
    const b = EDITOR_COLOURS[i + 1];
    out.push([0, 1, 2].map((c) => Math.round(a[c] + (b[c] - a[c]) * f)) as [number, number, number]);
  }
  return out;
}

export function cssColour([r, g, b]: [number, number, number]) {
  return `rgb(${r} ${g} ${b})`;
}

/**
 * 64x64 RGBA pixels, top row first. `plane` set: only that plane's pixels (a layer of the 3D stack).
 * The game draws only the picture's own pixels, so with the picture known its clear pixels (index 0)
 * stay clear even where a plane covers them; without it, the planes' colours show the shape.
 */
export function renderPixels(
  edit: EditFile,
  picture: Picture | null,
  frame: number,
  look: Look,
  plane?: number,
): Uint8ClampedArray {
  const out = new Uint8ClampedArray(DIM * DIM * 4);
  const map = edit.maps[Math.min(frame, edit.frames - 1)];
  const idx = picture ? picture.frames[Math.min(frame, picture.frames.length - 1)] : null;
  const colours = planeColours(edit.planes);
  for (let i = 0; i < DIM * DIM; i++) {
    const p = map[i];
    if (p === NONE || (plane !== undefined && p !== plane)) continue;
    let rgb: [number, number, number];
    if (idx) {
      const c = idx[i];
      if (c === 0) continue;
      const pic = picture!.palette[c];
      if (look === "colour") rgb = pic;
      else if (look === "planes") rgb = colours[p];
      else rgb = [0, 1, 2].map((k) => Math.round(pic[k] * 0.55 + colours[p][k] * 0.45)) as [number, number, number];
    } else {
      rgb = colours[p];
    }
    out[i * 4] = rgb[0];
    out[i * 4 + 1] = rgb[1];
    out[i * 4 + 2] = rgb[2];
    out[i * 4 + 3] = 255;
  }
  return out;
}

/** The game picture alone. */
export function renderPicture(picture: Picture, frame: number): Uint8ClampedArray {
  const out = new Uint8ClampedArray(DIM * DIM * 4);
  const idx = picture.frames[Math.min(frame, picture.frames.length - 1)];
  for (let i = 0; i < DIM * DIM; i++) {
    const c = idx[i];
    if (c === 0) continue;
    const [r, g, b] = picture.palette[c];
    out.set([r, g, b, 255], i * 4);
  }
  return out;
}

/** How many pixels of the picture (or of the planes, without it) each plane holds, in a frame. */
export function planeCounts(edit: EditFile, picture: Picture | null, frame: number): number[] {
  const counts = new Array(edit.planes).fill(0);
  const map = edit.maps[Math.min(frame, edit.frames - 1)];
  const idx = picture ? picture.frames[Math.min(frame, picture.frames.length - 1)] : null;
  for (let i = 0; i < DIM * DIM; i++) {
    if (map[i] !== NONE && (!idx || idx[i] !== 0)) counts[map[i]]++;
  }
  return counts;
}
