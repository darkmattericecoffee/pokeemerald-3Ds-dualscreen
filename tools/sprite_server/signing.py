"""Signed plain HTTP for consoles (docs/ONLINE_SPRITE_SHARING.md section 7.6).

The 3DS cannot do the TLS that a free Cloudflare zone serves (ECDSA only), so
consoles talk plain HTTP and nothing secret ever travels:

- Linking: the console sends an X25519 public key and an HMAC of it made with
  the pairing code (the code itself is not sent). The server answers with its
  own public key; both derive the device token from the shared secret, and a
  confirmation MAC shows the console that the server knew the code.
- Every request carries the device's key id, the server's time, a nonce and an
  HMAC over method, path, time, nonce and the body's SHA-256. The key is
  SHA-256 of the token, which is what the database keeps (devices.token_hash),
  so consoles linked before this keep working.
- Every answer to a signed request carries an HMAC over the request's MAC, the
  status and the body's SHA-256: the console trusts nothing unsigned.

The console's side is 3ds_port/src/3ds_online.c with 3ds_online_crypto.c.
Standard library only (X25519 from RFC 7748, in Python integers).
"""
import hashlib
import hmac
import secrets
from typing import Tuple

KEY_HEADER = "X-Em3ds-Key"
TIME_HEADER = "X-Em3ds-Time"
NONCE_HEADER = "X-Em3ds-Nonce"
SIGN_HEADER = "X-Em3ds-Sign"
CLOCK_WINDOW = 300  # seconds a request's time may differ from the server's


# ── X25519 (RFC 7748) ───────────────────────────────────────────────────────

_P = 2 ** 255 - 19
_A24 = 121665


def _clamp(k: bytes) -> int:
    b = bytearray(k)
    b[0] &= 248
    b[31] = (b[31] & 127) | 64
    return int.from_bytes(bytes(b), "little")


def x25519(scalar: bytes, point: bytes) -> bytes:
    k = _clamp(scalar)
    x1 = int.from_bytes(point, "little") & ((1 << 255) - 1)
    x2, z2, x3, z3, swap = 1, 0, x1, 1, 0
    for t in reversed(range(255)):
        bit = (k >> t) & 1
        swap ^= bit
        if swap:
            x2, x3, z2, z3 = x3, x2, z3, z2
        swap = bit
        a, b = (x2 + z2) % _P, (x2 - z2) % _P
        aa, bb = a * a % _P, b * b % _P
        e = (aa - bb) % _P
        c, d = (x3 + z3) % _P, (x3 - z3) % _P
        da, cb = d * a % _P, c * b % _P
        x3, z3 = (da + cb) ** 2 % _P, x1 * (da - cb) ** 2 % _P
        x2, z2 = aa * bb % _P, e * (aa + _A24 * e) % _P
    if swap:
        x2, z2 = x3, z3
    return (x2 * pow(z2, _P - 2, _P) % _P).to_bytes(32, "little")


def x25519_keypair() -> Tuple[bytes, bytes]:
    secret = secrets.token_bytes(32)
    return secret, x25519(secret, (9).to_bytes(32, "little"))


# ── The messages ────────────────────────────────────────────────────────────

def _mac(key: bytes, text: str) -> str:
    return hmac.new(key, text.encode(), hashlib.sha256).hexdigest()


def device_key(token_hash_hex: str) -> bytes:
    """The signing key: SHA-256 of the token, as devices.token_hash keeps it."""
    return bytes.fromhex(token_hash_hex)


def key_id(key: bytes) -> str:
    return hashlib.sha256(b"EM3DS kid\n" + key).hexdigest()[:16]


def request_mac(key: bytes, method: str, target: str, when: str, nonce: str, body: bytes) -> str:
    return _mac(key, "EM3DS1\n%s\n%s\n%s\n%s\n%s" % (method, target, when, nonce, hashlib.sha256(body).hexdigest()))


def reply_mac(key: bytes, request_sign: str, status: int, body: bytes) -> str:
    return _mac(key, "EM3DS1 reply\n%s\n%d\n%s" % (request_sign, status, hashlib.sha256(body).hexdigest()))


def pair_proof(code_hash_hex: str, pub_hex: str) -> str:
    """What shows the server the console has the code: keyed with SHA-256 of
    the (normalised) code, which is what pairing_codes.code_hash keeps."""
    return _mac(bytes.fromhex(code_hash_hex), "EM3DS pair\n" + pub_hex)


def pair_token(shared: bytes, console_pub: str, server_pub: str, code_hash_hex: str) -> str:
    return _mac(shared, "EM3DS token\n%s\n%s\n%s" % (console_pub, server_pub, code_hash_hex))


def pair_confirm(token: str, console_pub: str, server_pub: str, user: str) -> str:
    return _mac(hashlib.sha256(token.encode()).digest(), "EM3DS paired\n%s\n%s\n%s" % (console_pub, server_pub, user))
