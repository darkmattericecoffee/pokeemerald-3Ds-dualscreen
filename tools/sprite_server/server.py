#!/usr/bin/env python3
"""The plane-sharing server for the 3DS port's on-device plane editor (v1).

Volunteers upload the edit files their consoles save (sdmc:/3ds/emerald3ds/stereo/<key>.bin);
everybody sees in the web app (web/, a React app built into web/dist and served from here)
which Pokémon have been done; consoles and PCs download the current version of each. Runs
behind a Cloudflare Tunnel; listens on localhost only.

    python3 tools/sprite_server/server.py adduser arnoud --admin
    python3 tools/sprite_server/server.py export-sprites --upstream build/upstream
    python3 tools/sprite_server/server.py serve
    tools/sprite_server/run.sh            # serve + a Cloudflare quick tunnel

Data (database, exported pictures) lives in ~/emerald3ds-sprite-server unless --data says
otherwise. See tools/sprite_server/README.md and docs/ONLINE_SPRITE_SHARING.md.

Standard library only (Python 3.9+).
"""
import argparse
import collections
import getpass
import json
import os
import re
import secrets
import sys
import threading
import time
import traceback
from http.cookies import SimpleCookie
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Callable, Dict, List, Optional, Tuple
from urllib.parse import parse_qs, urlsplit

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import db  # noqa: E402
import editfile  # noqa: E402
import events  # noqa: E402
import signing  # noqa: E402

# 2: signed plain HTTP for consoles (signing.py), /api/pair2, the clock in /api/health.
API_VERSION = 2
DEFAULT_DATA = os.path.expanduser("~/emerald3ds-sprite-server")
# The built web app (cd web && npm ci && npm run build); EM3DS_WEB_DIR points elsewhere.
WEB_DIR = os.environ.get("EM3DS_WEB_DIR") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "web", "dist")
MAX_BODY = 64 * 1024
COOKIE = "e3s"
CSRF_HEADER = "X-CSRF-Token"
DEX_COUNT = 386
MIN_PASSWORD = 10


class Config:
    def __init__(self, data_dir: str, sprites_path: Optional[str] = None):
        self.data_dir = data_dir
        self.db_path = os.path.join(data_dir, "sprites.db")
        self.sprites: Dict[str, editfile.Sprite] = {}
        self.names: Dict[str, str] = {}
        path = sprites_path or os.path.join(data_dir, "sprites.json")
        if os.path.exists(path):
            raw = json.load(open(path))
            self.names = raw.get("names", {})
            for key, s in raw.get("sprites", {}).items():
                self.sprites[key] = editfile.Sprite(s["pal"], s["frames"])

    def name(self, key: str) -> str:
        if key in self.names:
            return self.names[key]
        kind = editfile.key_kind(key)
        return "%s %s" % (kind.title(), key.lstrip("btp"))


CFG: Config = None  # set by serve()


# ── Small helpers ───────────────────────────────────────────────────────────

class Limiter:
    """At most `limit` hits per `window` seconds per key, in memory."""

    def __init__(self):
        self.lock = threading.Lock()
        self.hits: Dict[str, collections.deque] = {}

    def allow(self, key: str, limit: int, window: int) -> bool:
        t = time.time()
        with self.lock:
            q = self.hits.setdefault(key, collections.deque())
            while q and q[0] < t - window:
                q.popleft()
            if len(q) >= limit:
                return False
            q.append(t)
            return True


LIMITS = Limiter()


class PngCache:
    def __init__(self, size: int = 3000):
        self.lock = threading.Lock()
        self.size = size
        self.items: "collections.OrderedDict[tuple, bytes]" = collections.OrderedDict()

    def get(self, key: tuple, make: Callable[[], bytes]) -> bytes:
        with self.lock:
            if key in self.items:
                self.items.move_to_end(key)
                return self.items[key]
        value = make()
        with self.lock:
            self.items[key] = value
            while len(self.items) > self.size:
                self.items.popitem(last=False)
        return value


PNGS = PngCache()


class Response:
    def __init__(self, body: bytes = b"", status: int = 200, ctype: str = "text/html; charset=utf-8",
                 headers: Optional[List[Tuple[str, str]]] = None):
        self.body, self.status, self.ctype = body, status, ctype
        self.headers = headers or []


def json_resp(obj, status: int = 200, headers=None) -> Response:
    return Response(json.dumps(obj, separators=(",", ":")).encode(), status, "application/json", headers)


def api_error(status: int, message: str) -> Response:
    return json_resp({"error": message}, status)


class Request:
    def __init__(self, method: str, path: str, query: Dict[str, List[str]], headers, body: bytes, ip: str,
                 https: bool, conn):
        self.method, self.path, self.query, self.headers = method, path, query, headers
        self.body, self.ip, self.https, self.conn = body, ip, https, conn
        self.user = None
        self.csrf = None
        self.device = None
        self.session_token = None
        self.target = path      # the path with its query, as sent (what a console signs)
        self.sign_key = None    # a signed request's device key and MAC: the answer is signed back
        self.sign = None
        self.plain_edge = False # came over plain HTTP through Cloudflare

    def q(self, name: str, default: str = "") -> str:
        return self.query.get(name, [default])[0]

    def json(self) -> dict:
        try:
            obj = json.loads(self.body.decode("utf-8"))
            return obj if isinstance(obj, dict) else {}
        except ValueError:
            return {}


# ── Routing ─────────────────────────────────────────────────────────────────

ROUTES: List[Tuple[str, "re.Pattern", Callable, Optional[str]]] = []
KEY = r"(?P<key>[btp]?\d{3,5})"


def route(method: str, pattern: str, auth: Optional[str] = None):
    """auth: None, 'web' (a signed-in web user; POSTs carry the X-CSRF-Token header), 'admin' (the
    same, an admin), 'device' (a linked console) or 'any' (a console or a web user)."""
    def wrap(fn):
        ROUTES.append((method, re.compile("^" + pattern + "$"), fn, auth))
        return fn
    return wrap


def authorise(req: Request, auth: Optional[str]) -> Optional[Response]:
    if auth is None:
        return None
    if auth == "device" and req.device is None:
        return api_error(401, "this console is not linked (or was unlinked)")
    if auth == "any" and req.device is None and req.user is None:
        return api_error(401, "sign in or link a console")
    if auth in ("web", "admin"):
        if req.user is None:
            return api_error(401, "sign in")
        if auth == "admin" and req.user["role"] != "admin":
            return api_error(403, "only admins can do that")
        # A custom header no other site can send (no CORS here), and the session's own token.
        if req.method == "POST" and not secrets.compare_digest(req.headers.get(CSRF_HEADER, ""), req.csrf or ""):
            return api_error(403, "this page had expired; reload it and try again")
    return None


# ── Pictures and downloads ──────────────────────────────────────────────────

def _version_edit(conn, vid: int) -> Optional[editfile.Edit]:
    row = conn.execute("SELECT data FROM versions WHERE id = ?", (vid,)).fetchone()
    return editfile.parse(row["data"]) if row else None


IMG_HEADERS = [("Cache-Control", "private, max-age=86400")]


@route("GET", r"/img/v(?P<vid>\d+)/(?P<mode>tint|planes|layer\d+)\.png", "any")
def version_png(req: Request, vid: str, mode: str) -> Response:
    frame = int(req.q("f", "0")) if req.q("f", "0").isdigit() else 0

    def make() -> Optional[bytes]:
        edit = _version_edit(req.conn, int(vid))
        if edit is None:
            return None
        sprite = CFG.sprites.get(edit.key)
        if mode.startswith("layer"):
            return editfile.png(editfile.render(edit, sprite, "layer", frame, int(mode[5:])))
        return editfile.png(editfile.render(edit, sprite, mode, frame))

    data = PNGS.get(("v", int(vid), mode, frame), make)
    if data is None:
        return Response(b"not found", 404, "text/plain")
    return Response(data, 200, "image/png", IMG_HEADERS)


@route("GET", r"/img/sprite/" + KEY + r"\.png", "any")
def sprite_png(req: Request, key: str) -> Response:
    sprite = CFG.sprites.get(key)
    if sprite is None:
        return Response(b"not found", 404, "text/plain")
    data = PNGS.get(("s", key), lambda: editfile.png(editfile.render(None, sprite, "sprite")))
    return Response(data, 200, "image/png", IMG_HEADERS)


def bin_response(row, req: Request, attachment: bool = False) -> Response:
    etag = '"%s"' % row["sha256"]
    headers = [("ETag", etag), ("X-Version-Id", str(row["id"])), ("X-Revision", str(row["revision"])),
               ("X-Sha256", row["sha256"]), ("Cache-Control", "private, no-cache")]
    if attachment:
        headers.append(("Content-Disposition", 'attachment; filename="%s.bin"' % row["mon_key"]))
    if req.headers.get("If-None-Match") == etag:
        return Response(b"", 304, "application/octet-stream", headers)
    return Response(bytes(row["data"]), 200, "application/octet-stream", headers)


@route("GET", r"/dl/v(?P<vid>\d+)/" + KEY + r"\.bin", "web")
def download(req: Request, vid: str, key: str) -> Response:
    row = req.conn.execute("SELECT * FROM versions WHERE id = ? AND mon_key = ?", (int(vid), key)).fetchone()
    if row is None:
        return Response(b"not found", 404, "text/plain")
    return bin_response(row, req, attachment=True)


# ── The web app's files ─────────────────────────────────────────────────────

WEB_TYPES = {".js": "text/javascript; charset=utf-8", ".css": "text/css; charset=utf-8", ".woff2": "font/woff2",
             ".woff": "font/woff", ".svg": "image/svg+xml", ".png": "image/png", ".ico": "image/x-icon",
             ".webmanifest": "application/manifest+json", ".txt": "text/plain; charset=utf-8"}
# The app's own pages (React Router); anything else gets the app with a 404, which shows its own page.
APP_PAGES = re.compile(r"^/(?:login|feed|account|users|mon/[btp]?\d{3,5}|compare/[btp]?\d{3,5})?/?$")
APP_CSP = ("default-src 'self'; img-src 'self' data: blob:; style-src 'self' 'unsafe-inline'; font-src 'self' data:; "
           "connect-src 'self'; frame-ancestors 'none'; form-action 'self'; base-uri 'self'")
NOT_BUILT = b"""<!doctype html><meta charset="utf-8"><title>Emerald 3D planes</title>
<p style="font-family:sans-serif">The web app is not built yet: <code>cd tools/sprite_server/web &amp;&amp; npm ci &amp;&amp;
npm run build</code>, then reload. (The console API works without it.)</p>"""


def web_file(name: str, cache: str) -> Response:
    path = os.path.join(WEB_DIR, name)
    ctype = WEB_TYPES.get(os.path.splitext(name)[1])
    if ctype is None or not os.path.isfile(path):
        return Response(b"not found", 404, "text/plain")
    with open(path, "rb") as f:
        return Response(f.read(), 200, ctype, [("Cache-Control", cache)])


@route("GET", r"/assets/(?P<name>[A-Za-z0-9_.-]+)")
def web_asset(req: Request, name: str) -> Response:
    # Vite puts a hash of the content in every asset's name.
    return web_file(os.path.join("assets", name), "public, max-age=31536000, immutable")


@route("GET", r"/(?P<name>[A-Za-z0-9_-]+\.(?:svg|png|ico|webmanifest|txt))")
def web_root_file(req: Request, name: str) -> Response:
    return web_file(name, "public, max-age=86400")


def app_page(req: Request) -> Response:
    """The web app's index.html, for every page it draws itself."""
    path = os.path.join(WEB_DIR, "index.html")
    status = 200 if APP_PAGES.match(req.path) else 404
    if not os.path.isfile(path):
        return Response(NOT_BUILT, 503)
    with open(path, "rb") as f:
        return Response(f.read(), status, "text/html; charset=utf-8", [("Content-Security-Policy", APP_CSP)])


# ── The web app's API ───────────────────────────────────────────────────────
#
# JSON under /api/web/, for the signed-in browser: the session cookie (HttpOnly, SameSite=Lax)
# and, on every POST, the session's CSRF token in the X-CSRF-Token header. /api/web/events is
# the live channel (Server-Sent Events, events.py) that tells open pages what changed.

def other_keys() -> List[str]:
    """The trainers' fronts and the players' backs the game has (sprites.json), trainers first."""
    return sorted((k for k in CFG.names if k[:1] in ("t", "p")), key=lambda k: (k[0] != "t", k))


def neighbours(key: str) -> Tuple[Optional[str], Optional[str], Optional[str]]:
    """Previous, next and the other side's key."""
    m = editfile.KEY_RE.match(key)
    prefix, n = m.group(1), int(m.group(2))
    if prefix in ("", "b") and 1 <= n <= DEX_COUNT:
        prev = "%s%03u" % (prefix, n - 1) if n > 1 else None
        nxt = "%s%03u" % (prefix, n + 1) if n < DEX_COUNT else None
        other = ("b%03u" % n) if prefix == "" else ("%03u" % n)
        return prev, nxt, other
    others = other_keys()
    if key in others:
        i = others.index(key)
        return others[i - 1] if i else None, others[i + 1] if i + 1 < len(others) else None, None
    return None, None, None


def session_cookie(req: Request, token: str) -> Tuple[str, str]:
    return ("Set-Cookie", "%s=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=%d%s" % (
        COOKIE, token, db.SESSION_SECONDS, "; Secure" if req.https else ""))


def session_json(req: Request) -> dict:
    u = req.user
    user = None if u is None else {"id": u["id"], "name": u["name"], "display_name": db.shown_name(u),
                                   "role": u["role"]}
    return {"user": user, "csrf": req.csrf if u is not None else None, "api": API_VERSION}


def publish(kind: str, **data) -> None:
    """Tells every open page (events.py). `_to` (a user id) keeps an event to that user's pages."""
    events.BUS.publish(kind, **data)


def json_body(req: Request) -> Optional[dict]:
    """The JSON object a web POST sent, or None."""
    if not req.headers.get("Content-Type", "").startswith("application/json"):
        return None
    try:
        obj = json.loads(req.body.decode("utf-8") or "{}")
    except ValueError:
        return None
    return obj if isinstance(obj, dict) else None


def int_field(body: dict, name: str) -> Optional[int]:
    v = body.get(name)
    return v if isinstance(v, int) and not isinstance(v, bool) else None


@route("GET", r"/api/web/session")
def web_session(req: Request) -> Response:
    return json_resp(session_json(req))


@route("POST", r"/api/web/login")
def web_login(req: Request) -> Response:
    # JSON only: a form on another site cannot send it (no CORS here), so no login CSRF.
    body = json_body(req)
    if body is None:
        return api_error(415, "send JSON")
    name = str(body.get("name", "")).strip()
    if not LIMITS.allow("login:" + req.ip, 10, 900) or not LIMITS.allow("login:" + name.lower(), 10, 900):
        return api_error(429, "too many attempts; wait a quarter of an hour")
    user = db.login(req.conn, name, str(body.get("password", "")))
    if user is None:
        return api_error(401, "wrong name or password")
    token = db.new_session(req.conn, user["id"])
    req.user, req.csrf = db.session_user(req.conn, token)
    return json_resp(session_json(req), headers=[session_cookie(req, token)])


@route("POST", r"/api/web/logout", "web")
def web_logout(req: Request) -> Response:
    db.end_session(req.conn, req.session_token)
    req.user = None
    return json_resp(session_json(req), headers=[("Set-Cookie", "%s=; Path=/; Max-Age=0" % COOKIE)])


@route("GET", r"/api/web/grid", "web")
def web_grid(req: Request) -> Response:
    """Everything the progress grid shows: each picture's current version, and the caller's own."""
    rows = req.conn.execute(
        "SELECT c.mon_key, c.version_id, c.pinned, v.final, v.uploaded_at, COALESCE(u.display_name, u.name) AS author, "
        "(SELECT COUNT(*) FROM versions w WHERE w.mon_key = c.mon_key AND w.replaced_by IS NULL) AS n "
        "FROM current c JOIN versions v ON v.id = c.version_id JOIN users u ON u.id = v.user_id").fetchall()
    uid = req.user["id"]
    mine = [r[0] for r in req.conn.execute("SELECT DISTINCT mon_key FROM versions WHERE user_id = ? AND " + db.LIVE,
                                           (uid,))]
    voted = [r[0] for r in req.conn.execute("SELECT DISTINCT mon_key FROM votes WHERE user_id = ?", (uid,))]
    fronts = ["%03u" % n for n in range(1, DEX_COUNT + 1)]
    known = set(fronts) | {"b" + k for k in fronts}
    listed = other_keys()
    others = listed + sorted({r["mon_key"] for r in rows} - known - set(listed))
    keys = fronts + ["b" + k for k in fronts] + others
    return json_resp({
        "rev": db.current_rev(req.conn), "dex_count": DEX_COUNT, "others": others,
        "names": {k: CFG.name(k) for k in keys},
        "pictures": [k for k in keys if k in CFG.sprites],
        "mine": sorted(mine), "voted": sorted(voted), "reviews": db.reviews(req.conn),
        "items": {r["mon_key"]: {"version_id": r["version_id"], "pinned": bool(r["pinned"]), "final": bool(r["final"]),
                                 "author": r["author"], "versions": r["n"],
                                 "uploaded_at": r["uploaded_at"]} for r in rows}})


def mon_json(req: Request, key: str) -> dict:
    """A picture's page: every version with its uploader, console, votes (and who) and flags."""
    rows = req.conn.execute(
        "SELECT v.id, v.user_id, v.planes, v.frames, v.final, v.revision, v.author_tag, v.saved_at, v.sha256, v.size, "
        "v.uploaded_at, COALESCE(u.display_name, u.name) AS uploader, d.console_name "
        "FROM versions v JOIN users u ON u.id = v.user_id LEFT JOIN devices d ON d.id = v.device_id "
        "WHERE v.mon_key = ? AND v.replaced_by IS NULL ORDER BY v.uploaded_at DESC, v.id DESC", (key,)).fetchall()
    cur = req.conn.execute("SELECT * FROM current WHERE mon_key = ?", (key,)).fetchone()
    counts = db.vote_counts(req.conn, key)
    voters: Dict[int, List[str]] = {}
    for r in req.conn.execute(
            "SELECT t.version_id, COALESCE(u.display_name, u.name) AS who FROM votes t JOIN users u ON u.id = t.user_id "
            "WHERE t.mon_key = ? ORDER BY t.voted_at, t.user_id", (key,)):
        voters.setdefault(r["version_id"], []).append(r["who"])
    prev, nxt, other = neighbours(key)
    near = lambda k: {"key": k, "name": CFG.name(k)} if k else None  # noqa: E731
    uid = req.user["id"]
    return {
        "key": key, "name": CFG.name(key), "kind": editfile.key_kind(key), "picture": key in CFG.sprites,
        "prev": near(prev), "next": near(nxt), "other": near(other),
        "current": cur["version_id"] if cur else None, "pinned": bool(cur and cur["pinned"]),
        "favourites": db.favourites(req.conn, uid, key), "max_favourites": db.MAX_FAVOURITES,
        "admin": req.user["role"] == "admin", "review": db.review(req.conn, key),
        "versions": [{"version_id": r["id"], "author": r["uploader"], "signed": r["author_tag"],
                      "console": r["console_name"], "revision": r["revision"], "final": bool(r["final"]),
                      "votes": counts.get(r["id"], 0), "voters": voters.get(r["id"], []),
                      "planes": r["planes"], "frames": r["frames"],
                      "sha256": r["sha256"], "size": r["size"], "saved_at": r["saved_at"],
                      "uploaded_at": r["uploaded_at"], "mine": r["user_id"] == uid}
                     for r in by_votes(rows, counts)]}


@route("GET", r"/api/web/mons/" + KEY, "web")
def web_mon(req: Request, key: str) -> Response:
    if editfile.key_to_id(key) is None:
        return api_error(404, "no such picture")
    return json_resp(mon_json(req, key))


@route("POST", r"/api/web/mons/" + KEY + r"/vote", "web")
def web_vote(req: Request, key: str) -> Response:
    """{"version_id", "on"}: the caller's vote for one version, given or taken back."""
    body = json_body(req) or {}
    vid, on = int_field(body, "version_id"), body.get("on")
    if editfile.key_to_id(key) is None or vid is None or not isinstance(on, bool):
        return api_error(400, "send version_id and on")
    mine = db.favourites(req.conn, req.user["id"], key)
    if on and vid not in mine:
        if len(mine) >= db.MAX_FAVOURITES:
            return api_error(400, "that is as many votes as one picture takes (%d)" % db.MAX_FAVOURITES)
        mine.append(vid)
    elif not on and vid in mine:
        mine.remove(vid)
    err = db.set_votes(req.conn, req.user["id"], key, mine)
    if err is not None:
        return api_error(404, err)
    publish("vote", key=key, version_id=vid, on=on, who=db.shown_name(req.user))
    return json_resp(mon_json(req, key))


@route("POST", r"/api/web/mons/" + KEY + r"/pin", "admin")
def web_pin(req: Request, key: str) -> Response:
    vid = int_field(json_body(req) or {}, "version_id")
    if vid is None or not db.pin(req.conn, key, vid, by=req.user["id"]):
        return api_error(404, "no such version")
    publish("pin", key=key, version_id=vid)
    return json_resp(mon_json(req, key))


@route("POST", r"/api/web/mons/" + KEY + r"/unpin", "admin")
def web_unpin(req: Request, key: str) -> Response:
    db.unpin(req.conn, key, by=req.user["id"])
    publish("pin", key=key, version_id=None)
    return json_resp(mon_json(req, key))


def review_status(body: dict) -> Optional[str]:
    """The status a review POST sent ('fix', 'ok' or '' to clear), or None."""
    s = body.get("status")
    return s if s == "" or s in db.REVIEW_STATUSES else None


@route("POST", r"/api/web/mons/" + KEY + r"/review", "web")
def web_review(req: Request, key: str) -> Response:
    """{"status": "fix" | "ok" | ""}: the picture's review before the beta, for everybody."""
    status = review_status(json_body(req) or {})
    if editfile.key_to_id(key) is None or status is None:
        return api_error(400, "send status: fix, ok or an empty string")
    if db.set_review(req.conn, req.user["id"], key, status):
        publish("review", key=key, status=status, who=db.shown_name(req.user))
    return json_resp(mon_json(req, key))


@route("POST", r"/api/web/versions/(?P<vid>\d+)/delete", "web")
def web_delete(req: Request, vid: str) -> Response:
    """Takes a version off the server: its uploader, or an admin."""
    row = req.conn.execute("SELECT mon_key, user_id FROM versions WHERE id = ?", (int(vid),)).fetchone()
    if row is None:
        return api_error(404, "no such version")
    if row["user_id"] != req.user["id"] and req.user["role"] != "admin":
        return api_error(403, "only its uploader or an admin can delete a version")
    db.delete_version(req.conn, int(vid), by=req.user["id"])
    publish("delete", key=row["mon_key"], version_id=int(vid))
    return json_resp(mon_json(req, row["mon_key"]))


@route("GET", r"/api/web/pictures/" + KEY, "web")
def web_picture(req: Request, key: str) -> Response:
    """The game's picture as data, for the 3D viewer: as in sprites.json, the palette and per frame
    one hex digit (the colour index) per pixel."""
    sprite = CFG.sprites.get(key)
    if sprite is None:
        return api_error(404, "no game picture for %s" % key)
    return json_resp({"key": key, "palette": ["#%02x%02x%02x" % c for c in sprite.palette],
                      "frames": ["".join("%x" % i for i in f) for f in sprite.frames]}, headers=IMG_HEADERS)


def account_json(req: Request) -> dict:
    devices = req.conn.execute("SELECT * FROM devices WHERE user_id = ? AND revoked = 0 ORDER BY created DESC",
                               (req.user["id"],)).fetchall()
    uploads = req.conn.execute("SELECT COUNT(*) FROM versions WHERE user_id = ? AND " + db.LIVE,
                               (req.user["id"],)).fetchone()[0]
    return {"name": req.user["name"], "display_name": db.shown_name(req.user), "role": req.user["role"],
            "uploads": uploads, "min_password": MIN_PASSWORD, "base": public_base(req),
            "devices": [{"id": d["id"], "console_name": d["console_name"], "created": d["created"],
                         "last_seen": d["last_seen"]} for d in devices]}


@route("GET", r"/api/web/account", "web")
def web_account(req: Request) -> Response:
    return json_resp(account_json(req))


@route("POST", r"/api/web/account/name", "web")
def web_account_name(req: Request) -> Response:
    name = db.set_display_name(req.conn, req.user["id"], str((json_body(req) or {}).get("name", "")))
    if name is None:
        return api_error(400, "names are letters, digits, spaces, dots, dashes and underscores, up to 16")
    db.bump_rev(req.conn)
    db.log(req.conn, "name", req.user["id"], name=name)
    req.user = req.conn.execute("SELECT * FROM users WHERE id = ?", (req.user["id"],)).fetchone()
    publish("name", name=name)
    return json_resp(account_json(req))


@route("POST", r"/api/web/account/pair", "web")
def web_account_pair(req: Request) -> Response:
    code = db.new_pairing_code(req.conn, req.user["id"])
    return json_resp({"code": code[:4] + "-" + code[4:], "expires": db.now() + db.PAIR_SECONDS,
                      "seconds": db.PAIR_SECONDS, "base": public_base(req)})


@route("POST", r"/api/web/account/devices/(?P<did>\d+)/revoke", "web")
def web_account_revoke(req: Request, did: str) -> Response:
    req.conn.execute("UPDATE devices SET revoked = 1 WHERE id = ? AND user_id = ?", (int(did), req.user["id"]))
    publish("console", _to=req.user["id"], action="unlinked", device_id=int(did))
    return json_resp(account_json(req))


@route("POST", r"/api/web/account/password", "web")
def web_account_password(req: Request) -> Response:
    body = json_body(req) or {}
    old, new = str(body.get("old", "")), str(body.get("new", ""))
    if not db.check_password(old, req.user["pass_hash"]):
        return api_error(400, "the current password was wrong")
    if len(new) < MIN_PASSWORD:
        return api_error(400, "the new password needs at least %d characters" % MIN_PASSWORD)
    db.set_password(req.conn, req.user["id"], new)  # signs out every other browser
    token = db.new_session(req.conn, req.user["id"])
    req.user, req.csrf = db.session_user(req.conn, token)
    return json_resp(session_json(req), headers=[session_cookie(req, token)])


def users_json(req: Request) -> dict:
    users = req.conn.execute(
        "SELECT u.*, (SELECT COUNT(*) FROM devices d WHERE d.user_id = u.id AND d.revoked = 0) AS consoles, "
        "(SELECT MAX(d.last_seen) FROM devices d WHERE d.user_id = u.id AND d.revoked = 0) AS last_seen, "
        "(SELECT COUNT(*) FROM versions v WHERE v.user_id = u.id AND v.replaced_by IS NULL) AS uploads "
        "FROM users u ORDER BY u.name").fetchall()
    return {"users": [{"id": u["id"], "name": u["name"], "display_name": db.shown_name(u), "role": u["role"],
                       "disabled": bool(u["disabled"]), "consoles": u["consoles"], "uploads": u["uploads"],
                       "created": u["created"], "last_seen": u["last_seen"], "me": u["id"] == req.user["id"]}
                      for u in users]}


@route("GET", r"/api/web/users", "admin")
def web_users(req: Request) -> Response:
    return json_resp(users_json(req))


@route("POST", r"/api/web/users", "admin")
def web_users_add(req: Request) -> Response:
    body = json_body(req) or {}
    name, role = str(body.get("name", "")).strip(), body.get("role", "artist")
    if not re.fullmatch(r"[A-Za-z0-9_. -]{1,32}", name) or role not in ("artist", "admin"):
        return api_error(400, "names are letters, digits, spaces, dots, dashes and underscores, up to 32")
    if req.conn.execute("SELECT 1 FROM users WHERE name = ?", (name,)).fetchone():
        return api_error(409, "there is already a user called %s" % name)
    password = new_password()
    db.add_user(req.conn, name, password, role)
    publish("users")
    return json_resp(dict(users_json(req), name=name, password=password), 201)


@route("POST", r"/api/web/users/(?P<uid>\d+)/(?P<act>enable|disable|reset)", "admin")
def web_users_act(req: Request, uid: str, act: str) -> Response:
    user = req.conn.execute("SELECT * FROM users WHERE id = ?", (int(uid),)).fetchone()
    if user is None:
        return api_error(404, "no such user")
    if act == "reset":
        password = new_password()
        db.set_password(req.conn, user["id"], password)
        return json_resp(dict(users_json(req), name=user["name"], password=password))
    if user["id"] == req.user["id"]:
        return api_error(400, "you cannot disable yourself")
    req.conn.execute("UPDATE users SET disabled = ? WHERE id = ?", (int(act == "disable"), user["id"]))
    if act == "disable":
        req.conn.execute("DELETE FROM sessions WHERE user_id = ?", (user["id"],))
    publish("users")
    return json_resp(users_json(req))


class Focus:
    """What each account's web app is looking at, for its consoles' "follow the web" (GET /api/focus):
    the picture, the versions being compared, and the one selected. In memory: a restart forgets it,
    and the web app sends it again on its next page. Also when a console of the account last asked,
    so the web app can say whether a 3DS is following."""
    STALE = 10 * 60
    EMPTY = {"key": None, "version_ids": [], "selected": None, "seq": 0, "at": 0}

    def __init__(self):
        self.lock = threading.Lock()
        self.seq = 0
        self.by_user: Dict[int, dict] = {}
        self.polled: Dict[int, Tuple[float, str]] = {}  # user id -> (when, console name)

    def set(self, user_id: int, key: Optional[str], version_ids: List[int], selected: Optional[int],
            push: bool = False) -> dict:
        """`push` ("Show on 3DS") moves seq on even when nothing changed, so a console jumps there again."""
        with self.lock:
            old = self.by_user.get(user_id)
            same = (old is not None and old["key"] == key and old["version_ids"] == version_ids
                    and old["selected"] == selected)
            if same and not push:
                old["at"] = int(time.time())  # the same page again: still looking
                return dict(old)
            self.seq += 1
            self.by_user[user_id] = {"key": key, "version_ids": version_ids, "selected": selected,
                                     "seq": self.seq, "at": int(time.time())}
            return dict(self.by_user[user_id])

    def get(self, user_id: int) -> dict:
        with self.lock:
            f = dict(self.by_user.get(user_id) or self.EMPTY)
        if time.time() - f["at"] > self.STALE:
            f["key"], f["version_ids"], f["selected"] = None, [], None
        return f

    def polled_by(self, user_id: int, console_name: str) -> None:
        with self.lock:
            self.polled[user_id] = (time.time(), console_name)

    def follower(self, user_id: int) -> Optional[dict]:
        """The console that asked last, if one asked in the last 15 s (consoles ask every ~2 s)."""
        with self.lock:
            p = self.polled.get(user_id)
        if p is None or time.time() - p[0] > 15:
            return None
        return {"console_name": p[1], "seen": int(p[0])}


FOCUS = Focus()


@route("POST", r"/api/web/focus", "web")
def web_focus(req: Request) -> Response:
    """{"key", "version_ids", "selected", "push"}: the picture, the versions being compared and the one
    selected that this account's web app shows; push (the "Show on 3DS" button) makes consoles jump
    there again. Answers the focus and the console following it ("follower", or null)."""
    body = json_body(req) or {}
    key = body.get("key")
    ids = body.get("version_ids") or []
    selected = body.get("selected")
    if key is not None and (not isinstance(key, str) or editfile.key_to_id(key) is None):
        return api_error(400, "no such picture")
    if not isinstance(ids, list) or len(ids) > 16 or any(int_field({"v": v}, "v") is None for v in ids):
        return api_error(400, "version_ids must be up to 16 numbers")
    if selected is not None and int_field(body, "selected") is None:
        return api_error(400, "selected must be a version id or null")
    focus = FOCUS.set(req.user["id"], key, ids, selected, body.get("push") is True)
    return json_resp(dict(focus, follower=FOCUS.follower(req.user["id"])))


@route("GET", r"/api/web/focus", "web")
def web_focus_get(req: Request) -> Response:
    return json_resp(dict(FOCUS.get(req.user["id"]), follower=FOCUS.follower(req.user["id"])))


class Stream:
    """What /api/web/events answers with: the Handler keeps the connection open and writes events."""

    def __init__(self, user_id: int, rev: int):
        self.user_id, self.rev = user_id, rev


@route("GET", r"/api/web/events", "web")
def web_events(req: Request):
    return Stream(req.user["id"], db.current_rev(req.conn))


FEED_KINDS = {"all": None, "uploads": ("upload",), "votes": ("vote", "unvote"),
              "curation": ("pin", "unpin", "delete", "review"),
              "people": ("name", "console")}
FEED_LIMIT = 40


@route("GET", r"/api/web/feed", "web")
def web_feed(req: Request) -> Response:
    """The activity feed, newest first: ?before=<id> for the lines older than one, ?kind=all|uploads|votes|
    curation|people, ?limit up to 100. Each line says who (as the site shows them), what, which picture
    and version, and what became of that version since (gone, replaced, current)."""
    kinds = FEED_KINDS.get(req.q("kind", "all"))
    if req.q("kind", "all") not in FEED_KINDS:
        return api_error(400, "kind is one of " + ", ".join(FEED_KINDS))
    before = int(req.q("before", "0")) if req.q("before", "0").isdigit() else 0
    limit = min(100, max(1, int(req.q("limit", str(FEED_LIMIT))) if req.q("limit", "").isdigit() else FEED_LIMIT))
    rows = db.activity(req.conn, before, limit + 1, kinds)
    items = []
    for r in rows[:limit]:
        detail = json.loads(r["detail"] or "{}")
        item = {"id": r["id"], "at": r["at"], "kind": r["kind"], "who": r["who"], "me": r["user_id"] == req.user["id"],
                "key": r["mon_key"], "name": CFG.name(r["mon_key"]) if r["mon_key"] else None,
                "version_id": r["version_id"]}
        if r["version_id"] is not None:
            item["exists"] = r["live_id"] is not None
            item["replaced"] = r["replaced_by"] is not None
            item["current"] = r["live_id"] is not None and r["current_id"] == r["version_id"]
            item["owner"] = r["owner"] or detail.get("owner")
            item["final"] = bool(r["live_final"]) if r["live_id"] is not None else bool(detail.get("final", 0))
        for k in ("revision", "again", "base", "own", "console", "status", "was"):
            if k in detail and k not in item:
                item[k] = detail[k]
        if "name" in detail:  # a name change: the picture name slot is free, the new name goes beside it
            item["new_name"] = detail["name"]
        items.append(item)
    return json_resp({"items": items, "more": len(rows) > limit, "rev": db.current_rev(req.conn)})


def public_base(req: Request) -> str:
    host = req.headers.get("Host", "localhost")
    return ("https://" if req.https else "http://") + host


def new_password() -> str:
    return "-".join(secrets.token_urlsafe(6) for _ in range(3))


# ── The console API ─────────────────────────────────────────────────────────

@route("GET", r"/api/health")
def api_health(req: Request) -> Response:
    # The clock: what signed requests carry (a console's own is whatever its owner set).
    return json_resp({"ok": True, "api": API_VERSION, "time": int(time.time())})


@route("POST", r"/api/pair")
def api_pair(req: Request) -> Response:
    if not LIMITS.allow("pair:" + req.ip, 10, 600):
        return api_error(429, "too many tries; wait ten minutes")
    body = req.json()
    name = "".join(c for c in str(body.get("console_name", "")) if c.isprintable()).strip()
    token, user = db.pair(req.conn, str(body.get("code", "")), name or "3DS")
    if token is None:
        return api_error(403, "that code is wrong, used or expired")
    publish_linked(req, token)
    return json_resp({"token": token, "user": user})


def publish_linked(req: Request, token: str) -> None:
    """A console or PC just traded its code: the account page that made the code shows it at once."""
    d = db.device_for_token(req.conn, token)
    if d is not None:
        db.log(req.conn, "console", d["user_id"], console=d["console_name"])
        publish("console", _to=d["user_id"], action="linked", device_id=d["id"], console_name=d["console_name"])


@route("POST", r"/api/pair2")
def api_pair_signed(req: Request) -> Response:
    """Linking over plain HTTP: an X25519 exchange proven with the code, which is not sent (signing.py)."""
    if not LIMITS.allow("pair:" + req.ip, 10, 600):
        return api_error(429, "too many tries; wait ten minutes")
    body = req.json()
    name = "".join(c for c in str(body.get("console_name", "")) if c.isprintable()).strip()
    pub = str(body.get("pub", "")).lower()
    server_pub, token, user = db.pair_signed(req.conn, pub, str(body.get("proof", "")).lower(), name or "3DS")
    if token is None:
        return api_error(403, "that code is wrong, used or expired")
    publish_linked(req, token)
    return json_resp({"pub": server_pub, "user": user, "confirm": signing.pair_confirm(token, pub, server_pub, user)})


@route("GET", r"/api/me", "device")
def api_me(req: Request) -> Response:
    d = req.device
    user = req.conn.execute("SELECT * FROM users WHERE id = ?", (d["user_id"],)).fetchone()
    return json_resp({"user": d["user_name"], "name": db.shown_name(user), "role": d["role"],
                      "console_name": d["console_name"], "seq": db.current_seq(req.conn)})


@route("POST", r"/api/me", "device")
def api_me_name(req: Request) -> Response:
    """{"name": ...}: the console's author name becomes the name the site shows for this account."""
    name = db.set_display_name(req.conn, req.device["user_id"], str(req.json().get("name", "")))
    if name is None:
        return api_error(400, "names are letters, digits, spaces, dots, dashes and underscores, up to 16")
    db.bump_rev(req.conn)
    db.log(req.conn, "name", req.device["user_id"], name=name)
    publish("name")
    return api_me(req)


@route("POST", r"/api/mons/" + KEY + r"/versions", "device")
def api_upload(req: Request, key: str) -> Response:
    if editfile.key_to_id(key) is None:
        return api_error(404, "no such picture")
    if not LIMITS.allow("up:%d" % req.device["id"], 120, 60):
        return api_error(429, "too many uploads; wait a minute")
    try:
        edit = editfile.parse(req.body, key)
    except editfile.EditError as err:
        return api_error(400, str(err))
    vid, current, duplicate = db.add_version(req.conn, edit, req.device["user_id"], req.device["id"])
    if not duplicate:
        who = req.conn.execute("SELECT * FROM users WHERE id = ?", (req.device["user_id"],)).fetchone()
        publish("upload", key=key, version_id=vid, current=current, who=db.shown_name(who), name=CFG.name(key))
    return json_resp({"key": key, "version_id": vid, "current": current, "duplicate": duplicate,
                      "sha256": edit.sha256}, 200 if duplicate else 201)


def by_votes(rows, counts: dict) -> list:
    """The most votes first; among equals the newest (rows come newest first)."""
    return sorted(rows, key=lambda r: -counts.get(r["id"], 0))


def _voter(req: Request) -> int:
    return req.device["user_id"] if req.device is not None else req.user["id"]


def _versions_json(req: Request, key: str) -> dict:
    rows = req.conn.execute(
        "SELECT v.id, v.user_id, v.revision, v.final, v.depth, v.author_tag, v.sha256, v.size, v.uploaded_at, "
        "v.base_author, v.base_revision, COALESCE(u.display_name, u.name) AS uploader "
        "FROM versions v JOIN users u ON u.id = v.user_id WHERE v.mon_key = ? AND v.replaced_by IS NULL "
        "ORDER BY v.uploaded_at DESC, v.id DESC",
        (key,)).fetchall()
    cur = req.conn.execute("SELECT * FROM current WHERE mon_key = ?", (key,)).fetchone()
    counts = db.vote_counts(req.conn, key)
    rows = by_votes(rows, counts)
    return {"key": key, "current": cur["version_id"] if cur else None, "pinned": bool(cur and cur["pinned"]),
            "favourites": db.favourites(req.conn, _voter(req), key), "max_favourites": db.MAX_FAVOURITES,
            "versions": [{"version_id": r["id"], "author": r["uploader"], "signed": r["author_tag"],
                          "revision": r["revision"], "final": bool(r["final"]), "depth": editfile.sweet_spot(r["depth"]),
                          "votes": counts.get(r["id"], 0),
                          "base": r["base_author"], "base_revision": r["base_revision"],
                          "sha256": r["sha256"], "size": r["size"], "uploaded_at": r["uploaded_at"],
                          "mine": r["user_id"] == _voter(req)} for r in rows]}


@route("GET", r"/api/mons/" + KEY + r"/versions", "any")
def api_versions(req: Request, key: str) -> Response:
    if editfile.key_to_id(key) is None:
        return api_error(404, "no such picture")
    return json_resp(_versions_json(req, key))


@route("POST", r"/api/mons/" + KEY + r"/votes", "device")
def api_votes(req: Request, key: str) -> Response:
    """{"version_ids": [...]}: the caller's favourites for the picture, replacing the earlier ones."""
    if editfile.key_to_id(key) is None:
        return api_error(404, "no such picture")
    ids = req.json().get("version_ids")
    if not isinstance(ids, list):
        return api_error(400, "version_ids must be a list")
    err = db.set_votes(req.conn, _voter(req), key, ids)
    if err is not None:
        return api_error(400, err)
    publish("vote", key=key)
    return json_resp(_versions_json(req, key))


@route("POST", r"/api/mons/" + KEY + r"/review", "device")
def api_review(req: Request, key: str) -> Response:
    """{"status": "fix" | "ok" | ""}: the review button beside the editor's SAVE."""
    status = review_status(req.json())
    if editfile.key_to_id(key) is None or status is None:
        return api_error(400, "send status: fix, ok or an empty string")
    if db.set_review(req.conn, req.device["user_id"], key, status):
        publish("review", key=key, status=status, who=req.device["console_name"])
    return json_resp({"key": key, "status": status})


@route("POST", r"/api/versions/(?P<vid>\d+)/delete", "device")
def api_delete_version(req: Request, vid: str) -> Response:
    """A console takes its account's own version off the server (an admin's console any)."""
    row = req.conn.execute("SELECT mon_key, user_id FROM versions WHERE id = ?", (int(vid),)).fetchone()
    if row is None:
        return api_error(404, "no such version")
    if row["user_id"] != req.device["user_id"] and req.device["role"] != "admin":
        return api_error(403, "only its uploader can delete a version")
    db.delete_version(req.conn, int(vid), by=req.device["user_id"])
    publish("delete", key=row["mon_key"], version_id=int(vid))
    return json_resp(_versions_json(req, row["mon_key"]))


@route("GET", r"/api/mons/" + KEY + r"/current", "any")
def api_current(req: Request, key: str) -> Response:
    row = req.conn.execute("SELECT v.* FROM current c JOIN versions v ON v.id = c.version_id WHERE c.mon_key = ?",
                           (key,)).fetchone()
    if row is None:
        return api_error(404, "nobody has uploaded this picture yet")
    return bin_response(row, req)


@route("GET", r"/api/versions/(?P<vid>\d+)", "any")
def api_version(req: Request, vid: str) -> Response:
    row = req.conn.execute("SELECT * FROM versions WHERE id = ?", (int(vid),)).fetchone()
    if row is None:
        return api_error(404, "no such version")
    return bin_response(row, req)


@route("GET", r"/api/sync", "any")
def api_sync(req: Request) -> Response:
    try:
        since = int(req.q("since", "0"))
    except ValueError:
        return api_error(400, "since must be a number")
    rows = req.conn.execute(
        "SELECT c.mon_key, c.version_id, c.seq, v.sha256, v.size, v.revision, v.final, v.depth FROM current c "
        "JOIN versions v ON v.id = c.version_id WHERE c.seq > ? ORDER BY c.seq", (since,)).fetchall()
    return json_resp({"seq": db.current_seq(req.conn),
                      "items": [{"key": r["mon_key"], "version_id": r["version_id"], "sha256": r["sha256"],
                                 "size": r["size"], "revision": r["revision"], "final": bool(r["final"]),
                                 "depth": editfile.sweet_spot(r["depth"])}
                                for r in rows]})


@route("GET", r"/api/mons", "any")
def api_mons(req: Request) -> Response:
    """Every picture with a current version: how many versions it has, and how many of them are the
    caller's favourites (a console's Pokédex marks both); and every picture's review, versions or
    not ("reviews": {key: "fix" | "ok"})."""
    rows = req.conn.execute(
        "SELECT c.mon_key, c.version_id, c.pinned, v.final, COALESCE(u.display_name, u.name) AS author, "
        "(SELECT COUNT(*) FROM versions w WHERE w.mon_key = c.mon_key AND w.replaced_by IS NULL) AS n, "
        "(SELECT COUNT(*) FROM votes t WHERE t.mon_key = c.mon_key AND t.user_id = ?) AS mine "
        "FROM current c JOIN versions v ON v.id = c.version_id JOIN users u ON u.id = v.user_id "
        "ORDER BY c.mon_key", (_voter(req),)).fetchall()
    return json_resp({"rev": db.current_rev(req.conn),
                      "items": [{"key": r["mon_key"], "name": CFG.name(r["mon_key"]), "current": r["version_id"],
                                 "pinned": bool(r["pinned"]), "final": bool(r["final"]), "author": r["author"],
                                 "versions": r["n"], "mine": r["mine"]} for r in rows],
                      "reviews": {k: r["status"] for k, r in db.reviews(req.conn).items()}})


@route("GET", r"/api/focus", "device")
def api_focus(req: Request) -> Response:
    """Follow the web: {"key", "version_ids", "selected", "seq", "at"}, what the account's web app shows
    (key null when it has shown nothing for ten minutes; selected the version chosen there, or null).
    `seq` moves on with every change and every "Show on 3DS"; consoles act on a new one."""
    FOCUS.polled_by(req.device["user_id"], req.device["console_name"])
    return json_resp(FOCUS.get(req.device["user_id"]))


@route("GET", r"/api/rev", "any")
def api_rev(req: Request) -> Response:
    """What the web pages poll: moves on with every upload, vote, pin and delete."""
    return json_resp({"rev": db.current_rev(req.conn), "seq": db.current_seq(req.conn)})


# ── The HTTP server ─────────────────────────────────────────────────────────

SECURITY_HEADERS = [
    ("X-Content-Type-Options", "nosniff"),
    ("Referrer-Policy", "same-origin"),
    ("X-Frame-Options", "DENY"),
    ("Content-Security-Policy", "default-src 'self'; img-src 'self' data:; frame-ancestors 'none'; form-action 'self'"),
]


class Nonces:
    """Signed requests seen within the clock window: none is taken twice."""

    def __init__(self):
        self.lock = threading.Lock()
        self.seen: Dict[str, float] = {}

    def fresh(self, kid: str, nonce: str) -> bool:
        t = time.time()
        with self.lock:
            if len(self.seen) > 50000 or (self.seen and len(self.seen) % 1000 == 0):
                self.seen = {k: v for k, v in self.seen.items() if v > t}
            if (kid + nonce) in self.seen:
                return False
            self.seen[kid + nonce] = t + 2 * signing.CLOCK_WINDOW
            return True


NONCES = Nonces()


def check_signed(req: Request) -> Optional[Response]:
    """A console's signed request (signing.py): who it is, or the 401 that says why not."""
    h = req.headers
    kid, when, nonce, sign = (h.get(signing.KEY_HEADER, ""), h.get(signing.TIME_HEADER, ""),
                              h.get(signing.NONCE_HEADER, ""), h.get(signing.SIGN_HEADER, "").lower())
    if not (re.fullmatch(r"[0-9a-f]{16}", kid) and re.fullmatch(r"\d{1,12}", when)
            and re.fullmatch(r"[0-9a-f]{8,64}", nonce) and re.fullmatch(r"[0-9a-f]{64}", sign)):
        return api_error(401, "malformed signature")
    device, key = db.device_for_key_id(req.conn, kid)
    if device is None:
        return api_error(401, "this console is not linked (or was unlinked)")
    if not secrets.compare_digest(signing.request_mac(key, req.method, req.target, when, nonce, req.body), sign):
        return api_error(401, "bad signature")
    if abs(int(when) - time.time()) > signing.CLOCK_WINDOW:
        return api_error(401, "clock: the request's time is too far from the server's")
    if not NONCES.fresh(kid, nonce):
        return api_error(401, "replayed request")
    req.device, req.sign_key, req.sign = device, key, sign
    return None


class Handler(BaseHTTPRequestHandler):
    server_version = "emerald3ds-sprites/1"
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        self.dispatch("GET")

    def do_POST(self):
        self.dispatch("POST")

    def do_HEAD(self):
        self.dispatch("GET", head=True)

    def client_ip(self) -> str:
        return self.headers.get("CF-Connecting-IP") or self.client_address[0]

    def dispatch(self, method: str, head: bool = False) -> None:
        started = time.time()
        url = urlsplit(self.path)
        if self.headers.get("Transfer-Encoding", "").lower() == "chunked":
            return self.send(Response(b"chunked bodies are not supported", 411, "text/plain"), head, started)
        try:
            length = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            length = -1
        if length < 0 or length > MAX_BODY:
            self.close_connection = True
            return self.send(Response(b"body too large", 413, "text/plain"), head, started)
        body = self.rfile.read(length) if length else b""
        https = (self.headers.get("X-Forwarded-Proto", "") == "https"
                 or '"https"' in self.headers.get("Cf-Visitor", ""))
        # Plain HTTP through Cloudflare is for signed console requests only:
        # browsers go to HTTPS, and a bearer token sent in the clear is not taken.
        plain_edge = '"http"' in self.headers.get("Cf-Visitor", "")
        if plain_edge and not url.path.startswith("/api/") and self.headers.get("Host"):
            return self.send(Response(b"", 301, "text/plain",
                                      [("Location", "https://%s%s" % (self.headers["Host"], self.path))]), head, started)
        conn = db.connect(CFG.db_path)
        req = Request(method, url.path, parse_qs(url.query), self.headers, body, self.client_ip(), https, conn)
        req.target = self.path
        req.plain_edge = plain_edge
        try:
            resp = self.handle_request(req)
        except Exception:
            traceback.print_exc()
            resp = (api_error(500, "server error") if url.path.startswith("/api/")
                    else Response(b"Something went wrong on the server.", 500, "text/plain"))
        finally:
            conn.close()
        if isinstance(resp, Stream):
            return self.stream(resp, req, started)
        self.send(resp, head, started, req)

    def handle_request(self, req: Request) -> Response:
        cookie = SimpleCookie(req.headers.get("Cookie", ""))
        if COOKIE in cookie and not req.plain_edge:  # a session cookie seen in the clear is not taken
            req.session_token = cookie[COOKIE].value
            req.user, req.csrf = db.session_user(req.conn, req.session_token)
        auth = req.headers.get("Authorization", "")
        if req.headers.get(signing.SIGN_HEADER):
            refused = check_signed(req)
            if refused is not None:
                return refused
        elif auth.startswith("Bearer ") and len(auth) < 200 and not req.plain_edge:
            req.device = db.device_for_token(req.conn, auth[7:].strip())
        allowed = False
        for method, pattern, fn, need in ROUTES:
            m = pattern.match(req.path)
            if not m:
                continue
            if method != req.method:
                allowed = True
                continue
            denied = authorise(req, need)
            return denied or fn(req, **m.groupdict())
        if allowed:
            return Response(b"method not allowed", 405, "text/plain")
        if req.path.startswith("/api/"):
            return api_error(404, "no such endpoint")
        if req.method == "GET" and not req.path.startswith(("/img/", "/dl/", "/assets/")):
            return app_page(req)
        return Response(b"not found", 404, "text/plain")

    def send(self, resp: Response, head: bool, started: float, req: Optional[Request] = None) -> None:
        self.send_response(resp.status)
        self.send_header("Content-Type", resp.ctype)
        self.send_header("Content-Length", str(len(resp.body)))
        for k, v in resp.headers:
            self.send_header(k, v)
        if req is not None and req.sign_key is not None:
            self.send_header(signing.SIGN_HEADER, signing.reply_mac(req.sign_key, req.sign, resp.status, resp.body))
        own = {k.lower() for k, _ in resp.headers}
        for k, v in SECURITY_HEADERS:
            if k.lower() not in own:
                self.send_header(k, v)
        if resp.ctype.startswith("text/html"):
            self.send_header("Cache-Control", "no-store")
        self.end_headers()
        if not head and resp.status != 304:
            self.wfile.write(resp.body)
        who = ""
        if req is not None:
            who = req.user["name"] if req.user else (("console:%s" % req.device["user_name"]) if req.device else "-")
        sys.stderr.write("%s %s %s %s %d %dB %dms %s\n" % (
            time.strftime("%H:%M:%S"), self.client_ip(), self.command, urlsplit(self.path).path, resp.status,
            len(resp.body), (time.time() - started) * 1000, who))

    def stream(self, s: Stream, req: Request, started: float) -> None:
        """The live channel: Server-Sent Events, chunked, until the browser goes or half an hour passes
        (it then reconnects by itself). A comment every 15 s keeps Cloudflare from closing it."""
        if not events.BUS.open_stream():
            return self.send(api_error(503, "too many live pages open; try again later"), False, started, req)
        self.close_connection = True
        sys.stderr.write("%s %s GET /api/web/events stream opened %s\n" % (
            time.strftime("%H:%M:%S"), self.client_ip(), req.user["name"]))

        def write(text: str) -> None:
            data = text.encode()
            self.wfile.write(b"%x\r\n%s\r\n" % (len(data), data))
            self.wfile.flush()

        try:
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream; charset=utf-8")
            self.send_header("Cache-Control", "no-cache, no-transform")
            self.send_header("X-Accel-Buffering", "no")
            self.send_header("Transfer-Encoding", "chunked")
            for k, v in SECURITY_HEADERS:
                self.send_header(k, v)
            self.end_headers()
            last = events.BUS.last_id
            write("retry: 3000\ndata: %s\n\n" % json.dumps({"type": "hello", "rev": s.rev}))
            deadline = time.time() + 30 * 60
            while time.time() < deadline:
                fresh = events.BUS.wait(last, 15)
                if not fresh:
                    write(": ping\n\n")
                    continue
                for i, ev in fresh:
                    last = i
                    if ev.get("_to", s.user_id) != s.user_id:
                        continue
                    shown = {k: v for k, v in ev.items() if k != "_to"}
                    write("id: %d\ndata: %s\n\n" % (i, json.dumps(shown, separators=(",", ":"))))
            self.wfile.write(b"0\r\n\r\n")
        except OSError:  # the browser went away
            pass
        finally:
            events.BUS.close_stream()

    def log_message(self, fmt, *args):  # the default log line is replaced by send()'s
        pass


def make_server(data_dir: str, host: str, port: int, sprites: Optional[str] = None) -> ThreadingHTTPServer:
    global CFG
    db.init(os.path.join(data_dir, "sprites.db"))
    CFG = Config(data_dir, sprites)
    server = ThreadingHTTPServer((host, port), Handler)
    server.daemon_threads = True
    return server


# ── Command line ────────────────────────────────────────────────────────────

def main(argv=None) -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", default=DEFAULT_DATA, help="the data folder (default %(default)s)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("serve", help="run the server")
    s.add_argument("--host", default="127.0.0.1")
    s.add_argument("--port", type=int, default=8787)
    s.add_argument("--sprites", help="sprites.json (default: <data>/sprites.json)")
    u = sub.add_parser("adduser", help="add a user; prints a generated password")
    u.add_argument("name")
    u.add_argument("--admin", action="store_true")
    p = sub.add_parser("passwd", help="set a user's password (asks for it)")
    p.add_argument("name")
    x = sub.add_parser("export-sprites", help="export the game's pictures for the previews")
    x.add_argument("--upstream", required=True, help="the pokeemerald tree (build/upstream)")
    a = ap.parse_args(argv)

    dbpath = os.path.join(a.data, "sprites.db")
    if a.cmd == "serve":
        server = make_server(a.data, a.host, a.port, a.sprites)
        print("serving %s on http://%s:%d (%d pictures for previews)" % (a.data, a.host, a.port, len(CFG.sprites)),
              file=sys.stderr)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
    elif a.cmd == "adduser":
        db.init(dbpath)
        conn = db.connect(dbpath)
        if conn.execute("SELECT 1 FROM users WHERE name = ?", (a.name,)).fetchone():
            sys.exit("there is already a user called %s" % a.name)
        password = new_password()
        db.add_user(conn, a.name, password, "admin" if a.admin else "artist")
        print("added %s (%s). Password: %s" % (a.name, "admin" if a.admin else "artist", password))
    elif a.cmd == "passwd":
        db.init(dbpath)
        conn = db.connect(dbpath)
        row = conn.execute("SELECT id FROM users WHERE name = ?", (a.name,)).fetchone()
        if row is None:
            sys.exit("no user called %s" % a.name)
        pw = getpass.getpass("New password for %s: " % a.name)
        if len(pw) < MIN_PASSWORD or pw != getpass.getpass("Again: "):
            sys.exit("passwords differ or are shorter than %d characters" % MIN_PASSWORD)
        db.set_password(conn, row["id"], pw)
        print("password changed")
    elif a.cmd == "export-sprites":
        import sprites
        data = sprites.export(a.upstream)
        os.makedirs(a.data, exist_ok=True)
        out = os.path.join(a.data, "sprites.json")
        with open(out, "w") as f:
            json.dump(data, f, separators=(",", ":"))
        print("%d pictures -> %s (restart the server to use them)" % (len(data["sprites"]), out))


if __name__ == "__main__":
    main()
