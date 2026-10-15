"""The server's SQLite database: users, sessions, consoles, versions, votes and the current pick.

One file (sprites.db in the data folder), WAL mode, a connection per request.
Standard library only.
"""
import hashlib
import hmac
import os
import secrets
import sqlite3
import time
from typing import Optional

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


def device_for_token(conn: sqlite3.Connection, token: str):
    row = conn.execute(
        "SELECT d.*, u.name AS user_name, u.role AS role FROM devices d JOIN users u ON u.id = d.user_id "
        "WHERE d.token_hash = ? AND d.revoked = 0 AND u.disabled = 0", (sha(token),)).fetchone()
    if row is not None:
        conn.execute("UPDATE devices SET last_seen = ? WHERE id = ?", (now(), row["id"]))
    return row


# ── Versions ────────────────────────────────────────────────────────────────

def add_version(conn: sqlite3.Connection, edit, user_id: int, device_id: Optional[int]):
    """(version_id, became_current, duplicate)."""
    with Tx(conn):
        old = conn.execute("SELECT id FROM versions WHERE mon_key = ? AND sha256 = ?",
                           (edit.key, edit.sha256)).fetchone()
        if old is not None:
            return old["id"], False, True
        vid = conn.execute(
            "INSERT INTO versions (mon_key, user_id, device_id, planes, frames, final, revision, author_tag, "
            "saved_at, sha256, size, data, uploaded_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (edit.key, user_id, device_id, edit.planes, edit.frames, int(edit.final), edit.revision,
             edit.author, edit.saved, edit.sha256, len(edit.data), edit.data, now())).lastrowid
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
        "WHERE v.mon_key = ? GROUP BY v.id ORDER BY n DESC, v.uploaded_at DESC, v.id DESC LIMIT 1",
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


MAX_FAVOURITES = 2


def set_votes(conn: sqlite3.Connection, user_id: int, key: str, version_ids) -> Optional[str]:
    """Replaces the user's favourites for a picture (0 to MAX_FAVOURITES of its versions); the current
    version follows the votes unless an admin pinned one. An error message, or None."""
    ids = []
    for v in version_ids:
        if not isinstance(v, int) or isinstance(v, bool):
            return "version ids must be numbers"
        if v not in ids:
            ids.append(v)
    if len(ids) > MAX_FAVOURITES:
        return "pick at most %d favourites" % MAX_FAVOURITES
    with Tx(conn):
        for v in ids:
            if conn.execute("SELECT 1 FROM versions WHERE id = ? AND mon_key = ?", (v, key)).fetchone() is None:
                return "version %d is not one of this picture's" % v
        conn.execute("DELETE FROM votes WHERE user_id = ? AND mon_key = ?", (user_id, key))
        t = now()
        for i, v in enumerate(ids):
            conn.execute("INSERT INTO votes (user_id, mon_key, version_id, voted_at) VALUES (?, ?, ?, ?)",
                         (user_id, key, v, t + i))
        cur = conn.execute("SELECT * FROM current WHERE mon_key = ?", (key,)).fetchone()
        if cur is None or not cur["pinned"]:
            _set_best(conn, key)
        bump_rev(conn)
    return None


def pin(conn: sqlite3.Connection, key: str, version_id: int) -> bool:
    with Tx(conn):
        if conn.execute("SELECT 1 FROM versions WHERE id = ? AND mon_key = ?", (version_id, key)).fetchone() is None:
            return False
        conn.execute("INSERT OR REPLACE INTO current (mon_key, version_id, pinned, seq) VALUES (?, ?, 1, ?)",
                     (key, version_id, next_seq(conn)))
        bump_rev(conn)
        return True


def unpin(conn: sqlite3.Connection, key: str) -> None:
    """Back to 'the most votes win, else the newest'."""
    with Tx(conn):
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


def delete_version(conn: sqlite3.Connection, version_id: int) -> Optional[str]:
    """The version's key, or None if there is no such version."""
    with Tx(conn):
        row = conn.execute("SELECT mon_key FROM versions WHERE id = ?", (version_id,)).fetchone()
        if row is None:
            return None
        cur = conn.execute("SELECT * FROM current WHERE mon_key = ?", (row["mon_key"],)).fetchone()
        if cur is not None and cur["version_id"] == version_id:
            conn.execute("DELETE FROM current WHERE mon_key = ?", (row["mon_key"],))
        conn.execute("DELETE FROM votes WHERE version_id = ?", (version_id,))
        conn.execute("DELETE FROM versions WHERE id = ?", (version_id,))
        if cur is not None and (cur["version_id"] == version_id or not cur["pinned"]):
            _set_best(conn, row["mon_key"])
        bump_rev(conn)
        return row["mon_key"]
