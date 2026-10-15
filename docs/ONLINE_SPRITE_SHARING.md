# Sharing plane edits over Wi-Fi — investigation and design

Written 2026-10-15 on the branch `investigate/online-sprite-sharing`. Nothing here is
implemented yet; this is the result of looking at the 3DS's networking services, at how
existing homebrew (FBI, Universal-Updater) uses them, and at what the port already has.

## 1. What we want

Up to ~10 volunteers repaint the stereo planes of Pokémon on their own consoles with the
on-device plane editor. Each edit is one small file on the SD card
(`sdmc:/3ds/emerald3ds/stereo/NNN.bin`, see §4). We want to:

1. **Upload** an edit from the console to a server Arnoud owns, tagged with who made it.
2. **See progress** on a web page: which Pokémon have been redone, by whom, how many
   versions exist, which version is current.
3. **Vote**: several versions per Pokémon; people vote, the best becomes the "current" one.
4. **Download** the current versions back to a console over Wi-Fi, so everybody plays with
   the same, best set.
5. **Sign in once** per console with an account Arnoud hands out, and never again.

## 2. How the 3DS talks to the internet (what homebrew can use)

The console's Wi-Fi is set up once in System Settings; every title, homebrew included, can
then use it. libctru (already linked, `-lctru`) exposes the system's network services as
plain C. No extra library and no extra pacman package is needed for option A below.

| Service | libctru API | What it is | Use it for |
|---|---|---|---|
| `http:C` | `httpcInit`, `httpcOpenContext`, `httpcAddRequestHeaderField`, `httpcAddPostDataRaw`, `httpcBeginRequest`, `httpcGetResponseStatusCode`, `httpcGetResponseHeader`, `httpcDownloadData`, `httpcCloseContext` | Nintendo's own HTTP client, running in a system module. Handles DNS, TCP, TLS, redirects' headers, keep-alive. | **Everything we need.** Small JSON/binary GET and POST requests. This is what the official `network/http_post` example and FBI use. |
| `soc:U` | `socInit(buffer, size)` + BSD sockets (`socket`, `connect`, `send`, `recv`, `getaddrinfo`) | Raw sockets. Needs a 0x1000-aligned shared buffer (examples use 128 KiB–1 MiB). | Only if we wrote our own HTTP; libcurl (`3ds-curl`, a devkitPro port) sits on top of this. Not needed. |
| `ssl:C` | `sslcInit`, `sslcCreateContext`, `sslcStartConnection`, `sslcRead/Write` | TLS over a `soc:U` socket. | Not needed when `http:C` does HTTPS for us. |
| `ac:u` | `acInit`, `ACU_GetWifiStatus`, `acWaitInternetConnection` | Wi-Fi connection state. | "Is Wi-Fi up?" before trying, a friendly message when it isn't. |
| `cam` | `camInit`, `CAMU_SetSize`, `CAMU_SetReceiving`, `CAMU_StartCapture` | The cameras. | QR-code pairing (optional, §6). FBI pairs it with the small `quirc` decoder. |
| `swkbd` | `swkbdInit`, `swkbdInputText` | The system's software keyboard. | Typing a pairing code or a server address. |

### 2.1 The `http:C` call sequence (verified against libctru's `httpc.h` and the official example)

```c
httpcInit(64 * 1024);                                   /* shared memory for POST bodies, multiple of 0x1000 */

httpcContext ctx;
httpcOpenContext(&ctx, HTTPC_METHOD_POST, url, 0);
httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);            /* see §3 before deciding */
httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_ENABLED);
httpcAddRequestHeaderField(&ctx, "User-Agent", "emerald3ds/1.0");
httpcAddRequestHeaderField(&ctx, "Authorization", "Bearer <device token>");
httpcAddRequestHeaderField(&ctx, "Content-Type", "application/octet-stream");
httpcAddPostDataRaw(&ctx, (u32 *)body, bodyLen);        /* the edit .bin, as is */
httpcBeginRequest(&ctx);                                /* blocks until headers arrive */

u32 status; httpcGetResponseStatusCode(&ctx, &status);
/* 301/302/303/307/308: read "Location" with httpcGetResponseHeader and loop */
u32 got; httpcDownloadData(&ctx, buf, bufSize, &got);   /* HTTPC_RESULTCODE_DOWNLOADPENDING = more */
httpcCloseContext(&ctx);
```

Facts worth knowing:

- `httpcInit`'s size is only for request bodies; GET needs 0. Our bodies are ≤ 11 KiB, so
  64 KiB is plenty. It comes from the normal heap, not the linear heap the textures fight over.
- `httpcBeginRequest` and `httpcDownloadData` **block**. The game must not stall, so all of
  this runs on a worker thread (`CtrPlatform_StartThread`, `3ds_platform.c`, already used by
  the port; the port is New 3DS only so a second core is available). The UI polls a status
  word and shows a small spinner/result.
- Redirect handling is manual (read `Location`, reopen). Keep the server free of redirects
  and this disappears; the example's loop is ~20 lines if we want it anyway.
- `httpcGetDownloadSizeState` gives `Content-Length` when the server sends one; always send it.
- Sleep mode (closing the lid) drops Wi-Fi. For the few seconds of a transfer, hold the
  console awake with `aptSetSleepAllowed(false)` and restore it after.
- The `.3dsx` homebrew path through hbmenu/Luma gets network access like any title; no
  special permission bits are needed.

## 3. TLS: the one real problem

The 3DS's SSL module is old and the console no longer gets updates:

- 3dbrew's SSL Services page documents TLS **1.0/1.1** as the highest version; there are
  user reports of TLS 1.2 working after firmware 11.9 (Dec 2018) but no authoritative record
  of what the `sslc` module accepts. Treat TLS 1.2 as "probably, test it", TLS 1.3 as no.
- The built-in root store (libctru `SSLC_DefaultRootCert_*`) has eleven roots: six Nintendo
  ones, GTE CyberTrust, AddTrust External, COMODO RSA, USERTrust RSA, DigiCert High Assurance
  EV. **Let's Encrypt's ISRG Root X1 is not in it.** A Let's Encrypt certificate therefore
  fails verification unless we add the root ourselves.
- `httpcAddTrustedRootCA(ctx, derBytes, size)` adds a root (DER) per context, and
  `httpcSetSSLOpt(ctx, SSLCOPT_DisableVerify)` turns verification off. The 3DS does not check
  certificate dates against the clock either.

Three ways through, in order of preference:

1. **HTTPS with our own pinned root.** Embed ISRG Root X1 (or, simpler and fully under our
   control, a self-made CA that signs the server's certificate) as DER in the binary and call
   `httpcAddTrustedRootCA` on every context. The server must offer TLS 1.0–1.2 with an
   RSA/AES-CBC cipher (nginx: `ssl_protocols TLSv1 TLSv1.1 TLSv1.2; ssl_ciphers AES128-SHA:AES256-SHA:...`
   on a host name used only by the consoles, so the main site keeps modern settings).
   Pinning our own CA makes the "old TLS" compromise far less bad: only our key signs it.
2. **HTTPS with `SSLCOPT_DisableVerify`.** What FBI and the official example do. Encrypts the
   token in transit but lets anyone on the path impersonate the server. Acceptable fallback
   if (1) fights us on a specific console; the data is not secret, the token is per device
   and revocable.
3. **Plain HTTP with signed requests.** If the TLS handshake itself fails against the server
   (cipher mismatch), fall back to `http://` and sign each request with an HMAC-SHA256 of
   (method, path, body hash, timestamp) using the device token as the key. The token itself
   never travels; replay is bounded by the timestamp. SHA-256 is ~100 lines of C.

Plan: implement (1) with (3)'s signing anyway (it is cheap, works over both), and keep (2) as a
settings-file switch for debugging. Test on hardware early: the Azahar emulator's `http:C`
reimplementation does its own TLS with the host's stack, so **a working transfer in Azahar
says nothing about TLS on the console**.

### 3.1 Hosting: Cloudflare Tunnel (decided 2026-10-15)

The server will sit behind a Cloudflare Tunnel (`cloudflared`), so no port forwarding and
Cloudflare terminates TLS. Measured from the Mac against a `trycloudflare.com` quick tunnel:

- Cloudflare's edge still accepts **TLS 1.0** with `ECDHE-RSA-AES128-SHA` and serves an
  **RSA** certificate to clients that offer only RSA suites. Modern clients get ECDSA + TLS 1.3.
- That RSA chain is leaf → Google Trust Services WR1 → GTS Root R1 → **GlobalSign Root CA**
  (R1, RSA, valid to 2028-01-28). None of these are in the 3DS's eleven built-in roots, so
  the console needs GlobalSign Root CA added with `httpcAddTrustedRootCA`.
- Plain `http://` through the tunnel also answers 200, so §3 option 3 stays available.

**Hardware result (New 3DS, system 11.17.0-50E, 2026-10-15,
`docs/probe-results/tlsprobe-2026-10-15-new3ds.txt`):**

| Through the Cloudflare tunnel | Result |
|---|---|
| default roots | fails, `D8A0A03C` |
| + ISRG Root X1 | fails, `D8A0A03C` |
| **+ GlobalSign Root CA** | **200 OK, fully verified, ~1.2 s** |
| verification off | 200 OK |
| plain http | 200 OK |

letsencrypt.org and digicert.com fail even with verification off, so `D8A0A03C` also covers
handshakes the console cannot do (modern-only TLS); Cloudflare's edge is the exception that
still serves old clients. **Decision: §3 option 1.** Embed GlobalSign Root CA (DER, 889 bytes,
SHA-256 `EBD41040…331C99`) and call `httpcAddTrustedRootCA` on every context. The root expires
2028-01-28; ship GTS Root R1's other cross-signs as a second root before then, and keep the
verify-off switch in `online.txt` for debugging.

## 4. What travels

The on-device editor writes, per picture, `sdmc:/3ds/emerald3ds/stereo/<key>.bin` where
`<key>` is `NNN` (front), `bNNN` (back), `tNNN` (trainer), `pNNN` (player):

| Part | Bytes |
|---|---|
| header `EM3DSED1`-style magic, id, planes, frames, 64×0×64 | 16 |
| planes, one nibble per pixel, 64×64 per frame | 2048 × frames (1–5) |
| trailer `EM3DSMD1`: final flag, revision, save time, author[16] | 32 |

So one edit is **2–10 KiB**: tiny. The server does not need the picture pixels; it already
has the game's sprites (what `tools/sprite_editor/export_data.py` dumps for the web
prototype), so it can render previews and the tinted-plane view itself. The companion
`<key>.log` (the editor's notes) can go along as a second part or be dropped.

Upload = the file bytes, verbatim. Download = the same bytes written to the same path, then
`CtrStereoMon_EditScan()` re-run and the plane cache forgotten
(`CtrStereoBattle_ForgetPlanes`), exactly as after a local save.

## 5. The server

Small and boring on purpose: one Python process (FastAPI or Flask), SQLite, static HTML.
Runs behind nginx on the server Arnoud owns. `tools/sprite_server/` in this repo.

### 5.1 Data

```
users      id, name, pass_hash, role (admin|artist), created
devices    id, user_id, token_hash, console_name, created, last_seen, revoked
mons       key (e.g. "006", "b006", "t012"), national, name, kind
versions   id, mon_key, user_id, device_id, revision, planes, frames, final,
           sha256, bytes (BLOB), uploaded_at, preview_png (BLOB, server-rendered)
votes      version_id, user_id, value (+1), voted_at      -- one vote per user per mon
current    mon_key, version_id, set_by (vote|admin), set_at
```

"Current" is recomputed on every vote: most votes wins, ties by newest; an admin can pin.

### 5.2 API (all JSON unless noted; consoles send `Authorization: Bearer <token>`)

| Method & path | Who | Does |
|---|---|---|
| `POST /api/pair` `{code}` | console | Trades a pairing code (§6) for a long-lived device token. |
| `GET /api/me` | console | Who am I, is my token still good. Cheap first call. |
| `POST /api/mons/{key}/versions` (body: the `.bin`) | console | Upload. Server validates magic/size, stores, renders preview, returns `{version_id, revision}`. |
| `GET /api/mons/{key}/current` → `.bin` | console | Download the current version (`ETag`/`X-Revision` so unchanged ones are skipped). |
| `GET /api/sync?since=<unix>` | console | `[{key, version_id, revision, sha256}]` of everything whose current changed since. One request, then only the needed `.bin`s. |
| `GET /api/mons` | web | Progress table: per Pokémon the version count, current author, done/open. |
| `GET /api/mons/{key}` | web | All versions with previews and vote counts. |
| `POST /api/mons/{key}/vote` `{version_id}` | web (session) | Vote; replaces the user's earlier vote on that Pokémon. |
| `POST /api/admin/current` | admin | Pin a version. |
| `POST /api/admin/pairing-code` | admin/user | Mint a code for pairing a console. |

Voting happens on the website, not on the console: a 3DS is a poor place to compare ten
versions, and it keeps the console side to three endpoints (`pair`, upload, `sync`+download).
A later on-device "vote" button can call the same endpoint.

### 5.3 Web UI

- **Progress grid**: all 386 (+ backs/trainers) as tiles with the current preview, grey when
  nobody has done it, a badge with the version count. Filters: open / done / mine.
- **Pokémon page**: the game sprite, every version side by side (tinted planes and an
  animated parallax preview built from the planes, as the web prototype already draws),
  author, date, votes, a vote button, the current one marked.
- **Account page**: pair a console (shows a code and a QR), list/revoke devices.

### 5.4 PC fallback

`tools/sprite_editor/import_sd_edits.py` already reads an SD card's `stereo/` folder. A tiny
`sprite_server/client.py upload|sync <sd-root>` speaking the same API means a volunteer with
no Wi-Fi (or when TLS misbehaves on their console) copies the card to a PC and syncs from
there. Same accounts, same data.

### 5.5 What v1 built (2026-10-15)

`tools/sprite_server/` (see its README): users, sessions, pairing codes, device tokens,
upload with validation, newest-wins current with admin pin, unpin and delete, `sync` by a
server sequence number, ETag downloads, the progress grid, a page per Pokémon with tinted and
swaying-plane previews from the game's own pictures, Account and Users pages, a PC client,
`run.sh` for server plus tunnel, and end-to-end tests. Differences from the plan above:

- Standard library only (`http.server`, `sqlite3`), not FastAPI: nothing to install.
- Voting is not in v1. The newest upload is current until an admin pins one (§5.1's
  `votes` table is v2).
- `sync` takes `since=<seq>`, a server counter, not a time.
- The PC client links with a pairing code like a console does. There is no web upload.

Verified on 2026-10-15: all 14 real edit files from the New 3DS's card were accepted, both
with and without the trailer and with up to 14 planes. Pages were checked at desktop and
phone width. Sign-in, pairing and sync worked through a quick tunnel, with a `Secure` cookie
and the public address on the pairing page.

## 6. Sign in once: pairing

Never type a password on the console. The website gives a signed-in user an 8-character
**pairing code** (valid 10 minutes, one use), shown as text and as a QR code. On the console:

1. OPTIONS › ENHANCEMENTS › **ONLINE** › "Link this console".
2. Type the code with the system keyboard (`swkbd`, numbers+letters, 8 chars), or
3. (later) point the outer camera at the QR: `cam` service at 400×240 RGB565 like FBI's
   `capturecam.c`, frames fed to `quirc` (FBI vendors it under `source/libs/quirc`, ~1k lines,
   ISC). Works on hardware only; Azahar has no camera. Nice to have, not needed for v1.
4. `POST /api/pair {code, console_name}` → `{token}`; the token (32 random bytes, hex) is
   written to `sdmc:/3ds/emerald3ds/online.txt` next to `settings.txt`. The server stores
   only its hash. Revoking a device on the website invalidates it.

Every later request carries the token. No account UI on the console beyond "Linked as
*name* — Unlink".

## 7. On the console

New native unit `3ds_port/src/3ds_online.c` with an SDK-free header `3ds_online.h`
(game units never include libctru, as everywhere in the port):

```c
bool  CtrOnline_Available(void);                 /* Wi-Fi up and a token on the card */
void  CtrOnline_Pair(const char *code);          /* async; result via Status */
void  CtrOnline_Upload(uint16_t id);             /* this picture's edit file */
void  CtrOnline_UploadAll(void);                 /* every edit file not yet uploaded */
void  CtrOnline_Sync(void);                      /* fetch newer "current" files */
CtrOnlineStatus CtrOnline_Status(char *text, unsigned size);  /* IDLE/BUSY/DONE/ERROR + message */
```

- One worker thread at a time; the game loop never waits. The editor's status corner shows
  "UPLOADING… / UPLOADED / OFFLINE / ERROR 403".
- Hooks: the editor's SAVE confirmation ("FINALIZE") gets an **UPLOAD** option; the gallery
  gets **SYNC**; OPTIONS › ONLINE has link/unlink, upload all, sync, and an "auto-upload on
  finalize" toggle (`settings.txt` key `online_auto`).
- Server address lives in `online.txt` too (default compiled in), so a test server works
  without a rebuild.
- Memory: the response buffer is a few KiB from the heap; the shared memory for `httpcInit`
  is 64 KiB. Initialise `httpc`/`ac` lazily on first use and tear down after, so the modules
  cost nothing while playing.
- Game font gotchas apply to the status text (no `+`, `%` draws as `x`, 39-char ring buffers).

### 7.1 What was built (2026-10-15)

`3ds_port/src/3ds_online.c` with `include/3ds_online.h`, on the plane-editor line
(`private/plane-editor` plus this branch's docs and tools):

- **Jobs** `CtrOnline_Pair/Upload/UploadAll/Sync/Unlink`, one at a time on a worker thread
  (system core, `CtrPlatform_StartThread`); `httpcInit(64 KiB)` and `httpcExit` per job;
  `aptSetSleepAllowed(false)` while one runs (`CtrOnline_Frame`, called from
  `CtrStereoMonGame_Frame`). 15 s timeouts. Every request is logged to port.log with its HTTP
  status, size and time, or the failing `Result` (`D8A0A03C` is flagged as TLS).
- **TLS**: GlobalSign Root CA (`src/3ds_online_root.h`, SHA-256 checked) added to every
  `https://` context; `verify=0` in `online.txt` turns checks off for debugging.
- **`online.txt`**: `server=`, `token=`, `user=`, `seq=` (last sync), optional `verify=0`.
  No compiled-in server unless built with `make ONLINE_SERVER=https://...`; without either,
  LINK CONSOLE asks for the address with the system keyboard first.
- **`online_sent.txt`**: key and SHA-256 of every file the server is known to have (uploaded,
  already there, or synced down). UPLOAD ALL sends only the rest.
- **Sync**: compares each changed item's SHA-256 with the card's file, downloads only the
  differing ones, checks their SHA-256 and magic, copies a differing card file to
  `stereo/backup/<key>-<time>.bin`, writes `<key>.bin.part` and renames it over. `seq` moves
  on only when every item worked. Afterwards the main thread runs
  `CtrStereoMon_EditsChanged()`: rescan, battle planes forgotten, the gallery's picture read
  again (a picture open in the editor keeps what the editor holds).
- **UI**: OPTIONS › ENHANCEMENTS (third screen) › ONLINE: STATUS, LINK CONSOLE (`swkbd`),
  UNLINK, UPLOAD ALL EDITS, SYNC FROM SERVER, UPLOAD ON FINAL (`online_auto`, on by default).
  The editor's FINAL question gets an UPLOAD TO THE SERVER row when linked, and FINALIZE
  uploads by itself with UPLOAD ON FINAL on; the upload's progress shows as toasts. The
  gallery: SELECT syncs, and the result shows top right for six seconds.
- **Scripted check**: if `sdmc:/3ds/emerald3ds/online_test.txt` exists, its lines (`pair
  CODE`, `sync`, `upload 001`, `uploadall`, `unlink`, `wait N`) run one after the other ten
  seconds after start, each result logged as `online test: -> ...`. Works on hardware too.

Verified in Azahar against a local server (`http://127.0.0.1`): pairing with a code minted
through the website's Account page, sync of another account's upload, upload, upload-all
skipping known files, a sync that kept the card's differing file in `backup/`, unlink, and
the error statuses for a wrong code, a missing file and an unlinked console. **Not yet
verified**: the TLS path on the real New 3DS, and the on-screen parts (ONLINE page, the
keyboard, the editor's UPLOAD row, the gallery line), which were built but not looked at.

### 7.2 Round two (2026-10-15, after the first hardware test)

The first New 3DS test passed: 14 edits uploaded through the Cloudflare tunnel with the
pinned GlobalSign root, and a version made on the PC synced down. Then, on the user's request:

- **Votes** (server): `votes` table, at most two favourites per user per picture, from the
  website (a star per version) or the console (`POST /api/mons/<key>/votes`). Current = admin
  pin, else most votes, else newest. `GET /api/mons/<key>/versions` lists a picture's versions
  with their votes and the caller's favourites.
- **Versions on the console**: the editor's MORE › VERSIONS ON SERVER. UP/DOWN (or a tap)
  puts that version on the card and shows it at once; the card's own edit goes to
  `stereo/backup/` unless the server has it. A (or the star) toggles a favourite.
- **New pictures**: each sync that brings pictures writes them to `online_new.txt`. The
  editor's chooser has a NEW filter. After such a sync the editor turns to the first new
  picture, the gallery shows it, or on the field the editor opens on it (OPEN NEW ONES).
- **Menus**: the column's trainer-card button is EDITOR (opens the plane editor; icon
  `tools/sprite_editor/sprites/editor_menu.png` via `gen_menu_icon.py`). OPTIONS' CHEATS tab
  is now EDITOR (`CTR_EXTRAS_EDITOR`; the cheats stay in the source behind
  `CTR_CHEATS_MENU`): a status row across the whole width with the new pictures' icons (three,
  then a drawn "+n"), the editor, link, upload all, sync, unlink, NEW FROM SERVER, OPEN NEW
  ONES, UPLOAD ON FINAL, SHOW CONTROLS.
- **Top-screen help**: on the START and SELECT rows the action word was placed against the
  button rather than the "START"/"SELECT" label, so BATTLE and POKEDEX overlapped those labels.

### 7.3 Round three (2026-10-15): voting where the pictures are

- **Editor's bar**: a globe next to DEX and MORE (the user's picture, `sprites/online.png`):
  a sync, then the Pokédex opens on what it brought. A star next to SAVE votes for the version
  on the card (two per Pokémon). The C-stick down shows the next server version (up: the one
  before). The top right says which: "VERSION 2 OF 3 / BY TESTER2 / 1 VOTE  YOUR VOTE".
- **Votes never get lost**: a vote is written to `online_votes.txt` first (pending), then
  sent; with no server it waits ("SAVED - SENDS LATER") and goes out after the next job
  that reaches the server, or on a retry every minute. Azahar's http:C answers an
  unreachable server with "success" and status 0xFFFFFFFF; anything outside 100-599 now
  counts as no answer.
- **The Pokédex** (the editor's chooser) is the voting view: Circle Pad through the Pokémon
  (held, it runs), D-pad left/right through the one's versions, X votes, A edits, L/R pages,
  START filters ALL/EDITED/NEW, Y searches. Tiles show the number of versions ("3V") and a
  star where this account voted (`GET /api/mons` gives `versions` and `mine`). The top screen
  shows the controls, the page ("PAGE 3 OF 43"), and the version on the card.
- **Menus**: the third tab is ONLINE (status, LOAD NEW ONES, SEE AND VOTE, UPLOAD ALL EDITS,
  LINK, UNLINK); OPEN NEW ONES, UPLOAD ON FINAL and EDITOR CONTROLS moved to SETTINGS.
- **Website**: polls `/api/rev` and refreshes the grid and Pokémon pages in place; Docker
  (`Dockerfile`, `docker-compose.yml` with quick and named tunnel profiles).
- `online_sent.txt` lines carry the server's version number (`key sha256 version`), so the
  console knows which version is on the card.

### 7.4 Round four (2026-10-15): one screen, one job

- **The globe** (the user's 16px picture, `sprites/globe.png`, now an editor sprite) is a
  button in the editor's bar and on every Pokédex entry. Both open the **versions screen**:
  the server's versions of the Pokémon's front (left list) and back (right list), each with
  who made it, its votes, SHOWN (on the card) and YOURS. UP/DOWN shows a version, LEFT/RIGHT
  picks the front or the back, A or the star votes (front and back separately), Y animates;
  UPLOAD MINE sends this console's own edits of both sides, DELETE MINE (asked first) takes
  this account's version off the server (`POST /api/versions/<id>/delete`, own versions only;
  an admin's console may delete any). The top screen shows the front and the back side by
  side, the side looked at in 3D, the other flat (`CtrStereoMonGame_WantCompanion`, its
  texture made only while the screen is open), with the screen's controls along the bottom.
- **The editor** edits: the vote star and the C-stick's version turning are gone from it
  (voting is the versions screen's). The top right still names the version on the card.
- **The Pokédex** picks: two columns of taller entries showing the number and the name, how
  many front and back versions the server has, and "YOUR EDIT" or "BY <uploader>" (the current
  version's, from `GET /api/mons`); a star where this account voted. A (or a tap) edits, X (or
  the entry's globe) opens its versions screen.
- The versions JSON has `mine` per version.

### 7.5 Round five (2026-10-15)

- **Both sides in 3D**: the versions screen and the Pokédex show the front and the back side by
  side, both as their planes. The other side's planes (its card edit, else the pack's) are
  composed into a texture of its own (4 planes x 2 frames, 64 KiB, made while one of those
  screens is open; `stereo: other side ... as planes` in port.log).
- **The column's POKéDEX** opens the plane editor's Pokédex instead of the game's.
- **The world waits for the editor**: open on the field, the game's main loop is held in
  `CtrGame_WaitFrame` (frames are presented, so the VBlank handler and the sound go on; the
  bottom screen and the editor run), so nothing moves, no script runs and no battle starts.
  The Pokémon Center theme plays meanwhile and the map's music comes back on closing
  (`editor: the world waits` in port.log). The editor's own BATTLE preview lets the game run.

### 7.6 Signed plain HTTP (2026-10-15)

The standard server, poke.cloudnoud.nl, is a Free-plan Cloudflare zone: its edge certificate is
ECDSA only (P-256, WE1 -> GTS Root R4 -> GlobalSign), and every RSA cipher suite is refused. The
3DS was only ever seen to finish a handshake with an RSA certificate (the quick tunnel's), and an
RSA edge certificate costs a paid plan. So consoles speak **plain HTTP** to the same hostname
(Cloudflare passes `http://` through: "Always Use HTTPS" is off) and nothing secret travels:

- **Linking** (`POST /api/pair2 {"pub","proof","console_name"}` -> `{"pub","user","confirm"}`):
  an X25519 exchange. The console sends its public key and an HMAC of it keyed with SHA-256 of
  the pairing code (what `pairing_codes.code_hash` keeps); the code is not sent. Both sides make
  the token as HMAC(shared secret, both public keys and the code hash); `confirm` (keyed with
  SHA-256 of that token) shows the console the server had the code. An onlooker sees two public
  keys and learns nothing; someone in the middle would have to guess the code (2^40) while the
  console waits.
- **Requests**: `X-Em3ds-Key` (16 hex: a hash of the device key), `X-Em3ds-Time` (the server's
  clock, fetched from `/api/health` at the start of every job and every 10 minutes: the console's
  own is whatever its owner set), `X-Em3ds-Nonce` and `X-Em3ds-Sign` = HMAC-SHA256(device key,
  `EM3DS1\n<method>\n<path with query>\n<time>\n<nonce>\n<SHA-256 of the body>`). The device
  key is SHA-256 of the token, which is exactly `devices.token_hash`: consoles linked the old way
  keep working, and stop sending their token. The server refuses a wrong MAC, a time more than
  5 minutes off and a nonce it has seen (401).
- **Answers** to a signed request carry `X-Em3ds-Sign` = HMAC(device key, `EM3DS1 reply\n<the
  request's MAC>\n<status>\n<SHA-256 of the body>`). The console takes no 2xx answer without
  it (`ANSWER NOT SIGNED`), so nobody on the way can feed it pictures or votes.
- **Browsers**: a non-API request that came over plain HTTP through Cloudflare (`Cf-Visitor`
  says http) is sent to HTTPS (301), and a bearer token arriving that way is not taken.
- What an onlooker still sees: which pictures a console fetches and uploads (public anyway).
  Whoever holds the database can sign as any console (it keeps the keys' hashes, which are the
  keys); revoking a console on the website still ends it.

Code: `3ds_port/src/3ds_online_crypto.c` (SHA-256, HMAC, TweetNaCl's X25519) and
`tools/sprite_server/signing.py` (the same in Python); `make verify-online` checks them against
RFC 7748/4231 vectors and each other. The standard server is `http://poke.cloudnoud.nl`; an
`https://` server still works as before (TLS with GlobalSign pinned, plus the signatures), and a
bare host name typed on the console means `http://`. TEST SERVER shows `HTTP OK`, `HEALTH OK
API 2` and `LINKED AS <name>` when a signed request and its signed answer went through.

### 7.7 Round six (2026-10-15, after TEST SERVER passed on the New 3DS)

- **Votes**: every row of the versions screen has a see-through VOTE button (gold, a star and
  VOTED once given); A votes for the row under the cursor too. Vote for as many versions as you
  like, up to 16 a picture; a front and its back are voted on apart. Lists, on the console and
  the website, show the most votes first (among equals the newest). online_votes.txt keeps any
  number per line (`key pending v1 v2 ...`); old two-column lines still read.
- **Closing the versions screen** (CLOSE, B or START) goes back where it was opened from: the
  Pokédex (its X or an entry's globe) or the editor (the globe in its bar).
- **The editor's Pokédex no longer flickers**: the bottom screen's Pokémon icon cache held six
  icons (the party's), the page shows ten, so every frame loaded one and dropped another. It
  now holds 40.
- **The build is "Pokémon Emerald 3D Editor"**, "Stereoscopic sprites and editor by darkmatter" (HOME menu / Homebrew
  Launcher), with a globe-and-pencil icon; the title screen's bottom says that credit
  above the version.
- Test aids for online_test.txt: `verscreen` opens the versions screen, `shot NAME` and
  `bottomshot NAME` write the editor's or the bottom screen's canvas to `shot-NAME.bin`.

### 7.8 Round seven (2026-10-15)

- **YOUR NAME** moved from the editor's options to OPTIONS, ONLINE: the author's name that signs
  the edits, typed with the system keyboard. Once linked it is also the name the site shows for
  the account (`POST /api/me {"name"}`, users.display_name; the Account page can set it too).
  Sent now, or after the job that runs; linking sends the card's name.
- A **refresh** button (Pixelarticons, 24 px) at the STATUS row's right end: a tap there
  fetches what is new. (Icons on the other cells were tried and taken out again: too big.)
- **The editor's Pokédex**: "YOUR EDIT, FINAL" became YOU, centred under the Pokémon's icon
  (moved up in its row) on a half-white tag, and a small flag left of the globe once FINAL. The
  EDITED filter is the "mine" filter.
- **The editor**: its bar reads Pokédex, globe, settings; the settings have **COLLAPSE TO 1
  LAYER** (every plane merged into plane 0, the farthest, in every frame; UNDO takes it back).
- **Website**: the sign-in form is centred; the Users page starts with Add a user. Accounts
  made: darkmatter (admin, the master account) and Nukularkoffee (artist).
- Test aid: `optionstab N` shows the OPTIONS screen on tab N (2 is ONLINE), even from the title
  screen, for `bottomshot`.

## 8. Open questions and risks

| Item | Status |
|---|---|
| Does the console complete a TLS handshake with a modern nginx? Which protocols/ciphers? | **Open**; the first hardware test. Settle §3's choice from its result (`port.log` gets the `Result` codes; `0xD8A0A03C` is "TLS verify failed"). |
| Does `httpcAddTrustedRootCA` accept a 2048-bit RSA self-signed root in DER? | Open; FBI never uses it, only documented on 3dbrew. Fallback is `SSLCOPT_DisableVerify` + request signing. |
| Azahar's `http:C` is reimplemented (`src/core/hle/service/http/http_c.cpp`: Open/Begin/ReceiveData/AddPostDataRaw/GetResponseHeader/SetSSLOpt all present; `RootCertChainAddDefaultCert` still a stub). | Good enough to develop the client flow in the emulator against a local server; TLS and camera only on hardware. |
| Wi-Fi in the field while the game runs: does the port's heavy GPU work leave CPU for a blocking HTTP thread on core 1? | Expect yes (transfers are tiny); measure frame time during a sync. |
| Clock: the 3DS clock is user-set; use server time in `/api/sync?since=`, never the console's. | Design decision, done. |
| Abuse: tokens are per device and revocable; uploads are capped (e.g. 20/min, 16 KiB) and validated against the file format. | Design decision, done. |

## 9. Suggested order of work

1. **Hardware TLS probe** (half a day): a throwaway `.3dsx` (the official `http_post` example
   plus `httpcAddTrustedRootCA`) against the real server's host name; log the results.
   Decides §3.
2. **Server v1** (a day or two): users, devices, pairing, upload, current, sync, the progress
   grid. SQLite file, systemd unit, nginx vhost.
3. **Console client** (two days): `3ds_online.c`, pairing by code, upload from the editor,
   sync from the gallery, status text. Develop in Azahar against the local server.
4. **Web voting UI + previews** (a day): Pokémon page with the versions, votes, pin.
5. **PC client script**, then the **QR pairing** and on-device vote button if wanted.

## Sources

- libctru `httpc.h`, `sslc.h`, `soc.h` (devkitPro/libctru, master) and the official
  `3ds-examples/network/http_post/source/main.c`.
- 3dbrew: SSL Services (TLS versions, root chains, SSLOpt flags), HTTPC services.
- FBI (Steveice10): `source/core/http.c` (httpc with `SSLCOPT_DisableVerify`, libcurl
  fallback on `0xD8A0A03C`, manual redirects, 15 s timeouts), `source/core/task/capturecam.c`
  (camera setup), `source/libs/quirc` (QR decoding).
- Universal-Updater: builds on `3ds-curl` (devkitPro pacman) instead of `http:C`.
- Azahar: `src/core/hle/service/http/http_c.cpp`, `soc/soc_u.cpp` (which commands are emulated).
