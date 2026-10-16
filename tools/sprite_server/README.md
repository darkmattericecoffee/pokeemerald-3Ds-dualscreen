# Plane-sharing server (v1)

Volunteers repaint Pokémon's stereo planes with the on-device plane editor. This server
collects their edit files, shows in a web app which Pokémon are done (with every version in
3D, side by side, to vote on), and hands the current version of each back to consoles and
PCs. Design and background:
[docs/ONLINE_SPRITE_SHARING.md](../../docs/ONLINE_SPRITE_SHARING.md).

The server is Python 3.9+, standard library only, with SQLite. The web app (`web/`) is
React + TypeScript, built once into static files that the server serves. Listens on
localhost; Cloudflare Tunnel puts it on the internet.

## Start it

```bash
python3 tools/sprite_server/server.py export-sprites --upstream build/upstream   # once: game pictures for previews
python3 tools/sprite_server/server.py adduser arnoud --admin                   # once: prints a generated password
(cd tools/sprite_server/web && npm ci && npm run build)                         # the web app (needs Node 20+)
tools/sprite_server/run.sh                                                      # server + quick tunnel
```

Without the web app built the console API still works, and the site says how to build it.
`run.sh` builds it by itself when `web/dist` is missing and `npm` is there.

`run.sh` prints a `https://….trycloudflare.com` address. A quick tunnel gets a new address
every start, which breaks linked consoles; see "A fixed address" below for the real setup.

Everything lives in `~/emerald3ds-sprite-server` (`--data` / `DATA=` to change):
`sprites.db` is the database and the only thing to back up; `sprites.json` is the exported
game pictures (Pokémon fronts and backs, trainers' fronts, players' backs) and can be made again.

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

## The web app

`web/` holds the site: Vite, React 19 and TypeScript, Tailwind CSS 4 with shadcn-style
components on Radix, TanStack Query for the data, react-three-fiber (three.js) for the 3D
views. Pages:

- **Progress**: fronts, backs, trainers (all 93 trainers' fronts and 8 players' backs the game has,
  numbered T000/P000 as the editor numbers them, listed before anyone uploads one); all, not done, done, mine, voted; search by name or
  number; counts of finals and of pictures with two or more versions to vote on.
- **A Pokémon** (`/mon/025`): its versions, each with uploader, console, votes (and who),
  and final flag, and a 3D view of any of them. The 3D view draws every plane at its depth,
  spread as the console spreads every picture (one standard, 3 px from the farthest plane to
  the nearest; a slider exaggerates it to see the layers better). **Solid** extends each plane's pixels
  back to the farthest plane, so turning it shows no gaps; **Cards** shows the flat planes
  as the console draws them. Drag to turn it (sideways on touch, so the page still
  scrolls), pinch or Ctrl + scroll to zoom; it sways by itself after a moment. Frame 1, 2 or
  both in turn; the game's colours, tinted by plane, or the editor's plane colours.
- **Compare** (`/compare/025?v=12,15`): 2 to 4 versions side by side whose 3D views turn
  together, each with its facts and a Vote button. Pick versions with Compare on their cards.
- **Account**: your name, linked consoles, a pairing code that turns into "Linked!" the moment
  the console uses it, password. **Users** (admins): add, disable, new passwords.

### Live

Every page follows the server by itself. `/api/web/events` is a Server-Sent Events stream
that tells open pages about every upload, vote, pin, delete, name change and console link;
the pages fetch what changed at once (and flash it). The lens light in the header is green
while it is connected. After a dropped connection the browser reconnects by itself and the
pages fetch everything again; switching back to the tab does too. A comment goes down the
stream every 15 s so Cloudflare keeps it open.

### Following the web on a console

The web app tells the server which Pokémon it shows (and which versions it compares), per
account: `POST /api/web/focus`. A linked console asks `GET /api/focus` and can show the same
versions, so the 3DS and the browser compare the same thing. In memory only; ten minutes
without a page and it reads as nothing.

### Working on it

```bash
python3 tools/sprite_server/server.py --data /tmp/e3d serve        # the API, on :8787
cd tools/sprite_server/web && npm ci && npm run dev                # the app on :5173, API proxied
npm test                                                           # the web tests (vitest)
```

`demo_seed.py` fills a throwaway data folder with demo users, consoles, versions and votes
made from the game pictures (copy `sprites.json` in first; it refuses the real data folder):

```bash
mkdir -p /tmp/e3d && cp ~/emerald3ds-sprite-server/sprites.json /tmp/e3d/
python3 tools/sprite_server/demo_seed.py --data /tmp/e3d           # passwords: /tmp/e3d/demo-passwords.txt
```

## Accounts and linking

- **Users**: an admin adds people on the web app's Users page. The server makes the
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
vote for (each row's VOTE button: as many versions as you like, front and back apart, kept on
the card and sent when the server can be reached), upload
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

**Everybody votes for as many versions of a picture as they like** (up to 16; Vote on the
Pokémon's page, or the VOTE button on the console's versions screen; a front and its back are
voted on apart). Lists show the most votes first. The version with the most votes is current; among
equals, and with no votes at all, the newest. An admin can pin one instead. Admins can also
delete a version; its votes go with it and the current one is worked out again.

## API (what the 3DS client will call)

Consoles sign every request instead of sending their token, so they can use plain HTTP
(`signing.py`; docs/ONLINE_SPRITE_SHARING.md section 7.6): `X-Em3ds-Key`, `X-Em3ds-Time` (the
server's clock, from `/api/health`), `X-Em3ds-Nonce` and `X-Em3ds-Sign`, and every answer to a
signed request carries the server's `X-Em3ds-Sign`. The PC client still sends
`Authorization: Bearer <token>`, over HTTPS only (a bearer token that came over plain HTTP
through Cloudflare is ignored). Bodies are raw bytes or JSON. Every response has a
`Content-Length`. API calls are never redirected; web pages reached over plain HTTP through
Cloudflare go to HTTPS.

| Call | Does |
|---|---|
| `GET /api/health` | `{"ok":true,"api":2,"time":<unix seconds>}`, no sign-in |
| `POST /api/pair2` `{"pub","proof","console_name"}` | consoles: `{"pub","user","confirm"}` (an X25519 exchange proven with the code, which is not sent), or 403 |
| `POST /api/pair` `{"code","console_name"}` | the PC client, over HTTPS: `{"token","user"}`, or 403 |
| `GET /api/me` | `{"user","role","console_name","seq"}` |
| `POST /api/mons/<key>/versions` (the `.bin`) | 201 `{"version_id","current","duplicate","sha256"}`; 200 if already there; 400 with `error` if malformed |
| `GET /api/sync?since=<seq>` | `{"seq","items":[{"key","version_id","sha256","size","revision","final","depth"}]}`: what changed since `seq` |
| `GET /api/versions/<id>` | the `.bin`, with `ETag`, `X-Version-Id`, `X-Sha256` |
| `GET /api/mons/<key>/current` | the current `.bin`; `If-None-Match` gives 304 |
| `GET /api/mons` | every Pokémon with a current version: `versions` (how many), `mine` (how many are the caller's favourites) |
| `GET /api/mons/<key>/versions` | `{"current","pinned","favourites","max_favourites","versions":[{"version_id","author","signed","revision","final","depth","votes","sha256","size","uploaded_at"}]}`, the most votes first (among equals the newest); `favourites` are the caller's votes |
| `POST /api/mons/<key>/votes` `{"version_ids":[…]}` | consoles only: the caller's votes (0 to 16 of its versions) for the picture, replacing the earlier ones; answers like `…/versions` |
| `POST /api/versions/<id>/delete` | consoles only: takes the caller's own version off the server (an admin's console: any); answers like `…/versions` |
| `GET /api/rev` | `{"rev","seq"}`: `rev` moves on with every upload, vote, pin and delete |
| `GET /api/focus` | consoles: `{"key","version_ids","seq","at"}`, what the account's web app shows (`key` null after ten idle minutes); act when `seq` changes |

### The web app's API

JSON under `/api/web/`, for a signed-in browser: the session cookie (`e3s`, HttpOnly,
SameSite=Lax, Secure over HTTPS) and, on every POST, the session's CSRF token (from
`/api/web/session`) in an `X-CSRF-Token` header. Sign-in takes JSON only. A session cookie
that came over plain HTTP through Cloudflare is ignored.

| Call | Does |
|---|---|
| `GET /api/web/session`, `POST …/login` `{name,password}`, `POST …/logout` | who is signed in, and the CSRF token |
| `GET /api/web/grid` | every picture's current version (author, versions, final), names, which have a game picture, the caller's own and voted |
| `GET /api/web/mons/<key>` | a picture's versions with uploader, console, votes and voters, flags; neighbours |
| `POST /api/web/mons/<key>/vote` `{version_id,on}` | the caller's vote on or off; answers like `GET …/mons/<key>` |
| `POST /api/web/mons/<key>/pin` `{version_id}`, `…/unpin` | admins |
| `POST /api/web/versions/<id>/delete` | its uploader, or an admin |
| `GET /api/web/pictures/<key>` | the game picture as data (as in `sprites.json`), for the 3D views |
| `GET /api/web/account`, `POST …/account/name`, `…/account/pair`, `…/account/devices/<id>/revoke`, `…/account/password` | the account page |
| `GET /api/web/users`, `POST /api/web/users` `{name,role}`, `POST /api/web/users/<id>/enable\|disable\|reset` | admins |
| `POST /api/web/focus` `{key,version_ids}` | what this page shows, for consoles following the web |
| `GET /api/web/events` | the live channel (Server-Sent Events) |

Edit files themselves come from `GET /api/versions/<id>` (any signed-in user); the app parses
and draws them in the browser.

`depth` is the edit file's depth byte as a sweet spot (1..3, 0 read as 2); the database keeps
it, but pictures are all drawn at the one standard depth. `<key>` is the edit file's name: `006` front, `b006` back, `t012` trainer, `p001` player.
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

The live server runs on the Mac mini (`arnoudwarschauer@192.168.2.37`, mac-mini-m1.local on the LAN; ssh by the address, since the name fails the host-key check) in Docker, at
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

Update and restart it after a change here (run from the repo root on the MacBook). The
image builds the web app itself (a Node stage in the Dockerfile), so `node_modules` and
`dist` stay behind:

```bash
rsync -a --exclude __pycache__ --exclude .env --exclude data --exclude node_modules --exclude web/dist tools/sprite_server/ arnoudwarschauer@192.168.2.37:emerald3ds-server/
ssh arnoudwarschauer@192.168.2.37 'cd ~/emerald3ds-server/deploy && /usr/local/bin/docker compose up -d --build'
```

Consoles notice nothing: the console API is the same. Browsers that had the old pages open
get the new app on their next load.

Logs, a restart, a new account, a backup:

```bash
ssh arnoudwarschauer@192.168.2.37 'cd ~/emerald3ds-server/deploy && /usr/local/bin/docker compose logs --tail 50'
ssh arnoudwarschauer@192.168.2.37 'cd ~/emerald3ds-server/deploy && /usr/local/bin/docker compose restart'
ssh arnoudwarschauer@192.168.2.37 '/usr/local/bin/docker exec emerald3ds-server-1 python server.py --data /data adduser NAME'
ssh arnoudwarschauer@192.168.2.37 '/usr/local/bin/docker exec emerald3ds-server-1 python -c "import sqlite3; sqlite3.connect(\"/data/sprites.db\").backup(sqlite3.connect(\"/data/sprites-backup.db\"))"'
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
(cd tools/sprite_server/web && npm test)
```

The Python tests start a real server on a free port with an empty data folder. They cover
sign-in, CSRF, admin-only calls, pairing, signed console requests, upload validation,
duplicates, votes, newest-wins, pin and unpin, delete, sync with backups, ETags, unlinking,
disabled users, the account calls, the live channel, following the web, the picture data,
serving the app, and that previews keep the picture's clear pixels clear. The web tests
cover reading edit files, drawing them, the plane spacing, and a
few components.
