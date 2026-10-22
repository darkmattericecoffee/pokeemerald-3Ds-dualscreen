"""The server's SQLite database: users, sessions, consoles, versions, votes and the current pick.

One file (sprites.db in the data folder), WAL mode, a connection per request.
Standard library only.
"""
import hashlib
import hmac
import json
import os
import secrets
import sqlite3
import time
from typing import Optional

import signing

SCHEMA = """
CREATE TABLE IF NOT EXISTS users (
    id         INTEGER PRIMARY KEY,
    name       TEXT NOT NULL UNIQUE COLLATE NOCASE,
    pass_hash  TEXT NOT NULL,
    role       TEXT NOT NULL CHECK (role IN ('admin', 'artist')),
    disabled   INTEGER NOT NULL DEFAULT 0,
    created    INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS sessions (
    token_hash TEXT PRIMARY KEY,
    user_id    INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    csrf       TEXT NOT NULL,
    created    INTEGER NOT NULL,
    expires    INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS pairing_codes (
    code_hash  TEXT PRIMARY KEY,
    user_id    INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created    INTEGER NOT NULL,
    expires    INTEGER NOT NULL,
    used       INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS devices (
    id           INTEGER PRIMARY KEY,
    user_id      INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    token_hash   TEXT NOT NULL UNIQUE,
    console_name TEXT NOT NULL,
    created      INTEGER NOT NULL,
    last_seen    INTEGER,
    revoked      INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS versions (
    id          INTEGER PRIMARY KEY,
    mon_key     TEXT NOT NULL,
    user_id     INTEGER NOT NULL REFERENCES users(id),
    device_id   INTEGER REFERENCES devices(id),
    planes      INTEGER NOT NULL,
    frames      INTEGER NOT NULL,
    final       INTEGER NOT NULL,
    revision    INTEGER NOT NULL,
    author_tag  TEXT NOT NULL,
    saved_at    INTEGER NOT NULL,
    sha256      TEXT NOT NULL,
    size        INTEGER NOT NULL,
    data        BLOB NOT NULL,
    uploaded_at INTEGER NOT NULL,
    UNIQUE (mon_key, sha256)
);
CREATE INDEX IF NOT EXISTS versions_by_mon ON versions (mon_key, uploaded_at);
CREATE TABLE IF NOT EXISTS current (
    mon_key    TEXT PRIMARY KEY,
    version_id INTEGER NOT NULL REFERENCES versions(id),
    pinned     INTEGER NOT NULL DEFAULT 0,
    seq        INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS current_by_seq ON current (seq);
-- Each user's favourites: at most two versions per picture (set_votes).
CREATE TABLE IF NOT EXISTS votes (
    user_id    INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    mon_key    TEXT NOT NULL,
    version_id INTEGER NOT NULL REFERENCES versions(id) ON DELETE CASCADE,
    voted_at   INTEGER NOT NULL,
    PRIMARY KEY (user_id, version_id)
);
CREATE INDEX IF NOT EXISTS votes_by_mon ON votes (mon_key, user_id);
-- What happened, for the web app's feed: every upload, vote taken or given back, pin, unpin,
-- delete, name change and console link, with who did it and when. Versions and users it names
-- may be gone by now (the feed says so).
CREATE TABLE IF NOT EXISTS activity (
    id         INTEGER PRIMARY KEY,
    at         INTEGER NOT NULL,
    kind       TEXT NOT NULL,
    user_id    INTEGER REFERENCES users(id) ON DELETE SET NULL,
    mon_key    TEXT,
    version_id INTEGER,
    detail     TEXT NOT NULL DEFAULT '{}'
);
CREATE INDEX IF NOT EXISTS activity_by_time ON activity (at, id);
CREATE TABLE IF NOT EXISTS meta (k TEXT PRIMARY KEY, v INTEGER NOT NULL);
INSERT OR IGNORE INTO meta (k, v) VALUES ('seq', 0);
-- Moves on with every upload, vote, pin and delete: the web pages poll it.
INSERT OR IGNORE INTO meta (k, v) VALUES ('rev', 0);
"""

PBKDF2_ROUNDS = 200_000
SESSION_SECONDS = 30 * 24 * 3600
PAIR_SECONDS = 10 * 60
PAIR_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"  # no 0/O, 1/I


def now() -> int:
    return int(time.time())


def sha(s: str) -> str:
    return hashlib.sha256(s.encode()).hexdigest()


def hash_password(password: str) -> str:
    salt = secrets.token_bytes(16)
    dk = hashlib.pbkdf2_hmac("sha256", password.encode(), salt, PBKDF2_ROUNDS)
    return "pbkdf2_sha256$%d$%s$%s" % (PBKDF2_ROUNDS, salt.hex(), dk.hex())


def check_password(password: str, stored: str) -> bool:
    try:
        algo, rounds, salt, want = stored.split("$")
        if algo != "pbkdf2_sha256":
            return False
        dk = hashlib.pbkdf2_hmac("sha256", password.encode(), bytes.fromhex(salt), int(rounds))
        return hmac.compare_digest(dk.hex(), want)
    except ValueError:
        return False


def connect(path: str) -> sqlite3.Connection:
    conn = sqlite3.connect(path, timeout=10, isolation_level=None)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys = ON")
    conn.execute("PRAGMA busy_timeout = 10000")
    return conn


def init(path: str) -> None:
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    conn = connect(path)
    conn.execute("PRAGMA journal_mode = WAL")
    conn.executescript(SCHEMA)
    # Added later: the name a user goes by on the site (set on the console or the account page).
    if "display_name" not in [r["name"] for r in conn.execute("PRAGMA table_info(users)")]:
        conn.execute("ALTER TABLE users ADD COLUMN display_name TEXT")
    # Added later: the version's depth sweet spot, its trailer's byte (editfile.SWEET_SPOTS; 0 none
    # chosen, Standard). The versions from before carry none: their files all have 0 there.
    if "depth" not in [r["name"] for r in conn.execute("PRAGMA table_info(versions)")]:
        conn.execute("ALTER TABLE versions ADD COLUMN depth INTEGER NOT NULL DEFAULT 0")
    # Added later: the lineage, whose version the edit was made over (the file's second
    # trailer, editfile.META2_MAGIC); empty for one made from the game's planes.
    if "base_author" not in [r["name"] for r in conn.execute("PRAGMA table_info(versions)")]:
        conn.execute("ALTER TABLE versions ADD COLUMN base_author TEXT NOT NULL DEFAULT ''")
        conn.execute("ALTER TABLE versions ADD COLUMN base_revision INTEGER NOT NULL DEFAULT 0")
    # Added later: one version per person per picture. A newer upload of the same person replaces
    # the older one, which stays as history (replaced_by = the newer one's id) but is not listed,
    # voted on or current. Older databases are brought in line once, here.
    if "replaced_by" not in [r["name"] for r in conn.execute("PRAGMA table_info(versions)")]:
        conn.execute("ALTER TABLE versions ADD COLUMN replaced_by INTEGER")
    with Tx(conn):
        for r in conn.execute(
                "SELECT mon_key, user_id FROM versions WHERE replaced_by IS NULL GROUP BY mon_key, user_id "
                "HAVING COUNT(*) > 1").fetchall():
            ids = [x["id"] for x in conn.execute(
                "SELECT id FROM versions WHERE mon_key = ? AND user_id = ? AND replaced_by IS NULL "
                "ORDER BY uploaded_at DESC, id DESC", (r["mon_key"], r["user_id"]))]
            for old in ids[1:]:
                _replace(conn, old, ids[0], r["mon_key"])
            cur = conn.execute("SELECT pinned FROM current WHERE mon_key = ?", (r["mon_key"],)).fetchone()
            if cur is None or not cur["pinned"]:
                _set_best(conn, r["mon_key"])
    # Added later: the activity feed. A database from before gets its uploads and votes once, so the
    # feed starts with the history it has (pins and deletes from before left no trace).
    with Tx(conn):
        if conn.execute("SELECT 1 FROM meta WHERE k = 'activity_filled'").fetchone() is None:
            conn.execute("INSERT INTO activity (at, kind, user_id, mon_key, version_id, detail) "
                         "SELECT uploaded_at, 'upload', user_id, mon_key, id, "
                         "json_object('final', final, 'revision', revision) FROM versions ORDER BY uploaded_at, id")
            conn.execute("INSERT INTO activity (at, kind, user_id, mon_key, version_id) "
                         "SELECT voted_at, 'vote', user_id, mon_key, version_id FROM votes ORDER BY voted_at")
            conn.execute("INSERT INTO meta (k, v) VALUES ('activity_filled', 1)")
    conn.close()


class Tx:
    """BEGIN IMMEDIATE ... COMMIT (or ROLLBACK on an exception)."""

    def __init__(self, conn: sqlite3.Connection):
        self.conn = conn

    def __enter__(self) -> sqlite3.Connection:
        self.conn.execute("BEGIN IMMEDIATE")
        return self.conn

    def __exit__(self, kind, value, tb) -> None:
        self.conn.execute("ROLLBACK" if kind else "COMMIT")


def next_seq(conn: sqlite3.Connection) -> int:
    conn.execute("UPDATE meta SET v = v + 1 WHERE k = 'seq'")
    return conn.execute("SELECT v FROM meta WHERE k = 'seq'").fetchone()[0]


def current_seq(conn: sqlite3.Connection) -> int:
    return conn.execute("SELECT v FROM meta WHERE k = 'seq'").fetchone()[0]


def bump_rev(conn: sqlite3.Connection) -> None:
    conn.execute("UPDATE meta SET v = v + 1 WHERE k = 'rev'")


def current_rev(conn: sqlite3.Connection) -> int:
    row = conn.execute("SELECT v FROM meta WHERE k = 'rev'").fetchone()
    return row[0] if row else 0


# ── Activity (the feed) ─────────────────────────────────────────────────────

ACTIVITY_KINDS = ("upload", "vote", "unvote", "pin", "unpin", "delete", "name", "console")


def log(conn: sqlite3.Connection, kind: str, user_id: Optional[int] = None, mon_key: Optional[str] = None,
        version_id: Optional[int] = None, **detail) -> int:
    """One line of the feed. Inside the caller's transaction when there is one."""
    assert kind in ACTIVITY_KINDS, kind
    return conn.execute(
        "INSERT INTO activity (at, kind, user_id, mon_key, version_id, detail) VALUES (?, ?, ?, ?, ?, ?)",
        (now(), kind, user_id, mon_key, version_id, json.dumps(detail, separators=(",", ":")))).lastrowid


def activity(conn: sqlite3.Connection, before: int = 0, limit: int = 40, kinds=None) -> list:
    """The newest `limit` lines (older than line `before` when given), newest first by time (lines
    brought in from before the feed, and a seed's back-dated ones, are not in id order), each with who
    did it as the site shows them and what became of the version it names: whether it still exists,
    was replaced by its person's newer one, and is the picture's current one, plus its owner."""
    where, args = [], []
    if before:
        edge = conn.execute("SELECT at FROM activity WHERE id = ?", (before,)).fetchone()
        if edge is not None:
            where.append("(a.at < ? OR (a.at = ? AND a.id < ?))")
            args.extend([edge["at"], edge["at"], before])
    if kinds:
        where.append("a.kind IN (%s)" % ",".join("?" * len(kinds)))
        args.extend(kinds)
    return conn.execute(
        "SELECT a.*, COALESCE(u.display_name, u.name) AS who, v.id AS live_id, v.replaced_by, v.final AS live_final, "
        "v.revision AS live_revision, COALESCE(o.display_name, o.name) AS owner, c.version_id AS current_id "
        "FROM activity a LEFT JOIN users u ON u.id = a.user_id LEFT JOIN versions v ON v.id = a.version_id "
        "LEFT JOIN users o ON o.id = v.user_id LEFT JOIN current c ON c.mon_key = a.mon_key "
        + ("WHERE " + " AND ".join(where) if where else "") + " ORDER BY a.at DESC, a.id DESC LIMIT ?",
        args + [limit]).fetchall()


# ── Users ───────────────────────────────────────────────────────────────────

def add_user(conn: sqlite3.Connection, name: str, password: str, role: str = "artist") -> int:
    cur = conn.execute("INSERT INTO users (name, pass_hash, role, created) VALUES (?, ?, ?, ?)",
                       (name, hash_password(password), role, now()))
    return cur.lastrowid


def set_password(conn: sqlite3.Connection, user_id: int, password: str) -> None:
    conn.execute("UPDATE users SET pass_hash = ? WHERE id = ?", (hash_password(password), user_id))
    conn.execute("DELETE FROM sessions WHERE user_id = ?", (user_id,))


def login(conn: sqlite3.Connection, name: str, password: str) -> Optional[sqlite3.Row]:
    row = conn.execute("SELECT * FROM users WHERE name = ?", (name,)).fetchone()
    if row is None:
        check_password(password, hash_password("x"))  # same work either way
        return None
    if row["disabled"] or not check_password(password, row["pass_hash"]):
        return None
    return row


def new_session(conn: sqlite3.Connection, user_id: int) -> str:
    token = secrets.token_urlsafe(32)
    conn.execute("INSERT INTO sessions (token_hash, user_id, csrf, created, expires) VALUES (?, ?, ?, ?, ?)",
                 (sha(token), user_id, secrets.token_urlsafe(16), now(), now() + SESSION_SECONDS))
    return token


def session_user(conn: sqlite3.Connection, token: str):
    """(user, csrf) for a live session token, or (None, None)."""
    row = conn.execute(
        "SELECT u.*, s.csrf AS csrf FROM sessions s JOIN users u ON u.id = s.user_id "
        "WHERE s.token_hash = ? AND s.expires > ? AND u.disabled = 0", (sha(token), now())).fetchone()
    return (row, row["csrf"]) if row else (None, None)


def end_session(conn: sqlite3.Connection, token: str) -> None:
    conn.execute("DELETE FROM sessions WHERE token_hash = ?", (sha(token),))


# ── Consoles ────────────────────────────────────────────────────────────────

def normalise_code(code: str) -> str:
    return "".join(c for c in (code or "").upper() if c.isalnum())


def new_pairing_code(conn: sqlite3.Connection, user_id: int) -> str:
    code = "".join(secrets.choice(PAIR_ALPHABET) for _ in range(8))
    conn.execute("DELETE FROM pairing_codes WHERE expires < ? OR used = 1", (now(),))
    conn.execute("INSERT INTO pairing_codes (code_hash, user_id, created, expires) VALUES (?, ?, ?, ?)",
                 (sha(code), user_id, now(), now() + PAIR_SECONDS))
    return code


def pair(conn: sqlite3.Connection, code: str, console_name: str):
    """(token, user) for a good, unused, unexpired code; (None, None) otherwise."""
    with Tx(conn):
        row = conn.execute(
            "SELECT p.*, u.name AS user_name FROM pairing_codes p JOIN users u ON u.id = p.user_id "
            "WHERE p.code_hash = ? AND p.used = 0 AND p.expires > ? AND u.disabled = 0",
            (sha(normalise_code(code)), now())).fetchone()
        if row is None:
            return None, None
        conn.execute("UPDATE pairing_codes SET used = 1 WHERE code_hash = ?", (row["code_hash"],))
        token = secrets.token_hex(32)
        conn.execute("INSERT INTO devices (user_id, token_hash, console_name, created) VALUES (?, ?, ?, ?)",
                     (row["user_id"], sha(token), console_name[:40] or "3DS", now()))
        return token, row["user_name"]


def pair_signed(conn: sqlite3.Connection, console_pub: str, proof: str, console_name: str):
    """Linking over plain HTTP (signing.py): the console's X25519 key and a MAC of it made with the code.
    (server public key hex, token, user) for a good, unused, unexpired code; (None, None, None) otherwise."""
    try:
        pub = bytes.fromhex(console_pub)
    except ValueError:
        return None, None, None
    if len(pub) != 32 or len(proof) != 64:
        return None, None, None
    with Tx(conn):
        rows = conn.execute(
            "SELECT p.*, u.name AS user_name FROM pairing_codes p JOIN users u ON u.id = p.user_id "
            "WHERE p.used = 0 AND p.expires > ? AND u.disabled = 0", (now(),)).fetchall()
        row = next((r for r in rows if hmac.compare_digest(signing.pair_proof(r["code_hash"], console_pub), proof)),
                   None)
        if row is None:
            return None, None, None
        secret, server_pub = signing.x25519_keypair()
        shared = signing.x25519(secret, pub)
        if shared == bytes(32):
            return None, None, None
        conn.execute("UPDATE pairing_codes SET used = 1 WHERE code_hash = ?", (row["code_hash"],))
        token = signing.pair_token(shared, console_pub, server_pub.hex(), row["code_hash"])
        conn.execute("INSERT INTO devices (user_id, token_hash, console_name, created) VALUES (?, ?, ?, ?)",
                     (row["user_id"], sha(token), console_name[:40] or "3DS", now()))
        return server_pub.hex(), token, row["user_name"]


_KEY_IDS = {}  # key id -> token_hash, for signed requests (filled on a miss)


def device_for_key_id(conn: sqlite3.Connection, kid: str):
    """The (live) device whose signing key has this id, with its key: (row, key) or (None, None)."""
    token_hash = _KEY_IDS.get(kid)
    if token_hash is None:
        for r in conn.execute("SELECT token_hash FROM devices WHERE revoked = 0"):
            _KEY_IDS[signing.key_id(signing.device_key(r["token_hash"]))] = r["token_hash"]
        token_hash = _KEY_IDS.get(kid)
        if token_hash is None:
            return None, None
    row = conn.execute(
        "SELECT d.*, u.name AS user_name, u.role AS role FROM devices d JOIN users u ON u.id = d.user_id "
        "WHERE d.token_hash = ? AND d.revoked = 0 AND u.disabled = 0", (token_hash,)).fetchone()
    if row is None:
        return None, None
    conn.execute("UPDATE devices SET last_seen = ? WHERE id = ?", (now(), row["id"]))
    return row, signing.device_key(token_hash)


DISPLAY_NAME = r"[A-Za-z0-9 ._-]{1,16}"


def set_display_name(conn: sqlite3.Connection, user_id: int, name: str) -> Optional[str]:
    """The name shown with a user's versions (the console's author name). The cleaned name, or None."""
    import re
    name = " ".join((name or "").split())
    if not re.fullmatch(DISPLAY_NAME, name):
        return None
    conn.execute("UPDATE users SET display_name = ? WHERE id = ?", (name, user_id))
    return name


def shown_name(row) -> str:
    """A users row's name as the site shows it."""
    return row["display_name"] or row["name"]


def device_for_token(conn: sqlite3.Connection, token: str):
    row = conn.execute(
        "SELECT d.*, u.name AS user_name, u.role AS role FROM devices d JOIN users u ON u.id = d.user_id "
        "WHERE d.token_hash = ? AND d.revoked = 0 AND u.disabled = 0", (sha(token),)).fetchone()
    if row is not None:
        conn.execute("UPDATE devices SET last_seen = ? WHERE id = ?", (now(), row["id"]))
    return row


# ── Versions ────────────────────────────────────────────────────────────────

LIVE = "replaced_by IS NULL"  # a version that is listed (not replaced by its person's newer one)


def _replace(conn: sqlite3.Connection, old: int, new: int, key: str) -> None:
    """`new` takes `old`'s place (the same person's): its votes (where the voter has not voted for
    `new` already) and a pin move over; `old` stays as history."""
    conn.execute("UPDATE OR IGNORE votes SET version_id = ? WHERE version_id = ?", (new, old))
    conn.execute("DELETE FROM votes WHERE version_id = ?", (old,))
    conn.execute("UPDATE versions SET replaced_by = ? WHERE id = ?", (new, old))
    conn.execute("UPDATE versions SET replaced_by = NULL WHERE id = ?", (new,))
    cur = conn.execute("SELECT * FROM current WHERE mon_key = ?", (key,)).fetchone()
    if cur is not None and cur["version_id"] == old:
        conn.execute("UPDATE current SET version_id = ?, seq = ? WHERE mon_key = ?", (new, next_seq(conn), key))


def add_version(conn: sqlite3.Connection, edit, user_id: int, device_id: Optional[int]):
    """(version_id, became_current, duplicate). One version per person per picture: the new one
    replaces the uploader's earlier one (_replace). Sending the same file again is a duplicate; an
    earlier file of one's own that was replaced comes back as one's version."""
    with Tx(conn):
        old = conn.execute("SELECT id, user_id, replaced_by FROM versions WHERE mon_key = ? AND sha256 = ?",
                           (edit.key, edit.sha256)).fetchone()
        if old is not None and (old["replaced_by"] is None or old["user_id"] != user_id):
            return old["id"], False, True
        mine = [r["id"] for r in conn.execute(
            "SELECT id FROM versions WHERE mon_key = ? AND user_id = ? AND " + LIVE, (edit.key, user_id))]
        if old is not None:  # an earlier file of one's own, back again
            for m in mine:
                _replace(conn, m, old["id"], edit.key)
            log(conn, "upload", user_id, edit.key, old["id"], final=int(edit.final), revision=edit.revision, again=1)
            bump_rev(conn)
            cur = conn.execute("SELECT * FROM current WHERE mon_key = ?", (edit.key,)).fetchone()
            if cur is None or not cur["pinned"]:
                _set_best(conn, edit.key)
            cur = conn.execute("SELECT version_id FROM current WHERE mon_key = ?", (edit.key,)).fetchone()
            return old["id"], cur is not None and cur["version_id"] == old["id"], False
        vid = conn.execute(
            "INSERT INTO versions (mon_key, user_id, device_id, planes, frames, final, depth, revision, author_tag, "
            "saved_at, sha256, size, data, uploaded_at, base_author, base_revision) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (edit.key, user_id, device_id, edit.planes, edit.frames, int(edit.final), edit.depth, edit.revision,
             edit.author, edit.saved, edit.sha256, len(edit.data), edit.data, now(),
             edit.base_author, edit.base_revision)).lastrowid
        for m in mine:
            _replace(conn, m, vid, edit.key)
        log(conn, "upload", user_id, edit.key, vid, final=int(edit.final), revision=edit.revision,
            replaced=len(mine), base=edit.base_author)
        bump_rev(conn)
        cur = conn.execute("SELECT * FROM current WHERE mon_key = ?", (edit.key,)).fetchone()
        if cur is not None and cur["pinned"]:
            return vid, False, False
        _set_best(conn, edit.key)
        cur = conn.execute("SELECT version_id FROM current WHERE mon_key = ?", (edit.key,)).fetchone()
        return vid, cur is not None and cur["version_id"] == vid, False


def _best(conn: sqlite3.Connection, key: str) -> Optional[int]:
    """The version with the most votes; among equals (and with none) the newest."""
    row = conn.execute(
        "SELECT v.id, COUNT(t.version_id) AS n FROM versions v LEFT JOIN votes t ON t.version_id = v.id "
        "WHERE v.mon_key = ? AND v." + LIVE + " GROUP BY v.id ORDER BY n DESC, v.uploaded_at DESC, v.id DESC LIMIT 1",
        (key,)).fetchone()
    return row["id"] if row else None


def vote_counts(conn: sqlite3.Connection, key: str) -> dict:
    """{version_id: votes} for a picture's versions that have any."""
    return {r["version_id"]: r["n"] for r in conn.execute(
        "SELECT version_id, COUNT(*) AS n FROM votes WHERE mon_key = ? GROUP BY version_id", (key,))}


def favourites(conn: sqlite3.Connection, user_id: int, key: str) -> list:
    """The user's (at most two) favourite versions of a picture, oldest vote first."""
    return [r["version_id"] for r in conn.execute(
        "SELECT version_id FROM votes WHERE user_id = ? AND mon_key = ? ORDER BY voted_at, version_id",
        (user_id, key))]


# Votes per user per picture: any of its versions (front and back are pictures of their own). The
# console lists 16 versions, so it can always show and send all of a user's votes.
MAX_FAVOURITES = 16


def set_votes(conn: sqlite3.Connection, user_id: int, key: str, version_ids) -> Optional[str]:
    """Replaces the user's votes for a picture (0 to MAX_FAVOURITES of its versions); the current
    version follows the votes unless an admin pinned one. An error message, or None."""
    ids = []
    for v in version_ids:
        if not isinstance(v, int) or isinstance(v, bool):
            return "version ids must be numbers"
        if v not in ids:
            ids.append(v)
    if len(ids) > MAX_FAVOURITES:
        return "at most %d votes per picture" % MAX_FAVOURITES
    with Tx(conn):
        live = []
        for v in ids:
            # A vote for a version its person has replaced since (a console's queued votes) goes to the newer one.
            row = conn.execute("SELECT id, replaced_by FROM versions WHERE id = ? AND mon_key = ?", (v, key)).fetchone()
            for _ in range(32):
                if row is None or row["replaced_by"] is None:
                    break
                row = conn.execute("SELECT id, replaced_by FROM versions WHERE id = ?", (row["replaced_by"],)).fetchone()
            if row is None or row["replaced_by"] is not None:
                return "version %d is not one of this picture's" % v
            if row["id"] not in live:
                live.append(row["id"])
        ids = live
        before = [r["version_id"] for r in conn.execute(
            "SELECT version_id FROM votes WHERE user_id = ? AND mon_key = ? ORDER BY voted_at, version_id", (user_id, key))]
        conn.execute("DELETE FROM votes WHERE user_id = ? AND mon_key = ?", (user_id, key))
        t = now()
        for i, v in enumerate(ids):
            conn.execute("INSERT INTO votes (user_id, mon_key, version_id, voted_at) VALUES (?, ?, ?, ?)",
                         (user_id, key, v, t + i))
        for v in before:
            if v not in ids:
                log(conn, "unvote", user_id, key, v)
        for v in ids:
            if v not in before:
                log(conn, "vote", user_id, key, v)
        cur = conn.execute("SELECT * FROM current WHERE mon_key = ?", (key,)).fetchone()
        if cur is None or not cur["pinned"]:
            _set_best(conn, key)
        bump_rev(conn)
    return None


def pin(conn: sqlite3.Connection, key: str, version_id: int, by: Optional[int] = None) -> bool:
    with Tx(conn):
        if conn.execute("SELECT 1 FROM versions WHERE id = ? AND mon_key = ? AND " + LIVE,
                        (version_id, key)).fetchone() is None:
            return False
        conn.execute("INSERT OR REPLACE INTO current (mon_key, version_id, pinned, seq) VALUES (?, ?, 1, ?)",
                     (key, version_id, next_seq(conn)))
        log(conn, "pin", by, key, version_id)
        bump_rev(conn)
        return True


def unpin(conn: sqlite3.Connection, key: str, by: Optional[int] = None) -> None:
    """Back to 'the most votes win, else the newest'."""
    with Tx(conn):
        cur = conn.execute("SELECT * FROM current WHERE mon_key = ?", (key,)).fetchone()
        if cur is not None and cur["pinned"]:
            log(conn, "unpin", by, key, cur["version_id"])
        _set_best(conn, key)
        bump_rev(conn)


def _set_best(conn: sqlite3.Connection, key: str) -> None:
    best = _best(conn, key)
    cur = conn.execute("SELECT * FROM current WHERE mon_key = ?", (key,)).fetchone()
    if best is None:
        conn.execute("DELETE FROM current WHERE mon_key = ?", (key,))
    elif cur is None or cur["version_id"] != best or cur["pinned"]:
        conn.execute("INSERT OR REPLACE INTO current (mon_key, version_id, pinned, seq) VALUES (?, ?, 0, ?)",
                     (key, best, next_seq(conn)))


def delete_version(conn: sqlite3.Connection, version_id: int, by: Optional[int] = None) -> Optional[str]:
    """The version's key, or None if there is no such version."""
    with Tx(conn):
        row = conn.execute("SELECT v.mon_key, v.user_id, v.revision, COALESCE(u.display_name, u.name) AS owner "
                           "FROM versions v JOIN users u ON u.id = v.user_id WHERE v.id = ?", (version_id,)).fetchone()
        if row is None:
            return None
        log(conn, "delete", by, row["mon_key"], version_id, owner=row["owner"], own=int(row["user_id"] == by),
            revision=row["revision"])
        cur = conn.execute("SELECT * FROM current WHERE mon_key = ?", (row["mon_key"],)).fetchone()
        if cur is not None and cur["version_id"] == version_id:
            conn.execute("DELETE FROM current WHERE mon_key = ?", (row["mon_key"],))
        conn.execute("DELETE FROM votes WHERE version_id = ?", (version_id,))
        conn.execute("DELETE FROM versions WHERE id = ?", (version_id,))
        if cur is not None and (cur["version_id"] == version_id or not cur["pinned"]):
            _set_best(conn, row["mon_key"])
        bump_rev(conn)
        return row["mon_key"]
