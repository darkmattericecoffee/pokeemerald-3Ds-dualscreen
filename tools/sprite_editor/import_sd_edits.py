#!/usr/bin/env python3
"""Fold the 3DS plane editor's SD files back into the stereo pack, and export them as JSON.

The editor on the 3DS (3ds_port/src/3ds_stereo_edit.c) keeps one file per
picture in sdmc:/3ds/emerald3ds/stereo/: NNN.bin for a front, bNNN.bin a
Pokémon's back, tNNN.bin a trainer's front, pNNN.bin a player's back (header
"EM3DSED1", u16 id, u8 planes, u8 frames, u16 width, u16 height, u32
reserved, then frames x 2048 bytes of nibbles, the planes file's layout) and
the same name .log (one line per edit: "f0 brush P5 123px +45").

  python3 tools/sprite_editor/import_sd_edits.py --sd "~/Library/Application Support/Azahar/sdmc/3ds/emerald3ds/stereo" \
      --pack output/stereo-all-four/mons.bin [--backs output/stereo-all-four/backs.bin] \
      --out output/stereo-edited [--root build/upstream]

Writes out/mons.bin (the fronts pack with the edited entries replaced; its
plane count raised to the largest edited one, which keeps every nibble
valid), out/backs.bin likewise when --backs is given (its entries keep their
own frame counts), and out/json/NNN_name.json per edited front (the web
prototype's document:
version, dex, planes, frames_planes as row runs, the edit log as text) for
the master JSON files and as guidance for the AI passes.
"""
import argparse, json, os, struct, sys

HEADER = struct.Struct("<8sHBBHH")
INDEX = struct.Struct("<HHI")
MAGIC = b"EM3DSTM1"
EDIT_MAGIC = b"EM3DSED1"
W = H = 64
NONE = 15


def dex_names(root):
    names, inside = {}, False
    path = os.path.join(root, "include", "constants", "pokedex.h")
    if not os.path.exists(path):
        return {}
    with open(path) as f:
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


KINDS = {"b": 0x8000, "t": 0x4000, "p": 0xC000}


def edit_id(name):
    """The id an edit file's name encodes, or None."""
    stem = name[:-4]
    kind = KINDS.get(stem[0], 0)
    digits = stem[1:] if kind else stem
    return (kind | int(digits)) if digits.isdigit() else None


def read_edit(path):
    blob = open(path, "rb").read()
    if len(blob) < 16 or blob[:8] != EDIT_MAGIC:
        raise ValueError("not an edit file")
    national = blob[8] | (blob[9] << 8)
    planes, frames = blob[10], blob[11]
    if (blob[12], blob[14]) != (W, H) or frames < 1 or planes < 2 or planes > 14:
        raise ValueError("bad header")
    data = blob[16:16 + frames * W * H // 2]
    if len(data) != frames * W * H // 2:
        raise ValueError("short file")
    return national, planes, frames, data


def nibbles(data, frame):
    out = []
    base = frame * W * H // 2
    for i in range(W * H):
        b = data[base + (i >> 1)]
        out.append((b >> ((i & 1) * 4)) & 15)
    return out


def runs(px):
    rows = []
    for y in range(H):
        r, x = [], 0
        while x < W:
            v = px[y * W + x]
            if v == NONE:
                x += 1
                continue
            e = x
            while e < W and px[y * W + e] == v:
                e += 1
            r.append([x, e, v])
            x = e
        rows.append(r)
    return rows


def fold(pack_path, edited, out_path):
    """The pack with the edited entries replaced: fronts keep the pack's frame
    count (an edit padded with its last frame); backs entries keep their own."""
    blob = open(pack_path, "rb").read()
    magic, count, planes, frames, w, h = HEADER.unpack_from(blob)
    assert magic == MAGIC and (w, h) == (W, H), "not a planes pack"
    entries = {}
    for i in range(count):
        ident, reserved, off = INDEX.unpack_from(blob, 16 + 8 * i)
        n = reserved if 0 < reserved <= frames else frames
        entries[ident] = (reserved, blob[off:off + n * W * H // 2])
    max_planes = max([planes] + [e[0] for e in edited.values()])
    max_frames = max([frames] + [e[1] for e in edited.values()])
    ids = sorted(set(entries) | set(edited))
    out = bytearray(HEADER.pack(MAGIC, len(ids), max_planes, max_frames, W, H))
    body = bytearray()
    for ident in ids:
        if ident in edited:
            eplanes, eframes, data, _ = edited[ident]
            if ident & 0xC000:
                reserved = eframes
            else:
                reserved = 0
                if eframes < frames:
                    data = data + data[-W * H // 2:] * (frames - eframes)
                data = data[:frames * W * H // 2]
        else:
            reserved, data = entries[ident]
        out += INDEX.pack(ident, reserved, 16 + 8 * len(ids) + len(body))
        body += data
    out += body
    open(out_path, "wb").write(out)
    print(f"wrote {out_path}: {len(ids)} entries, {max_planes} planes, up to {max_frames} frames")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sd", required=True, help="the SD card's stereo/ directory")
    ap.add_argument("--pack", required=True, help="the fronts pack to fold the edits into")
    ap.add_argument("--backs", help="the backs pack (Pokémon backs, trainers, players) to fold theirs into")
    ap.add_argument("--out", required=True)
    ap.add_argument("--root", default="build/upstream", help="the decomp tree, for the species' names")
    a = ap.parse_args()
    sd = os.path.expanduser(a.sd)
    names = dex_names(a.root)
    edited, edited_backs = {}, {}
    for name in sorted(os.listdir(sd)):
        if not name.endswith(".bin") or edit_id(name) is None:
            continue
        try:
            ident, eplanes, eframes, data = read_edit(os.path.join(sd, name))
        except ValueError as e:
            print(f"{name}: {e}", file=sys.stderr)
            continue
        if ident != edit_id(name):
            print(f"{name}: names id {edit_id(name):04x} but holds {ident:04x}, skipped", file=sys.stderr)
            continue
        log_path = os.path.join(sd, name[:-4] + ".log")
        log = open(log_path).read().splitlines() if os.path.exists(log_path) else []
        (edited_backs if ident & 0xC000 else edited)[ident] = (eplanes, eframes, data, log)
        label = f"#{ident:03d} {names.get(ident, '?')}" if not ident & 0xC000 else f"id {ident:04x}"
        print(f"{label}: {eplanes} planes, {eframes} frames, {len(log)} edits")
    if not edited and not edited_backs:
        print("no edit files in", sd)
        return 1
    os.makedirs(os.path.join(a.out, "json"), exist_ok=True)
    if edited:
        fold(a.pack, edited, os.path.join(a.out, "mons.bin"))
    if edited_backs:
        if a.backs:
            fold(a.backs, edited_backs, os.path.join(a.out, "backs.bin"))
        else:
            print(f"{len(edited_backs)} back/trainer/player edits need --backs to be folded", file=sys.stderr)
    for dex, (eplanes, eframes, data, log) in edited.items():
        name = names.get(dex, f"dex{dex}")
        doc = {
            "version": 1, "kind": "stereo-plane-edits", "dex": dex, "name": name, "size": [W, H],
            "planes": eplanes, "frames": eframes,
            "source": {"sprite": f"graphics/pokemon/{name}/anim_front.png", "pack": os.path.basename(a.pack)},
            "frames_planes": [{"rows": runs(nibbles(data, f))} for f in range(eframes)],
            "edits": log, "notes": "",
        }
        path = os.path.join(a.out, "json", f"{dex:03d}_{name}.json")
        json.dump(doc, open(path, "w"), separators=(",", ":"))
        print("wrote", path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
