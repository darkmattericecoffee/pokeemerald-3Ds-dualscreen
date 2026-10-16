import { describe, expect, it } from "vitest";
import { DEPTH_SCALE, planeZ, STANDARD_PX } from "@/lib/depth";
import { NONE, parseEdit, parsePicture } from "@/lib/editfile";
import { planeColours, planeCounts, renderPixels } from "@/lib/render";

/** An edit file like the console writes: `plane(x, y)` per pixel, optional trailer. */
function makeEdit(opts: {
  planes?: number;
  frames?: number;
  plane: (x: number, y: number, f: number) => number;
  trailer?: { final?: boolean; depth?: number; revision?: number; author?: string };
}): ArrayBuffer {
  const planes = opts.planes ?? 4;
  const frames = opts.frames ?? 1;
  const out = new Uint8Array(16 + frames * 2048 + (opts.trailer ? 32 : 0));
  out.set([..."EM3DSED1"].map((c) => c.charCodeAt(0)), 0);
  out.set([25, 0x00, planes, frames, 64, 0, 64, 0], 8);
  for (let f = 0; f < frames; f++) {
    for (let i = 0; i < 4096; i++) {
      const v = opts.plane(i % 64, Math.floor(i / 64), f) & 15;
      out[16 + f * 2048 + (i >> 1)] |= v << ((i & 1) * 4);
    }
  }
  if (opts.trailer) {
    const t = 16 + frames * 2048;
    out.set([..."EM3DSMD1"].map((c) => c.charCodeAt(0)), t);
    out[t + 8] = opts.trailer.final ? 1 : 0;
    out[t + 9] = opts.trailer.depth ?? 0;
    out[t + 10] = (opts.trailer.revision ?? 0) & 255;
    out.set(new TextEncoder().encode(opts.trailer.author ?? ""), t + 16);
  }
  return out.buffer;
}

describe("parseEdit", () => {
  it("reads planes, frames and the trailer", () => {
    const e = parseEdit(
      makeEdit({ frames: 2, plane: (x, _y, f) => (x < 32 ? f : 15), trailer: { final: true, depth: 3, revision: 7, author: "NUKULAR" } }),
    );
    expect([e.planes, e.frames, e.final, e.revision, e.author]).toEqual([4, 2, true, 7, "NUKULAR"]);
    expect(e.maps[0][0]).toBe(0);
    expect(e.maps[1][0]).toBe(1);
    expect(e.maps[0][40]).toBe(NONE);
    expect(e.used).toEqual([0, 1]);
  });

  it("reads files with and without the trailer", () => {
    expect(parseEdit(makeEdit({ plane: () => 0 })).final).toBe(false);
    expect(parseEdit(makeEdit({ plane: () => 0, trailer: { final: true } })).final).toBe(true);
  });

  it("counts planes at or above the plane count as clear", () => {
    const e = parseEdit(makeEdit({ planes: 2, plane: () => 3 }));
    expect(e.maps[0].every((p) => p === NONE)).toBe(true);
  });

  it("refuses what is not an edit file", () => {
    expect(() => parseEdit(new Uint8Array(100).buffer)).toThrow();
    const short = new Uint8Array(makeEdit({ plane: () => 0 })).slice(0, 1000);
    expect(() => parseEdit(short.buffer)).toThrow();
  });
});

describe("renderPixels", () => {
  // Top half clear in the game picture (index 0), bottom half colour 1.
  const picture = parsePicture({
    palette: ["#000000", "#ff8800", ...Array(14).fill("#000000")],
    frames: ["0".repeat(64 * 32) + "1".repeat(64 * 32)],
  });
  const full = parseEdit(makeEdit({ planes: 2, plane: () => 1 }));

  it("keeps the picture's clear pixels clear even where a plane covers them", () => {
    for (const look of ["colour", "tint", "planes"] as const) {
      const px = renderPixels(full, picture, 0, look);
      expect(px[3]).toBe(0); // (0, 0)
      expect(px[(40 * 64) * 4 + 3]).toBe(255); // (0, 40)
    }
    expect(Array.from(renderPixels(full, picture, 0, "colour").slice(40 * 64 * 4, 40 * 64 * 4 + 3))).toEqual([255, 136, 0]);
  });

  it("shows plane colours without the picture, and one plane at a time for the 3D layers", () => {
    const px = renderPixels(full, null, 0, "colour");
    expect(Array.from(px.slice(0, 4))).toEqual([...planeColours(2)[1], 255]);
    expect(renderPixels(full, picture, 0, "colour", 0)[(40 * 64) * 4 + 3]).toBe(0);
    expect(planeCounts(full, picture, 0)).toEqual([0, 64 * 32]);
  });
});

describe("depth", () => {
  it("spreads every picture's planes around the screen at the one standard", () => {
    expect(STANDARD_PX).toBe(3);
    expect(planeZ(0, 4)).toBeCloseTo(-1.5 * DEPTH_SCALE);
    expect(planeZ(0, 4, 3)).toBeCloseTo(-1.5 * DEPTH_SCALE);
    expect(planeZ(3, 4, 3)).toBeCloseTo(1.5 * DEPTH_SCALE);
    expect(planeZ(0, 1, 3)).toBe(0);
    expect(planeZ(3, 4, 3, 2)).toBeCloseTo(3 * DEPTH_SCALE);
  });
});
