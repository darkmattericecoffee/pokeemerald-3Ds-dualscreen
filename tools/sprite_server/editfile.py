"""The plane editor's edit files: parse, validate and render previews.

An edit file is what the on-device editor saves as sdmc:/3ds/emerald3ds/stereo/<key>.bin
(3ds_port/src/3ds_stereo_mon.c, CtrStereoMon_SaveEdit):

    16 bytes   "EM3DSED1", id (u16 LE), planes, frames, 64, 0, 64, 0
    2048 * frames   one nibble per pixel (low nibble first), the pixel's plane;
                    0 = farthest, planes-1 = nearest, >= planes = transparent
    32 bytes   optional trailer "EM3DSMD1", flags (1 = final), depth (the sweet spot,
               1..3; 0 in files from before it, Standard), revision (u16 LE),
               saved (u32 LE, seconds since 2000-01-01), author (16 bytes, NUL padded)

The key is the file's name: NNN for a front (national dex), bNNN a back, tNNN a
trainer's front, pNNN a player's back; the id is the same thing as a number
(0x8000 | n, 0x4000 | n, 0xC000 | n).

Standard library only.
"""
import hashlib
import re
import struct
import zlib
from dataclasses import dataclass
from typing import List, Optional, Sequence, Tuple

MAGIC = b"EM3DSED1"
META_MAGIC = b"EM3DSMD1"
# A second trailer after the first, written by the console when the edit was
# made over someone else's version: the base's revision (u16 LE at 8) and
# author (16 bytes at 16), the lineage ("from X").
META2_MAGIC = b"EM3DSMD2"
HEADER_BYTES = 16
DIM = 64
FRAME_BYTES = DIM * DIM // 2
META_BYTES = 32
MAX_FRAMES = 5
MAX_PLANES = 15
NONE = 15
META_FINAL = 1
META_EPOCH = 946684800  # 2000-01-01 in unix time
MAX_BYTES = HEADER_BYTES + MAX_FRAMES * FRAME_BYTES + 2 * META_BYTES

KIND_PREFIX = {0x0000: "", 0x4000: "t", 0x8000: "b", 0xC000: "p"}
PREFIX_KIND = {v: k for k, v in KIND_PREFIX.items()}
KEY_RE = re.compile(r"^([btp]?)(\d{3,5})$")
KIND_NAMES = {"": "front", "b": "back", "t": "trainer", "p": "player"}

# How deep a picture's planes go, chosen per picture in the editor (3ds_stereo_mon.h
# CTR_SWEET_SPOTS): the stack from its farthest plane to its nearest in GBA pixels per eye.
SWEET_SPOTS = {1: ("Shallow", 2.0), 2: ("Standard", 3.0), 3: ("Deep", 4.5)}
SWEET_STANDARD = 2


def sweet_spot(depth: int) -> int:
    """The trailer's depth byte as a sweet spot: 0 (none chosen) and anything odd are Standard."""
    return depth if depth in SWEET_SPOTS else SWEET_STANDARD


def sweet_name(depth: int) -> str:
    n = sweet_spot(depth)
    return "%d %s" % (n, SWEET_SPOTS[n][0])


# The editor's plane colours (CTR_STEREO_EDIT_COLOURS, RGB565 0xF800 0xFFE0 0x07FF 0xF81F).
EDITOR_COLOURS = [(0xFF, 0x00, 0x00), (0xFF, 0xFF, 0x00), (0x00, 0xFF, 0xFF), (0xFF, 0x00, 0xFF)]

RGB = Tuple[int, int, int]


class EditError(ValueError):
    """The bytes are not a well-formed edit file."""


def key_to_id(key: str) -> Optional[int]:
    """The id of a canonical key ('006', 'b006', 't012'), or None."""
    m = KEY_RE.match(key or "")
    if not m:
        return None
    n = int(m.group(2))
    if n > 0x3FFF or "%03u" % n != m.group(2):
        return None
    if m.group(1) == "" and not 1 <= n <= 511:
        return None
    return PREFIX_KIND[m.group(1)] | n


def id_to_key(ident: int) -> str:
    return "%s%03u" % (KIND_PREFIX[ident & 0xC000], ident & 0x3FFF)


def key_kind(key: str) -> str:
    """'front', 'back', 'trainer' or 'player'."""
    m = KEY_RE.match(key)
    return KIND_NAMES[m.group(1)] if m else "front"


@dataclass
class Edit:
    ident: int
    key: str
    planes: int
    frames: int
    data: bytes
    has_meta: bool = False
    final: bool = False
    depth: int = 0  # the trailer's sweet spot byte, 0 if none was chosen (sweet_spot)
    revision: int = 0
    saved: int = 0  # unix time, 0 if unknown
    author: str = ""
    base_author: str = ""  # the second trailer: whose version this edit was made over
    base_revision: int = 0

    @property
    def sha256(self) -> str:
        return hashlib.sha256(self.data).hexdigest()

    def plane(self, frame: int, x: int, y: int) -> int:
        if frame >= self.frames:
            frame = self.frames - 1
        i = frame * DIM * DIM + y * DIM + x
        v = (self.data[HEADER_BYTES + (i >> 1)] >> ((i & 1) * 4)) & 15
        return NONE if v >= self.planes else v

    def plane_map(self, frame: int = 0) -> List[int]:
        return [self.plane(frame, x, y) for y in range(DIM) for x in range(DIM)]

    def used_planes(self) -> List[int]:
        seen = set()
        for f in range(self.frames):
            seen.update(self.plane_map(f))
        seen.discard(NONE)
        return sorted(seen)


def parse(data: bytes, expect_key: Optional[str] = None) -> Edit:
    """The edit file in `data`, or EditError saying what is wrong."""
    if len(data) < HEADER_BYTES:
        raise EditError("too short for an edit file (%d bytes)" % len(data))
    if len(data) > MAX_BYTES:
        raise EditError("too long for an edit file (%d bytes)" % len(data))
    if data[:8] != MAGIC:
        raise EditError("not an edit file (no EM3DSED1 magic)")
    ident = data[8] | (data[9] << 8)
    planes, frames = data[10], data[11]
    if data[12] != DIM or data[13] != 0 or data[14] != DIM:
        raise EditError("not 64x64 planes")
    if not 1 <= planes <= MAX_PLANES:
        raise EditError("bad plane count %d" % planes)
    if not 1 <= frames <= MAX_FRAMES:
        raise EditError("bad frame count %d" % frames)
    key = id_to_key(ident)
    if key_to_id(key) is None:
        raise EditError("bad id 0x%04x" % ident)
    if expect_key is not None and key != expect_key:
        raise EditError("the file is for %s, not %s" % (key, expect_key))
    body = HEADER_BYTES + frames * FRAME_BYTES
    edit = Edit(ident=ident, key=key, planes=planes, frames=frames, data=bytes(data))
    if len(data) == body:
        return edit
    if len(data) in (body + META_BYTES, body + 2 * META_BYTES):
        t = data[body:body + META_BYTES]
        t2 = data[body + META_BYTES:]
        if t2 and t2[:8] != META2_MAGIC:
            raise EditError("a second trailer without the EM3DSMD2 magic")
        if t2:
            edit.base_revision = t2[8] | (t2[9] << 8)
            edit.base_author = t2[16:32].split(b"\0", 1)[0].decode("utf-8", "replace").strip()
        if t[:8] == META_MAGIC:
            edit.has_meta = True
            edit.final = bool(t[8] & META_FINAL)
            edit.depth = t[9] if t[9] in SWEET_SPOTS else 0
            edit.revision = t[10] | (t[11] << 8)
            saved = struct.unpack_from("<I", t, 12)[0]
            edit.saved = saved + META_EPOCH if saved else 0
            edit.author = t[16:32].split(b"\0", 1)[0].decode("utf-8", "replace").strip()
            return edit
    raise EditError("size %d does not match %d frames" % (len(data), frames))


# ── Colours ─────────────────────────────────────────────────────────────────

def plane_colours(planes: int) -> List[RGB]:
    """A colour per plane, far to near: the editor's four, or a ramp between them."""
    if planes <= len(EDITOR_COLOURS):
        return EDITOR_COLOURS[:planes]
    out = []
    stops = EDITOR_COLOURS
    for p in range(planes):
        t = p * (len(stops) - 1) / (planes - 1)
        i = min(int(t), len(stops) - 2)
        f = t - i
        a, b = stops[i], stops[i + 1]
        out.append(tuple(round(a[c] + (b[c] - a[c]) * f) for c in range(3)))
    return out


def hex_rgb(s: str) -> RGB:
    s = s.lstrip("#")
    return int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16)


class Sprite:
    """A game picture: a 16-colour palette and 64x64 colour indices per frame (0 = clear)."""

    def __init__(self, palette: Sequence[str], frames: Sequence[str]):
        self.palette = [hex_rgb(c) for c in palette]
        self.frames = [bytes(int(ch, 16) for ch in f) for f in frames]

    def index(self, frame: int, x: int, y: int) -> int:
        f = self.frames[min(frame, len(self.frames) - 1)]
        return f[y * DIM + x]


# ── Rendering ───────────────────────────────────────────────────────────────

def _mix(a: RGB, b: RGB, t: float) -> RGB:
    return tuple(round(a[c] * (1 - t) + b[c] * t) for c in range(3))


def render(edit: Optional[Edit], sprite: Optional[Sprite], mode: str = "tint", frame: int = 0,
           layer: Optional[int] = None) -> bytes:
    """64x64 RGBA pixels.

    mode 'tint'   the picture with every pixel mixed with its plane's colour
         'planes' plane colours only
         'layer'  only plane `layer`'s pixels, in the picture's own colours
         'sprite' the picture alone (edit may be None)
    """
    out = bytearray(DIM * DIM * 4)
    colours = plane_colours(edit.planes) if edit is not None else []
    for y in range(DIM):
        for x in range(DIM):
            o = (y * DIM + x) * 4
            idx = sprite.index(frame, x, y) if sprite is not None else 0
            pic = sprite.palette[idx] if idx else None
            if mode == "sprite" or edit is None:
                if pic is not None:
                    out[o:o + 4] = bytes((*pic, 255))
                continue
            p = edit.plane(frame, x, y)
            if p == NONE:
                continue
            if mode == "layer":
                if p != layer:
                    continue
                rgb = pic if pic is not None else colours[p]
            elif mode == "planes" or pic is None:
                rgb = colours[p]
            else:
                rgb = _mix(pic, colours[p], 0.45)
            out[o:o + 4] = bytes((*rgb, 255))
    return bytes(out)


def png(rgba: bytes, width: int = DIM, height: int = DIM) -> bytes:
    """An RGBA PNG of the pixels."""
    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    stride = width * 4
    raw = b"".join(b"\0" + rgba[y * stride:(y + 1) * stride] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))
