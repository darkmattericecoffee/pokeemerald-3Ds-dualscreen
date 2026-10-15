#!/usr/bin/env python3
"""The plane-sharing server's PC client: what a console will do over Wi-Fi, from a computer.

    python3 tools/sprite_server/client.py pair https://example.trycloudflare.com ABCD-EFGH
    python3 tools/sprite_server/client.py upload /Volumes/NEW3DS          # every edit on the card
    python3 tools/sprite_server/client.py upload 006.bin b006.bin
    python3 tools/sprite_server/client.py sync /Volumes/NEW3DS            # fetch the current versions
    python3 tools/sprite_server/client.py status

The link (server address and token) is kept in ~/.emerald3ds-online.json, or --config.
`sync` never loses work: a local file that differs from the server's is first copied to
3ds/emerald3ds/stereo/backup/<key>-<time>.bin.

Standard library only.
"""
import argparse
import glob
import hashlib
import json
import os
import platform
import sys
import time
import urllib.error
import urllib.request
from typing import List, Optional, Tuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import editfile  # noqa: E402

DEFAULT_CONFIG = os.path.expanduser("~/.emerald3ds-online.json")
STEREO = os.path.join("3ds", "emerald3ds", "stereo")
USER_AGENT = "emerald3ds-client/1"


class ClientError(Exception):
    pass


def load_config(path: str) -> dict:
    try:
        with open(path) as f:
            return json.load(f)
    except FileNotFoundError:
        return {}


def save_config(path: str, cfg: dict) -> None:
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(cfg, f, indent=2)
    os.chmod(tmp, 0o600)
    os.replace(tmp, path)


def request(method: str, url: str, token: Optional[str] = None, body: Optional[bytes] = None,
            ctype: Optional[str] = None) -> Tuple[int, dict, bytes]:
    headers = {"User-Agent": USER_AGENT}
    if token:
        headers["Authorization"] = "Bearer " + token
    if ctype:
        headers["Content-Type"] = ctype
    req = urllib.request.Request(url, data=body, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            return resp.status, dict(resp.headers), resp.read()
    except urllib.error.HTTPError as err:
        return err.code, dict(err.headers), err.read()
    except urllib.error.URLError as err:
        raise ClientError("cannot reach %s: %s" % (url, err.reason))


def api_json(status: int, data: bytes) -> dict:
    try:
        return json.loads(data.decode())
    except ValueError:
        return {"error": "HTTP %d" % status}


def linked(cfg: dict) -> Tuple[str, str]:
    if not cfg.get("server") or not cfg.get("token"):
        raise ClientError("not linked yet: run `client.py pair <server> <code>` first")
    return cfg["server"].rstrip("/"), cfg["token"]


def stereo_dir(root: str) -> str:
    """The card's stereo folder for an SD root (or the folder itself if given directly)."""
    if os.path.basename(os.path.normpath(root)) == "stereo":
        return root
    return os.path.join(root, STEREO)


# ── Commands ────────────────────────────────────────────────────────────────

def cmd_pair(cfg: dict, server: str, code: str, name: Optional[str]) -> str:
    server = server.rstrip("/")
    name = name or "PC %s" % platform.node().split(".")[0]
    status, _, data = request("POST", server + "/api/pair", body=json.dumps({"code": code, "console_name": name}).encode(),
                              ctype="application/json")
    obj = api_json(status, data)
    if status != 200:
        raise ClientError("pairing failed: %s" % obj.get("error"))
    cfg.update({"server": server, "token": obj["token"], "user": obj["user"], "seq": 0})
    return "linked to %s as %s (%s)" % (server, obj["user"], name)


def edit_files(paths: List[str]) -> List[str]:
    out = []
    for p in paths:
        if os.path.isdir(p):
            out.extend(sorted(glob.glob(os.path.join(stereo_dir(p), "*.bin"))))
        else:
            out.append(p)
    return out


def cmd_upload(cfg: dict, paths: List[str]) -> List[str]:
    server, token = linked(cfg)
    lines = []
    files = edit_files(paths)
    if not files:
        raise ClientError("no edit files found in %s" % ", ".join(paths))
    for path in files:
        key = os.path.splitext(os.path.basename(path))[0]
        with open(path, "rb") as f:
            data = f.read()
        try:
            editfile.parse(data, key)
        except editfile.EditError as err:
            lines.append("%-5s skipped: %s" % (key, err))
            continue
        status, _, body = request("POST", "%s/api/mons/%s/versions" % (server, key), token, data,
                                  "application/octet-stream")
        obj = api_json(status, body)
        if status == 201:
            lines.append("%-5s uploaded as version %d%s" % (key, obj["version_id"],
                                                             ", now current" if obj["current"] else ""))
        elif status == 200 and obj.get("duplicate"):
            lines.append("%-5s already on the server (version %d)" % (key, obj["version_id"]))
        else:
            lines.append("%-5s FAILED: %s" % (key, obj.get("error", "HTTP %d" % status)))
    return lines


def cmd_sync(cfg: dict, root: str, dry_run: bool = False, full: bool = False) -> List[str]:
    server, token = linked(cfg)
    folder = stereo_dir(root)
    if not dry_run:
        os.makedirs(folder, exist_ok=True)
    since = 0 if full else int(cfg.get("seq", 0))
    status, _, body = request("GET", "%s/api/sync?since=%d" % (server, since), token)
    obj = api_json(status, body)
    if status != 200:
        raise ClientError("sync failed: %s" % obj.get("error"))
    lines = []
    for item in obj["items"]:
        key = item["key"]
        if editfile.key_to_id(key) is None:
            continue
        path = os.path.join(folder, key + ".bin")
        local = None
        if os.path.exists(path):
            with open(path, "rb") as f:
                local = f.read()
        if local is not None and hashlib.sha256(local).hexdigest() == item["sha256"]:
            continue
        if dry_run:
            lines.append("%-5s would get version %d%s" % (key, item["version_id"], " (replacing yours)" if local else ""))
            continue
        st, _, data = request("GET", "%s/api/versions/%d" % (server, item["version_id"]), token)
        if st != 200 or hashlib.sha256(data).hexdigest() != item["sha256"]:
            raise ClientError("download of %s failed (HTTP %d); nothing after it was written" % (key, st))
        editfile.parse(data, key)
        if local is not None:
            backup = os.path.join(folder, "backup")
            os.makedirs(backup, exist_ok=True)
            kept = os.path.join(backup, "%s-%s.bin" % (key, time.strftime("%Y%m%d-%H%M%S")))
            with open(kept, "wb") as f:
                f.write(local)
        tmp = path + ".part"
        with open(tmp, "wb") as f:
            f.write(data)
        os.replace(tmp, path)
        lines.append("%-5s now version %d%s" % (key, item["version_id"], " (yours kept in backup/)" if local else ""))
    if not dry_run:
        cfg["seq"] = obj["seq"]
    if not lines:
        lines.append("up to date")
    return lines


def cmd_status(cfg: dict) -> str:
    server, token = linked(cfg)
    status, _, body = request("GET", server + "/api/me", token)
    obj = api_json(status, body)
    if status != 200:
        return "%s: %s" % (server, obj.get("error"))
    return "%s: linked as %s (%s, %s); server seq %d, synced to %d" % (
        server, obj["user"], obj["console_name"], obj["role"], obj["seq"], cfg.get("seq", 0))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config", default=DEFAULT_CONFIG)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pair", help="link this computer with a code from the website")
    p.add_argument("server")
    p.add_argument("code")
    p.add_argument("--name", help="how the website lists this computer")
    u = sub.add_parser("upload", help="upload edit files, or every edit on an SD card")
    u.add_argument("paths", nargs="+")
    s = sub.add_parser("sync", help="fetch the current version of everything that changed")
    s.add_argument("root", help="the SD card (or a copy of it)")
    s.add_argument("--dry-run", action="store_true")
    s.add_argument("--full", action="store_true", help="check everything, not just what changed since last time")
    sub.add_parser("status", help="show the link")
    a = ap.parse_args(argv)
    cfg = load_config(a.config)
    try:
        if a.cmd == "pair":
            print(cmd_pair(cfg, a.server, a.code, a.name))
        elif a.cmd == "upload":
            print("\n".join(cmd_upload(cfg, a.paths)))
        elif a.cmd == "sync":
            print("\n".join(cmd_sync(cfg, a.root, a.dry_run, a.full)))
        else:
            print(cmd_status(cfg))
    except ClientError as err:
        print(err, file=sys.stderr)
        return 1
    if a.cmd in ("pair", "sync"):
        save_config(a.config, cfg)
    return 0


if __name__ == "__main__":
    sys.exit(main())
