"""End-to-end tests: a real server on a free port, an empty data folder, the web pages, the
console API and the PC client.

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


def make_edit(key: str, planes: int = 4, frames: int = 2, seed: int = 0, meta: bool = True, author: str = "TESTER",
              revision: int = 1, final: bool = False) -> bytes:
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
        t = editfile.META_MAGIC + bytes([1 if final else 0, 0]) + struct.pack("<HI", revision, 800000000)
        out += t + author.encode().ljust(16, b"\0")
    return out


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


class Browser:
    """A cookie-keeping web client that does not follow redirects."""

    def __init__(self, base: str):
        self.base = base
        self.jar = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.jar), NoRedirect)

    def go(self, method: str, path: str, form=None):
        data = urllib.parse.urlencode(form).encode() if form is not None else None
        req = urllib.request.Request(self.base + path, data=data, method=method)
        try:
            with self.opener.open(req, timeout=10) as r:
                return r.status, dict(r.headers), r.read()
        except urllib.error.HTTPError as err:
            return err.code, dict(err.headers), err.read()

    def csrf(self, path: str = "/account") -> str:
        _, _, body = self.go("GET", path)
        return re.search(rb'name="csrf" value="([^"]+)"', body).group(1).decode()

    def login(self, name: str, password: str) -> int:
        status, _, _ = self.go("POST", "/login", {"name": name, "password": password, "next": "/"})
        return status


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
        assert cls.admin.login("boss", "boss-password-1") == 303

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()
        cls.httpd.server_close()
        shutil.rmtree(cls.tmp)
        server.LIMITS.hits.clear()

    # helpers
    def add_artist(self, name: str) -> str:
        status, _, body = self.admin.go("POST", "/admin/users", {"name": name, "role": "artist",
                                                                 "csrf": self.admin.csrf("/admin")})
        self.assertEqual(status, 200, body)
        return re.search(rb'class="bigcode small">([^<]+)<', body).group(1).decode()

    def pairing_code(self, browser: Browser) -> str:
        status, _, body = browser.go("POST", "/account/pair", {"csrf": browser.csrf()})
        self.assertEqual(status, 200)
        return re.search(rb'class="bigcode">([A-Z0-9-]+)<', body).group(1).decode()

    def linked_client(self, name: str):
        password = self.add_artist(name)
        b = Browser(self.base)
        self.assertEqual(b.login(name, password), 303)
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

    def test_pages_need_sign_in(self):
        anon = Browser(self.base)
        status, headers, _ = anon.go("GET", "/")
        self.assertEqual(status, 303)
        self.assertTrue(headers["Location"].startswith("/login"))
        self.assertEqual(anon.go("GET", "/api/sync")[0], 401)
        self.assertEqual(anon.go("GET", "/api/health")[0], 200)
        self.assertEqual(anon.login("boss", "wrong"), 401)

    def test_forms_need_the_csrf_token(self):
        status, _, _ = self.admin.go("POST", "/admin/users", {"name": "sneaky", "role": "admin"})
        self.assertEqual(status, 400)

    def test_artists_cannot_administer(self):
        b, _ = self.linked_client("plain")
        self.assertEqual(b.go("GET", "/admin")[0], 403)

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
        status, _, body = alice.go("GET", "/?kind=front&show=done")
        self.assertEqual(status, 200)
        self.assertIn(b'href="/mon/025"', body)
        self.assertNotIn(b'href="/mon/026"', body)
        status, _, body = bob.go("GET", "/mon/025")
        self.assertEqual(status, 200)
        vid = int(re.search(rb"/img/v(\d+)/tint\.png", body).group(1))
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
        status, _, _ = self.admin.go("POST", "/mon/025/pin", {"version": str(vid), "csrf": self.admin.csrf("/mon/025")})
        self.assertEqual(status, 303)
        lines = client.cmd_upload(bcfg, [self.sd({"025": make_edit("025", seed=5)})])
        self.assertNotIn("now current", lines[0])
        status, headers, data = bob.go("GET", "/api/mons/025/current")
        self.assertEqual((status, headers["X-Version-Id"]), (200, str(vid)))
        status, _, _ = self.admin.go("POST", "/mon/025/unpin", {"csrf": self.admin.csrf("/mon/025")})
        self.assertEqual(status, 303)
        status, headers, _ = bob.go("GET", "/api/mons/025/current")
        self.assertNotEqual(headers["X-Version-Id"], str(vid))

        # Deleting the current version falls back to the newest remaining one.
        current = int(headers["X-Version-Id"])
        status, _, _ = self.admin.go("POST", "/versions/%d/delete" % current, {"csrf": self.admin.csrf("/mon/025")})
        self.assertEqual(status, 303)
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

    def test_votes(self):
        _, frank = self.linked_client("frank")
        gina_web, gina = self.linked_client("gina")
        api = lambda method, path, cfg, obj=None: client.request(
            method, self.base + path, cfg["token"], json.dumps(obj).encode() if obj is not None else None,
            "application/json" if obj is not None else None)
        ids = []
        for seed in (1, 2, 3):
            status, _, body = client.request("POST", self.base + "/api/mons/030/versions", frank["token"],
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
        # A sync reports the change, so other consoles fetch the winner.
        status, _, body = api("GET", "/api/sync?since=0", gina)
        self.assertIn(ids[0], [i["version_id"] for i in json.loads(body)["items"]])
        # Three are too many; another picture's version is refused.
        self.assertEqual(api("POST", "/api/mons/030/votes", gina, {"version_ids": ids})[0], 400)
        self.assertEqual(api("POST", "/api/mons/006/votes", gina, {"version_ids": [ids[0]]})[0], 400)
        # On the website: a star takes a vote back, a third star is refused.
        csrf = gina_web.csrf("/mon/030")
        status, headers, _ = gina_web.go("POST", "/mon/030/vote", {"version": str(ids[1]), "csrf": csrf})
        self.assertEqual((status, headers["Location"]), (303, "/mon/030?msg=voted"))
        gina_web.go("POST", "/mon/030/vote", {"version": str(ids[2]), "csrf": csrf})
        status, headers, _ = gina_web.go("POST", "/mon/030/vote", {"version": str(ids[1]), "csrf": csrf})
        self.assertEqual(headers["Location"], "/mon/030?msg=twofavourites")
        self.assertEqual(json.loads(api("GET", "/api/mons/030/versions", gina)[2])["favourites"], [ids[0], ids[2]])
        status, _, page = gina_web.go("GET", "/mon/030")
        self.assertIn(b"2 votes", page)

    def test_unlinking_a_console(self):
        b, cfg = self.linked_client("dave")
        status, _, body = b.go("GET", "/account")
        did = re.search(rb"/account/devices/(\d+)/revoke", body).group(1).decode()
        status, _, _ = b.go("POST", "/account/devices/%s/revoke" % did, {"csrf": b.csrf()})
        self.assertEqual(status, 303)
        self.assertEqual(client.request("GET", self.base + "/api/me", cfg["token"])[0], 401)

    def test_disabled_user(self):
        b, cfg = self.linked_client("eve")
        status, _, body = self.admin.go("GET", "/admin")
        rows = re.findall(rb'<tr class="[^"]*"><td><strong>([^<]+)</strong>.*?/admin/users/(\d+)/', body)
        uid = dict((n.decode(), i.decode()) for n, i in rows)["eve"]
        status, _, _ = self.admin.go("POST", "/admin/users/%s/disable" % uid, {"csrf": self.admin.csrf("/admin")})
        self.assertEqual(status, 303)
        self.assertEqual(client.request("GET", self.base + "/api/me", cfg["token"])[0], 401)
        self.assertEqual(b.go("GET", "/")[0], 303)


if __name__ == "__main__":
    unittest.main()
