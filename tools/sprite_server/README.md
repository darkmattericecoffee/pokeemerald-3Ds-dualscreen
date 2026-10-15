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

## Accounts and linking

- **Users**: an admin adds people on the website's Users page. The server makes the
  password and shows it once. Users change it on their Account page. Disabling a user
  signs them out and stops their consoles.
- **Linking a console or PC**: on the Account page, "Link a console" gives a code that works
  once for ten minutes. The device trades it for a long-lived token (the server keeps only its
  hash). Unlinking on the Account page kills the token.
- No passwords are ever typed on a console.

## The 3DS client

OPTIONS › EDITOR › LINK CONSOLE, then the code from the Account page (the first time also
the server's address). After that: FINALIZE in the plane editor uploads the picture, UPLOAD
ALL EDITS sends everything new, SELECT in the gallery or SYNC FROM SERVER fetches the current
versions (and opens the editor on what came in), and the editor's MORE › VERSIONS ON SERVER
lists a picture's versions to switch between and star. Details: `3ds_port/src/3ds_online.c` and
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
| `GET /api/mons` | every Pokémon with a current version |
| `GET /api/mons/<key>/versions` | `{"current","pinned","favourites","max_favourites","versions":[{"version_id","author","signed","revision","final","votes","sha256","size","uploaded_at"}]}`, newest first; `favourites` are the caller's |
| `POST /api/mons/<key>/votes` `{"version_ids":[…]}` | consoles only: the caller's favourites (0 to 2) for the picture, replacing the earlier ones; answers like `…/versions` |

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

## Tests

```bash
python3 -m unittest discover -s tools/sprite_server/tests -v
```

They start a real server on a free port with an empty data folder. They cover sign-in, CSRF,
admin-only pages, pairing, upload validation, duplicates, newest-wins, pin and unpin, delete,
sync with backups, ETags, unlinking and disabled users.
