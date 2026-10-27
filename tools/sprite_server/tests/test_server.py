"""End-to-end tests: a real server on a free port, an empty data folder, the web app's API,
the console API and the PC client.

    python3 -m unittest discover -s tools/sprite_server/tests -v
"""
import http.cookiejar
import json
import os
import re
import shutil
import struct
import sys
import tempfile
import threading
import unittest
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import client  # noqa: E402
import db  # noqa: E402
import editfile  # noqa: E402
import server  # noqa: E402
import signing  # noqa: E402


def make_edit(key: str, planes: int = 4, frames: int = 2, seed: int = 0, meta: bool = True, author: str = "TESTER",
              revision: int = 1, final: bool = False, depth: int = 0) -> bytes:
    ident = editfile.key_to_id(key)
    head = editfile.MAGIC + struct.pack("<HBBBBBB", ident, planes, frames, 64, 0, 64, 0)
    body = bytearray()
    for f in range(frames):
        for i in range(64 * 32):
            y = (2 * i) // 64
            lo = (y + seed + f) % planes if 8 <= y < 56 else 15
            hi = lo
            body.append(lo | (hi << 4))
    out = head + bytes(body)
    if meta:
        t = editfile.META_MAGIC + bytes([1 if final else 0, depth]) + struct.pack("<HI", revision, 800000000)
        out += t + author.encode().ljust(16, b"\0")
    return out


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


class Browser:
    """What the web app does: a cookie-keeping client sending JSON, with the CSRF header on POSTs."""

    def __init__(self, base: str):
        self.base = base
        self.jar = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.jar), NoRedirect)
        self.token = ""

    def go(self, method: str, path: str, obj=None, csrf=None, headers=None):
        """(status, headers, body bytes)."""
        h = dict(headers or {})
        data = None
        if method == "POST":
            data = json.dumps(obj if obj is not None else {}).encode()
            h["Content-Type"] = "application/json"
            h["X-CSRF-Token"] = self.token if csrf is None else csrf
        req = urllib.request.Request(self.base + path, data=data, method=method, headers=h)
        try:
            with self.opener.open(req, timeout=10) as r:
                return r.status, dict(r.headers), r.read()
        except urllib.error.HTTPError as err:
            return err.code, dict(err.headers), err.read()

    def api(self, method: str, path: str, obj=None, **kw):
        """(status, the JSON answer)."""
        status, _, body = self.go(method, path, obj, **kw)
        return status, json.loads(body) if body else None

    def login(self, name: str, password: str) -> int:
        status, answer = self.api("POST", "/api/web/login", {"name": name, "password": password}, csrf="")
        if status == 200:
            self.token = answer["csrf"]
        return status


class SignedConsole:
    """What 3ds_online.c does over plain HTTP: linking by X25519, signed requests, signed answers checked."""

    def __init__(self, base: str, token: str = ""):
        self.base, self.token = base, token

    def raw(self, method: str, path: str, body: bytes = b"", headers=None):
        req = urllib.request.Request(self.base + path, data=body if method == "POST" else None, method=method,
                                     headers=headers or {})
        try:
            with urllib.request.build_opener(NoRedirect).open(req, timeout=10) as r:
                return r.status, dict(r.headers), r.read()
        except urllib.error.HTTPError as err:
            return err.code, dict(err.headers), err.read()

    def pair(self, code: str, name: str = "New 3DS"):
        import hashlib
        code = db.normalise_code(code)
        code_hash = hashlib.sha256(code.encode()).hexdigest()
        secret, pub = signing.x25519_keypair()
        body = json.dumps({"pub": pub.hex(), "proof": signing.pair_proof(code_hash, pub.hex()),
                           "console_name": name}).encode()
        status, _, out = self.raw("POST", "/api/pair2", body, {"Content-Type": "application/json"})
        if status != 200:
            return status, None
        ans = json.loads(out)
        token = signing.pair_token(signing.x25519(secret, bytes.fromhex(ans["pub"])), pub.hex(), ans["pub"], code_hash)
        assert ans["confirm"] == signing.pair_confirm(token, pub.hex(), ans["pub"], ans["user"]), "not confirmed"
        self.token = token
        return status, ans["user"]

    def headers(self, method: str, path: str, body: bytes, when=None, nonce=None):
        import hashlib
        import secrets as rnd
        import time as clock
        key = hashlib.sha256(self.token.encode()).digest()
        when = str(int(clock.time()) if when is None else when)
        nonce = nonce or rnd.token_hex(8)
        sign = signing.request_mac(key, method, path, when, nonce, body)
        return key, sign, {signing.KEY_HEADER: signing.key_id(key), signing.TIME_HEADER: when,
                           signing.NONCE_HEADER: nonce, signing.SIGN_HEADER: sign}

    def call(self, method: str, path: str, body: bytes = b"", extra=None, **kw):
        key, sign, h = self.headers(method, path, body, **kw)
        h.update(extra or {})
        status, headers, out = self.raw(method, path, body, h)
        got = {k.lower(): v for k, v in headers.items()}.get(signing.SIGN_HEADER.lower())  # Cloudflare lowercases
        good = got == signing.reply_mac(key, sign, status, out)
        return status, out, good, h


class ServerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="spriteserver-")
        cls.data = os.path.join(cls.tmp, "data")
        cls.httpd = server.make_server(cls.data, "127.0.0.1", 0)
        cls.base = "http://127.0.0.1:%d" % cls.httpd.server_address[1]
        cls.thread = threading.Thread(target=cls.httpd.serve_forever, daemon=True)
        cls.thread.start()
        conn = db.connect(os.path.join(cls.data, "sprites.db"))
        db.add_user(conn, "boss", "boss-password-1", "admin")
        conn.close()
        cls.admin = Browser(cls.base)
        assert cls.admin.login("boss", "boss-password-1") == 200

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()
        cls.httpd.server_close()
        shutil.rmtree(cls.tmp)
        server.LIMITS.hits.clear()

    def setUp(self):
        server.LIMITS.hits.clear()  # every test may link and sign in a few times

    # helpers
    def add_artist(self, name: str) -> str:
        status, answer = self.admin.api("POST", "/api/web/users", {"name": name, "role": "artist"})
        self.assertEqual(status, 201, answer)
        return answer["password"]

    def pairing_code(self, browser: Browser) -> str:
        status, answer = browser.api("POST", "/api/web/account/pair")
        self.assertEqual(status, 200)
        self.assertRegex(answer["code"], r"^[A-Z0-9]{4}-[A-Z0-9]{4}$")
        return answer["code"]

    def linked_client(self, name: str):
        password = self.add_artist(name)
        b = Browser(self.base)
        self.assertEqual(b.login(name, password), 200)
        cfg = {}
        msg = client.cmd_pair(cfg, self.base, self.pairing_code(b), "%s's 3DS" % name)
        self.assertIn("linked", msg)
        return b, cfg

    def sd(self, files: dict) -> str:
        root = tempfile.mkdtemp(dir=self.tmp)
        folder = os.path.join(root, client.STEREO)
        os.makedirs(folder)
        for key, data in files.items():
            with open(os.path.join(folder, key + ".bin"), "wb") as f:
                f.write(data)
        return root

    # tests
    def test_edit_file_rules(self):
        e = editfile.parse(make_edit("006", author="ARNOUD", revision=7, final=True), "006")
        self.assertEqual((e.planes, e.frames, e.author, e.revision, e.final), (4, 2, "ARNOUD", 7, True))
        self.assertEqual(editfile.parse(make_edit("b006", frames=1, meta=False)).key, "b006")
        for bad, why in ((b"nope" * 10, "magic"), (make_edit("006")[:-1], "size"), (make_edit("006") + b"x", "size")):
            with self.assertRaisesRegex(editfile.EditError, why):
                editfile.parse(bad)
        with self.assertRaisesRegex(editfile.EditError, "not 007"):
            editfile.parse(make_edit("006"), "007")
        self.assertIsNone(editfile.key_to_id("6"))
        self.assertIsNone(editfile.key_to_id("0006"))
        self.assertIsNone(editfile.key_to_id("000"))
        self.assertEqual(editfile.key_to_id("t012"), 0x4000 | 12)
        png = editfile.png(editfile.render(e, None, "tint"))
        self.assertTrue(png.startswith(b"\x89PNG"))

    def test_previews_keep_the_pictures_clear_pixels_clear(self):
        # Planes that cover the whole 64x64 (as many backs do) must not paint the picture's clear
        # pixels: only where the game picture has a colour (index 1-15) is anything drawn.
        e = editfile.parse(make_edit("b006", planes=2, frames=1, meta=False))
        full = bytearray(e.data)
        full[16:16 + 2048] = bytes([0x11] * 2048)  # every pixel on plane 1
        e = editfile.parse(bytes(full))
        rows = ["0" * 64] * 32 + ["1" * 64] * 32  # top half clear, bottom half colour 1
        sprite = editfile.Sprite(["#000000", "#ff8800"] + ["#000000"] * 14, ["".join(rows)])
        for mode in ("tint", "planes", "layer"):
            px = editfile.render(e, sprite, mode, 0, 1)
            self.assertEqual(px[3], 0, mode)                       # (0, 0): clear in the picture
            self.assertEqual(px[(40 * 64) * 4 + 3], 255, mode)     # (0, 40): the picture's colour
        self.assertEqual(editfile.render(e, None, "planes")[3], 255)  # no picture known: planes shown

    def test_pages_need_sign_in(self):
        anon = Browser(self.base)
        self.assertEqual(anon.api("GET", "/api/web/session")[1]["user"], None)
        for path in ("/api/web/grid", "/api/web/mons/025", "/api/web/account", "/api/web/events", "/api/sync",
                     "/img/sprite/025.png"):
            self.assertEqual(anon.go("GET", path)[0], 401, path)
        self.assertEqual(anon.go("GET", "/api/health")[0], 200)
        self.assertEqual(anon.login("boss", "wrong"), 401)
        # A form post (not JSON) cannot sign anybody in.
        req = urllib.request.Request(self.base + "/api/web/login", data=b"name=boss&password=boss-password-1",
                                     headers={"Content-Type": "application/x-www-form-urlencoded"})
        with self.assertRaises(urllib.error.HTTPError) as caught:
            urllib.request.urlopen(req, timeout=10)
        self.assertEqual(caught.exception.code, 415)

    def test_forms_need_the_csrf_token(self):
        status, _ = self.admin.api("POST", "/api/web/users", {"name": "sneaky", "role": "admin"}, csrf="")
        self.assertEqual(status, 403)
        status, _ = self.admin.api("POST", "/api/web/users", {"name": "sneaky", "role": "admin"}, csrf="wrong")
        self.assertEqual(status, 403)

    def test_artists_cannot_administer(self):
        b, _ = self.linked_client("plain")
        self.assertEqual(b.go("GET", "/api/web/users")[0], 403)
        self.assertEqual(b.api("POST", "/api/web/users", {"name": "x", "role": "admin"})[0], 403)

    def test_picture_data_for_the_3d_viewer(self):
        """The game picture as the web app's viewer reads it: sprites.json's own format."""
        frame = "0" * 2048 + "1f" * 1024
        server.CFG.sprites["007"] = editfile.Sprite(["#000000", "#ff8800"] + ["#102030"] * 14, [frame, frame])
        try:
            b, _ = self.linked_client("lena")
            status, data = b.api("GET", "/api/web/pictures/007")
            self.assertEqual(status, 200)
            self.assertEqual(data["frames"], [frame, frame])  # one hex digit per pixel
            self.assertEqual(data["palette"][:2], ["#000000", "#ff8800"])
            self.assertEqual(b.go("GET", "/api/web/pictures/008")[0], 404)
            self.assertEqual(b.api("GET", "/api/web/mons/007")[1]["picture"], True)
            self.assertIn("007", b.api("GET", "/api/web/grid")[1]["pictures"])
        finally:
            del server.CFG.sprites["007"]

    def test_trainers_are_listed_before_anyone_uploads(self):
        """The trainers' fronts and players' backs from sprites.json are on the grid's third tab."""
        frame = "1" * 4096
        for key, name in (("t000", "Hiker"), ("t001", "Aqua Grunt M"), ("p000", "Brendan (back)")):
            server.CFG.sprites[key] = editfile.Sprite(["#000000", "#ff8800"] + ["#102030"] * 14, [frame])
            server.CFG.names[key] = name
        try:
            b, _ = self.linked_client("trainerfan")
            grid = b.api("GET", "/api/web/grid")[1]
            self.assertEqual(grid["others"][:3], ["t000", "t001", "p000"])
            self.assertEqual(grid["names"]["t001"], "Aqua Grunt M")
            self.assertIn("p000", grid["pictures"])
            page = b.api("GET", "/api/web/mons/t001")[1]
            self.assertEqual((page["prev"]["key"], page["next"]["key"], page["other"]), ("t000", "p000", None))
            self.assertEqual(page["kind"], "trainer")
        finally:
            for key in ("t000", "t001", "p000"):
                del server.CFG.sprites[key], server.CFG.names[key]

    def test_the_web_app_is_served(self):
        web = tempfile.mkdtemp(dir=self.tmp)
        os.makedirs(os.path.join(web, "assets"))
        with open(os.path.join(web, "index.html"), "w") as f:
            f.write("<!doctype html><title>app</title>")
        with open(os.path.join(web, "assets", "index-abc123.js"), "w") as f:
            f.write("console.log(1)")
        old, server.WEB_DIR = server.WEB_DIR, web
        try:
            anon = Browser(self.base)
            for path in ("/", "/login", "/mon/025", "/compare/b025", "/account", "/users"):
                status, headers, body = anon.go("GET", path)
                self.assertEqual((status, body[:15]), (200, b"<!doctype html>"), path)
                self.assertIn("style-src 'self'", headers["Content-Security-Policy"])
                self.assertEqual(headers["Cache-Control"], "no-store")
            self.assertEqual(anon.go("GET", "/nothing/here")[0], 404)
            status, headers, body = anon.go("GET", "/assets/index-abc123.js")
            self.assertEqual((status, headers["Content-Type"], body), (200, "text/javascript; charset=utf-8",
                                                                       b"console.log(1)"))
            self.assertIn("immutable", headers["Cache-Control"])
            self.assertEqual(anon.go("GET", "/assets/../server.py")[0], 404)
            server.WEB_DIR = os.path.join(web, "missing")
            self.assertEqual(anon.go("GET", "/")[0], 503)
        finally:
            server.WEB_DIR = old

    def test_full_flow(self):
        alice, acfg = self.linked_client("alice")
        bob, bcfg = self.linked_client("bob")

        # Alice uploads three pictures; one bad file is skipped locally.
        card = self.sd({"025": make_edit("025", seed=1), "b025": make_edit("b025", frames=1, seed=2),
                        "150": make_edit("150", seed=3), "151": b"garbage"})
        lines = client.cmd_upload(acfg, [card])
        self.assertEqual(sum("uploaded" in l for l in lines), 3, lines)
        self.assertTrue(any("skipped" in l for l in lines))
        self.assertTrue(all("already" in l for l in client.cmd_upload(acfg, [card]) if "skipped" not in l))

        # The grid shows them done, the picture's page and its previews render.
        status, grid = alice.api("GET", "/api/web/grid")
        self.assertEqual(status, 200)
        self.assertIn("025", grid["items"])
        self.assertNotIn("026", grid["items"])
        self.assertEqual(sorted(grid["mine"]), ["025", "150", "b025"])
        self.assertEqual(grid["names"]["025"], "Front 025")  # no sprites.json in the test data
        status, page = bob.api("GET", "/api/web/mons/025")
        self.assertEqual(status, 200)
        vid = page["current"]
        self.assertEqual((page["next"]["key"], page["other"]["key"], page["prev"]["key"]), ("026", "b025", "024"))
        v = page["versions"][0]
        self.assertEqual((v["version_id"], v["author"], v["console"], v["planes"], v["frames"], v["mine"]),
                         (vid, "alice", "alice's 3DS", 4, 2, False))
        for path in ("/img/v%d/tint.png" % vid, "/img/v%d/planes.png" % vid, "/img/v%d/layer0.png" % vid):
            status, headers, png = bob.go("GET", path)
            self.assertEqual((status, headers["Content-Type"], png[:4]), (200, "image/png", b"\x89PNG"))
        self.assertEqual(bob.go("GET", "/dl/v%d/025.bin" % vid)[2], make_edit("025", seed=1))

        # Bob syncs an empty card: he gets all three, then nothing.
        bob_card = self.sd({})
        lines = client.cmd_sync(bcfg, bob_card)
        for key in ("025", "b025", "150"):
            self.assertTrue(any(l.startswith(key + " ") for l in lines), lines)
        self.assertEqual(client.cmd_sync(bcfg, bob_card), ["up to date"])

        # Bob uploads his own Pikachu: newest wins and Alice's sync replaces hers, keeping a backup.
        bob_pika = make_edit("025", seed=9, author="BOB")
        lines = client.cmd_upload(bcfg, [self.sd({"025": bob_pika})])
        self.assertIn("now current", lines[0])
        lines = client.cmd_sync(acfg, card)
        self.assertTrue(any("025" in l and "backup" in l for l in lines), lines)
        with open(os.path.join(card, client.STEREO, "025.bin"), "rb") as f:
            self.assertEqual(f.read(), bob_pika)
        self.assertEqual(len(os.listdir(os.path.join(card, client.STEREO, "backup"))), 1)

        # The admin pins Alice's version; a later upload does not displace it.
        self.assertEqual(bob.api("POST", "/api/web/mons/025/pin", {"version_id": vid})[0], 403)
        status, page = self.admin.api("POST", "/api/web/mons/025/pin", {"version_id": vid})
        self.assertEqual((status, page["current"], page["pinned"]), (200, vid, True))
        lines = client.cmd_upload(bcfg, [self.sd({"025": make_edit("025", seed=5)})])
        self.assertNotIn("now current", lines[0])
        status, headers, data = bob.go("GET", "/api/mons/025/current")
        self.assertEqual((status, headers["X-Version-Id"]), (200, str(vid)))
        status, page = self.admin.api("POST", "/api/web/mons/025/unpin")
        self.assertEqual((status, page["pinned"]), (200, False))
        status, headers, _ = bob.go("GET", "/api/mons/025/current")
        self.assertNotEqual(headers["X-Version-Id"], str(vid))

        # Deleting the current version falls back to the newest remaining one.
        current = int(headers["X-Version-Id"])
        self.assertEqual(alice.api("POST", "/api/web/versions/%d/delete" % current)[0], 403)  # Bob's
        status, page = self.admin.api("POST", "/api/web/versions/%d/delete" % current)
        self.assertEqual(status, 200)
        self.assertNotIn(current, [v["version_id"] for v in page["versions"]])
        status, headers, _ = bob.go("GET", "/api/mons/025/current")
        self.assertEqual(status, 200)
        self.assertNotEqual(headers["X-Version-Id"], str(current))

        # The JSON overview and the conditional download.
        status, _, body = bob.go("GET", "/api/mons")
        self.assertTrue({"025", "b025", "150"} <= {i["key"] for i in json.loads(body)["items"]})

    def test_console_api(self):
        _, cfg = self.linked_client("carol")
        token, base = cfg["token"], self.base
        status, _, body = client.request("GET", base + "/api/me", token)
        self.assertEqual((status, json.loads(body)["user"]), (200, "carol"))
        # A file uploaded under the wrong name, or not an edit file at all.
        status, _, body = client.request("POST", base + "/api/mons/007/versions", token, make_edit("006"))
        self.assertEqual(status, 400)
        self.assertIn("not 007", json.loads(body)["error"])
        status, _, _ = client.request("POST", base + "/api/mons/006/versions", token, b"x" * 70000)
        self.assertEqual(status, 413)
        status, headers, _ = client.request("POST", base + "/api/mons/006/versions", token, make_edit("006", seed=4))
        self.assertEqual(status, 201)
        # The ETag saves a download that would change nothing.
        status, headers, _ = client.request("GET", base + "/api/mons/006/current", token)
        req = urllib.request.Request(base + "/api/mons/006/current",
                                     headers={"Authorization": "Bearer " + token, "If-None-Match": headers["ETag"]})
        with self.assertRaises(urllib.error.HTTPError) as caught:
            urllib.request.urlopen(req, timeout=10)
        self.assertEqual(caught.exception.code, 304)
        # A used pairing code does not work twice; a wrong one fails.
        status, _, _ = client.request("POST", base + "/api/pair", body=json.dumps({"code": "AAAA-BBBB"}).encode(),
                                      ctype="application/json")
        self.assertEqual(status, 403)

    def test_depth_sweet_spots(self):
        """A file's depth byte is kept in the database and still travels in the console API (consoles
        in the field read it); the web app no longer shows it."""
        self.assertEqual(editfile.parse(make_edit("009")).depth, 0)
        self.assertEqual(editfile.parse(make_edit("009", depth=3)).depth, 3)
        self.assertEqual(editfile.parse(make_edit("009", depth=5)).depth, 5)  # kept as sent (MON PLANES px)
        self.assertEqual(editfile.sweet_spot(5), 2)  # the console API's field stays 1..3
        self.assertEqual(editfile.sweet_name(0), "2 Standard")
        browser, cfg = self.linked_client("hana")
        token, base = cfg["token"], self.base
        for seed, depth in ((1, 1), (2, 3), (3, 0)):
            status, _, _ = client.request("POST", base + "/api/mons/009/versions", token,
                                          make_edit("009", seed=seed, depth=depth))
            self.assertEqual(status, 201)
        # The same planes at another sweet spot are a version of their own.
        status, _, _ = client.request("POST", base + "/api/mons/009/versions", token,
                                      make_edit("009", seed=1, depth=2))
        self.assertEqual(status, 201)
        status, _, body = client.request("GET", base + "/api/mons/009/versions", token)
        depths = sorted(v["depth"] for v in json.loads(body)["versions"])
        self.assertEqual(depths, [2])  # one person: only their newest is listed
        # The newest is current: the Standard one, which sync says too.
        status, _, body = client.request("GET", base + "/api/sync?since=0", token)
        self.assertEqual([i["depth"] for i in json.loads(body)["items"] if i["key"] == "009"], [2])
        # The web app shows no depth: every picture is drawn at the one standard.
        page = browser.api("GET", "/api/web/mons/009")[1]
        self.assertTrue(all("depth" not in v for v in page["versions"]))
        self.assertNotIn("sweet", browser.api("GET", "/api/web/grid")[1])
        conn = db.connect(os.path.join(self.data, "sprites.db"))  # the database still keeps each file's byte
        self.assertEqual(sorted(r[0] for r in conn.execute("SELECT depth FROM versions WHERE mon_key = '009'")),
                         [0, 1, 2, 3])
        conn.close()

    def test_one_version_per_person(self):
        """A person's new upload replaces their earlier one: votes and a pin move over, the earlier one
        stays as hidden history, and sending it again brings it back."""
        _, ann = self.linked_client("ann")
        ben_web, ben = self.linked_client("ben")
        up = lambda cfg, seed: json.loads(client.request(  # noqa: E731
            "POST", self.base + "/api/mons/040/versions", cfg["token"], make_edit("040", seed=seed))[2])
        listing = lambda: json.loads(client.request(  # noqa: E731
            "GET", self.base + "/api/mons/040/versions", ben["token"])[2])
        a1 = up(ann, 1)["version_id"]
        b1 = up(ben, 2)["version_id"]
        self.assertEqual(ben_web.api("POST", "/api/web/mons/040/vote", {"version_id": a1, "on": True})[0], 200)
        status, _ = self.admin.api("POST", "/api/web/mons/040/pin", {"version_id": a1})
        a2 = up(ann, 3)
        self.assertFalse(a2["duplicate"])
        got = listing()
        self.assertEqual(sorted(v["version_id"] for v in got["versions"]), sorted([a2["version_id"], b1]))
        self.assertEqual(got["favourites"], [a2["version_id"]])          # Ben's vote moved along
        self.assertEqual((got["current"], got["pinned"]), (a2["version_id"], True))  # so did the pin
        # The old one is history: not votable (a queued vote for it lands on the new one), still downloadable.
        status, _, body = client.request("POST", self.base + "/api/mons/040/votes", ben["token"],
                                         json.dumps({"version_ids": [a1, b1]}).encode(), "application/json")
        self.assertEqual(sorted(json.loads(body)["favourites"]), sorted([a2["version_id"], b1]))
        self.assertEqual(client.request("GET", self.base + "/api/versions/%d" % a1, ben["token"])[0], 200)
        self.assertEqual(self.admin.api("POST", "/api/web/mons/040/pin", {"version_id": a1})[0], 404)
        # The same file again is a duplicate; Ann's first file again becomes her version once more.
        self.assertTrue(up(ann, 3)["duplicate"])
        back = up(ann, 1)
        self.assertEqual((back["version_id"], back["duplicate"]), (a1, False))
        self.assertEqual(sorted(v["version_id"] for v in listing()["versions"]), sorted([a1, b1]))
        grid = ben_web.api("GET", "/api/web/grid")[1]
        self.assertEqual(grid["items"]["040"]["versions"], 2)

    def test_votes(self):
        _, frank = self.linked_client("frank")
        gina_web, gina = self.linked_client("gina")
        api = lambda method, path, cfg, obj=None: client.request(
            method, self.base + path, cfg["token"], json.dumps(obj).encode() if obj is not None else None,
            "application/json" if obj is not None else None)
        # Three people, one version each (one version per person per picture).
        _, fay = self.linked_client("fay")
        _, finn = self.linked_client("finn")
        ids = []
        for seed, who in ((1, frank), (2, fay), (3, finn)):
            status, _, body = client.request("POST", self.base + "/api/mons/030/versions", who["token"],
                                             make_edit("030", seed=seed))
            self.assertEqual(status, 201)
            ids.append(json.loads(body)["version_id"])
        # No votes: the newest is current.
        status, _, body = api("GET", "/api/mons/030/versions", gina)
        listing = json.loads(body)
        self.assertEqual((status, listing["current"], listing["favourites"]), (200, ids[2], []))
        self.assertEqual([v["version_id"] for v in listing["versions"]], ids[::-1])
        rev = json.loads(api("GET", "/api/rev", gina)[2])["rev"]
        # Two favourites from the console; the voted one wins over the newest.
        status, _, body = api("POST", "/api/mons/030/votes", gina, {"version_ids": [ids[0], ids[1]]})
        self.assertGreater(json.loads(api("GET", "/api/rev", gina)[2])["rev"], rev)
        mons = {m["key"]: m for m in json.loads(api("GET", "/api/mons", gina)[2])["items"]}
        self.assertEqual((mons["030"]["versions"], mons["030"]["mine"]), (3, 2))
        self.assertEqual(status, 200, body)
        listing = json.loads(body)
        self.assertEqual(listing["favourites"], [ids[0], ids[1]])
        self.assertIn(listing["current"], (ids[0], ids[1]))
        status, _, body = api("POST", "/api/mons/030/votes", frank, {"version_ids": [ids[0]]})
        self.assertEqual(json.loads(body)["current"], ids[0])
        votes = {v["version_id"]: v["votes"] for v in json.loads(body)["versions"]}
        self.assertEqual(votes, {ids[0]: 2, ids[1]: 1, ids[2]: 0})
        # The most votes first, then the newest.
        self.assertEqual([v["version_id"] for v in json.loads(body)["versions"]], ids)
        # A sync reports the change, so other consoles fetch the winner.
        status, _, body = api("GET", "/api/sync?since=0", gina)
        self.assertIn(ids[0], [i["version_id"] for i in json.loads(body)["items"]])
        # Any number of a picture's versions (up to 16); another picture's version is refused.
        self.assertEqual(api("POST", "/api/mons/030/votes", gina, {"version_ids": ids})[0], 200)
        self.assertEqual(api("POST", "/api/mons/030/votes", gina, {"version_ids": list(range(1, 18))})[0], 400)
        self.assertEqual(api("POST", "/api/mons/006/votes", gina, {"version_ids": [ids[0]]})[0], 400)
        # In the web app: Vote takes a vote back, and gives it again.
        status, page = gina_web.api("POST", "/api/web/mons/030/vote", {"version_id": ids[1], "on": False})
        self.assertEqual((status, page["favourites"]), (200, [ids[0], ids[2]]))
        self.assertEqual(json.loads(api("GET", "/api/mons/030/versions", gina)[2])["favourites"], [ids[0], ids[2]])
        status, page = gina_web.api("POST", "/api/web/mons/030/vote", {"version_id": ids[1], "on": True})
        self.assertEqual(json.loads(api("GET", "/api/mons/030/versions", gina)[2])["favourites"],
                         [ids[0], ids[2], ids[1]])
        votes = {v["version_id"]: (v["votes"], v["voters"]) for v in page["versions"]}
        self.assertEqual(votes[ids[1]], (1, ["gina"]))
        self.assertEqual(votes[ids[0]][0], 2)
        self.assertEqual(sorted(votes[ids[0]][1]), ["frank", "gina"])
        self.assertEqual(gina_web.api("POST", "/api/web/mons/030/vote", {"version_id": 999999, "on": True})[0], 404)
        self.assertEqual(gina_web.api("POST", "/api/web/mons/030/vote", {"version_id": ids[1]})[0], 400)
        # Each knows its own versions, and only its uploader takes one off the server.
        listing = json.loads(api("GET", "/api/mons/030/versions", frank)[2])
        self.assertEqual([v["version_id"] for v in listing["versions"] if v["mine"]], [ids[0]])
        self.assertFalse(any(v["mine"] for v in json.loads(api("GET", "/api/mons/030/versions", gina)[2])["versions"]))
        self.assertEqual(api("POST", "/api/versions/%d/delete" % ids[2], gina)[0], 403)
        status, _, body = api("POST", "/api/versions/%d/delete" % ids[2], finn)
        self.assertEqual(status, 200)
        self.assertEqual([v["version_id"] for v in json.loads(body)["versions"]], [ids[0], ids[1]])

    def test_signed_console(self):
        password = self.add_artist("signer")
        b = Browser(self.base)
        self.assertEqual(b.login("signer", password), 200)
        console = SignedConsole(self.base)
        self.assertEqual(console.pair("WRONGCOD")[0], 403)
        code = self.pairing_code(b)
        self.assertEqual(console.pair(code), (200, "signer"))
        self.assertEqual(SignedConsole(self.base).pair(code)[0], 403)  # used once
        status, _, out = console.raw("GET", "/api/health")
        self.assertEqual(json.loads(out)["api"], server.API_VERSION)
        self.assertGreater(json.loads(out)["time"], 1700000000)
        status, out, good, _ = console.call("GET", "/api/me")
        self.assertEqual((status, json.loads(out)["user"], good), (200, "signer", True))
        # An upload: the body is signed too.
        edit = make_edit("025", author="SIGNER")
        status, out, good, _ = console.call("POST", "/api/mons/025/versions", edit,
                                            {"Content-Type": "application/octet-stream"})
        self.assertEqual((status, good), (201, True), out)
        status, out, good, _ = console.call("GET", "/api/sync?since=0")
        self.assertTrue(good)
        self.assertIn(b"025", out)
        # The same request again, a changed body, a bad clock, an unknown key: refused.
        key, sign, h = console.headers("GET", "/api/me", b"")
        self.assertEqual(console.raw("GET", "/api/me", b"", h)[0], 200)
        self.assertEqual(console.raw("GET", "/api/me", b"", h)[0], 401)
        _, _, h = console.headers("POST", "/api/mons/025/versions", edit)
        h["Content-Type"] = "application/octet-stream"
        self.assertEqual(console.raw("POST", "/api/mons/025/versions", edit[:-1] + b"x", h)[0], 401)
        self.assertEqual(console.call("GET", "/api/me", when=1000)[0], 401)
        self.assertEqual(SignedConsole(self.base, "f" * 64).call("GET", "/api/me")[0], 401)
        # The console's author name is what the site shows for the account.
        status, out, good, _ = console.call("POST", "/api/me", json.dumps({"name": "Dark Matter"}).encode(),
                                            {"Content-Type": "application/json"})
        self.assertEqual((status, json.loads(out)["name"], good), (200, "Dark Matter", True))
        self.assertEqual(console.call("POST", "/api/me", json.dumps({"name": "<b>"}).encode(),
                                      {"Content-Type": "application/json"})[0], 400)
        listing = json.loads(console.call("GET", "/api/mons/025/versions")[1])
        self.assertEqual(listing["versions"][0]["author"], "Dark Matter")
        self.assertEqual(b.api("GET", "/api/web/account")[1]["display_name"], "Dark Matter")
        # A console linked the old way (bearer token) signs with the same token.
        _, cfg = self.linked_client("oldstyle")
        old = SignedConsole(self.base, cfg["token"])
        status, out, good, _ = old.call("GET", "/api/me")
        self.assertEqual((status, json.loads(out)["user"], good), (200, "oldstyle", True))
        # Through Cloudflare over plain HTTP: no bearer tokens, and browsers go to HTTPS.
        edge = {"Cf-Visitor": '{"scheme":"http"}', "Authorization": "Bearer " + cfg["token"]}
        self.assertEqual(old.raw("GET", "/api/me", b"", edge)[0], 401)
        self.assertEqual(old.call("GET", "/api/me", extra={"Cf-Visitor": '{"scheme":"http"}'})[0], 200)
        status, headers, _ = old.raw("GET", "/account", b"", {"Cf-Visitor": '{"scheme":"http"}'})
        self.assertEqual(status, 301)
        self.assertTrue(headers["Location"].startswith("https://"))

    def test_unlinking_a_console(self):
        b, cfg = self.linked_client("dave")
        status, account = b.api("GET", "/api/web/account")
        self.assertEqual([d["console_name"] for d in account["devices"]], ["dave's 3DS"])
        did = account["devices"][0]["id"]
        status, account = b.api("POST", "/api/web/account/devices/%d/revoke" % did)
        self.assertEqual((status, account["devices"]), (200, []))
        self.assertEqual(client.request("GET", self.base + "/api/me", cfg["token"])[0], 401)

    def test_disabled_user(self):
        b, cfg = self.linked_client("eve")
        status, users = self.admin.api("GET", "/api/web/users")
        uid = {u["name"]: u["id"] for u in users["users"]}["eve"]
        status, users = self.admin.api("POST", "/api/web/users/%d/disable" % uid)
        self.assertEqual(status, 200)
        self.assertTrue({u["name"]: u for u in users["users"]}["eve"]["disabled"])
        self.assertEqual(client.request("GET", self.base + "/api/me", cfg["token"])[0], 401)
        self.assertEqual(b.go("GET", "/api/web/grid")[0], 401)
        me = {u["name"]: u["id"] for u in users["users"]}["boss"]
        self.assertEqual(self.admin.api("POST", "/api/web/users/%d/disable" % me)[0], 400)

    def test_account(self):
        b, _ = self.linked_client("hiro")
        self.assertEqual(b.api("POST", "/api/web/account/name", {"name": "<script>"})[0], 400)
        status, account = b.api("POST", "/api/web/account/name", {"name": "Hana  B"})
        self.assertEqual((status, account["display_name"]), (200, "Hana B"))
        self.assertEqual(b.api("POST", "/api/web/account/password", {"old": "wrong", "new": "x" * 12})[0], 400)
        self.assertEqual(b.api("POST", "/api/web/account/password", {"old": "", "new": "short"})[0], 400)
        password = self.admin.api("POST", "/api/web/users/%d/reset" % {
            u["name"]: u["id"] for u in self.admin.api("GET", "/api/web/users")[1]["users"]}["hiro"])[1]["password"]
        self.assertEqual(b.go("GET", "/api/web/account")[0], 401)  # a new password signs everybody out
        self.assertEqual(b.login("hiro", password), 200)
        old_csrf = b.token
        status, session = b.api("POST", "/api/web/account/password", {"old": password, "new": "a-new-password"})
        self.assertEqual(status, 200)
        self.assertNotEqual(session["csrf"], old_csrf)  # a fresh session (and cookie) comes back
        b.token = session["csrf"]
        self.assertEqual(b.api("GET", "/api/web/account")[0], 200)
        self.assertEqual(b.api("POST", "/api/web/logout")[0], 200)
        self.assertEqual(b.go("GET", "/api/web/account")[0], 401)

    def test_live_events(self):
        """The live channel: an upload, a vote and a console linking reach an open page at once."""
        import http.client
        import socket
        b, cfg = self.linked_client("ivan")
        other, _ = self.linked_client("jo")
        cookies = lambda br: "; ".join("%s=%s" % (c.name, c.value) for c in br.jar)  # noqa: E731

        def open_stream(br):
            conn = http.client.HTTPConnection("127.0.0.1", self.httpd.server_address[1], timeout=10)
            conn.request("GET", "/api/web/events", headers={"Cookie": cookies(br)})
            resp = conn.getresponse()
            self.assertEqual((resp.status, resp.getheader("Content-Type")), (200, "text/event-stream; charset=utf-8"))
            return conn, resp

        def next_event(resp):
            data = b""
            while True:
                line = resp.fp.readline()
                if line.startswith(b"data: "):
                    data = line[6:]
                elif line in (b"\n", b"\r\n") and data:
                    return json.loads(data)

        conn, resp = open_stream(b)
        conn2, resp2 = open_stream(other)
        try:
            self.assertEqual(next_event(resp)["type"], "hello")
            self.assertEqual(next_event(resp2)["type"], "hello")
            status, _, _ = client.request("POST", self.base + "/api/mons/045/versions", cfg["token"], make_edit("045"))
            self.assertEqual(status, 201)
            ev = next_event(resp)
            self.assertEqual((ev["type"], ev["key"], ev["who"], ev["current"]), ("upload", "045", "ivan", True))
            self.assertEqual(next_event(resp2)["type"], "upload")
            other.api("POST", "/api/web/mons/045/vote", {"version_id": ev["version_id"], "on": True})
            self.assertEqual((next_event(resp)["type"]), "vote")
            self.assertEqual((next_event(resp2)["type"]), "vote")
            # A console linking shows on its own account's page only.
            msg = client.cmd_pair({}, self.base, self.pairing_code(b), "Ivan's New 3DS")
            self.assertIn("linked", msg)
            ev = next_event(resp)
            self.assertEqual((ev["type"], ev["action"], ev["console_name"]), ("console", "linked", "Ivan's New 3DS"))
            other.api("POST", "/api/web/account/name", {"name": "Jo"})
            self.assertEqual(next_event(resp2)["type"], "name")  # Jo's stream skipped Ivan's console
        finally:
            for c in (conn, conn2):
                try:
                    c.sock.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                c.close()

    def test_activity_feed(self):
        """Every upload, vote, pin and delete leaves a line the web app's feed shows, newest first."""
        hal_web, hal = self.linked_client("hal")
        ida_web, ida = self.linked_client("ida")
        api = lambda method, path, cfg, obj=None: client.request(
            method, self.base + path, cfg["token"], json.dumps(obj).encode() if obj is not None else None,
            "application/json" if obj is not None else None)
        status, _, body = client.request("POST", self.base + "/api/mons/133/versions", hal["token"], make_edit("133", seed=1))
        self.assertEqual(status, 201)
        first = json.loads(body)["version_id"]
        status, _, body = client.request("POST", self.base + "/api/mons/133/versions", ida["token"],
                                         make_edit("133", seed=2, final=True))
        second = json.loads(body)["version_id"]
        # A vote from the web, one taken back from the console, a pin by the admin, a delete by the owner.
        self.assertEqual(hal_web.api("POST", "/api/web/mons/133/vote", {"version_id": second, "on": True})[0], 200)
        self.assertEqual(api("POST", "/api/mons/133/votes", hal, {"version_ids": []})[0], 200)
        self.assertEqual(self.admin.api("POST", "/api/web/mons/133/pin", {"version_id": first})[0], 200)
        self.assertEqual(ida_web.api("POST", "/api/web/versions/%d/delete" % second)[0], 200)
        self.assertEqual(ida_web.api("POST", "/api/web/account/name", {"name": "Ida B"})[0], 200)

        status, feed = hal_web.api("GET", "/api/web/feed?limit=6")
        self.assertEqual(status, 200, feed)
        kinds = [(i["kind"], i["who"]) for i in feed["items"]]
        self.assertEqual(kinds, [("name", "Ida B"), ("delete", "Ida B"), ("pin", "boss"), ("unvote", "hal"),
                                 ("vote", "hal"), ("upload", "Ida B")])
        self.assertTrue(feed["more"])
        name, delete, pin, unvote, vote, upload = feed["items"]
        self.assertEqual((name["new_name"], name["key"]), ("Ida B", None))
        # The deleted version is gone; the line still knows whose it was (the name at the time) and which picture.
        self.assertEqual((delete["exists"], delete["owner"], delete["own"], delete["key"], delete["name"]),
                         (False, "ida", 1, "133", "Front 133"))
        self.assertEqual((pin["version_id"], pin["current"], pin["exists"]), (first, True, True))
        self.assertEqual((vote["version_id"], vote["me"]), (second, True))
        self.assertEqual((upload["final"], upload["revision"], upload["exists"], upload["me"]), (True, 1, False, False))
        self.assertTrue(all(i["at"] > 0 for i in feed["items"]))
        # Older lines come by id; the kinds narrow it down.
        status, older = hal_web.api("GET", "/api/web/feed?before=%d" % feed["items"][-1]["id"])
        self.assertEqual(older["items"][0]["kind"], "upload")
        self.assertEqual(older["items"][0]["version_id"], first)
        status, votes = hal_web.api("GET", "/api/web/feed?kind=votes&limit=2")
        self.assertEqual([i["kind"] for i in votes["items"]], ["unvote", "vote"])
        self.assertEqual(hal_web.api("GET", "/api/web/feed?kind=nonsense")[0], 400)
        # The console API cannot read it; a signed-out browser neither.
        self.assertEqual(Browser(self.base).api("GET", "/api/web/feed")[0], 401)

    def test_beta_review(self):
        """One mark per picture for everybody, from the web or a console, versions or not; in the grid,
        the picture's page, the consoles' list and the feed."""
        kim_web, kim = self.linked_client("rev")
        api = lambda method, path, obj=None: client.request(
            method, self.base + path, kim["token"], json.dumps(obj).encode() if obj is not None else None,
            "application/json" if obj is not None else None)
        # A picture nobody uploaded can be judged too (the game's own planes).
        status, mon = kim_web.api("POST", "/api/web/mons/150/review", {"status": "fix"})
        self.assertEqual(status, 200, mon)
        self.assertEqual((mon["review"]["status"], mon["review"]["by"]), ("fix", "rev"))
        status, _, body = api("POST", "/api/mons/b150/review", {"status": "ok"})
        self.assertEqual((status, json.loads(body)), (200, {"key": "b150", "status": "ok"}))
        self.assertEqual(api("POST", "/api/mons/150/review", {"status": "meh"})[0], 400)
        self.assertEqual(kim_web.api("POST", "/api/web/mons/150/review", {})[0], 400)
        # Another person overrides it; the grid and the consoles' list see the last one.
        self.assertEqual(self.admin.api("POST", "/api/web/mons/150/review", {"status": "ok"})[0], 200)
        status, grid = kim_web.api("GET", "/api/web/grid")
        self.assertEqual((grid["reviews"]["150"]["status"], grid["reviews"]["150"]["by"]), ("ok", "boss"))
        self.assertEqual(grid["reviews"]["b150"]["status"], "ok")
        status, _, body = api("GET", "/api/mons")
        self.assertEqual(json.loads(body)["reviews"], {"150": "ok", "b150": "ok"})
        # Cleared: gone everywhere; clearing again changes nothing and logs nothing.
        self.assertEqual(api("POST", "/api/mons/b150/review", {"status": ""})[0], 200)
        self.assertEqual(api("POST", "/api/mons/b150/review", {"status": ""})[0], 200)
        status, mon = kim_web.api("GET", "/api/web/mons/b150")
        self.assertIsNone(mon["review"])
        status, feed = kim_web.api("GET", "/api/web/feed?kind=curation&limit=4")
        self.assertEqual([(i["kind"], i["key"], i["status"], i["was"]) for i in feed["items"][:4]],
                         [("review", "b150", "", "ok"), ("review", "150", "ok", "fix"),
                          ("review", "b150", "ok", ""), ("review", "150", "fix", "")])
        self.assertEqual(Browser(self.base).api("POST", "/api/web/mons/150/review", {"status": "ok"})[0], 401)

    def test_follow_the_web(self):
        """The web app says what it shows; the same account's console reads it (GET /api/focus)."""
        b, cfg = self.linked_client("kim")
        status, _, body = client.request("GET", self.base + "/api/focus", cfg["token"])
        self.assertEqual((status, json.loads(body)["key"], json.loads(body)["selected"]), (200, None, None))
        status, f = b.api("POST", "/api/web/focus", {"key": "025", "version_ids": [3, 4], "selected": 4})
        self.assertEqual(status, 200)
        self.assertEqual(f["follower"]["console_name"], "kim's 3DS")  # it just asked
        seq = f["seq"]
        status, _, body = client.request("GET", self.base + "/api/focus", cfg["token"])
        got = json.loads(body)
        self.assertEqual((got["key"], got["version_ids"], got["selected"], got["seq"]), ("025", [3, 4], 4, seq))
        # The same page again changes nothing; "Show on 3DS" (push) moves seq on all the same.
        self.assertEqual(b.api("POST", "/api/web/focus", {"key": "025", "version_ids": [3, 4], "selected": 4})[1]["seq"],
                         seq)
        pushed = b.api("POST", "/api/web/focus", {"key": "025", "version_ids": [3, 4], "selected": 4, "push": True})[1]
        self.assertGreater(pushed["seq"], seq)
        self.assertGreater(b.api("POST", "/api/web/focus", {"key": "025", "version_ids": [3, 4], "selected": 3})[1]["seq"],
                           pushed["seq"])
        self.assertGreater(b.api("POST", "/api/web/focus", {"key": "b025"})[1]["seq"], seq)
        self.assertEqual(b.api("GET", "/api/web/focus")[1]["key"], "b025")
        self.assertEqual(b.api("POST", "/api/web/focus", {"key": "nope"})[0], 400)
        self.assertEqual(b.api("POST", "/api/web/focus", {"key": "025", "version_ids": ["x"]})[0], 400)
        self.assertEqual(b.api("POST", "/api/web/focus", {"key": "025", "selected": "4"})[0], 400)
        self.assertEqual(client.request("GET", self.base + "/api/focus")[0], 401)
        other, _ = self.linked_client("lou")
        self.assertIsNone(other.api("GET", "/api/web/focus")[1]["follower"])  # Lou's console never asked


if __name__ == "__main__":
    unittest.main()
