#!/usr/bin/env python3
"""The plane-sharing server for the 3DS port's on-device plane editor (v1).

Volunteers upload the edit files their consoles save (sdmc:/3ds/emerald3ds/stereo/<key>.bin);
everybody sees on a web page which Pokémon have been done; consoles and PCs download the
current version of each. Runs behind a Cloudflare Tunnel; listens on localhost only.

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
import html
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
from urllib.parse import parse_qs, quote, urlsplit

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import db  # noqa: E402
import editfile  # noqa: E402
import signing  # noqa: E402

# 2: signed plain HTTP for consoles (signing.py), /api/pair2, the clock in /api/health.
API_VERSION = 2
DEFAULT_DATA = os.path.expanduser("~/emerald3ds-sprite-server")
STATIC_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "static")
STATIC_FILES = {"style.css": "text/css; charset=utf-8", "app.js": "text/javascript; charset=utf-8"}
# Changes whenever a static file does, so browsers never keep an old one.
STATIC_STAMP = "%x" % int(max(os.path.getmtime(os.path.join(STATIC_DIR, n)) for n in STATIC_FILES))
MAX_BODY = 64 * 1024
COOKIE = "e3s"
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


def e(s) -> str:
    return html.escape(str(s), quote=True)


def ago(t: int) -> str:
    if not t:
        return "?"
    d = max(0, int(time.time()) - t)
    for unit, n in (("d", 86400), ("h", 3600), ("min", 60)):
        if d >= n:
            return "%d %s ago" % (d // n, unit)
    return "just now"


def utc(t: int) -> str:
    return time.strftime("%Y-%m-%d %H:%M UTC", time.gmtime(t)) if t else ""


class Response:
    def __init__(self, body: bytes = b"", status: int = 200, ctype: str = "text/html; charset=utf-8",
                 headers: Optional[List[Tuple[str, str]]] = None):
        self.body, self.status, self.ctype = body, status, ctype
        self.headers = headers or []


def json_resp(obj, status: int = 200, headers=None) -> Response:
    return Response(json.dumps(obj, separators=(",", ":")).encode(), status, "application/json", headers)


def api_error(status: int, message: str) -> Response:
    return json_resp({"error": message}, status)


def redirect(location: str, headers=None) -> Response:
    return Response(b"", 303, "text/plain", [("Location", location)] + (headers or []))


class Request:
    def __init__(self, method: str, path: str, query: Dict[str, List[str]], headers, body: bytes, ip: str,
                 https: bool, conn):
        self.method, self.path, self.query, self.headers = method, path, query, headers
        self.body, self.ip, self.https, self.conn = body, ip, https, conn
        self.user = None
        self.csrf = None
        self.device = None
        self.session_token = None
        self._form = None
        self.target = path      # the path with its query, as sent (what a console signs)
        self.sign_key = None    # a signed request's device key and MAC: the answer is signed back
        self.sign = None
        self.plain_edge = False # came over plain HTTP through Cloudflare

    def q(self, name: str, default: str = "") -> str:
        return self.query.get(name, [default])[0]

    @property
    def form(self) -> Dict[str, str]:
        if self._form is None:
            parsed = parse_qs(self.body.decode("utf-8", "replace"), keep_blank_values=True)
            self._form = {k: v[0] for k, v in parsed.items()}
        return self._form

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
    """auth: None, 'session' (signed-in web user), 'admin', 'device' (console token) or 'any' (either)."""
    def wrap(fn):
        ROUTES.append((method, re.compile("^" + pattern + "$"), fn, auth))
        return fn
    return wrap


def authorise(req: Request, auth: Optional[str]) -> Optional[Response]:
    api = req.path.startswith("/api/")
    if auth is None:
        return None
    if auth == "device" and req.device is None:
        return api_error(401, "this console is not linked (or was unlinked)")
    if auth == "any" and req.device is None and req.user is None:
        return api_error(401, "sign in or link a console") if api else redirect("/login?next=" + quote(req.path))
    if auth in ("session", "admin"):
        if req.user is None:
            return api_error(401, "sign in") if api else redirect("/login?next=" + quote(req.path))
        if auth == "admin" and req.user["role"] != "admin":
            return Response(page(req, "Not allowed", "<p>Only admins can do that.</p>"), 403)
        if req.method == "POST" and not secrets.compare_digest(req.form.get("csrf", ""), req.csrf or ""):
            return Response(page(req, "Expired form", "<p>That form had expired. Go back, reload and try again.</p>"),
                            400)
    return None


# ── Pages ───────────────────────────────────────────────────────────────────

def page(req: Request, title: str, body: str, flash: str = "") -> bytes:
    nav = ""
    if req.user is not None:
        admin = '<a href="/admin">Users</a>' if req.user["role"] == "admin" else ""
        nav = ('<nav><a href="/">Progress</a><a href="/account">Account</a>%s'
               '<form method="post" action="/logout" class="inline">%s<button class="link">Sign out %s</button>'
               '</form></nav>') % (admin, csrf_field(req), e(req.user["name"]))
    flash_html = '<p class="flash">%s</p>' % e(flash) if flash else ""
    return ("""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>%s · Emerald 3DS planes</title><link rel="stylesheet" href="/static/style.css?%s">
<script src="/static/app.js?%s" defer></script></head>
<body><header><a class="brand" href="/">Emerald 3DS <span>plane sharing</span></a>%s</header>
<main>%s%s</main></body></html>""" % (e(title), STATIC_STAMP, STATIC_STAMP, nav, flash_html, body)).encode()


def csrf_field(req: Request) -> str:
    return '<input type="hidden" name="csrf" value="%s">' % e(req.csrf or "")


MESSAGES = {
    "pinned": "That version is now the current one and stays so until unpinned.",
    "unpinned": "Unpinned: the most votes win again (among equals, the newest).",
    "voted": "Your votes for this picture are saved.",
    "twofavourites": "That is as many votes as one picture takes.",
    "deleted": "The version was deleted.",
    "revoked": "The console was unlinked. Its token no longer works.",
    "password": "Password changed. Other browsers were signed out.",
    "named": "Your name is saved; your versions show it from now on.",
    "disabled": "The user can no longer sign in, and their consoles stop working.",
    "enabled": "The user can sign in again.",
}


@route("GET", r"/login")
def login_form(req: Request, error: str = "") -> Response:
    nxt = req.q("next", "/")
    err = '<p class="error">%s</p>' % e(error) if error else ""
    body = """<section class="narrow signin"><h1>Sign in</h1>%s
<form method="post" action="/login" class="stack">
<input type="hidden" name="next" value="%s">
<label>Name <input name="name" autocomplete="username" required autofocus></label>
<label>Password <input name="password" type="password" autocomplete="current-password" required></label>
<button>Sign in</button></form>
<p class="muted">Accounts are handed out by the project's admin.</p></section>""" % (err, e(nxt))
    return Response(page(req, "Sign in", body), 401 if error else 200)


@route("POST", r"/login")
def login_post(req: Request) -> Response:
    name = req.form.get("name", "").strip()
    if not LIMITS.allow("login:" + req.ip, 10, 900) or not LIMITS.allow("login:" + name.lower(), 10, 900):
        return login_form(req, "Too many attempts. Wait a quarter of an hour.")
    user = db.login(req.conn, name, req.form.get("password", ""))
    if user is None:
        req.query["next"] = [req.form.get("next", "/")]
        return login_form(req, "Wrong name or password.")
    token = db.new_session(req.conn, user["id"])
    nxt = req.form.get("next", "/")
    if not nxt.startswith("/") or nxt.startswith("//"):
        nxt = "/"
    cookie = "%s=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=%d%s" % (
        COOKIE, token, db.SESSION_SECONDS, "; Secure" if req.https else "")
    return redirect(nxt, [("Set-Cookie", cookie)])


@route("POST", r"/logout", "session")
def logout(req: Request) -> Response:
    db.end_session(req.conn, req.session_token)
    return redirect("/login", [("Set-Cookie", "%s=; Path=/; Max-Age=0" % COOKIE)])


def tile_image(key: str, vid: Optional[int]) -> str:
    if vid is not None:
        return "/img/v%d/tint.png" % vid
    if key in CFG.sprites:
        return "/img/sprite/%s.png" % key
    return ""


@route("GET", r"/", "session")
def grid(req: Request) -> Response:
    kind = req.q("kind", "front")
    show = req.q("show", "all")
    rows = req.conn.execute(
        "SELECT c.mon_key, c.version_id, c.pinned, v.final, COALESCE(u.display_name, u.name) AS author, "
        "(SELECT COUNT(*) FROM versions w WHERE w.mon_key = c.mon_key) AS n "
        "FROM current c JOIN versions v ON v.id = c.version_id JOIN users u ON u.id = v.user_id").fetchall()
    cur = {r["mon_key"]: r for r in rows}
    mine = {r[0] for r in req.conn.execute("SELECT DISTINCT mon_key FROM versions WHERE user_id = ?",
                                           (req.user["id"],))}
    fronts = ["%03u" % n for n in range(1, DEX_COUNT + 1)]
    backs = ["b" + k for k in fronts]
    known = set(fronts) | set(backs)
    others = sorted(k for k in cur if k not in known)
    keys = {"front": fronts, "back": backs, "other": others}.get(kind, fronts)

    def stat(ks):
        done = sum(1 for k in ks if k in cur)
        final = sum(1 for k in ks if k in cur and cur[k]["final"])
        return done, final, len(ks)

    tabs = []
    for k, label, ks in (("front", "Fronts", fronts), ("back", "Backs", backs), ("other", "Trainers & more", others)):
        done, final, total = stat(ks)
        count = "%d/%d" % (done, total) if k != "other" else str(total)
        tabs.append('<a href="/?kind=%s&show=%s" class="%s">%s <small>%s</small></a>'
                    % (k, show, "on" if k == kind else "", label, count))
    filters = []
    for s, label in (("all", "All"), ("open", "Not done"), ("done", "Done"), ("mine", "Mine")):
        filters.append('<a href="/?kind=%s&show=%s" class="%s">%s</a>' % (kind, s, "on" if s == show else "", label))

    tiles = []
    for key in keys:
        c = cur.get(key)
        if show == "open" and c is not None or show == "done" and c is None or show == "mine" and key not in mine:
            continue
        img = tile_image(key, c["version_id"] if c else None)
        img_html = '<img src="%s" width="64" height="64" alt="" loading="lazy">' % img if img else '<i></i>'
        cls = ["tile", "done" if c else "open"]
        if c and c["final"]:
            cls.append("final")
        badge = '<b class="badge" title="versions">%d</b>' % c["n"] if c and c["n"] > 1 else ""
        who = '<span class="who">%s</span>' % e(c["author"]) if c else ""
        tiles.append('<a class="%s" href="/mon/%s">%s<span class="num">%s</span><span class="name">%s</span>%s%s</a>'
                     % (" ".join(cls), key, img_html, e(key), e(CFG.name(key)), who, badge))
    done, final, total = stat(keys)
    summary = ('<p class="summary"><strong>%d</strong> of %d done, <strong>%d</strong> marked final.</p>'
               % (done, total, final)) if kind != "other" else ""
    empty = '<p class="muted">Nothing here yet.</p>' if not tiles else ""
    body = ('<div class="bar"><div class="tabs">%s</div><div class="filters">%s</div></div>%s'
            '<div class="grid">%s</div>%s') % ("".join(tabs), "".join(filters), summary, "".join(tiles), empty)
    return Response(page(req, "Progress", body, MESSAGES.get(req.q("msg"), "")))


def neighbours(key: str) -> Tuple[Optional[str], Optional[str], Optional[str]]:
    """Previous, next and the other side's key."""
    m = editfile.KEY_RE.match(key)
    prefix, n = m.group(1), int(m.group(2))
    if prefix in ("", "b") and 1 <= n <= DEX_COUNT:
        prev = "%s%03u" % (prefix, n - 1) if n > 1 else None
        nxt = "%s%03u" % (prefix, n + 1) if n < DEX_COUNT else None
        other = ("b%03u" % n) if prefix == "" else ("%03u" % n)
        return prev, nxt, other
    return None, None, None


def layer_stack(vid: int, edit: editfile.Edit) -> str:
    used = edit.used_planes()
    n = edit.planes
    imgs = []
    for p in used:
        d = (p - (n - 1) / 2) / ((n - 1) / 2) if n > 1 else 0
        imgs.append('<img src="/img/v%d/layer%d.png" data-d="%.3f" alt="">' % (vid, p, d))
    return '<div class="sway" title="The planes, swaying: nearer planes move more">%s</div>' % "".join(imgs)


@route("GET", r"/mon/" + KEY, "session")
def mon_page(req: Request, key: str) -> Response:
    if editfile.key_to_id(key) is None:
        return Response(page(req, "Not found", "<p>No such picture.</p>"), 404)
    versions = req.conn.execute(
        "SELECT v.id, v.planes, v.frames, v.final, v.revision, v.author_tag, v.saved_at, v.size, v.uploaded_at, "
        "COALESCE(u.display_name, u.name) AS uploader, d.console_name FROM versions v JOIN users u ON u.id = v.user_id "
        "LEFT JOIN devices d ON d.id = v.device_id WHERE v.mon_key = ? ORDER BY v.uploaded_at DESC, v.id DESC",
        (key,)).fetchall()
    cur = req.conn.execute("SELECT * FROM current WHERE mon_key = ?", (key,)).fetchone()
    admin = req.user["role"] == "admin"
    counts = db.vote_counts(req.conn, key)
    versions = by_votes(versions, counts)
    mine = db.favourites(req.conn, req.user["id"], key)
    prev, nxt, other = neighbours(key)
    links = []
    if prev:
        links.append('<a href="/mon/%s">‹ %s</a>' % (prev, e(CFG.name(prev))))
    if other:
        links.append('<a href="/mon/%s">%s side</a>' % (other, "Back" if other.startswith("b") else "Front"))
    if nxt:
        links.append('<a href="/mon/%s">%s ›</a>' % (nxt, e(CFG.name(nxt))))
    head = ('<div class="monhead"><h1><span class="num">%s</span> %s <small>%s</small></h1><div class="nav2">%s</div></div>'
            % (e(key), e(CFG.name(key)), e(editfile.key_kind(key)), " · ".join(links)))
    hero = ""
    if cur is not None:
        row = req.conn.execute("SELECT data FROM versions WHERE id = ?", (cur["version_id"],)).fetchone()
        edit = editfile.parse(row["data"])
        vid = cur["version_id"]
        pin_note = ""
        if cur["pinned"]:
            pin_note = '<p class="pill">Pinned by an admin</p>'
            if admin:
                pin_note += ('<form method="post" action="/mon/%s/unpin" class="inline">%s<button class="small">'
                             'Unpin (newest wins again)</button></form>') % (key, csrf_field(req))
        sprite = ('<figure><img class="big" src="/img/sprite/%s.png" alt=""><figcaption>Game picture</figcaption></figure>'
                  % key) if key in CFG.sprites else ""
        hero = ('<div class="hero">%s<figure>%s<figcaption>Planes in motion</figcaption></figure>'
                '<figure><img class="big" src="/img/v%d/tint.png" alt=""><figcaption>Tinted by plane</figcaption></figure>'
                '<figure><img class="big" src="/img/v%d/planes.png" alt=""><figcaption>Planes only</figcaption></figure>'
                '</div><div class="current-note"><p>Current version: #%d. %s</p>%s</div>') % (
            sprite, layer_stack(vid, edit), vid, vid,
            vid, "Consoles download this one when they sync.", pin_note)
    elif key in CFG.sprites:
        hero = ('<div class="hero"><figure><img class="big faded" src="/img/sprite/%s.png" alt="">'
                '<figcaption>Nobody has uploaded planes for this picture yet.</figcaption></figure></div>') % key
    else:
        hero = '<p class="muted">Nobody has uploaded planes for this picture yet.</p>'

    rows = []
    for v in versions:
        is_cur = cur is not None and cur["version_id"] == v["id"]
        tags = []
        if is_cur:
            tags.append('<span class="pill on">%s</span>' % ("current, pinned" if cur["pinned"] else "current"))
        if v["final"]:
            tags.append('<span class="pill final">final</span>')
        author = e(v["uploader"])
        if v["author_tag"] and v["author_tag"].lower() != v["uploader"].lower():
            author += ' <span class="muted">(signed %s)</span>' % e(v["author_tag"])
        where = ' <span class="muted">from %s</span>' % e(v["console_name"]) if v["console_name"] else ""
        starred = v["id"] in mine
        actions = ['<form method="post" action="/mon/%s/vote" class="inline">%s<input type="hidden" name="version" '
                   'value="%d"><button class="small%s" title="%s">%s</button></form>'
                   % (key, csrf_field(req), v["id"], " on" if starred else "",
                      "Take your vote back" if starred else "Vote for this version",
                      "&#9733; Voted" if starred else "&#9734; Vote"),
                   '<a href="/dl/v%d/%s.bin">Download</a>' % (v["id"], key)]
        if admin and not (is_cur and cur["pinned"]):
            actions.append('<form method="post" action="/mon/%s/pin" class="inline">%s'
                           '<input type="hidden" name="version" value="%d"><button class="small">%s</button>'
                           '</form>' % (key, csrf_field(req), v["id"], "Pin" if is_cur else "Make current"))
        if admin:
            actions.append('<form method="post" action="/versions/%d/delete" class="inline" '
                           'data-confirm="Delete version #%d for good?">%s<button class="small danger">Delete</button>'
                           '</form>' % (v["id"], v["id"], csrf_field(req)))
        rev = "rev %d" % v["revision"] if v["revision"] else ""
        rows.append(
            '<tr class="%s"><td><img src="/img/v%d/tint.png" width="64" height="64" alt=""></td>'
            '<td><strong>#%d</strong> %s<br>by %s%s</td><td>%d planes, %d frame%s<br>%s</td>'
            '<td class="votes">%s</td><td title="%s">%s</td><td class="actions">%s</td></tr>'
            % ("is-current" if is_cur else "", v["id"], v["id"], " ".join(tags), author, where, v["planes"],
               v["frames"], "s" if v["frames"] > 1 else "", rev,
               "%d vote%s" % (counts.get(v["id"], 0), "" if counts.get(v["id"], 0) == 1 else "s"),
               e(utc(v["uploaded_at"])), ago(v["uploaded_at"]), " ".join(actions)))
    table = ('<h2>Versions</h2><table class="versions"><tbody>%s</tbody></table>' % "".join(rows)) if rows else ""
    rule = ('<p class="muted rule">Vote for as many versions as you like, here or on the console; the front and '
            'the back are voted on apart. The most votes come first and are current (among equals, the newest), '
            'unless an admin pins one.</p>'
            if rows else "")
    return Response(page(req, CFG.name(key), head + hero + table + rule, MESSAGES.get(req.q("msg"), "")))


@route("POST", r"/mon/" + KEY + r"/vote", "session")
def vote_post(req: Request, key: str) -> Response:
    """A version's vote: on, or off again."""
    try:
        vid = int(req.form.get("version", ""))
    except ValueError:
        vid = -1
    mine = db.favourites(req.conn, req.user["id"], key)
    if vid in mine:
        mine.remove(vid)
    elif len(mine) >= db.MAX_FAVOURITES:
        return redirect("/mon/%s?msg=twofavourites" % key)
    else:
        mine.append(vid)
    if db.set_votes(req.conn, req.user["id"], key, mine) is not None:
        return Response(page(req, "Not found", "<p>No such version.</p>"), 404)
    return redirect("/mon/%s?msg=voted" % key)


@route("POST", r"/mon/" + KEY + r"/pin", "admin")
def pin_post(req: Request, key: str) -> Response:
    try:
        vid = int(req.form.get("version", ""))
    except ValueError:
        vid = -1
    if not db.pin(req.conn, key, vid):
        return Response(page(req, "Not found", "<p>No such version.</p>"), 404)
    return redirect("/mon/%s?msg=pinned" % key)


@route("POST", r"/mon/" + KEY + r"/unpin", "admin")
def unpin_post(req: Request, key: str) -> Response:
    db.unpin(req.conn, key)
    return redirect("/mon/%s?msg=unpinned" % key)


@route("POST", r"/versions/(?P<vid>\d+)/delete", "admin")
def delete_post(req: Request, vid: str) -> Response:
    key = db.delete_version(req.conn, int(vid))
    if key is None:
        return Response(page(req, "Not found", "<p>No such version.</p>"), 404)
    return redirect("/mon/%s?msg=deleted" % key)


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


@route("GET", r"/dl/v(?P<vid>\d+)/" + KEY + r"\.bin", "session")
def download(req: Request, vid: str, key: str) -> Response:
    row = req.conn.execute("SELECT * FROM versions WHERE id = ? AND mon_key = ?", (int(vid), key)).fetchone()
    if row is None:
        return Response(b"not found", 404, "text/plain")
    return bin_response(row, req, attachment=True)


@route("GET", r"/static/(?P<name>[a-z]+\.(?:css|js))")
def static(req: Request, name: str) -> Response:
    if name not in STATIC_FILES:
        return Response(b"not found", 404, "text/plain")
    with open(os.path.join(STATIC_DIR, name), "rb") as f:
        return Response(f.read(), 200, STATIC_FILES[name], [("Cache-Control", "public, max-age=31536000, immutable")])


# ── Account and admin ───────────────────────────────────────────────────────

@route("GET", r"/account", "session")
def account(req: Request, extra: str = "", flash: str = "") -> Response:
    devices = req.conn.execute(
        "SELECT * FROM devices WHERE user_id = ? AND revoked = 0 ORDER BY created DESC", (req.user["id"],)).fetchall()
    rows = "".join(
        '<tr><td>%s</td><td>linked %s</td><td>%s</td><td><form method="post" action="/account/devices/%d/revoke" '
        'class="inline" data-confirm="Unlink %s? It will need a new code to upload again.">%s'
        '<button class="small danger">Unlink</button></form></td></tr>'
        % (e(d["console_name"]), ago(d["created"]), "last seen " + ago(d["last_seen"]) if d["last_seen"] else "never used",
           d["id"], e(d["console_name"]), csrf_field(req)) for d in devices)
    devices_html = ('<table class="plain">%s</table>' % rows) if rows else '<p class="muted">No consoles linked yet.</p>'
    body = """<h1>Your account</h1>%s
<section class="narrow"><h2>Your name</h2>
<form method="post" action="/account/name" class="stack">%s
<label>Shown with your versions <input name="name" value="%s" maxlength="16" pattern="[A-Za-z0-9 ._-]+" required></label>
<button>Save name</button></form>
<p class="muted">The plane editor's ONLINE tab sets it too (YOUR NAME). You sign in as <strong>%s</strong>.</p></section>
<section><h2>Consoles and computers</h2>%s
<form method="post" action="/account/pair">%s<button>Link a console</button></form>
<p class="muted">You get a code that works once, for ten minutes. Type it on the 3DS, or give it to the PC script.</p></section>
<section class="narrow"><h2>Change password</h2>
<form method="post" action="/account/password" class="stack">%s
<label>Current password <input name="old" type="password" autocomplete="current-password" required></label>
<label>New password <input name="new" type="password" autocomplete="new-password" minlength="%d" required></label>
<label>New password again <input name="again" type="password" autocomplete="new-password" required></label>
<button>Change password</button></form></section>""" % (extra, csrf_field(req), e(db.shown_name(req.user)),
                                                      e(req.user["name"]), devices_html, csrf_field(req),
                                                      csrf_field(req), MIN_PASSWORD)
    return Response(page(req, "Account", body, flash or MESSAGES.get(req.q("msg"), "")))


@route("POST", r"/account/name", "session")
def account_name(req: Request) -> Response:
    if db.set_display_name(req.conn, req.user["id"], req.form.get("name", "")) is None:
        return account(req, flash="Names are letters, digits, spaces, dots, dashes and underscores, up to 16.")
    db.bump_rev(req.conn)
    return redirect("/account?msg=named")


@route("POST", r"/account/pair", "session")
def account_pair(req: Request) -> Response:
    code = db.new_pairing_code(req.conn, req.user["id"])
    shown = code[:4] + "-" + code[4:]
    base = public_base(req)
    extra = """<section class="code"><h2>Your pairing code</h2><p class="bigcode">%s</p>
<p>It works once and expires in ten minutes.</p>
<ul><li><strong>On the 3DS:</strong> OPTIONS, the ENHANCEMENTS tab's second screen, ONLINE, then LINK CONSOLE,
and type the code (the first time it also asks for this server's address: %s).</li>
<li><strong>On a computer:</strong><pre>python3 tools/sprite_server/client.py pair %s %s</pre></li></ul></section>""" % (
        e(shown), e(base), e(base), e(shown))
    return account(req, extra)


@route("POST", r"/account/devices/(?P<did>\d+)/revoke", "session")
def account_revoke(req: Request, did: str) -> Response:
    req.conn.execute("UPDATE devices SET revoked = 1 WHERE id = ? AND user_id = ?", (int(did), req.user["id"]))
    return redirect("/account?msg=revoked")


@route("POST", r"/account/password", "session")
def account_password(req: Request) -> Response:
    f = req.form
    if not db.check_password(f.get("old", ""), req.user["pass_hash"]):
        return account(req, flash="The current password was wrong.")
    if len(f.get("new", "")) < MIN_PASSWORD:
        return account(req, flash="The new password needs at least %d characters." % MIN_PASSWORD)
    if f.get("new") != f.get("again"):
        return account(req, flash="The two new passwords differ.")
    db.set_password(req.conn, req.user["id"], f["new"])
    token = db.new_session(req.conn, req.user["id"])
    cookie = "%s=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=%d%s" % (
        COOKIE, token, db.SESSION_SECONDS, "; Secure" if req.https else "")
    return redirect("/account?msg=password", [("Set-Cookie", cookie)])


def public_base(req: Request) -> str:
    host = req.headers.get("Host", "localhost")
    return ("https://" if req.https else "http://") + host


def new_password() -> str:
    return "-".join(secrets.token_urlsafe(6) for _ in range(3))


@route("GET", r"/admin", "admin")
def admin(req: Request, flash: str = "") -> Response:
    users = req.conn.execute(
        "SELECT u.*, (SELECT COUNT(*) FROM devices d WHERE d.user_id = u.id AND d.revoked = 0) AS consoles, "
        "(SELECT COUNT(*) FROM versions v WHERE v.user_id = u.id) AS uploads FROM users u ORDER BY u.name").fetchall()
    rows = []
    for u in users:
        actions = []
        if u["id"] != req.user["id"]:
            act = "enable" if u["disabled"] else "disable"
            actions.append('<form method="post" action="/admin/users/%d/%s" class="inline">%s<button class="small%s">'
                           '%s</button></form>' % (u["id"], act, csrf_field(req), "" if u["disabled"] else " danger",
                                                   act.title()))
        actions.append('<form method="post" action="/admin/users/%d/reset" class="inline" data-confirm="Make a new '
                       'password for %s? The old one stops working.">%s<button class="small">New password</button>'
                       '</form>' % (u["id"], e(u["name"]), csrf_field(req)))
        shown = "" if not u["display_name"] or u["display_name"] == u["name"] else (
            ' <span class="muted">(%s)</span>' % e(u["display_name"]))
        rows.append('<tr class="%s"><td><strong>%s</strong>%s</td><td>%s</td><td>%d consoles</td><td>%d uploads</td>'
                    '<td>%s</td><td class="actions">%s</td></tr>'
                    % ("disabled" if u["disabled"] else "", e(u["name"]), shown, u["role"], u["consoles"], u["uploads"],
                       "disabled" if u["disabled"] else "since " + ago(u["created"]), " ".join(actions)))
    body = """<h1>Users</h1>
<section class="narrow"><h2>Add a user</h2><form method="post" action="/admin/users" class="stack">%s
<label>Name <input name="name" required maxlength="32" pattern="[A-Za-z0-9_. -]+" autofocus></label>
<label>Role <select name="role"><option value="artist">Artist</option><option value="admin">Admin</option></select></label>
<button>Add user</button></form>
<p class="muted">The server makes the password; you see it once and pass it on.</p></section>
<h2>Everybody</h2><table class="plain">%s</table>""" % (csrf_field(req), "".join(rows))
    return Response(page(req, "Users", body, flash or MESSAGES.get(req.q("msg"), "")))


def password_shown(req: Request, name: str, password: str, title: str) -> Response:
    body = """<section class="narrow code"><h1>%s</h1><p>Pass this on to <strong>%s</strong>. It is shown only now.</p>
<p class="bigcode small">%s</p><p class="muted">They can change it on their account page after signing in at
<code>%s</code>.</p><p><a href="/admin">Back to users</a></p></section>""" % (
        e(title), e(name), e(password), e(public_base(req)))
    return Response(page(req, title, body))


@route("POST", r"/admin/users", "admin")
def admin_add(req: Request) -> Response:
    name = req.form.get("name", "").strip()
    role = req.form.get("role", "artist")
    if not re.fullmatch(r"[A-Za-z0-9_. -]{1,32}", name) or role not in ("artist", "admin"):
        return admin(req, "Names are letters, digits, spaces, dots, dashes and underscores, up to 32.")
    if req.conn.execute("SELECT 1 FROM users WHERE name = ?", (name,)).fetchone():
        return admin(req, "There is already a user called %s." % name)
    password = new_password()
    db.add_user(req.conn, name, password, role)
    return password_shown(req, name, password, "User added")


@route("POST", r"/admin/users/(?P<uid>\d+)/(?P<act>enable|disable|reset)", "admin")
def admin_user(req: Request, uid: str, act: str) -> Response:
    user = req.conn.execute("SELECT * FROM users WHERE id = ?", (int(uid),)).fetchone()
    if user is None:
        return Response(page(req, "Not found", "<p>No such user.</p>"), 404)
    if act == "reset":
        password = new_password()
        db.set_password(req.conn, user["id"], password)
        return password_shown(req, user["name"], password, "New password")
    if user["id"] == req.user["id"]:
        return admin(req, "You cannot disable yourself.")
    req.conn.execute("UPDATE users SET disabled = ? WHERE id = ?", (int(act == "disable"), user["id"]))
    if act == "disable":
        req.conn.execute("DELETE FROM sessions WHERE user_id = ?", (user["id"],))
    return redirect("/admin?msg=%sd" % act)


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
    return json_resp({"token": token, "user": user})


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
    if db.set_display_name(req.conn, req.device["user_id"], str(req.json().get("name", ""))) is None:
        return api_error(400, "names are letters, digits, spaces, dots, dashes and underscores, up to 16")
    db.bump_rev(req.conn)
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
    return json_resp({"key": key, "version_id": vid, "current": current, "duplicate": duplicate,
                      "sha256": edit.sha256}, 200 if duplicate else 201)


def by_votes(rows, counts: dict) -> list:
    """The most votes first; among equals the newest (rows come newest first)."""
    return sorted(rows, key=lambda r: -counts.get(r["id"], 0))


def _voter(req: Request) -> int:
    return req.device["user_id"] if req.device is not None else req.user["id"]


def _versions_json(req: Request, key: str) -> dict:
    rows = req.conn.execute(
        "SELECT v.id, v.user_id, v.revision, v.final, v.author_tag, v.sha256, v.size, v.uploaded_at, COALESCE(u.display_name, u.name) AS uploader "
        "FROM versions v JOIN users u ON u.id = v.user_id WHERE v.mon_key = ? ORDER BY v.uploaded_at DESC, v.id DESC",
        (key,)).fetchall()
    cur = req.conn.execute("SELECT * FROM current WHERE mon_key = ?", (key,)).fetchone()
    counts = db.vote_counts(req.conn, key)
    rows = by_votes(rows, counts)
    return {"key": key, "current": cur["version_id"] if cur else None, "pinned": bool(cur and cur["pinned"]),
            "favourites": db.favourites(req.conn, _voter(req), key), "max_favourites": db.MAX_FAVOURITES,
            "versions": [{"version_id": r["id"], "author": r["uploader"], "signed": r["author_tag"],
                          "revision": r["revision"], "final": bool(r["final"]), "votes": counts.get(r["id"], 0),
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
    return json_resp(_versions_json(req, key))


@route("POST", r"/api/versions/(?P<vid>\d+)/delete", "device")
def api_delete_version(req: Request, vid: str) -> Response:
    """A console takes its account's own version off the server (an admin's console any)."""
    row = req.conn.execute("SELECT mon_key, user_id FROM versions WHERE id = ?", (int(vid),)).fetchone()
    if row is None:
        return api_error(404, "no such version")
    if row["user_id"] != req.device["user_id"] and req.device["role"] != "admin":
        return api_error(403, "only its uploader can delete a version")
    db.delete_version(req.conn, int(vid))
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
        "SELECT c.mon_key, c.version_id, c.seq, v.sha256, v.size, v.revision, v.final FROM current c "
        "JOIN versions v ON v.id = c.version_id WHERE c.seq > ? ORDER BY c.seq", (since,)).fetchall()
    return json_resp({"seq": db.current_seq(req.conn),
                      "items": [{"key": r["mon_key"], "version_id": r["version_id"], "sha256": r["sha256"],
                                 "size": r["size"], "revision": r["revision"], "final": bool(r["final"])}
                                for r in rows]})


@route("GET", r"/api/mons", "any")
def api_mons(req: Request) -> Response:
    """Every picture with a current version: how many versions it has, and how many of them are the
    caller's favourites (a console's Pokédex marks both)."""
    rows = req.conn.execute(
        "SELECT c.mon_key, c.version_id, c.pinned, v.final, COALESCE(u.display_name, u.name) AS author, "
        "(SELECT COUNT(*) FROM versions w WHERE w.mon_key = c.mon_key) AS n, "
        "(SELECT COUNT(*) FROM votes t WHERE t.mon_key = c.mon_key AND t.user_id = ?) AS mine "
        "FROM current c JOIN versions v ON v.id = c.version_id JOIN users u ON u.id = v.user_id "
        "ORDER BY c.mon_key", (_voter(req),)).fetchall()
    return json_resp({"rev": db.current_rev(req.conn),
                      "items": [{"key": r["mon_key"], "name": CFG.name(r["mon_key"]), "current": r["version_id"],
                                 "pinned": bool(r["pinned"]), "final": bool(r["final"]), "author": r["author"],
                                 "versions": r["n"], "mine": r["mine"]} for r in rows]})


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
        self.send(resp, head, started, req)

    def handle_request(self, req: Request) -> Response:
        cookie = SimpleCookie(req.headers.get("Cookie", ""))
        if COOKIE in cookie:
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
        return Response(page(req, "Not found", "<p>There is nothing here.</p>"), 404)

    def send(self, resp: Response, head: bool, started: float, req: Optional[Request] = None) -> None:
        self.send_response(resp.status)
        self.send_header("Content-Type", resp.ctype)
        self.send_header("Content-Length", str(len(resp.body)))
        for k, v in resp.headers:
            self.send_header(k, v)
        if req is not None and req.sign_key is not None:
            self.send_header(signing.SIGN_HEADER, signing.reply_mac(req.sign_key, req.sign, resp.status, resp.body))
        for k, v in SECURITY_HEADERS:
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
