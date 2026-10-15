/*
 * 3ds_online_crypto.c on the build machine (make verify-online): RFC 7748's
 * X25519 vectors, FIPS 180-2's and RFC 4231's for SHA-256 and HMAC, and a
 * command line that tests/online_crypto_test.py compares with Python
 * (tools/sprite_server/signing.py):
 *   online_crypto_test x25519 <scalar hex> <point hex>
 *   online_crypto_test hmac <key hex> <message>
 *   online_crypto_test sha <message>
 */
#include <stdio.h>
#include <string.h>
#include "3ds_online_crypto.h"

static int sFailed;

static void Expect(const char *what, const char *got, const char *want)
{
    if (strcmp(got, want) != 0)
    {
        printf("FAIL %s\n  got  %s\n  want %s\n", what, got, want);
        sFailed = 1;
    }
}

static int SelfTest(void)
{
    uint8_t a[32], b[32], out[32], mac[32], key[131];
    char hex[65];
    CtrHmac m;
    CtrSha256 s;

    /* RFC 7748 5.2. */
    CtrUnhex("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4", a, 32);
    CtrUnhex("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c", b, 32);
    CtrX25519(out, a, b);
    CtrHex(out, 32, hex);
    Expect("x25519 vector 1", hex, "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552");
    /* RFC 7748 6.1: Alice's public key and the shared secret. */
    CtrUnhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", a, 32);
    CtrX25519_Public(out, a);
    CtrHex(out, 32, hex);
    Expect("x25519 public", hex, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
    CtrUnhex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", b, 32);
    CtrX25519(out, a, b);
    CtrHex(out, 32, hex);
    Expect("x25519 shared", hex, "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");
    /* SHA-256 "abc", and a million 'a' fed in odd pieces. */
    CtrSha256_Digest("abc", 3, out);
    CtrHex(out, 32, hex);
    Expect("sha abc", hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CtrSha256_Init(&s);
    memset(key, 'a', sizeof(key));
    for (unsigned n = 0; n < 1000000;)
    {
        unsigned piece = 1000000 - n < 131 ? 1000000 - n : 1 + n % 131;

        CtrSha256_Update(&s, key, piece);
        n += piece;
    }
    CtrSha256_Final(&s, out);
    CtrHex(out, 32, hex);
    Expect("sha million", hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    /* RFC 4231 cases 2 and 6 (a key longer than a block). */
    CtrHmac_Init(&m, (const uint8_t *)"Jefe", 4);
    CtrHmac_Update(&m, "what do ya want for nothing?", 28);
    CtrHmac_Final(&m, mac);
    CtrHex(mac, 32, hex);
    Expect("hmac case 2", hex, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    memset(key, 0xaa, sizeof(key));
    CtrHmac_Init(&m, key, 131);
    CtrHmac_Update(&m, "Test Using Larger Than Block-Size Key - Hash Key First", 54);
    CtrHmac_Final(&m, mac);
    CtrHex(mac, 32, hex);
    Expect("hmac case 6", hex, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
    printf(sFailed ? "online crypto: FAILED\n" : "online crypto: all vectors pass\n");
    return sFailed;
}

int main(int argc, char **argv)
{
    uint8_t a[32], b[32], out[32], key[256];
    char hex[65];
    CtrHmac m;

    if (argc == 1) return SelfTest();
    if (argc == 4 && strcmp(argv[1], "x25519") == 0 && CtrUnhex(argv[2], a, 32) && CtrUnhex(argv[3], b, 32))
        CtrX25519(out, a, b);
    else if (argc == 4 && strcmp(argv[1], "hmac") == 0 && strlen(argv[2]) <= 2 * sizeof(key)
             && CtrUnhex(argv[2], key, (unsigned)strlen(argv[2]) / 2))
    {
        CtrHmac_Init(&m, key, (uint32_t)strlen(argv[2]) / 2);
        CtrHmac_Update(&m, argv[3], (uint32_t)strlen(argv[3]));
        CtrHmac_Final(&m, out);
    }
    else if (argc == 3 && strcmp(argv[1], "sha") == 0)
        CtrSha256_Digest(argv[2], (uint32_t)strlen(argv[2]), out);
    else
    {
        fprintf(stderr, "usage: online_crypto_test [x25519 S P | hmac K M | sha M]\n");
        return 2;
    }
    CtrHex(out, 32, hex);
    puts(hex);
    return 0;
}
