/* SHA-1 implementation based on RFC 3174 (public domain) */
#include "appimagetool.h"

static uint32_t sha1_rol(uint32_t value, uint32_t bits)
{
    return (value << bits) | (value >> (32 - bits));
}

static void sha1_transform(uint32_t state[5], const uint8_t block[64])
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8)  |
               ((uint32_t)block[i * 4 + 3]);
    for (int i = 16; i < 80; i++)
        w[i] = sha1_rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    uint32_t a = state[0], b = state[1], c = state[2],
             d = state[3], e = state[4];

    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)       { f = (b & c) | (~b & d); k = 0x5a827999; }
        else if (i < 40)  { f = b ^ c ^ d;          k = 0x6ed9eba1; }
        else if (i < 60)  { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
        else              { f = b ^ c ^ d;          k = 0xca62c1d6; }
        uint32_t temp = sha1_rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = sha1_rol(b, 30); b = a; a = temp;
    }

    state[0] += a; state[1] += b; state[2] += c;
    state[3] += d; state[4] += e;
}

void sha1_init(SHA1_CTX *ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xc3d2e1f0;
    ctx->count = 0;
}

void sha1_update(SHA1_CTX *ctx, const uint8_t *data, size_t len)
{
    size_t idx = (size_t)(ctx->count & 63);
    ctx->count += len;
    size_t part = 64 - idx;
    if (len >= part) {
        memcpy(ctx->buffer + idx, data, part);
        sha1_transform(ctx->state, ctx->buffer);
        for (data += part, len -= part; len >= 64; data += 64, len -= 64)
            sha1_transform(ctx->state, data);
        idx = 0;
    }
    if (len > 0)
        memcpy(ctx->buffer + idx, data, len);
}

void sha1_final(SHA1_CTX *ctx, uint8_t digest[SHA1_DIGEST_SIZE])
{
    uint64_t bits = ctx->count * 8;
    size_t idx = (size_t)(ctx->count & 63);
    ctx->buffer[idx++] = 0x80;
    if (idx > 56) {
        memset(ctx->buffer + idx, 0, 64 - idx);
        sha1_transform(ctx->state, ctx->buffer);
        idx = 0;
    }
    memset(ctx->buffer + idx, 0, 56 - idx);
    for (int i = 0; i < 8; i++)
        ctx->buffer[56 + i] = (uint8_t)(bits >> (56 - i * 8));
    sha1_transform(ctx->state, ctx->buffer);
    for (int i = 0; i < 5; i++) {
        digest[i * 4]     = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}
