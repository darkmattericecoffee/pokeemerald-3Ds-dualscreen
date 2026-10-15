/*
 * SHA-256, HMAC-SHA256 and X25519 for plane sharing (3ds_online_crypto.h).
 * The X25519 is TweetNaCl's (public domain): small and constant-time, slow
 * but run once, when a console is linked.
 */
#include <stdio.h>
#include <string.h>
#include "3ds_online_crypto.h"

/* ── SHA-256 ─────────────────────────────────────────────────────────── */

static const uint32_t sK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void ShaBlock(CtrSha256 *s, const uint8_t *p)
{
    uint32_t w[64], a, b, c, d, e, f, g, h;

    for (unsigned i = 0; i < 16; ++i)
        w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) | ((uint32_t)p[4 * i + 2] << 8) | p[4 * i + 3];
    for (unsigned i = 16; i < 64; ++i)
    {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);

        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (unsigned i = 0; i < 64; ++i)
    {
        uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + sK[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));

        h = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
    }
    s->h[0] += a, s->h[1] += b, s->h[2] += c, s->h[3] += d, s->h[4] += e, s->h[5] += f, s->h[6] += g, s->h[7] += h;
}

void CtrSha256_Init(CtrSha256 *s)
{
    static const uint32_t start[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    memcpy(s->h, start, sizeof(start));
    s->length = 0;
    s->used = 0;
}

void CtrSha256_Update(CtrSha256 *s, const void *data, uint32_t size)
{
    const uint8_t *p = data;

    s->length += (uint64_t)size * 8;
    while (size)
    {
        unsigned n = 64 - s->used;

        if (s->used == 0 && size >= 64)
        {
            ShaBlock(s, p);
            p += 64, size -= 64;
            continue;
        }
        if (n > size) n = size;
        memcpy(s->block + s->used, p, n);
        s->used += n, p += n, size -= n;
        if (s->used == 64)
        {
            ShaBlock(s, s->block);
            s->used = 0;
        }
    }
}

void CtrSha256_Final(CtrSha256 *s, uint8_t digest[32])
{
    s->block[s->used++] = 0x80;
    if (s->used > 56)
    {
        memset(s->block + s->used, 0, 64 - s->used);
        ShaBlock(s, s->block);
        s->used = 0;
    }
    memset(s->block + s->used, 0, 56 - s->used);
    for (unsigned i = 0; i < 8; ++i) s->block[56 + i] = (uint8_t)(s->length >> (56 - 8 * i));
    ShaBlock(s, s->block);
    for (unsigned i = 0; i < 8; ++i)
    {
        digest[4 * i] = (uint8_t)(s->h[i] >> 24);
        digest[4 * i + 1] = (uint8_t)(s->h[i] >> 16);
        digest[4 * i + 2] = (uint8_t)(s->h[i] >> 8);
        digest[4 * i + 3] = (uint8_t)s->h[i];
    }
}

void CtrSha256_Digest(const void *data, uint32_t size, uint8_t digest[32])
{
    CtrSha256 s;

    CtrSha256_Init(&s);
    CtrSha256_Update(&s, data, size);
    CtrSha256_Final(&s, digest);
}

/* ── HMAC-SHA256 ─────────────────────────────────────────────────────── */

void CtrHmac_Init(CtrHmac *m, const uint8_t *key, uint32_t keySize)
{
    uint8_t block[64], hashed[32];

    if (keySize > 64)
    {
        CtrSha256_Digest(key, keySize, hashed);
        key = hashed, keySize = 32;
    }
    memset(block, 0, sizeof(block));
    memcpy(block, key, keySize);
    for (unsigned i = 0; i < 64; ++i) block[i] ^= 0x36;
    CtrSha256_Init(&m->inner);
    CtrSha256_Update(&m->inner, block, 64);
    for (unsigned i = 0; i < 64; ++i) block[i] ^= 0x36 ^ 0x5c;
    CtrSha256_Init(&m->outer);
    CtrSha256_Update(&m->outer, block, 64);
}

void CtrHmac_Update(CtrHmac *m, const void *data, uint32_t size) { CtrSha256_Update(&m->inner, data, size); }

void CtrHmac_Final(CtrHmac *m, uint8_t mac[32])
{
    uint8_t inner[32];

    CtrSha256_Final(&m->inner, inner);
    CtrSha256_Update(&m->outer, inner, 32);
    CtrSha256_Final(&m->outer, mac);
}

void CtrHex(const uint8_t *data, unsigned size, char *hex)
{
    static const char digits[] = "0123456789abcdef";

    for (unsigned i = 0; i < size; ++i)
    {
        hex[2 * i] = digits[data[i] >> 4];
        hex[2 * i + 1] = digits[data[i] & 15];
    }
    hex[2 * size] = 0;
}

static int Nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int CtrUnhex(const char *hex, uint8_t *out, unsigned size)
{
    if (strlen(hex) != 2 * size) return 0;
    for (unsigned i = 0; i < size; ++i)
    {
        int hi = Nibble(hex[2 * i]), lo = Nibble(hex[2 * i + 1]);

        if (hi < 0 || lo < 0) return 0;
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return 1;
}

/* ── X25519 (TweetNaCl) ──────────────────────────────────────────────── */

typedef int64_t Gf[16];

static const Gf s121665 = {0xDB41, 1};

static void Carry(Gf o)
{
    for (int i = 0; i < 16; ++i)
    {
        int64_t c;

        o[i] += (int64_t)1 << 16;
        c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c * ((int64_t)1 << 16);
    }
}

static void Select(Gf p, Gf q, int b)
{
    int64_t c = ~(int64_t)(b - 1);

    for (int i = 0; i < 16; ++i)
    {
        int64_t t = c & (p[i] ^ q[i]);

        p[i] ^= t;
        q[i] ^= t;
    }
}

static void Pack(uint8_t *o, const Gf n)
{
    Gf m, t;

    memcpy(t, n, sizeof(Gf));
    Carry(t);
    Carry(t);
    Carry(t);
    for (int j = 0; j < 2; ++j)
    {
        int b;

        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; ++i)
        {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        Select(t, m, 1 - b);
    }
    for (int i = 0; i < 16; ++i)
    {
        o[2 * i] = (uint8_t)(t[i] & 0xff);
        o[2 * i + 1] = (uint8_t)(t[i] >> 8);
    }
}

static void Unpack(Gf o, const uint8_t *n)
{
    for (int i = 0; i < 16; ++i) o[i] = n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
    o[15] &= 0x7fff;
}

static void Add(Gf o, const Gf a, const Gf b)
{
    for (int i = 0; i < 16; ++i) o[i] = a[i] + b[i];
}

static void Sub(Gf o, const Gf a, const Gf b)
{
    for (int i = 0; i < 16; ++i) o[i] = a[i] - b[i];
}

static void Mul(Gf o, const Gf a, const Gf b)
{
    int64_t t[31];

    memset(t, 0, sizeof(t));
    for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 16; ++j) t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; ++i) t[i] += 38 * t[i + 16];
    memcpy(o, t, sizeof(Gf));
    Carry(o);
    Carry(o);
}

static void Square(Gf o, const Gf a) { Mul(o, a, a); }

static void Invert(Gf o, const Gf in)
{
    Gf c;

    memcpy(c, in, sizeof(Gf));
    for (int a = 253; a >= 0; --a)
    {
        Square(c, c);
        if (a != 2 && a != 4) Mul(c, c, in);
    }
    memcpy(o, c, sizeof(Gf));
}

void CtrX25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32])
{
    uint8_t z[32];
    Gf x, a, b, c, d, e, f;

    memcpy(z, scalar, 32);
    z[31] = (uint8_t)((z[31] & 127) | 64);
    z[0] &= 248;
    Unpack(x, point);
    memcpy(b, x, sizeof(Gf));
    memset(a, 0, sizeof(Gf));
    memset(c, 0, sizeof(Gf));
    memset(d, 0, sizeof(Gf));
    a[0] = d[0] = 1;
    for (int i = 254; i >= 0; --i)
    {
        int r = (z[i >> 3] >> (i & 7)) & 1;

        Select(a, b, r);
        Select(c, d, r);
        Add(e, a, c);
        Sub(a, a, c);
        Add(c, b, d);
        Sub(b, b, d);
        Square(d, e);
        Square(f, a);
        Mul(a, c, a);
        Mul(c, b, e);
        Add(e, a, c);
        Sub(a, a, c);
        Square(b, a);
        Sub(c, d, f);
        Mul(a, c, s121665);
        Add(a, a, d);
        Mul(c, c, a);
        Mul(a, d, f);
        Mul(d, b, x);
        Square(b, e);
        Select(a, b, r);
        Select(c, d, r);
    }
    Invert(c, c);
    Mul(a, a, c);
    Pack(out, a);
}

void CtrX25519_Public(uint8_t pub[32], const uint8_t secret[32])
{
    static const uint8_t nine[32] = {9};

    CtrX25519(pub, secret, nine);
}
