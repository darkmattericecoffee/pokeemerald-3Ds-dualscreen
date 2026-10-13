#!/usr/bin/env python3
"""The top screen's button pictures (3ds_port/src/3ds_button_icons.h) as
one sprite sheet to edit by hand, and back.

    python3 tools/sprite_editor/button_pngs.py export   # header -> tools/sprite_editor/buttons.png
    python3 tools/sprite_editor/button_pngs.py import   # buttons.png -> header

The sheet has a 32x32 cell a button, 7 to a row, in the header's order
(A B X Y L R L-small / D-pad up-down, D-pad left-right, pad, C-stick,
START, SELECT), each picture at its cell's top left at its own size.
It is RGBA, one screen pixel a pixel, in four colours:
    clear              (alpha 0)        nothing
    white  248,248,250 (opaque, light)  the button's ink
    dark    30, 32, 38 (opaque, dark)   letters and arrows cut into it
    shadow   0,  0,  0 (alpha 64)       the faint pixel shadow
On import any opaque light pixel is ink, any opaque dark one dark, any
half-clear one shadow, so a slightly different white or grey still works.
Each picture keeps its size; pixels outside it in the cell are ignored.
The touch screen (BTN_TOUCH) is drawn in code.
"""
import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
HEADER = os.path.join(ROOT, "3ds_port", "src", "3ds_button_icons.h")
SHEET = os.path.join(HERE, "buttons.png")
CELL, COLS = 32, 7
COLOURS = {0: (0, 0, 0, 0), 1: (248, 248, 250, 255), 2: (0, 0, 0, 64), 3: (30, 32, 38, 255)}

sys.path.insert(0, HERE)
from gen_sprites import load_png  # noqa: E402


def save_png(path, w, h, pixels):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    raw = b"".join(b"\0" + b"".join(bytes(pixels[r * w + c]) for c in range(w)) for r in range(h))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def read_header():
    text = open(HEADER).read()
    names = re.search(r"enum \{ (.*?) \};", text).group(1).replace(" ", "").split(",")
    entries = re.findall(r"\{(\d+), (\d+), (\d+)\}, /\* (\w+)", text)
    pixels = [int(v) for v in re.search(r"sButtonPixels\[\d+\] = \{(.*?)\};", text, re.S).group(1).split(",") if v.strip()]
    return text, names, entries, pixels


def drawn(entries):
    """The pictures in the sheet: (index, w, h, offset, name), without the drawn ones."""
    return [(i, int(w), int(h), int(o), n) for i, (w, h, o, n) in enumerate([e for e in entries if int(e[0])])]


def export():
    _, _, entries, pixels = read_header()
    pics = drawn(entries)
    rows = (len(pics) + COLS - 1) // COLS
    sw, sh = COLS * CELL, rows * CELL
    sheet = [COLOURS[0]] * (sw * sh)
    for i, w, h, offset, _ in pics:
        x0, y0 = i % COLS * CELL, i // COLS * CELL
        for r in range(h):
            for c in range(w):
                sheet[(y0 + r) * sw + x0 + c] = COLOURS[pixels[offset + r * w + c]]
    save_png(SHEET, sw, sh, sheet)
    print(SHEET)


def code_of(r, g, b, a):
    if a < 32:
        return 0
    if a < 160:
        return 2
    return 1 if r + g + b > 3 * 128 else 3


def import_():
    text, names, entries, _ = read_header()
    _, _, img = load_png(SHEET)
    sheet = {n: (i, w, h) for i, w, h, _, n in drawn(entries)}
    rows, pixels = [], []
    for w, h, offset, name in entries:
        if name not in sheet:
            rows.append(f"    {{0, 0, 0}}, /* {name}: drawn */")
            continue
        i, w, h = sheet[name]
        x0, y0 = i % COLS * CELL, i // COLS * CELL
        rows.append(f"    {{{w}, {h}, {len(pixels)}}}, /* {name} */")
        pixels += [code_of(*img[y0 + r][x0 + c]) for r in range(h) for c in range(w)]
    body = "\n".join("    " + ",".join(str(v) for v in pixels[i:i + 40]) + "," for i in range(0, len(pixels), 40))
    text = re.sub(r"(sButtonIcons\[BTN_COUNT\] = \{\n).*?(\n\};)", lambda m: m.group(1) + "\n".join(rows) + m.group(2), text,
                  flags=re.S)
    text = re.sub(r"sButtonPixels\[\d+\] = \{.*?\};", lambda m: f"sButtonPixels[{len(pixels)}] = {{\n{body}\n}};", text,
                  flags=re.S)
    open(HEADER, "w").write(text)
    print(HEADER)


if __name__ == "__main__":
    {"export": export, "import": import_}.get(sys.argv[1] if len(sys.argv) > 1 else "", lambda: sys.exit(__doc__))()
