// The plane editor's edit files (tools/sprite_server/editfile.py has the full description):
//   16 bytes   "EM3DSED1", id (u16 LE), planes, frames, 64, 0, 64, 0
//   2048 * frames   one nibble per pixel (low nibble first): the pixel's plane, 0 = farthest
//   32 bytes   optional trailer "EM3DSMD1", flags (1 = final), depth sweet spot, revision ...

export const DIM = 64;
export const NONE = 15;
const HEADER = 16;
const FRAME_BYTES = (DIM * DIM) / 2;
const META = 32;

export interface EditFile {
  planes: number;
  frames: number;
  /** One plane per pixel per frame (NONE where clear). */
  maps: Uint8Array[];
  final: boolean;
  revision: number;
  depth: 1 | 2 | 3 | null;
  author: string;
  /** The planes any frame uses, far to near. */
  used: number[];
}

const ascii = (bytes: Uint8Array) => String.fromCharCode(...bytes);

export function parseEdit(buffer: ArrayBuffer): EditFile {
  const b = new Uint8Array(buffer);
  if (b.length < HEADER || ascii(b.subarray(0, 8)) !== "EM3DSED1") throw new Error("not an edit file");
  const planes = b[10];
  const frames = b[11];
  if (b[12] !== DIM || b[14] !== DIM || planes < 1 || planes > 15 || frames < 1 || frames > 5) {
    throw new Error("unexpected edit file header");
  }
  const body = HEADER + frames * FRAME_BYTES;
  if (b.length !== body && b.length !== body + META) throw new Error("edit file size does not match");
  const maps: Uint8Array[] = [];
  const seen = new Set<number>();
  for (let f = 0; f < frames; f++) {
    const map = new Uint8Array(DIM * DIM);
    for (let i = 0; i < DIM * DIM; i++) {
      const v = (b[HEADER + f * FRAME_BYTES + (i >> 1)] >> ((i & 1) * 4)) & 15;
      map[i] = v >= planes ? NONE : v;
      if (map[i] !== NONE) seen.add(map[i]);
    }
    maps.push(map);
  }
  const edit: EditFile = {
    planes,
    frames,
    maps,
    final: false,
    revision: 0,
    depth: null,
    author: "",
    used: [...seen].sort((a, z) => a - z),
  };
  if (b.length === body + META && ascii(b.subarray(body, body + 8)) === "EM3DSMD1") {
    const t = b.subarray(body);
    edit.final = (t[8] & 1) === 1;
    edit.depth = t[9] >= 1 && t[9] <= 3 ? (t[9] as 1 | 2 | 3) : null;
    edit.revision = t[10] | (t[11] << 8);
    const end = t.subarray(16, 32).indexOf(0);
    edit.author = new TextDecoder().decode(t.subarray(16, end < 0 ? 32 : 16 + end)).trim();
  }
  return edit;
}

/** The game picture: a palette and a colour index per pixel per frame (0 = clear). */
export interface Picture {
  palette: [number, number, number][];
  frames: Uint8Array[];
}

export function parsePicture(data: { palette: string[]; frames: string[] }): Picture {
  return {
    palette: data.palette.map((c) => [
      parseInt(c.slice(1, 3), 16),
      parseInt(c.slice(3, 5), 16),
      parseInt(c.slice(5, 7), 16),
    ]),
    frames: data.frames.map((hex) => {
      const out = new Uint8Array(hex.length);
      for (let i = 0; i < hex.length; i++) out[i] = parseInt(hex[i], 16);
      return out;
    }),
  };
}

export function frameCount(edit: EditFile, picture: Picture | null): number {
  return Math.max(edit.frames, picture?.frames.length ?? 1);
}
