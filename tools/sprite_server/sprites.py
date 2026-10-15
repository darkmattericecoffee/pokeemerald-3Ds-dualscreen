"""The game's own pictures for the server's previews, exported once from a pokeemerald tree.

    python3 tools/sprite_server/sprites.py --upstream build/upstream --out ~/emerald3ds-sprite-server/sprites.json

Reads graphics/pokemon/<species>/anim_front.png (two 64x64 frames) and back.png, and the
species names from include/constants/pokedex.h. The JSON holds, per key ('006', 'b006'),
the 16-colour palette and each frame's colour indices as a hex string, plus the names.
The pictures stay out of the repository: the export lives in the server's data folder.

Standard library only (a small PNG reader for the decomp's 4-bit palette PNGs).
"""
import argparse
import json
import os
import struct
import sys
import zlib
from typing import Dict, List, Tuple

SPECIAL_NAMES = {
    "nidoran_f": "Nidoran♀", "nidoran_m": "Nidoran♂", "mr_mime": "Mr. Mime",
    "farfetchd": "Farfetch'd", "ho_oh": "Ho-Oh",
}


def display_name(species: str) -> str:
    return SPECIAL_NAMES.get(species, species.replace("_", " ").title())


def read_png_indexed(path: str) -> Tuple[int, int, List[Tuple[int, int, int]], bytes]:
    """Width, height, palette and one index per pixel of a palette PNG."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s: not a PNG" % path)
    pos, idat, palette = 8, b"", []
    width = height = depth = ctype = interlace = 0
    while pos < len(data):
        length, kind = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            palette = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    if ctype != 3 or interlace != 0 or depth not in (1, 2, 4, 8):
        raise ValueError("%s: not a non-interlaced palette PNG" % path)
    raw = zlib.decompress(idat)
    stride = (width * depth + 7) // 8
    rows, prev, p = [], bytearray(stride), 0
    for _ in range(height):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - 1] if i else 0
            b = prev[i]
            c = prev[i - 1] if i else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 255
        rows.append(bytes(line))
        prev = line
    per = 8 // depth
    mask = (1 << depth) - 1
    out = bytearray()
    for line in rows:
        for x in range(width):
            byte = line[x // per]
            shift = 8 - depth * (x % per + 1)
            out.append((byte >> shift) & mask)
    return width, height, palette, bytes(out)


def dex_names(upstream: str) -> Dict[int, str]:
    names, inside = {}, False
    with open(os.path.join(upstream, "include", "constants", "pokedex.h")) as f:
        for line in f:
            s = line.strip()
            if s == "NATIONAL_DEX_NONE,":
                inside = True
                continue
            if inside:
                if s.startswith("}"):
                    break
                if s.startswith("NATIONAL_DEX_") and s.endswith(","):
                    names[len(names) + 1] = s[len("NATIONAL_DEX_"):-1].lower()
    return names


def _entry(path: str, frames: int) -> dict:
    w, h, pal, idx = read_png_indexed(path)
    if w != 64 or h < 64:
        raise ValueError("%s: %dx%d" % (path, w, h))
    frames = min(frames, h // 64)
    palette = ["#%02x%02x%02x" % tuple(pal[i]) if i < len(pal) else "#000000" for i in range(16)]
    return {"pal": palette,
            "frames": ["".join("%x" % (v & 15) for v in idx[f * 4096:(f + 1) * 4096]) for f in range(frames)]}


def export(upstream: str) -> dict:
    names = dex_names(upstream)
    out = {"names": {}, "sprites": {}}
    missing = []
    for n, species in names.items():
        if n > 386:
            break
        key = "%03u" % n
        out["names"][key] = display_name(species)
        out["names"]["b" + key] = display_name(species)
        folder = os.path.join(upstream, "graphics", "pokemon", species)
        for form in ("a", "normal"):  # Unown's and Castform's first form
            if not os.path.exists(os.path.join(folder, "back.png")) and os.path.isdir(os.path.join(folder, form)):
                folder = os.path.join(folder, form)
        for k, fname, frames in ((key, "anim_front.png", 2), ("b" + key, "back.png", 1)):
            path = os.path.join(folder, fname)
            if not os.path.exists(path) and fname == "anim_front.png":
                path, frames = os.path.join(folder, "front.png"), 1
            try:
                out["sprites"][k] = _entry(path, frames)
            except (OSError, ValueError) as e:
                missing.append("%s (%s)" % (k, e))
    if missing:
        print("missing %d pictures: %s" % (len(missing), ", ".join(missing[:10])), file=sys.stderr)
    return out


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--upstream", required=True, help="the pokeemerald tree (build/upstream)")
    ap.add_argument("--out", required=True, help="where sprites.json goes")
    a = ap.parse_args()
    data = export(a.upstream)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "w") as f:
        json.dump(data, f, separators=(",", ":"))
    print("%d pictures, %d names -> %s" % (len(data["sprites"]), len(data["names"]), a.out))


if __name__ == "__main__":
    main()
