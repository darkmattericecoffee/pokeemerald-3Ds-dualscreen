# Plane-sharing server (v1)

Volunteers repaint Pokémon's stereo planes with the on-device plane editor. This server
collects their edit files, shows on a web page which Pokémon are done, and hands the
current version of each back to consoles and PCs. Design and background:
[docs/ONLINE_SPRITE_SHARING.md](../../docs/ONLINE_SPRITE_SHARING.md).

Python 3.9+ standard library only. SQLite. Listens on localhost; Cloudflare Tunnel puts it
on the internet with HTTPS the 3DS can talk to.

## Start it

```bash
python3 tools/sprite_server/server.py export-sprites --upstream build/upstream   # once: game pictures for previews
python3 tools/sprite_server/server.py adduser arnoud --admin                   # once: prints a generated password
tools/sprite_server/run.sh                                                      # server + quick tunnel
```

`run.sh` prints a `https://….trycloudflare.com` address. A quick tunnel gets a new address
every start, which breaks linked consoles; see "A fixed address" below for the real setup.

Everything lives in `~/emerald3ds-sprite-server` (`--data` / `DATA=` to change):
`sprites.db` is the database and the only thing to back up; `sprites.json` is the exported
game pictures and can be made again.

## Docker (the easy install)

Everything in one go, on any machine with Docker: the server and its Cloudflare tunnel.

```bash
cd tools/sprite_server
DATA_DIR=~/emerald3ds-sprite-server docker compose --profile quick up -d --build
docker compose --profile quick logs tunnel | grep trycloudflare      # the https address
docker compose exec server python server.py --data /data adduser NAME --admin
```

- `DATA_DIR` is the data folder (database and `sprites.json`); it defaults to `./data`. An
  existing folder works as it is. On a Mac with colima the folder must be one colima mounts.
- `sprites.json` (the game pictures for the previews) is made on a computer with the game
  built: `python3 server.py export-sprites --upstream build/upstream --data <DATA_DIR>`.
- Profile `named` instead of `quick` runs a named tunnel that keeps its address:
  `TUNNEL_TOKEN=... docker compose --profile named up -d --build` (make the tunnel in the
  Cloudflare dashboard, public hostname pointing at `http://server:8787`).
- The server's port is published on the host's 127.0.0.1 only; the internet reaches it
  through the tunnel.

## Live pages

The progress grid and every Pokémon's page follow the server by themselves: every few
seconds (while the tab is visible) they ask `/api/rev`, which moves on with every upload,
vote, pin and delete, and swap in the new page when it moved. A small "live" mark in the
header shows it working (grey when the server cannot be reached).

## Accounts and linking

- **Users**: an admin adds people on the website's Users page. The server makes the
  password and shows it once. Users change it on their Account page. Disabling a user
  signs them out and stops their consoles.
- **Linking a console or PC**: on the Account page, "Link a console" gives a code that works
  once for ten minutes. The device trades it for a long-lived token (the server keeps only its
  hash). Unlinking on the Account page kills the token.
- No passwords are ever typed on a console.

## The 3DS client

OPTIONS › ONLINE › LINK CONSOLE, then the code from the Account page (the first time also
the server's address). After that, in the plane editor (the column's EDITOR button): the globe
opens the versions screen: the server's versions of the Pokémon's front and back, to show,
vote for (two per picture, kept on the card and sent when the server can be reached), upload
your own or delete yours. In the Pokédex (DEX) every entry shows its version counts and
uploader and has the same globe. OPTIONS › ONLINE has LOAD NEW ONES, SEE AND VOTE and UPLOAD
ALL EDITS. Details: `3ds_port/src/3ds_online.c` and
section 7.1 of the design doc.

## The PC client

Does what the console will do over Wi-Fi, for anyone working from a copy of their SD card.

```bash
python3 tools/sprite_server/client.py pair https://<address> ABCD-EFGH
python3 tools/sprite_server/client.py upload /Volumes/NEW3DS
python3 tools/sprite_server/client.py sync /Volumes/NEW3DS
python3 tools/sprite_server/client.py status
```

`upload` sends every `3ds/emerald3ds/stereo/*.bin` (already-known files are skipped by the
server). `sync` writes the current version of everything that changed since the last sync.
A local file that differs is copied to `stereo/backup/<key>-<time>.bin` first.

## Which version is current

**Everybody picks up to two favourite versions per picture** (a star on the Pokémon's page,
or on the console's VERSIONS screen). The version with the most votes is current; among
equals, and with no votes at all, the newest. An admin can pin one instead. Admins can also
delete a version; its votes go with it and the current one is worked out again.

## API (what the 3DS client will call)

Consoles send `Authorization: Bearer <token>`. Bodies are raw bytes or JSON. Every response
has a `Content-Length`. There are no redirects.

| Call | Does |
|---|---|
| `GET /api/health` | `{"ok":true,"api":1}`, no sign-in |
| `POST /api/pair` `{"code","console_name"}` | `{"token","user"}`, or 403 |
| `GET /api/me` | `{"user","role","console_name","seq"}` |
| `POST /api/mons/<key>/versions` (the `.bin`) | 201 `{"version_id","current","duplicate","sha256"}`; 200 if already there; 400 with `error` if malformed |
| `GET /api/sync?since=<seq>` | `{"seq","items":[{"key","version_id","sha256","size","revision","final"}]}`: what changed since `seq` |
| `GET /api/versions/<id>` | the `.bin`, with `ETag`, `X-Version-Id`, `X-Sha256` |
| `GET /api/mons/<key>/current` | the current `.bin`; `If-None-Match` gives 304 |
| `GET /api/mons` | every Pokémon with a current version: `versions` (how many), `mine` (how many are the caller's favourites) |
| `GET /api/mons/<key>/versions` | `{"current","pinned","favourites","max_favourites","versions":[{"version_id","author","signed","revision","final","votes","sha256","size","uploaded_at"}]}`, newest first; `favourites` are the caller's |
| `POST /api/mons/<key>/votes` `{"version_ids":[…]}` | consoles only: the caller's favourites (0 to 2) for the picture, replacing the earlier ones; answers like `…/versions` |
| `POST /api/versions/<id>/delete` | consoles only: takes the caller's own version off the server (an admin's console: any); answers like `…/versions` |
| `GET /api/rev` | `{"rev","seq"}`: `rev` moves on with every upload, vote, pin and delete (the web pages poll it) |

`<key>` is the edit file's name: `006` front, `b006` back, `t012` trainer, `p001` player.
`seq` is a server counter, so the console's clock never matters.

## A fixed address (for real)

A named tunnel keeps one address. It needs a Cloudflare account with a domain on it.

```bash
cloudflared tunnel login                                   # browser: pick the domain
cloudflared tunnel create emerald3ds
cloudflared tunnel route dns emerald3ds planes.example.com
TUNNEL=emerald3ds tools/sprite_server/run.sh
```

For the 3DS, in that domain's Cloudflare dashboard, keep **SSL/TLS › Edge Certificates ›
Minimum TLS Version at 1.0** (the default). Then run `tools/tlsprobe` against the new
address before handing it out. The probe passed through a quick tunnel, whose certificate
chains to GlobalSign Root CA. A custom domain may get a different certificate authority.

## Deploying on the Mac mini

The live server runs on the Mac mini (`arnoudwarschauer@mac-mini-m1.local`) in Docker, at
**https://poke.cloudnoud.nl**, through the named tunnel `poke`. Everything is in
`deploy/docker-compose.yml`: the server (on the Mac mini's 127.0.0.1:8788, since 8787 is
taken there) and a cloudflared container that runs the tunnel. Both have
`restart: unless-stopped`. Docker Desktop starts at login and the Mac mini logs in by
itself, so the server comes back after a reboot.

| On the Mac mini | |
|---|---|
| `~/emerald3ds-server/` | a copy of this folder (`deploy/.env` holds the tunnel id and paths) |
| `~/emerald3ds-sprite-server/` | the data: `sprites.db` (back it up) and `sprites.json` |
| `~/emerald3ds-cloudflared/.cloudflared/` | the tunnel's login (`cert.pem`) and credentials, apart from the Mac mini's other tunnel |

Update and restart it after a change here (run from the repo root on the MacBook):

```bash
rsync -a --exclude __pycache__ --exclude .env --exclude data tools/sprite_server/ arnoudwarschauer@mac-mini-m1.local:emerald3ds-server/
ssh arnoudwarschauer@mac-mini-m1.local 'cd ~/emerald3ds-server/deploy && /usr/local/bin/docker compose up -d --build'
```

Logs, a restart, a new account, a backup:

```bash
ssh arnoudwarschauer@mac-mini-m1.local 'cd ~/emerald3ds-server/deploy && /usr/local/bin/docker compose logs --tail 50'
ssh arnoudwarschauer@mac-mini-m1.local 'cd ~/emerald3ds-server/deploy && /usr/local/bin/docker compose restart'
ssh arnoudwarschauer@mac-mini-m1.local '/usr/local/bin/docker exec emerald3ds-server-1 python server.py --data /data adduser NAME'
ssh arnoudwarschauer@mac-mini-m1.local '/usr/local/bin/docker exec emerald3ds-server-1 python -c "import sqlite3; sqlite3.connect(\"/data/sprites.db\").backup(sqlite3.connect(\"/data/sprites-backup.db\"))"'
```

Make backups inside the container as above: the database is in WAL mode, and its lock
does not reach across Docker Desktop's file sharing, so leave `sprites.db` alone on the
Mac mini's side while the server runs.

The tunnel was made once, on the Mac mini, with its own login folder as HOME. Without
that, cloudflared reads the Mac mini's `~/.cloudflared/config.yml`, which names its other
tunnel, and `route dns` points the hostname at that tunnel instead:

```bash
export HOME=~/emerald3ds-cloudflared                # in a fresh shell on the Mac mini
cloudflared tunnel login                            # browser: pick cloudnoud.nl
cloudflared tunnel create --credentials-file ~/.cloudflared/poke.json poke
cloudflared tunnel route dns --overwrite-dns poke poke.cloudnoud.nl
cloudflared tunnel list                             # the id goes into deploy/.env
```

**The 3DS and this address.** cloudnoud.nl is on Cloudflare's Free plan, whose edge
certificate is ECDSA only (RSA needs a paid plan). TLS 1.0 works, and the chain ends at
GlobalSign Root CA (WE1 › GTS Root R4 cross-signed by GlobalSign), but the console was only
seen working with RSA (ECDHE-RSA-AES128-SHA, through a quick tunnel). Run `tools/tlsprobe`
on the 3DS against `https://poke.cloudnoud.nl/api/health` before handing the address out.

Keep the zone's **SSL/TLS › Edge Certificates › Minimum TLS Version at 1.0** for the 3DS,
and check it with `tools/tlsprobe` (put `https://poke.cloudnoud.nl/api/health` in
`sdmc:/3ds/tlsprobe/urls.txt`).

## Tests

```bash
python3 -m unittest discover -s tools/sprite_server/tests -v
```

They start a real server on a free port with an empty data folder. They cover sign-in, CSRF,
admin-only pages, pairing, upload validation, duplicates, newest-wins, pin and unpin, delete,
sync with backups, ETags, unlinking and disabled users.
