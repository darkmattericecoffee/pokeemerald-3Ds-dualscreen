"""3ds_online_crypto.c against tools/sprite_server/signing.py (make verify-online).

Usage: online_crypto_test.py <online_crypto_test.exe>
Random X25519 exchanges and HMACs must come out the same on both sides, and
signing.py's X25519 must pass RFC 7748's vectors.
"""
import hashlib
import hmac
import os
import secrets
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools", "sprite_server"))
import signing  # noqa: E402


def run(exe, *args):
    return subprocess.run([exe] + list(args), check=True, capture_output=True, text=True).stdout.strip()


def main():
    exe = sys.argv[1]
    v = signing.x25519(bytes.fromhex("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4"),
                       bytes.fromhex("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c"))
    assert v.hex() == "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552", v.hex()
    for _ in range(20):
        a, apub = signing.x25519_keypair()
        b, bpub = signing.x25519_keypair()
        shared = signing.x25519(a, bpub)
        assert shared == signing.x25519(b, apub)
        assert run(exe, "x25519", b.hex(), apub.hex()) == shared.hex()
        assert run(exe, "x25519", a.hex(), (9).to_bytes(32, "little").hex()) == apub.hex()
        key = secrets.token_bytes(32)
        msg = "EM3DS1\nGET\n/api/sync?since=%d\n1760000000\n%s\n%s" % (
            secrets.randbelow(999), secrets.token_hex(8), hashlib.sha256(b"").hexdigest())
        assert run(exe, "hmac", key.hex(), msg) == hmac.new(key, msg.encode(), hashlib.sha256).hexdigest()
        assert run(exe, "sha", msg) == hashlib.sha256(msg.encode()).hexdigest()
    print("online crypto: C and Python agree")


if __name__ == "__main__":
    main()
