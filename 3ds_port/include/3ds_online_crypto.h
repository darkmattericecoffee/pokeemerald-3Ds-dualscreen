#ifndef CTR_ONLINE_CRYPTO_H
#define CTR_ONLINE_CRYPTO_H

/*
 * The little cryptography plane sharing needs over plain HTTP (3ds_online.c,
 * docs/ONLINE_SPRITE_SHARING.md section 7.6): SHA-256, HMAC-SHA256 to sign
 * requests and check answers, and X25519 to agree on a console's key when it
 * is linked. Plain C without the SDK, so tests/online_crypto_test.c checks it
 * on the build machine against Python (tools/sprite_server/signing.py).
 */
#include <stdint.h>

typedef struct
{
    uint32_t h[8];
    uint64_t length;
    uint8_t block[64];
    unsigned used;
} CtrSha256;

void CtrSha256_Init(CtrSha256 *s);
void CtrSha256_Update(CtrSha256 *s, const void *data, uint32_t size);
void CtrSha256_Final(CtrSha256 *s, uint8_t digest[32]);
void CtrSha256_Digest(const void *data, uint32_t size, uint8_t digest[32]);

typedef struct
{
    CtrSha256 inner, outer;
} CtrHmac;

void CtrHmac_Init(CtrHmac *m, const uint8_t *key, uint32_t keySize);
void CtrHmac_Update(CtrHmac *m, const void *data, uint32_t size);
void CtrHmac_Final(CtrHmac *m, uint8_t mac[32]);

/* `size` bytes as lower-case hex (2 * size + 1 characters with the NUL). */
void CtrHex(const uint8_t *data, unsigned size, char *hex);
/* false unless `hex` is exactly 2 * size hex digits. */
int CtrUnhex(const char *hex, uint8_t *out, unsigned size);

/* X25519 (RFC 7748): out = scalar * point; the public key is scalar * 9. */
void CtrX25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]);
void CtrX25519_Public(uint8_t pub[32], const uint8_t secret[32]);

#endif
