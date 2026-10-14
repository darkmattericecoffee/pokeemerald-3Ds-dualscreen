#!/usr/bin/env python3
"""
Reads sdmc:/3ds/emerald3ds/dump.raw (3ds_video.c DumpOnKey: L+R+SELECT on the
console) and writes one PNG per record: the two eyes and the battle's
surfaces as the GPU laid them out, untiled.

    dump_frames.py dump.raw OUT_DIR

Each record: tag (u32, a letter), frame (u32), width | height << 16 (u32),
bytes a texel (u32), then width*height*bytes of 8x8 tiles in Morton order.
A render target's buffer is width x height with the screen's rows as the
GPU's columns (the eyes are 240 wide, 400 high): the eye PNGs are turned
upright.
"""
import os
import struct
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("needs Pillow (pip install pillow)")

NAMES = {"L": "left", "R": "right", "G": "logical", "S": "scene", "T": "slots", "W": "world", "O": "objwin"}


def morton(x, y):
    m = 0
    for i in range(3):
        m |= ((x >> i) & 1) << (2 * i) | ((y >> i) & 1) << (2 * i + 1)
    return m


MORTON = [[morton(x, y) for x in range(8)] for y in range(8)]


def untile(data, w, h, bytes_):
    out = bytearray(w * h * 4)
    tiles_w = w // 8
    for ty in range(h // 8):
        for tx in range(tiles_w):
            base = (ty * tiles_w + tx) * 64
            for y in range(8):
                row = MORTON[y]
                for x in range(8):
                    i = (base + row[x]) * bytes_
                    if bytes_ == 4:
                        a, b, g, r = data[i], data[i + 1], data[i + 2], data[i + 3]
                    elif bytes_ == 3:
                        b, g, r = data[i], data[i + 1], data[i + 2]
                        a = 255
                    else:
                        v = data[i] | data[i + 1] << 8
                        if "5551" in untile.fmt:
                            r = (v >> 11) * 255 // 31
                            g = ((v >> 6) & 31) * 255 // 31
                            b = ((v >> 1) & 31) * 255 // 31
                            a = 255 * (v & 1)
                        else:
                            r = (v >> 11) * 255 // 31
                            g = ((v >> 5) & 63) * 255 // 63
                            b = (v & 31) * 255 // 31
                            a = 255
                    o = ((ty * 8 + y) * w + tx * 8 + x) * 4
                    out[o:o + 4] = bytes((r, g, b, a))
    return Image.frombytes("RGBA", (w, h), bytes(out))


untile.fmt = "5551"


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    path, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    data = open(path, "rb").read()
    pos, n = 0, 0
    while pos + 16 <= len(data):
        tag, frame, dims, bytes_ = struct.unpack_from("<4I", data, pos)
        pos += 16
        w, h = dims & 0xFFFF, dims >> 16
        size = w * h * bytes_
        tag = chr(tag)
        untile.fmt = "565" if tag == "W" and bytes_ == 2 else "5551"
        img = untile(data[pos:pos + size], w, h, bytes_)
        pos += size
        if tag in "LR":
            # The GPU's rows are the screen's columns: 240x400 -> 400x240.
            img = img.rotate(90, expand=True)
        name = f"{frame:06d}_{NAMES.get(tag, tag)}.png"
        img.save(os.path.join(out, name))
        print(name, w, h, bytes_)
        n += 1
    print(f"{n} records")


if __name__ == "__main__":
    main()
