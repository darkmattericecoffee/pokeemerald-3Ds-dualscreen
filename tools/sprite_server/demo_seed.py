#!/usr/bin/env python3
"""Fills a throwaway data folder with demo users, consoles, versions and votes, for working on
the web app locally. Never point it at the real data folder.

    mkdir -p /tmp/e3d && cp ~/emerald3ds-sprite-server/sprites.json /tmp/e3d/
    python3 tools/sprite_server/demo_seed.py --data /tmp/e3d
    python3 tools/sprite_server/server.py --data /tmp/e3d serve

The planes are made from the game pictures (sprites.json): domes that rise towards the middle of
the picture, slopes, and a few flat ones, with every depth sweet spot. Passwords go to
<data>/demo-passwords.txt.

Standard library only.
"""
import argparse
import os
import random
import struct
import sys
from collections import deque

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import db  # noqa: E402
import editfile  # noqa: E402
from server import Config  # noqa: E402

USERS = [("darkmatter", "admin", "darkmatter"), ("Nukularkoffee", "artist", "Nukular"),
         ("claude-test", "artist", "Claude"), ("mira", "artist", "Mira")]


def inside_distance(indices: bytes) -> list:
    """Each picture pixel's distance (4-neighbour steps) to the nearest clear pixel; 0 outside."""
    dim = editfile.DIM
    dist = [0 if indices[i] == 0 else -1 for i in range(dim * dim)]
    q = deque(i for i in range(dim * dim) if dist[i] == 0)
    for i in range(dim * dim):  # the picture's border counts as outside
        x, y = i % dim, i // dim
        if dist[i] == -1 and (x in (0, dim - 1) or y in (0, dim - 1)):
            dist[i] = 1
            q.append(i)
    while q:
        i = q.popleft()
        x, y = i % dim, i // dim
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= nx < dim and 0 <= ny < dim:
                j = ny * dim + nx
                if dist[j] == -1:
                    dist[j] = dist[i] + 1
                    q.append(j)
    return dist


def make_edit(key: str, sprite: editfile.Sprite, style: str, planes: int, depth: int, final: bool,
              author: str, revision: int, cover_all: bool) -> bytes:
    dim = editfile.DIM
    frames = len(sprite.frames)
    body = bytearray()
    for f in range(frames):
        idx = sprite.frames[f]
        dist = inside_distance(idx)
        top = max(dist) or 1
        rows = [y for y in range(dim) if any(idx[y * dim + x] for x in range(dim))] or [0, dim - 1]
        y0, y1 = min(rows), max(rows)
        nibbles = []
        for i in range(dim * dim):
            x, y = i % dim, i // dim
            if idx[i] == 0:
                # Many real backs paint planes over the whole square: the previews must hide them.
                nibbles.append(0 if cover_all else 15)
                continue
            if style == "dome":
                t = dist[i] / top
            elif style == "steep":
                t = min(1.0, dist[i] / max(1, top * 0.45))
            elif style == "slope":
                t = (y - y0) / max(1, y1 - y0)
            else:  # flat-ish: two planes
                t = 0.7 if dist[i] > 2 else 0.2
            nibbles.append(min(planes - 1, int(t * planes)))
        for i in range(0, len(nibbles), 2):
            body.append(nibbles[i] | (nibbles[i + 1] << 4))
    head = editfile.MAGIC + struct.pack("<HBBBBBB", editfile.key_to_id(key), planes, frames, 64, 0, 64, 0)
    meta = editfile.META_MAGIC + bytes([1 if final else 0, depth]) + struct.pack("<HI", revision, 830000000)
    return head + bytes(body) + meta + author.encode()[:16].ljust(16, b"\0")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", required=True, help="a throwaway data folder with sprites.json in it")
    ap.add_argument("--seed", type=int, default=3)
    a = ap.parse_args()
    if os.path.realpath(a.data) == os.path.realpath(os.path.expanduser("~/emerald3ds-sprite-server")):
        sys.exit("not the real data folder")
    cfg = Config(a.data)
    if not cfg.sprites:
        sys.exit("put sprites.json in %s first" % a.data)
    path = os.path.join(a.data, "sprites.db")
    db.init(path)
    conn = db.connect(path)
    rnd = random.Random(a.seed)
    lines = []
    uids, dids = {}, {}
    for name, role, shown in USERS:
        row = conn.execute("SELECT id FROM users WHERE name = ?", (name,)).fetchone()
        if row is None:
            password = "demo-%s-pass" % name.lower()
            uid = db.add_user(conn, name, password, role)
            db.set_display_name(conn, uid, shown)
            lines.append("%s (%s): %s" % (name, role, password))
        else:
            uid = row["id"]
        uids[name] = uid
        cur = conn.execute("INSERT INTO devices (user_id, token_hash, console_name, created, last_seen) "
                           "VALUES (?, ?, ?, ?, ?)", (uid, db.sha("demo-%s-%d" % (name, rnd.random())),
                                                      "%s's New 3DS" % shown, db.now() - 86400 * 3, db.now() - 600))
        dids[name] = cur.lastrowid
    styles = ["dome", "steep", "slope", "flat"]
    keys = ["%03d" % n for n in list(range(1, 31)) + [25, 94, 150, 151, 249, 250, 380, 381, 384]]
    keys += ["b%03d" % n for n in range(1, 16)] + ["b025", "b150", "b384"]
    made = 0
    for key in sorted(set(keys)):
        sprite = cfg.sprites.get(key)
        if sprite is None:
            continue
        n_versions = 1 + (rnd.random() < 0.45) + (rnd.random() < 0.3) + (key in ("025", "150", "384", "b025"))
        for v in range(n_versions):
            who = rnd.choice(list(uids))
            data = make_edit(key, sprite, rnd.choice(styles), rnd.choice((3, 4, 4, 5)), rnd.choice((0, 1, 2, 2, 3)),
                             rnd.random() < 0.25, dict((u[0], u[2]) for u in USERS)[who], v + 1,
                             key.startswith("b") and rnd.random() < 0.6)
            edit = editfile.parse(data, key)
            vid, _, dup = db.add_version(conn, edit, uids[who], dids[who])
            if not dup:
                at = db.now() - rnd.randint(60, 86400 * 4)
                conn.execute("UPDATE versions SET uploaded_at = ? WHERE id = ?", (at, vid))
                conn.execute("UPDATE activity SET at = ? WHERE kind = 'upload' AND version_id = ?", (at, vid))
                made += 1
        ids = [r[0] for r in conn.execute("SELECT id FROM versions WHERE mon_key = ?", (key,))]
        for name in uids:
            if len(ids) > 1 and rnd.random() < 0.5:
                db.set_votes(conn, uids[name], key, rnd.sample(ids, rnd.randint(1, min(2, len(ids)))))
    with open(os.path.join(a.data, "demo-passwords.txt"), "a") as f:
        f.write("".join(l + "\n" for l in lines))
    print("%d versions of %d pictures; passwords in %s" % (made, len(set(keys)),
                                                          os.path.join(a.data, "demo-passwords.txt")))


if __name__ == "__main__":
    main()
