#!/usr/bin/env python3
"""Fold the 3DS plane editor's SD files back into the stereo pack, and keep them as versioned JSON.

The editor on the 3DS (3ds_port/src/3ds_stereo_edit.c) keeps one file per
picture in sdmc:/3ds/emerald3ds/stereo/: NNN.bin for a front, bNNN.bin a
Pokémon's back, tNNN.bin a trainer's front, pNNN.bin a player's back. Each is
a 16-byte header ("EM3DSED1", u16 id, u8 planes, u8 frames, u16 width, u16
height), then frames x 2048 bytes of nibbles (the planes file's layout), then
optionally a 32-byte metadata trailer ("EM3DSMD1", u8 flags with bit 0 =
finalized, u8 reserved, u16 revision, u32 saved time in seconds since
2000-01-01 by the 3DS clock (0 = unknown), char author[16] NUL-padded). A file
without the trailer reads as author "", revision 0, not finalized, time
unknown. Next to it the same name .log, one line per edit: "f0 brush P5 123px +45".

Import (the default mode):

  python3 tools/sprite_editor/import_sd_edits.py --sd "~/Library/Application Support/Azahar/sdmc/3ds/emerald3ds/stereo" \\
      --pack output/stereo-all-four/mons.bin [--backs output/stereo-all-four/backs.bin] \\
      --out output/stereo-edited [--json-dir master/json] [--author NAME] [--root build/upstream]

writes out/mons.bin (the fronts pack with every front on the SD card
replaced by its SD file; its plane count raised to the largest edited one,
which keeps every nibble valid), out/backs.bin likewise when --backs is given
(its entries keep their own frame counts), and one JSON document per SD file
in --json-dir (default out/json): NNN_name.json for a front, bNNN_name.json
for a Pokémon's back, tNNN.json for a trainer, pNNN.json for a player. When
the document is already there the SD file becomes a new version of it (unless
one with the same planes and author is already there, which then takes the
newer revision's metadata), so pointing several contributors' imports at one
--json-dir accumulates their versions. --author names the SD files that
carry no author of their own.

The document (version 2; version-1 documents of the web prototype or older
imports are read as a single version by author ""):

  {"version": 2, "kind": "stereo-plane-edits", "id": 25, "dex": 25 (fronts only),
   "name": "pikachu", "size": [64, 64], "source": {"picture": "front", "sprite": ..., "pack": ...},
   "versions": [{"author": "arnoud", "revision": 3, "saved": "2026-10-12T09:30:00Z" or null,
                 "finalized": false, "planes": 6, "frames": 2,
                 "frames_planes": [{"rows": [[[x0, x1, plane], ...] per row]} per frame],
                 "edits": ["f0 brush P5 123px +45", ...], "notes": "",
                 "imported": "2026-10-12T10:00:00Z"}, ...],
   "current": 0}

The versions are kept oldest first (by saved time, else import time);
"current" is the newest finalized one, else the newest. The planes are row
runs (x from, x to exclusive, plane), pixels without a plane left out.

Merge documents of the same pictures from different contributors into one
each, all their versions side by side (same planes and author kept once):

  python3 tools/sprite_editor/import_sd_edits.py merge alice/json bob/025_pikachu.json ... --out master/json

List the documents (id, name, versions, authors, the finalized ones):

  python3 tools/sprite_editor/import_sd_edits.py --list [master/json | file.json ...]

(without paths it lists --json-dir, or out/json with --out).
"""
import argparse, datetime, glob, json, os, re, struct, sys

HEADER = struct.Struct("<8sHBBHH")
INDEX = struct.Struct("<HHI")
MAGIC = b"EM3DSTM1"
EDIT_MAGIC = b"EM3DSED1"
EDIT_HEADER = 16
TRAILER = struct.Struct("<8sBBHI16s")
META_MAGIC = b"EM3DSMD1"
FINALIZED = 1
W = H = 64
FRAME_BYTES = W * H // 2
NONE = 15
DOC_VERSION = 2
DOC_KIND = "stereo-plane-edits"
UTC = datetime.timezone.utc
EPOCH_3DS = datetime.datetime(2000, 1, 1, tzinfo=UTC)

KINDS = {"b": 0x8000, "t": 0x4000, "p": 0xC000}
PREFIX = {0: "", 0x8000: "b", 0x4000: "t", 0xC000: "p"}
PICTURE = {0: "front", 0x8000: "back", 0x4000: "trainer", 0xC000: "player"}


# ---------- names from the decomp tree ----------

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


def pic_names(root, prefix):
    """{number: name} of the trainers.h defines `prefix`NAME number."""
    path = os.path.join(root, "include", "constants", "trainers.h")
    out = {}
    if not os.path.exists(path):
        return out
    pat = re.compile(r"#define\s+" + prefix + r"(\w+)\s+(\d+)\s*$")
    with open(path) as f:
        for line in f:
            m = pat.match(line.strip())
            if m:
                out.setdefault(int(m.group(2)), m.group(1).lower())
    return out


def load_names(root):
    return {0: dex_names(root), 0x8000: None, 0x4000: pic_names(root, "TRAINER_PIC_"),
            0xC000: pic_names(root, "TRAINER_BACK_PIC_")}


def picture_name(ident, names):
    kind, n = ident & 0xC000, ident & 0x3FFF
    table = names[0] if kind in (0, 0x8000) else names[kind]
    return table.get(n)


def label(ident):
    """025, b025, t012, p001."""
    return f"{PREFIX[ident & 0xC000]}{ident & 0x3FFF:03d}"


def json_stem(ident, name):
    if name and not ident & 0x4000:  # fronts and Pokémon backs carry the species
        return f"{label(ident)}_{name}"
    return label(ident)


def source_for(ident, name, pack, root):
    kind = ident & 0xC000
    src = {"picture": PICTURE[kind], "sprite": None, "pack": os.path.basename(pack) if pack else None}
    if kind in (0, 0x8000) and name:
        src["sprite"] = f"graphics/pokemon/{name}/{'anim_front' if kind == 0 else 'back'}.png"
    elif name:
        sprite = f"graphics/trainers/{'front' if kind == 0x4000 else 'back'}_pics/{name}.png"
        if os.path.exists(os.path.join(root, sprite)):
            src["sprite"] = sprite
        src["pic"] = ("TRAINER_PIC_" if kind == 0x4000 else "TRAINER_BACK_PIC_") + name.upper()
    return src


# ---------- the SD files ----------

def edit_id(name):
    """The id an edit file's name encodes, or None."""
    stem = name[:-4]
    if not stem:
        return None
    kind = KINDS.get(stem[0], 0)
    digits = stem[1:] if kind else stem
    return (kind | int(digits)) if digits.isdigit() else None


def iso(dt):
    return dt.astimezone(UTC).strftime("%Y-%m-%dT%H:%M:%SZ")


def parse_time(s):
    if not s:
        return None
    try:
        dt = datetime.datetime.fromisoformat(str(s).replace("Z", "+00:00"))
    except ValueError:
        return None
    return dt if dt.tzinfo else dt.replace(tzinfo=UTC)


def read_edit(path):
    """(id, planes, frames, data, meta) of an SD file; meta holds author,
    revision, saved (ISO string or None) and finalized."""
    blob = open(path, "rb").read()
    if len(blob) < EDIT_HEADER or blob[:8] != EDIT_MAGIC:
        raise ValueError("not an edit file")
    _, ident, planes, frames, w, h = HEADER.unpack_from(blob)
    if (w, h) != (W, H) or frames < 1 or planes < 2 or planes > 14:
        raise ValueError("bad header")
    end = EDIT_HEADER + frames * FRAME_BYTES
    data = blob[EDIT_HEADER:end]
    if len(data) != frames * FRAME_BYTES:
        raise ValueError("short file")
    meta = {"author": "", "revision": 0, "saved": None, "finalized": False}
    if len(blob) >= end + TRAILER.size and blob[end:end + 8] == META_MAGIC:
        _, flags, _, revision, saved, author = TRAILER.unpack_from(blob, end)
        meta = {"author": author.split(b"\0", 1)[0].decode("ascii", "replace").strip(),
                "revision": revision,
                "saved": iso(EPOCH_3DS + datetime.timedelta(seconds=saved)) if saved else None,
                "finalized": bool(flags & FINALIZED)}
    return ident, planes, frames, data, meta


def nibbles(data, frame):
    out = []
    base = frame * FRAME_BYTES
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


# ---------- the pack ----------

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
        entries[ident] = (reserved, blob[off:off + n * FRAME_BYTES])
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
                    data = data + data[-FRAME_BYTES:] * (frames - eframes)
                data = data[:frames * FRAME_BYTES]
        else:
            reserved, data = entries[ident]
        out += INDEX.pack(ident, reserved, 16 + 8 * len(ids) + len(body))
        body += data
    out += body
    open(out_path, "wb").write(out)
    print(f"wrote {out_path}: {len(ids)} entries, {max_planes} planes, up to {max_frames} frames")


# ---------- the JSON documents ----------

def new_version(planes, frames, frames_planes, edits, author="", revision=0, saved=None,
                finalized=False, notes="", imported=None):
    return {"author": author, "revision": revision, "saved": saved, "finalized": bool(finalized),
            "planes": planes, "frames": frames, "frames_planes": frames_planes,
            "edits": edits, "notes": notes, "imported": imported or iso(datetime.datetime.now(UTC))}


def load_doc(path):
    """A document read as version 2 (a version-1 one becomes one version by author "")."""
    doc = json.load(open(path))
    if doc.get("kind") != DOC_KIND:
        raise ValueError("not a stereo-plane-edits document")
    if doc.get("version") == 1:
        mtime = iso(datetime.datetime.fromtimestamp(os.path.getmtime(path), UTC))
        ident = doc["dex"]
        v = new_version(doc["planes"], doc["frames"], doc["frames_planes"], doc.get("edits", []),
                        saved=doc.get("saved"), notes=doc.get("notes", ""), imported=doc.get("saved") or mtime)
        doc = {"version": DOC_VERSION, "kind": DOC_KIND, "id": ident, "dex": ident, "name": doc.get("name"),
               "size": doc.get("size", [W, H]), "source": dict(doc.get("source", {}), picture="front"),
               "versions": [v], "current": 0}
    elif doc.get("version") != DOC_VERSION:
        raise ValueError(f"document version {doc.get('version')} unknown")
    if list(doc.get("size", [W, H])) != [W, H]:
        raise ValueError(f"size {doc.get('size')} is not {W}x{H}")
    return doc


def same_version(a, b):
    return (a.get("author", "") == b.get("author", "") and a["planes"] == b["planes"]
            and a["frames"] == b["frames"] and a["frames_planes"] == b["frames_planes"])


def version_time(v):
    return parse_time(v.get("saved")) or parse_time(v.get("imported")) or EPOCH_3DS


def add_version(doc, v):
    """Adds v to doc's versions; returns "added", "updated" (an identical one took
    v's newer revision, saved time, finalized flag and log, if it has one) or "kept"."""
    for old in doc["versions"]:
        if same_version(old, v):
            if v.get("revision", 0) > old.get("revision", 0) or (v.get("finalized") and not old.get("finalized")):
                for k in ("revision", "saved", "finalized"):
                    old[k] = v.get(k, old.get(k))
                if v.get("edits"):
                    old["edits"] = v["edits"]
                if v.get("notes") and not old.get("notes"):
                    old["notes"] = v["notes"]
                return "updated"
            return "kept"
    doc["versions"].append(v)
    return "added"


def settle(doc):
    """Versions oldest first; current = the newest finalized one, else the newest."""
    doc["versions"].sort(key=lambda v: (version_time(v), v.get("revision", 0)))
    final = [i for i, v in enumerate(doc["versions"]) if v.get("finalized")]
    doc["current"] = final[-1] if final else len(doc["versions"]) - 1
    return doc


def write_doc(doc, path):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w") as f:
        json.dump(doc, f, separators=(",", ":"))


def find_doc(json_dir, ident):
    """The path of the document for ident already in json_dir, or None (it may
    have been written with or without the species' name)."""
    lab = label(ident)
    for path in sorted(glob.glob(os.path.join(json_dir, lab + ".json")) +
                       glob.glob(os.path.join(json_dir, lab + "_*.json"))):
        try:
            if load_doc(path)["id"] == ident:
                return path
        except (ValueError, KeyError, json.JSONDecodeError):
            continue
    return None


def doc_paths(paths):
    out = []
    for p in paths:
        p = os.path.expanduser(p)
        out += sorted(glob.glob(os.path.join(p, "*.json"))) if os.path.isdir(p) else [p]
    return out


def version_tag(v):
    t = (v.get("saved") or "")[:10] or "?"
    return f"r{v.get('revision', 0)} {v.get('author') or '(unknown)'} {t}{' [final]' if v.get('finalized') else ''}"


def list_docs(paths):
    files = doc_paths(paths)
    if not files:
        print("no documents in", " ".join(paths))
        return 1
    for path in files:
        try:
            doc = load_doc(path)
        except (ValueError, KeyError, json.JSONDecodeError) as e:
            print(f"{path}: {e}", file=sys.stderr)
            continue
        vs = doc["versions"]
        authors = sorted({v.get("author") or "(unknown)" for v in vs})
        final = [version_tag(v) for v in vs if v.get("finalized")]
        cur = vs[doc["current"]] if vs else None
        print(f"{label(doc['id']):>5} id {doc['id']:#06x} {doc.get('name') or '-':<14} "
              f"{len(vs)} version{'s' if len(vs) != 1 else ' '}  authors: {', '.join(authors)}  "
              f"finalized: {'; '.join(final) or 'none'}  current: {version_tag(cur) if cur else '-'}")
    return 0


def merge_main(argv):
    ap = argparse.ArgumentParser(prog="import_sd_edits.py merge",
                                 description="Merge documents of the same pictures (from different contributors) "
                                             "into one each, with all their versions.")
    ap.add_argument("docs", nargs="+", help="JSON documents, or directories of them")
    ap.add_argument("--out", required=True, help="directory for the merged documents (same-named files are overwritten)")
    a = ap.parse_args(argv)
    by_id = {}
    for path in doc_paths(a.docs):
        try:
            doc = load_doc(path)
        except (ValueError, KeyError, json.JSONDecodeError) as e:
            print(f"{path}: {e}, skipped", file=sys.stderr)
            continue
        ident = doc["id"]
        if ident not in by_id:
            by_id[ident] = dict(doc, versions=[])
        base, counts = by_id[ident], {"added": 0, "updated": 0, "kept": 0}
        for k in ("name", "dex"):
            if base.get(k) is None and doc.get(k) is not None:
                base[k] = doc[k]
        for v in doc["versions"]:
            counts[add_version(base, v)] += 1
        print(f"{path}: {label(ident)} {counts['added']} added, {counts['updated']} updated, {counts['kept']} already there")
    if not by_id:
        return 1
    for ident, doc in sorted(by_id.items()):
        settle(doc)
        path = os.path.join(a.out, json_stem(ident, doc.get("name")) + ".json")
        write_doc(doc, path)
        print(f"wrote {path}: {len(doc['versions'])} version{'s' if len(doc['versions']) != 1 else ''}, current {version_tag(doc['versions'][doc['current']])}")
    return 0


# ---------- import ----------

def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if argv[:1] == ["merge"]:
        return merge_main(argv[1:])
    ap = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    ap.add_argument("--sd", help="the SD card's stereo/ directory")
    ap.add_argument("--pack", help="the fronts pack to fold the edits into")
    ap.add_argument("--backs", help="the backs pack (Pokémon backs, trainers, players) to fold theirs into")
    ap.add_argument("--out", help="where mons.bin / backs.bin are written")
    ap.add_argument("--json-dir", help="where the master JSON documents live (default <out>/json)")
    ap.add_argument("--author", default="", help="the author of SD files that carry none")
    ap.add_argument("--root", default="build/upstream", help="the decomp tree, for the pictures' names")
    ap.add_argument("--list", nargs="*", metavar="PATH",
                    help="list documents (files or directories; default --json-dir) instead of importing")
    a = ap.parse_args(argv)
    json_dir = a.json_dir or (os.path.join(a.out, "json") if a.out else None)
    if a.list is not None:
        paths = a.list or ([json_dir] if json_dir else [])
        if not paths:
            ap.error("--list needs paths, --json-dir or --out")
        return list_docs(paths)
    if not a.sd or not a.out:
        ap.error("--sd and --out are needed to import")
    sd = os.path.expanduser(a.sd)
    names = load_names(a.root)
    edited, edited_backs, found = {}, {}, []
    for name in sorted(os.listdir(sd)):
        if not name.endswith(".bin") or edit_id(name) is None:
            continue
        try:
            ident, eplanes, eframes, data, meta = read_edit(os.path.join(sd, name))
        except ValueError as e:
            print(f"{name}: {e}", file=sys.stderr)
            continue
        if ident != edit_id(name):
            print(f"{name}: names id {edit_id(name):04x} but holds {ident:04x}, skipped", file=sys.stderr)
            continue
        log_path = os.path.join(sd, name[:-4] + ".log")
        log = open(log_path).read().splitlines() if os.path.exists(log_path) else []
        (edited_backs if ident & 0xC000 else edited)[ident] = (eplanes, eframes, data, log)
        found.append((ident, eplanes, eframes, data, log, meta))
        print(f"{label(ident)} {picture_name(ident, names) or '?'}: {eplanes} planes, {eframes} frames, {len(log)} edits, "
              f"r{meta['revision']} by {meta['author'] or a.author or '(unknown)'}"
              f"{', saved ' + meta['saved'] if meta['saved'] else ''}{', finalized' if meta['finalized'] else ''}")
    if not found:
        print("no edit files in", sd)
        return 1
    os.makedirs(a.out, exist_ok=True)
    if edited:
        if a.pack:
            fold(a.pack, edited, os.path.join(a.out, "mons.bin"))
        else:
            print(f"{len(edited)} front edits need --pack to be folded", file=sys.stderr)
    if edited_backs:
        if a.backs:
            fold(a.backs, edited_backs, os.path.join(a.out, "backs.bin"))
        else:
            print(f"{len(edited_backs)} back/trainer/player edits need --backs to be folded", file=sys.stderr)
    for ident, eplanes, eframes, data, log, meta in found:
        name = picture_name(ident, names)
        v = new_version(eplanes, eframes, [{"rows": runs(nibbles(data, f))} for f in range(eframes)], log,
                        author=meta["author"] or a.author, revision=meta["revision"], saved=meta["saved"],
                        finalized=meta["finalized"])
        path = find_doc(json_dir, ident)
        if path:
            doc = load_doc(path)
            if doc.get("name") is None and name:
                doc["name"] = name
        else:
            path = os.path.join(json_dir, json_stem(ident, name) + ".json")
            doc = {"version": DOC_VERSION, "kind": DOC_KIND, "id": ident}
            if not ident & 0xC000:
                doc["dex"] = ident
            doc.update({"name": name, "size": [W, H],
                        "source": source_for(ident, name, a.backs if ident & 0xC000 else a.pack, a.root),
                        "versions": [], "current": 0})
        what = add_version(doc, v)
        settle(doc)
        write_doc(doc, path)
        print(f"wrote {path}: version {what}, {len(doc['versions'])} in all, "
              f"current {version_tag(doc['versions'][doc['current']])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
