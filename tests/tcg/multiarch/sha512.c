/*
 * sha512 test based on CCAN: https://ccodearchive.net/info/crypto/sha512.html
 *
 * src/crypto/sha512.cpp commit f914f1a746d7f91951c1da262a4a749dd3ebfa71
 * Copyright (c) 2014 The Bitcoin Core developers
 * Distributed under the MIT software license, see:
 *  http://www.opensource.org/licenses/mit-license.php.
 *
 * SPDX-License-Identifier: MIT CC0-1.0
 */
#define _GNU_SOURCE         /* See feature_test_macros(7) */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <stdarg.h>

/* Required portions from endian.h */

/**
 * BSWAP_64 - reverse bytes in a constant uint64_t value.
 * @val: constantvalue whose bytes to swap.
 *
 * Designed to be usable in constant-requiring initializers.
 *
 * Example:
 *  struct mystruct {
 *      char buf[BSWAP_64(0xff00000000000000ULL)];
 *  };
 */
#define BSWAP_64(val)                       \
    ((((uint64_t)(val) & 0x00000000000000ffULL) << 56)  \
     | (((uint64_t)(val) & 0x000000000000ff00ULL) << 40)    \
     | (((uint64_t)(val) & 0x0000000000ff0000ULL) << 24)    \
     | (((uint64_t)(val) & 0x00000000ff000000ULL) << 8)     \
     | (((uint64_t)(val) & 0x000000ff00000000ULL) >> 8)     \
     | (((uint64_t)(val) & 0x0000ff0000000000ULL) >> 24)    \
     | (((uint64_t)(val) & 0x00ff000000000000ULL) >> 40)    \
     | (((uint64_t)(val) & 0xff00000000000000ULL) >> 56))


typedef uint64_t beint64_t;

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

/**
 * CPU_TO_BE64 - convert a constant uint64_t value to big-endian
 * @native: constant to convert
 */
#define CPU_TO_BE64(native) ((beint64_t)(native))
/**
 * BE64_TO_CPU - convert a big-endian uint64_t constant
 * @le_val: big-endian constant to convert
 */
#define BE64_TO_CPU(le_val) ((uint64_t)(le_val))

#else /* ... HAVE_LITTLE_ENDIAN */
#define CPU_TO_BE64(native) ((beint64_t)BSWAP_64(native))
#define BE64_TO_CPU(le_val) BSWAP_64((uint64_t)le_val)
#endif /* HAVE_LITTE_ENDIAN */

/**
 * cpu_to_be64 - convert a uint64_t value to big endian.
 * @native: value to convert
 */
static inline beint64_t cpu_to_be64(uint64_t native)
{
    return CPU_TO_BE64(native);
}

/**
 * be64_to_cpu - convert a big-endian uint64_t value
 * @be_val: big-endian value to convert
 */
static inline uint64_t be64_to_cpu(beint64_t be_val)
{
    return BE64_TO_CPU(be_val);
}

/* From compiler.h */

#ifndef UNUSED
/**
 * UNUSED - a parameter is unused
 *
 * Some compilers (eg. gcc with -W or -Wunused) warn about unused
 * function parameters.  This suppresses such warnings and indicates
 * to the reader that it's deliberate.
 *
 * Example:
 *  // This is used as a callback, so needs to have this prototype.
 *  static int some_callback(void *unused UNUSED)
 *  {
 *      return 0;
 *  }
 */
#define UNUSED __attribute__((__unused__))
#endif

/* From sha512.h */

/**
 * struct sha512 - structure representing a completed SHA512.
 * @u.u8: an unsigned char array.
 * @u.u64: a 64-bit integer array.
 *
 * Other fields may be added to the union in future.
 */
struct sha512 {
    union {
        uint64_t u64[8];
        unsigned char u8[64];
    } u;
};

/**
 * sha512 - return sha512 of an object.
 * @sha512: the sha512 to fill in
 * @p: pointer to memory,
 * @size: the number of bytes pointed to by @p
 *
 * The bytes pointed to by @p is SHA512 hashed into @sha512.  This is
 * equivalent to sha512_init(), sha512_update() then sha512_done().
 */
void sha512(struct sha512 *sha, const void *p, size_t size);

/**
 * struct sha512_ctx - structure to store running context for sha512
 */
struct sha512_ctx {
    uint64_t s[8];
    union {
        uint64_t u64[16];
        unsigned char u8[128];
    } buf;
    size_t bytes;
};

/**
 * sha512_init - initialize an SHA512 context.
 * @ctx: the sha512_ctx to initialize
 *
 * This must be called before sha512_update or sha512_done, or
 * alternately you can assign SHA512_INIT.
 *
 * If it was already initialized, this forgets anything which was
 * hashed before.
 *
 * Example:
 * static void hash_all(const char **arr, struct sha512 *hash)
 * {
 *  size_t i;
 *  struct sha512_ctx ctx;
 *
 *  sha512_init(&ctx);
 *  for (i = 0; arr[i]; i++)
 *      sha512_update(&ctx, arr[i], strlen(arr[i]));
 *  sha512_done(&ctx, hash);
 * }
 */
void sha512_init(struct sha512_ctx *ctx);

/**
 * SHA512_INIT - initializer for an SHA512 context.
 *
 * This can be used to statically initialize an SHA512 context (instead
 * of sha512_init()).
 *
 * Example:
 * static void hash_all(const char **arr, struct sha512 *hash)
 * {
 *  size_t i;
 *  struct sha512_ctx ctx = SHA512_INIT;
 *
 *  for (i = 0; arr[i]; i++)
 *      sha512_update(&ctx, arr[i], strlen(arr[i]));
 *  sha512_done(&ctx, hash);
 * }
 */
#define SHA512_INIT                                         \
    { { 0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull,   \
        0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,   \
        0x510e527fade682d1ull, 0x9b05688c2b3e6c1full,   \
        0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull }, \
      { { 0 } }, 0 }

/**
 * sha512_update - include some memory in the hash.
 * @ctx: the sha512_ctx to use
 * @p: pointer to memory,
 * @size: the number of bytes pointed to by @p
 *
 * You can call this multiple times to hash more data, before calling
 * sha512_done().
 */
void sha512_update(struct sha512_ctx *ctx, const void *p, size_t size);

/**
 * sha512_done - finish SHA512 and return the hash
 * @ctx: the sha512_ctx to complete
 * @res: the hash to return.
 *
 * Note that @ctx is *destroyed* by this, and must be reinitialized.
 * To avoid that, pass a copy instead.
 */
void sha512_done(struct sha512_ctx *sha512, struct sha512 *res);

/* From sha512.c */

/*
 * SHA512 core code translated from the Bitcoin project's C++:
 *
 * src/crypto/sha512.cpp commit f914f1a746d7f91951c1da262a4a749dd3ebfa71
 * Copyright (c) 2014 The Bitcoin Core developers
 * Distributed under the MIT software license, see the accompanying
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.
 */
/* #include <ccan/endian/endian.h> */
/* #include <ccan/compiler/compiler.h> */
#include <stdbool.h>
#include <assert.h>
#include <string.h>

static void invalidate_sha512(struct sha512_ctx *ctx)
{
    ctx->bytes = (size_t)-1;
}

static void check_sha512(struct sha512_ctx *ctx UNUSED)
{
    assert(ctx->bytes != (size_t)-1);
}

static uint64_t Ch(uint64_t x, uint64_t y, uint64_t z)
{
    return z ^ (x & (y ^ z));
}
static uint64_t Maj(uint64_t x, uint64_t y, uint64_t z)
{
    return (x & y) | (z & (x | y));
}
static uint64_t Sigma0(uint64_t x)
{
    return (x >> 28 | x << 36) ^ (x >> 34 | x << 30) ^ (x >> 39 | x << 25);
}
static uint64_t Sigma1(uint64_t x)
{
    return (x >> 14 | x << 50) ^ (x >> 18 | x << 46) ^ (x >> 41 | x << 23);
}
static uint64_t sigma0(uint64_t x)
{
    return (x >> 1 | x << 63) ^ (x >> 8 | x << 56) ^ (x >> 7);
}
static uint64_t sigma1(uint64_t x)
{
    return (x >> 19 | x << 45) ^ (x >> 61 | x << 3) ^ (x >> 6);
}

/** One round of SHA-512. */
static void Round(uint64_t a, uint64_t b, uint64_t c, uint64_t *d, uint64_t e, uint64_t f, uint64_t g, uint64_t *h, uint64_t k, uint64_t w)
{
    uint64_t t1 = *h + Sigma1(e) + Ch(e, f, g) + k + w;
    uint64_t t2 = Sigma0(a) + Maj(a, b, c);
    *d += t1;
    *h = t1 + t2;
}

/** Perform one SHA-512 transformation, processing a 128-byte chunk. */
static void Transform(uint64_t *s, const uint64_t *chunk)
{
    uint64_t a = s[0], b = s[1], c = s[2], d = s[3], e = s[4], f = s[5], g = s[6], h = s[7];
    uint64_t w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10, w11, w12, w13, w14, w15;

    Round(a, b, c, &d, e, f, g, &h, 0x428a2f98d728ae22ull, w0 = be64_to_cpu(chunk[0]));
    Round(h, a, b, &c, d, e, f, &g, 0x7137449123ef65cdull, w1 = be64_to_cpu(chunk[1]));
    Round(g, h, a, &b, c, d, e, &f, 0xb5c0fbcfec4d3b2full, w2 = be64_to_cpu(chunk[2]));
    Round(f, g, h, &a, b, c, d, &e, 0xe9b5dba58189dbbcull, w3 = be64_to_cpu(chunk[3]));
    Round(e, f, g, &h, a, b, c, &d, 0x3956c25bf348b538ull, w4 = be64_to_cpu(chunk[4]));
    Round(d, e, f, &g, h, a, b, &c, 0x59f111f1b605d019ull, w5 = be64_to_cpu(chunk[5]));
    Round(c, d, e, &f, g, h, a, &b, 0x923f82a4af194f9bull, w6 = be64_to_cpu(chunk[6]));
    Round(b, c, d, &e, f, g, h, &a, 0xab1c5ed5da6d8118ull, w7 = be64_to_cpu(chunk[7]));
    Round(a, b, c, &d, e, f, g, &h, 0xd807aa98a3030242ull, w8 = be64_to_cpu(chunk[8]));
    Round(h, a, b, &c, d, e, f, &g, 0x12835b0145706fbeull, w9 = be64_to_cpu(chunk[9]));
    Round(g, h, a, &b, c, d, e, &f, 0x243185be4ee4b28cull, w10 = be64_to_cpu(chunk[10]));
    Round(f, g, h, &a, b, c, d, &e, 0x550c7dc3d5ffb4e2ull, w11 = be64_to_cpu(chunk[11]));
    Round(e, f, g, &h, a, b, c, &d, 0x72be5d74f27b896full, w12 = be64_to_cpu(chunk[12]));
    Round(d, e, f, &g, h, a, b, &c, 0x80deb1fe3b1696b1ull, w13 = be64_to_cpu(chunk[13]));
    Round(c, d, e, &f, g, h, a, &b, 0x9bdc06a725c71235ull, w14 = be64_to_cpu(chunk[14]));
    Round(b, c, d, &e, f, g, h, &a, 0xc19bf174cf692694ull, w15 = be64_to_cpu(chunk[15]));

    Round(a, b, c, &d, e, f, g, &h, 0xe49b69c19ef14ad2ull, w0 += sigma1(w14) + w9 + sigma0(w1));
    Round(h, a, b, &c, d, e, f, &g, 0xefbe4786384f25e3ull, w1 += sigma1(w15) + w10 + sigma0(w2));
    Round(g, h, a, &b, c, d, e, &f, 0x0fc19dc68b8cd5b5ull, w2 += sigma1(w0) + w11 + sigma0(w3));
    Round(f, g, h, &a, b, c, d, &e, 0x240ca1cc77ac9c65ull, w3 += sigma1(w1) + w12 + sigma0(w4));
    Round(e, f, g, &h, a, b, c, &d, 0x2de92c6f592b0275ull, w4 += sigma1(w2) + w13 + sigma0(w5));
    Round(d, e, f, &g, h, a, b, &c, 0x4a7484aa6ea6e483ull, w5 += sigma1(w3) + w14 + sigma0(w6));
    Round(c, d, e, &f, g, h, a, &b, 0x5cb0a9dcbd41fbd4ull, w6 += sigma1(w4) + w15 + sigma0(w7));
    Round(b, c, d, &e, f, g, h, &a, 0x76f988da831153b5ull, w7 += sigma1(w5) + w0 + sigma0(w8));
    Round(a, b, c, &d, e, f, g, &h, 0x983e5152ee66dfabull, w8 += sigma1(w6) + w1 + sigma0(w9));
    Round(h, a, b, &c, d, e, f, &g, 0xa831c66d2db43210ull, w9 += sigma1(w7) + w2 + sigma0(w10));
    Round(g, h, a, &b, c, d, e, &f, 0xb00327c898fb213full, w10 += sigma1(w8) + w3 + sigma0(w11));
    Round(f, g, h, &a, b, c, d, &e, 0xbf597fc7beef0ee4ull, w11 += sigma1(w9) + w4 + sigma0(w12));
    Round(e, f, g, &h, a, b, c, &d, 0xc6e00bf33da88fc2ull, w12 += sigma1(w10) + w5 + sigma0(w13));
    Round(d, e, f, &g, h, a, b, &c, 0xd5a79147930aa725ull, w13 += sigma1(w11) + w6 + sigma0(w14));
    Round(c, d, e, &f, g, h, a, &b, 0x06ca6351e003826full, w14 += sigma1(w12) + w7 + sigma0(w15));
    Round(b, c, d, &e, f, g, h, &a, 0x142929670a0e6e70ull, w15 += sigma1(w13) + w8 + sigma0(w0));

    Round(a, b, c, &d, e, f, g, &h, 0x27b70a8546d22ffcull, w0 += sigma1(w14) + w9 + sigma0(w1));
    Round(h, a, b, &c, d, e, f, &g, 0x2e1b21385c26c926ull, w1 += sigma1(w15) + w10 + sigma0(w2));
    Round(g, h, a, &b, c, d, e, &f, 0x4d2c6dfc5ac42aedull, w2 += sigma1(w0) + w11 + sigma0(w3));
    Round(f, g, h, &a, b, c, d, &e, 0x53380d139d95b3dfull, w3 += sigma1(w1) + w12 + sigma0(w4));
    Round(e, f, g, &h, a, b, c, &d, 0x650a73548baf63deull, w4 += sigma1(w2) + w13 + sigma0(w5));
    Round(d, e, f, &g, h, a, b, &c, 0x766a0abb3c77b2a8ull, w5 += sigma1(w3) + w14 + sigma0(w6));
    Round(c, d, e, &f, g, h, a, &b, 0x81c2c92e47edaee6ull, w6 += sigma1(w4) + w15 + sigma0(w7));
    Round(b, c, d, &e, f, g, h, &a, 0x92722c851482353bull, w7 += sigma1(w5) + w0 + sigma0(w8));
    Round(a, b, c, &d, e, f, g, &h, 0xa2bfe8a14cf10364ull, w8 += sigma1(w6) + w1 + sigma0(w9));
    Round(h, a, b, &c, d, e, f, &g, 0xa81a664bbc423001ull, w9 += sigma1(w7) + w2 + sigma0(w10));
    Round(g, h, a, &b, c, d, e, &f, 0xc24b8b70d0f89791ull, w10 += sigma1(w8) + w3 + sigma0(w11));
    Round(f, g, h, &a, b, c, d, &e, 0xc76c51a30654be30ull, w11 += sigma1(w9) + w4 + sigma0(w12));
    Round(e, f, g, &h, a, b, c, &d, 0xd192e819d6ef5218ull, w12 += sigma1(w10) + w5 + sigma0(w13));
    Round(d, e, f, &g, h, a, b, &c, 0xd69906245565a910ull, w13 += sigma1(w11) + w6 + sigma0(w14));
    Round(c, d, e, &f, g, h, a, &b, 0xf40e35855771202aull, w14 += sigma1(w12) + w7 + sigma0(w15));
    Round(b, c, d, &e, f, g, h, &a, 0x106aa07032bbd1b8ull, w15 += sigma1(w13) + w8 + sigma0(w0));

    Round(a, b, c, &d, e, f, g, &h, 0x19a4c116b8d2d0c8ull, w0 += sigma1(w14) + w9 + sigma0(w1));
    Round(h, a, b, &c, d, e, f, &g, 0x1e376c085141ab53ull, w1 += sigma1(w15) + w10 + sigma0(w2));
    Round(g, h, a, &b, c, d, e, &f, 0x2748774cdf8eeb99ull, w2 += sigma1(w0) + w11 + sigma0(w3));
    Round(f, g, h, &a, b, c, d, &e, 0x34b0bcb5e19b48a8ull, w3 += sigma1(w1) + w12 + sigma0(w4));
    Round(e, f, g, &h, a, b, c, &d, 0x391c0cb3c5c95a63ull, w4 += sigma1(w2) + w13 + sigma0(w5));
    Round(d, e, f, &g, h, a, b, &c, 0x4ed8aa4ae3418acbull, w5 += sigma1(w3) + w14 + sigma0(w6));
    Round(c, d, e, &f, g, h, a, &b, 0x5b9cca4f7763e373ull, w6 += sigma1(w4) + w15 + sigma0(w7));
    Round(b, c, d, &e, f, g, h, &a, 0x682e6ff3d6b2b8a3ull, w7 += sigma1(w5) + w0 + sigma0(w8));
    Round(a, b, c, &d, e, f, g, &h, 0x748f82ee5defb2fcull, w8 += sigma1(w6) + w1 + sigma0(w9));
    Round(h, a, b, &c, d, e, f, &g, 0x78a5636f43172f60ull, w9 += sigma1(w7) + w2 + sigma0(w10));
    Round(g, h, a, &b, c, d, e, &f, 0x84c87814a1f0ab72ull, w10 += sigma1(w8) + w3 + sigma0(w11));
    Round(f, g, h, &a, b, c, d, &e, 0x8cc702081a6439ecull, w11 += sigma1(w9) + w4 + sigma0(w12));
    Round(e, f, g, &h, a, b, c, &d, 0x90befffa23631e28ull, w12 += sigma1(w10) + w5 + sigma0(w13));
    Round(d, e, f, &g, h, a, b, &c, 0xa4506cebde82bde9ull, w13 += sigma1(w11) + w6 + sigma0(w14));
    Round(c, d, e, &f, g, h, a, &b, 0xbef9a3f7b2c67915ull, w14 += sigma1(w12) + w7 + sigma0(w15));
    Round(b, c, d, &e, f, g, h, &a, 0xc67178f2e372532bull, w15 += sigma1(w13) + w8 + sigma0(w0));

    Round(a, b, c, &d, e, f, g, &h, 0xca273eceea26619cull, w0 += sigma1(w14) + w9 + sigma0(w1));
    Round(h, a, b, &c, d, e, f, &g, 0xd186b8c721c0c207ull, w1 += sigma1(w15) + w10 + sigma0(w2));
    Round(g, h, a, &b, c, d, e, &f, 0xeada7dd6cde0eb1eull, w2 += sigma1(w0) + w11 + sigma0(w3));
    Round(f, g, h, &a, b, c, d, &e, 0xf57d4f7fee6ed178ull, w3 += sigma1(w1) + w12 + sigma0(w4));
    Round(e, f, g, &h, a, b, c, &d, 0x06f067aa72176fbaull, w4 += sigma1(w2) + w13 + sigma0(w5));
    Round(d, e, f, &g, h, a, b, &c, 0x0a637dc5a2c898a6ull, w5 += sigma1(w3) + w14 + sigma0(w6));
    Round(c, d, e, &f, g, h, a, &b, 0x113f9804bef90daeull, w6 += sigma1(w4) + w15 + sigma0(w7));
    Round(b, c, d, &e, f, g, h, &a, 0x1b710b35131c471bull, w7 += sigma1(w5) + w0 + sigma0(w8));
    Round(a, b, c, &d, e, f, g, &h, 0x28db77f523047d84ull, w8 += sigma1(w6) + w1 + sigma0(w9));
    Round(h, a, b, &c, d, e, f, &g, 0x32caab7b40c72493ull, w9 += sigma1(w7) + w2 + sigma0(w10));
    Round(g, h, a, &b, c, d, e, &f, 0x3c9ebe0a15c9bebcull, w10 += sigma1(w8) + w3 + sigma0(w11));
    Round(f, g, h, &a, b, c, d, &e, 0x431d67c49c100d4cull, w11 += sigma1(w9) + w4 + sigma0(w12));
    Round(e, f, g, &h, a, b, c, &d, 0x4cc5d4becb3e42b6ull, w12 += sigma1(w10) + w5 + sigma0(w13));
    Round(d, e, f, &g, h, a, b, &c, 0x597f299cfc657e2aull, w13 += sigma1(w11) + w6 + sigma0(w14));
    Round(c, d, e, &f, g, h, a, &b, 0x5fcb6fab3ad6faecull, w14 + sigma1(w12) + w7 + sigma0(w15));
    Round(b, c, d, &e, f, g, h, &a, 0x6c44198c4a475817ull, w15 + sigma1(w13) + w8 + sigma0(w0));

    s[0] += a;
    s[1] += b;
    s[2] += c;
    s[3] += d;
    s[4] += e;
    s[5] += f;
    s[6] += g;
    s[7] += h;
}

static bool alignment_ok(const void *p UNUSED, size_t n UNUSED)
{
#if HAVE_UNALIGNED_ACCESS
    return true;
#else
    return ((size_t)p % n == 0);
#endif
}

static void add(struct sha512_ctx *ctx, const void *p, size_t len)
{
    const unsigned char *data = p;
    size_t bufsize = ctx->bytes % 128;

    if (bufsize + len >= 128) {
        /* Fill the buffer, and process it. */
        memcpy(ctx->buf.u8 + bufsize, data, 128 - bufsize);
        ctx->bytes += 128 - bufsize;
        data += 128 - bufsize;
        len -= 128 - bufsize;
        Transform(ctx->s, ctx->buf.u64);
        bufsize = 0;
    }

    while (len >= 128) {
        /* Process full chunks directly from the source. */
        if (alignment_ok(data, sizeof(uint64_t)))
            Transform(ctx->s, (const uint64_t *)data);
        else {
            memcpy(ctx->buf.u8, data, sizeof(ctx->buf));
            Transform(ctx->s, ctx->buf.u64);
        }
        ctx->bytes += 128;
        data += 128;
        len -= 128;
    }

    if (len) {
        /* Fill the buffer with what remains. */
        memcpy(ctx->buf.u8 + bufsize, data, len);
        ctx->bytes += len;
    }
}

void sha512_init(struct sha512_ctx *ctx)
{
    struct sha512_ctx init = SHA512_INIT;
    *ctx = init;
}

void sha512_update(struct sha512_ctx *ctx, const void *p, size_t size)
{
    check_sha512(ctx);
    add(ctx, p, size);
}

void sha512_done(struct sha512_ctx *ctx, struct sha512 *res)
{
    static const unsigned char pad[128] = { 0x80 };
    uint64_t sizedesc[2] = { 0, 0 };
    size_t i;

    sizedesc[1] = cpu_to_be64((uint64_t)ctx->bytes << 3);

    /* Add '1' bit to terminate, then all 0 bits, up to next block - 16. */
    add(ctx, pad, 1 + ((256 - 16 - (ctx->bytes % 128) - 1) % 128));
    /* Add number of bits of data (big endian) */
    add(ctx, sizedesc, sizeof(sizedesc));
    for (i = 0; i < sizeof(ctx->s) / sizeof(ctx->s[0]); i++)
        res->u.u64[i] = cpu_to_be64(ctx->s[i]);
    invalidate_sha512(ctx);
}

void sha512(struct sha512 *sha, const void *p, size_t size)
{
    struct sha512_ctx ctx;

    sha512_init(&ctx);
    sha512_update(&ctx, p, size);
    sha512_done(&ctx, sha);
}

/* From hex.h */
/**
 * hex_decode - Unpack a hex string.
 * @str: the hexidecimal string
 * @slen: the length of @str
 * @buf: the buffer to write the data into
 * @bufsize: the length of @buf
 *
 * Returns false if there are any characters which aren't 0-9, a-f or A-F,
 * of the string wasn't the right length for @bufsize.
 *
 * Example:
 *  unsigned char data[20];
 *
 *  if (!hex_decode(argv[1], strlen(argv[1]), data, 20))
 *      printf("String is malformed!\n");
 */
bool hex_decode(const char *str, size_t slen, void *buf, size_t bufsize);

/**
 * hex_encode - Create a nul-terminated hex string
 * @buf: the buffer to read the data from
 * @bufsize: the length of @buf
 * @dest: the string to fill
 * @destsize: the max size of the string
 *
 * Returns true if the string, including terminator, fit in @destsize;
 *
 * Example:
 *  unsigned char buf[] = { 0x1F, 0x2F };
 *  char str[5];
 *
 *  if (!hex_encode(buf, sizeof(buf), str, sizeof(str)))
 *      abort();
 */
bool hex_encode(const void *buf, size_t bufsize, char *dest, size_t destsize);

/**
 * hex_str_size - Calculate how big a nul-terminated hex string is
 * @bytes: bytes of data to represent
 *
 * Example:
 *  unsigned char buf[] = { 0x1F, 0x2F };
 *  char str[hex_str_size(sizeof(buf))];
 *
 *  hex_encode(buf, sizeof(buf), str, sizeof(str));
 */
static inline size_t hex_str_size(size_t bytes)
{
    return 2 * bytes + 1;
}

/* From hex.c */
static bool char_to_hex(unsigned char *val, char c)
{
    if (c >= '0' && c <= '9') {
        *val = c - '0';
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        *val = c - 'a' + 10;
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        *val = c - 'A' + 10;
        return true;
    }
    return false;
}

bool hex_decode(const char *str, size_t slen, void *buf, size_t bufsize)
{
    unsigned char v1, v2;
    unsigned char *p = buf;

    while (slen > 1) {
        if (!char_to_hex(&v1, str[0]) || !char_to_hex(&v2, str[1]))
            return false;
        if (!bufsize)
            return false;
        *(p++) = (v1 << 4) | v2;
        str += 2;
        slen -= 2;
        bufsize--;
    }
    return slen == 0 && bufsize == 0;
}

static char hexchar(unsigned int val)
{
    if (val < 10)
        return '0' + val;
    if (val < 16)
        return 'a' + val - 10;
    abort();
}

bool hex_encode(const void *buf, size_t bufsize, char *dest, size_t destsize)
{
    size_t i;

    if (destsize < hex_str_size(bufsize))
        return false;

    for (i = 0; i < bufsize; i++) {
        unsigned int c = ((const unsigned char *)buf)[i];
        *(dest++) = hexchar(c >> 4);
        *(dest++) = hexchar(c & 0xF);
    }
    *dest = '\0';

    return true;
}

/* From tap.h */
/**
 * plan_tests - announce the number of tests you plan to run
 * @tests: the number of tests
 *
 * This should be the first call in your test program: it allows tracing
 * of failures which mean that not all tests are run.
 *
 * If you don't know how many tests will actually be run, assume all of them
 * and use skip() if you don't actually run some tests.
 *
 * Example:
 *  plan_tests(13);
 */
void plan_tests(unsigned int tests);

/**
 * ok1 - Simple conditional test
 * @e: the expression which we expect to be true.
 *
 * This is the simplest kind of test: if the expression is true, the
 * test passes.  The name of the test which is printed will simply be
 * file name, line number, and the expression itself.
 *
 * Example:
 *  ok1(somefunc() == 1);
 */
# define ok1(e) ((e) ?                          \
         _gen_result(1, __func__, __FILE__, __LINE__, "%s", #e) : \
         _gen_result(0, __func__, __FILE__, __LINE__, "%s", #e))

/**
 * exit_status - the value that main should return.
 *
 * For maximum compatibility your test program should return a particular exit
 * code (ie. 0 if all tests were run, and every test which was expected to
 * succeed succeeded).
 *
 * Example:
 *  exit(exit_status());
 */
int exit_status(void);

/**
 * tap_fail_callback - function to call when we fail
 *
 * This can be used to ease debugging, or exit on the first failure.
 */
void (*tap_fail_callback)(void);

/* From tap.c */

static int no_plan = 0;
static int skip_all = 0;
static int have_plan = 0;
static unsigned int test_count = 0; /* Number of tests that have been run */
static unsigned int e_tests = 0; /* Expected number of tests to run */
static unsigned int failures = 0; /* Number of tests that failed */
static char *todo_msg = NULL;
static const char *todo_msg_fixed = "libtap malloc issue";
static int todo = 0;
static int test_died = 0;
static int test_pid;

static void
_expected_tests(unsigned int tests)
{
    printf("1..%d\n", tests);
    e_tests = tests;
}

static void
diagv(const char *fmt, va_list ap)
{
    fputs("# ", stdout);
    vfprintf(stdout, fmt, ap);
    fputs("\n", stdout);
}

static void
_diag(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    diagv(fmt, ap);
    va_end(ap);
}

/*
 * Generate a test result.
 *
 * ok -- boolean, indicates whether or not the test passed.
 * test_name -- the name of the test, may be NULL
 * test_comment -- a comment to print afterwards, may be NULL
 */
unsigned int
_gen_result(int ok, const char *func, const char *file, unsigned int line,
        const char *test_name, ...)
{
    va_list ap;
    char *local_test_name = NULL;
    char *c;
    int name_is_digits;

    test_count++;

    /* Start by taking the test name and performing any printf()
       expansions on it */
    if(test_name != NULL) {
        va_start(ap, test_name);
        if (vasprintf(&local_test_name, test_name, ap) < 0)
            local_test_name = NULL;
        va_end(ap);

        /* Make sure the test name contains more than digits
           and spaces.  Emit an error message and exit if it
           does */
        if(local_test_name) {
            name_is_digits = 1;
            for(c = local_test_name; *c != '\0'; c++) {
                if(!isdigit((unsigned char)*c)
                   && !isspace((unsigned char)*c)) {
                    name_is_digits = 0;
                    break;
                }
            }

            if(name_is_digits) {
                _diag("    You named your test '%s'.  You shouldn't use numbers for your test names.", local_test_name);
                _diag("    Very confusing.");
            }
        }
    }

    if(!ok) {
        printf("not ");
        failures++;
    }

    printf("ok %d", test_count);

    if(test_name != NULL) {
        printf(" - ");

        /* Print the test name, escaping any '#' characters it
           might contain */
        if(local_test_name != NULL) {
            flockfile(stdout);
            for(c = local_test_name; *c != '\0'; c++) {
                if(*c == '#')
                    fputc('\\', stdout);
                fputc((int)*c, stdout);
            }
            funlockfile(stdout);
        } else {    /* vasprintf() failed, use a fixed message */
            printf("%s", todo_msg_fixed);
        }
    }

    /* If we're in a todo_start() block then flag the test as being
       TODO.  todo_msg should contain the message to print at this
       point.  If it's NULL then asprintf() failed, and we should
       use the fixed message.

       This is not counted as a failure, so decrement the counter if
       the test failed. */
    if(todo) {
        printf(" # TODO %s", todo_msg ? todo_msg : todo_msg_fixed);
        if(!ok)
            failures--;
    }

    printf("\n");

    if(!ok)
        _diag("    Failed %stest (%s:%s() at line %d)",
              todo ? "(TODO) " : "", file, func, line);

    free(local_test_name);

    if (!ok && tap_fail_callback)
        tap_fail_callback();

    /* We only care (when testing) that ok is positive, but here we
       specifically only want to return 1 or 0 */
    return ok ? 1 : 0;
}

/*
 * Cleanup at the end of the run, produce any final output that might be
 * required.
 */
static void
_cleanup(void)
{
    /* If we forked, don't do cleanup in child! */
    if (getpid() != test_pid)
        return;

    /* If plan_no_plan() wasn't called, and we don't have a plan,
       and we're not skipping everything, then something happened
       before we could produce any output */
    if(!no_plan && !have_plan && !skip_all) {
        _diag("Looks like your test died before it could output anything.");
        return;
    }

    if(test_died) {
        _diag("Looks like your test died just after %d.", test_count);
        return;
    }


    /* No plan provided, but now we know how many tests were run, and can
       print the header at the end */
    if(!skip_all && (no_plan || !have_plan)) {
        printf("1..%d\n", test_count);
    }

    if((have_plan && !no_plan) && e_tests < test_count) {
        _diag("Looks like you planned %d tests but ran %d extra.",
              e_tests, test_count - e_tests);
        return;
    }

    if((have_plan || !no_plan) && e_tests > test_count) {
        _diag("Looks like you planned %d tests but only ran %d.",
              e_tests, test_count);
        if(failures) {
            _diag("Looks like you failed %d tests of %d run.",
                  failures, test_count);
        }
        return;
    }

    if(failures)
        _diag("Looks like you failed %d tests of %d.",
              failures, test_count);

}

/*
 * Initialise the TAP library.  Will only do so once, however many times it's
 * called.
 */
static void
_tap_init(void)
{
    static int run_once = 0;

    if(!run_once) {
        test_pid = getpid();
        atexit(_cleanup);

        /* stdout needs to be unbuffered so that the output appears
           in the same place relative to stderr output as it does
           with Test::Harness */
//      setbuf(stdout, 0);
        run_once = 1;
    }
}

/*
 * Note the number of tests that will be run.
 */
void
plan_tests(unsigned int tests)
{

    _tap_init();

    if(have_plan != 0) {
        fprintf(stderr, "You tried to plan twice!\n");
        test_died = 1;
        exit(255);
    }

    if(tests == 0) {
        fprintf(stderr, "You said to run 0 tests!  You've got to run something.\n");
        test_died = 1;
        exit(255);
    }

    have_plan = 1;

    _expected_tests(tests);
}

static int
exit_status_(void)
{
    int r;

    /* If there's no plan, just return the number of failures */
    if(no_plan || !have_plan) {
        return failures;
    }

    /* Ran too many tests?  Return the number of tests that were run
       that shouldn't have been */
    if(e_tests < test_count) {
        r = test_count - e_tests;
        return r;
    }

    /* Return the number of tests that failed + the number of tests
       that weren't run */
    r = failures + e_tests - test_count;

    return r;
}

int
exit_status(void)
{
    int r = exit_status_();
    if (r > 255)
        r = 255;
    return r;
}

/* From run-test-vectors.c */

/* Test vectors. */
struct test {
    const char *vector;
    size_t repetitions;
    const char *expected;
};

static const char ZEROES[] =
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000";

static struct test tests[] = {
    /* http://csrc.nist.gov/groups/STM/cavp/secure-hashing.html ShortMsg */
    { "21", 1,
      "3831a6a6155e509dee59a7f451eb35324d8f8f2df6e3708894740f98fdee2388"
      "9f4de5adb0c5010dfb555cda77c8ab5dc902094c52de3278f35a75ebc25f093a" },
    { "9083", 1,
      "55586ebba48768aeb323655ab6f4298fc9f670964fc2e5f2731e34dfa4b0c09e"
      "6e1e12e3d7286b3145c61c2047fb1a2a1297f36da64160b31fa4c8c2cddd2fb4" },
    { "0a55db", 1,
      "7952585e5330cb247d72bae696fc8a6b0f7d0804577e347d99bc1b11e52f3849"
      "85a428449382306a89261ae143c2f3fb613804ab20b42dc097e5bf4a96ef919b" },
    { "23be86d5", 1,
      "76d42c8eadea35a69990c63a762f330614a4699977f058adb988f406fb0be8f2"
      "ea3dce3a2bbd1d827b70b9b299ae6f9e5058ee97b50bd4922d6d37ddc761f8eb" },
    { "eb0ca946c1", 1,
      "d39ecedfe6e705a821aee4f58bfc489c3d9433eb4ac1b03a97e321a2586b40dd"
      "0522f40fa5aef36afff591a78c916bfc6d1ca515c4983dd8695b1ec7951d723e" },
    { "38667f39277b", 1,
      "85708b8ff05d974d6af0801c152b95f5fa5c06af9a35230c5bea2752f031f9bd"
      "84bd844717b3add308a70dc777f90813c20b47b16385664eefc88449f04f2131" },
    { "b39f71aaa8a108", 1,
      "258b8efa05b4a06b1e63c7a3f925c5ef11fa03e3d47d631bf4d474983783d8c0"
      "b09449009e842fc9fa15de586c67cf8955a17d790b20f41dadf67ee8cdcdfce6" },
    { "dc28484ebfd293d62ac759d5754bdf502423e4d419fa79020805134b2ce3dff7"
      "38c7556c91d810adbad8dd210f041296b73c2185d4646c97fc0a5b69ed49ac8c"
      "7ced0bd1cfd7e3c3cca47374d189247da6811a40b0ab097067ed4ad40ade2e47"
      "91e39204e398b3204971445822a1be0dd93af8", 1,
      "615115d2e8b62e345adaa4bdb95395a3b4fe27d71c4a111b86c1841463c5f03d"
      "6b20d164a39948ab08ae060720d05c10f6022e5c8caf2fa3bca2e04d9c539ded" },
    { "fd2203e467574e834ab07c9097ae164532f24be1eb5d88f1af7748ceff0d2c67"
      "a21f4e4097f9d3bb4e9fbf97186e0db6db0100230a52b453d421f8ab9c9a6043"
      "aa3295ea20d2f06a2f37470d8a99075f1b8a8336f6228cf08b5942fc1fb4299c"
      "7d2480e8e82bce175540bdfad7752bc95b577f229515394f3ae5cec870a4b2f8",
      1,
      "a21b1077d52b27ac545af63b32746c6e3c51cb0cb9f281eb9f3580a6d4996d5c"
      "9917d2a6e484627a9d5a06fa1b25327a9d710e027387fc3e07d7c4d14c6086cc" },
    /* http://www.di-mgt.com.au/sha_testvectors.html */
    { ZEROES, 1,
      "7be9fda48f4179e611c698a73cff09faf72869431efee6eaad14de0cb44bbf66"
      "503f752b7a8eb17083355f3ce6eb7d2806f236b25af96a24e22b887405c20081" }
};

static void *xmalloc(size_t size)
{
    char * ret;
    ret = malloc(size);
    if (ret == NULL) {
        perror("malloc");
        abort();
    }
    return ret;
}

static bool do_test(const struct test *t)
{
    struct sha512 h;
    char got[128 + 1];
    bool passed;
    size_t i, vector_len = strlen(t->vector) / 2;
    void *vector = xmalloc(vector_len);

    hex_decode(t->vector, vector_len * 2, vector, vector_len);

    for (i = 0; i < t->repetitions; i++) {
        sha512(&h, vector, vector_len);
        if (t->repetitions > 1)
            memcpy(vector, &h, sizeof(h));
    }

    hex_encode(&h, sizeof(h), got, sizeof(got));

    passed = strcmp(t->expected, got) == 0;
    free(vector);
    return passed;
}

int main(void)
{
    const size_t num_tests = sizeof(tests) / sizeof(tests[0]);
    size_t i;

    /* This is how many tests you plan to run */
    plan_tests(num_tests);

    for (i = 0; i < num_tests; i++)
        ok1(do_test(&tests[i]));

    /* This exits depending on whether all tests passed */
    return exit_status();
}
