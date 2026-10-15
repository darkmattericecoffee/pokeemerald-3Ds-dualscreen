# tlsprobe

Throwaway hardware probe for docs/ONLINE_SPRITE_SHARING.md §3: can this console's
`http:C` module talk HTTPS to our server, and with which certificate setting?

Build: `tools/tlsprobe/build.sh` (devkitARM container) → `tools/tlsprobe/tlsprobe.3dsx`.

On the SD card:

- `sdmc:/3ds/tlsprobe.3dsx` — the app; start it from the Homebrew Launcher.
- `sdmc:/3ds/tlsprobe/urls.txt` — optional, one URL per line, `#` comments. Without it
  the probe tries letsencrypt.org, digicert.com and httpbin.org.
- `sdmc:/3ds/tlsprobe/root.der` — optional extra root certificate (DER), e.g. your own CA.
  `openssl x509 -in ca.pem -outform der -out root.der`.

Each URL is tried five ways: default verification, plus ISRG Root X1 (built in from
RomFS), plus root.der, verification off, and plain http://. Every line goes to the top
screen and is appended to `sdmc:/3ds/tlsprobe/result.txt`. START exits.

Reading the result: `OK status 200` means that setting works. A failed `begin` prints a
result code; `D8A0A03C` is the TLS verify failure FBI falls back on. If only "verify OFF"
works, the handshake is fine and the root store is the problem. If every https line fails
but plain http works, the server's TLS protocols or ciphers are too new for the console.
