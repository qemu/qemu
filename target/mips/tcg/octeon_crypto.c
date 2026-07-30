/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * MIPS Octeon crypto emulation helpers.
 *
 * Copyright (c) 2026 James Hilliard
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internal.h"
#include "exec/helper-proto.h"
#include "crypto/aes.h"
#include "crypto/clmul.h"
#include "crypto/sm4.h"
#include "qemu/bitops.h"
#include "qemu/host-utils.h"

#define OCTEON_LLM_NARROW_MASK ((1ULL << 36) - 1)

static uint64_t octeon_llm_pack_narrow(uint64_t value)
{
    value &= OCTEON_LLM_NARROW_MASK;
    return value | ((uint64_t)(ctpop64(value) & 1) << 36);
}

static void octeon_llm_read(MIPSOcteonCryptoState *crypto, unsigned int set,
                            uint64_t addr, bool wide)
{
    uint64_t value;

    if (wide) {
        value = mips_octeon_llm_load(crypto->llm64, addr);
    } else {
        value = octeon_llm_pack_narrow(
            mips_octeon_llm_load(crypto->llm36, addr));
    }

    crypto->llm_data[set] = value;
}

static void octeon_llm_write(MIPSOcteonCryptoState *crypto, unsigned int set,
                             uint64_t addr, bool wide)
{
    uint64_t value = crypto->llm_data[set];

    if (wide) {
        mips_octeon_llm_store(&crypto->llm64, addr, value);
    } else {
        mips_octeon_llm_store(&crypto->llm36, addr,
                              value & OCTEON_LLM_NARROW_MASK);
    }
}

static uint32_t octeon_crc_reflect32_by_byte(uint32_t v)
{
    return bswap32(revbit32(v));
}

static uint32_t octeon_crc_state_reflect(const MIPSOcteonCryptoState *crypto)
{
    return octeon_crc_reflect32_by_byte(crypto->crc_iv);
}

static void octeon_crc_set_state_reflect(MIPSOcteonCryptoState *crypto,
                                         uint32_t state)
{
    crypto->crc_iv = octeon_crc_reflect32_by_byte(state);
}

static void octeon_crc_update_normal(MIPSOcteonCryptoState *crypto,
                                     uint64_t value, unsigned int bytes)
{
    uint32_t crc = crypto->crc_iv;
    uint32_t poly = crypto->crc_poly;

    for (unsigned int i = 0; i < bytes; i++) {
        uint8_t byte = value >> ((bytes - 1 - i) * 8);

        crc ^= (uint32_t)byte << 24;
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80000000U) {
                crc = (crc << 1) ^ poly;
            } else {
                crc <<= 1;
            }
        }
    }

    crypto->crc_iv = crc;
}

static void octeon_crc_update_reflect(MIPSOcteonCryptoState *crypto,
                                      uint64_t value, unsigned int bytes)
{
    uint32_t crc = octeon_crc_state_reflect(crypto);
    uint32_t poly = bswap32(crypto->crc_poly);

    for (unsigned int i = 0; i < bytes; i++) {
        uint8_t byte = value >> ((bytes - 1 - i) * 8);

        crc ^= byte;
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 1U) {
                crc = (crc >> 1) ^ poly;
            } else {
                crc >>= 1;
            }
        }
    }

    octeon_crc_set_state_reflect(crypto, crc);
}

static void octeon_gfm_mul(const uint64_t x[2], const uint64_t y[2],
                           uint16_t poly, uint64_t out[2])
{
    uint64_t zh = 0, zl = 0;
    uint64_t vh = y[0], vl = y[1];
    uint64_t rh = (uint64_t)poly << 48;
    int i;

    /*
     * Keep the reflected-shift formulation used by Octeon software: the
     * selector polynomial is already in reflected bit order, and the software
     * view folds its 16 reduction bits from the top of the high word.
     */
    for (i = 0; i < 128; i++) {
        bool bit;
        bool lsb;

        if (i < 64) {
            bit = (x[0] >> (63 - i)) & 1;
        } else {
            bit = (x[1] >> (127 - i)) & 1;
        }
        if (bit) {
            zh ^= vh;
            zl ^= vl;
        }

        lsb = vl & 1;
        vl = (vh << 63) | (vl >> 1);
        vh >>= 1;
        if (lsb) {
            vh ^= rh;
        }
    }

    out[0] = zh;
    out[1] = zl;
}

static uint64_t octeon_gfm_reduce64(Int128 product, uint8_t poly)
{
    uint64_t lo = int128_getlo(product);
    uint64_t hi = int128_gethi(product);

    while (hi) {
        int bit = 63 - clz64(hi);

        hi ^= 1ULL << bit;
        lo ^= (uint64_t)poly << bit;
        if (bit > 56) {
            hi ^= (uint64_t)poly >> (64 - bit);
        }
    }

    return lo;
}

static void octeon_gfm_mul64_uia2(const uint64_t x[2], const uint64_t y[2],
                                  uint8_t poly, uint64_t out[2])
{
    /*
     * SNOW3G UIA2 uses the GFM datapath as a reflected 64-bit multiply in
     * the low half of the 128-bit register pair.  When RESINP[0], MUL[1],
     * and the high polynomial byte are all zero, octeon_gfm_mul() observes
     * only x[1], y[0], and the low 8-bit polynomial.  Reflect those operands
     * into normal carryless-multiply order and reflect the reduced result
     * back into RESINP[1].
     */
    uint64_t vx = revbit64(x[1]);
    uint64_t vy = revbit64(y[0]);
    Int128 product = clmul_64(vx, vy);
    uint64_t res = octeon_gfm_reduce64(product, revbit32(poly) >> 24);

    out[0] = 0;
    out[1] = revbit64(res);
}

static uint32_t octeon_hsh_get32(const uint64_t *regs, unsigned int index)
{
    return regs[index];
}

static void octeon_hsh_set32(uint64_t *regs, unsigned int index, uint32_t value)
{
    regs[index] = (regs[index] & ~(uint64_t)UINT32_MAX) | value;
}

static void octeon_hsh_set_pair(uint64_t *regs, unsigned int index,
                                uint64_t value)
{
    octeon_hsh_set32(regs, index * 2, value >> 32);
    octeon_hsh_set32(regs, index * 2 + 1, value);
}

static void octeon_md5_transform(MIPSOcteonCryptoState *crypto)
{
    static const uint32_t k[64] = {
        0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU,
        0xf57c0fafU, 0x4787c62aU, 0xa8304613U, 0xfd469501U,
        0x698098d8U, 0x8b44f7afU, 0xffff5bb1U, 0x895cd7beU,
        0x6b901122U, 0xfd987193U, 0xa679438eU, 0x49b40821U,
        0xf61e2562U, 0xc040b340U, 0x265e5a51U, 0xe9b6c7aaU,
        0xd62f105dU, 0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U,
        0x21e1cde6U, 0xc33707d6U, 0xf4d50d87U, 0x455a14edU,
        0xa9e3e905U, 0xfcefa3f8U, 0x676f02d9U, 0x8d2a4c8aU,
        0xfffa3942U, 0x8771f681U, 0x6d9d6122U, 0xfde5380cU,
        0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U,
        0x289b7ec6U, 0xeaa127faU, 0xd4ef3085U, 0x04881d05U,
        0xd9d4d039U, 0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U,
        0xf4292244U, 0x432aff97U, 0xab9423a7U, 0xfc93a039U,
        0x655b59c3U, 0x8f0ccc92U, 0xffeff47dU, 0x85845dd1U,
        0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U,
        0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU, 0xeb86d391U,
    };
    static const uint8_t s[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
    };
    uint32_t m[16];
    uint32_t a, b, c, d;
    uint32_t aa, bb, cc, dd;
    int i;

    for (i = 0; i < 16; i++) {
        m[i] = bswap32(octeon_hsh_get32(crypto->hsh_dat, i));
    }

    a = bswap32(octeon_hsh_get32(crypto->hsh_iv, 0));
    b = bswap32(octeon_hsh_get32(crypto->hsh_iv, 1));
    c = bswap32(octeon_hsh_get32(crypto->hsh_iv, 2));
    d = bswap32(octeon_hsh_get32(crypto->hsh_iv, 3));
    aa = a;
    bb = b;
    cc = c;
    dd = d;

    for (i = 0; i < 64; i++) {
        uint32_t f, g, tmp;

        if (i < 16) {
            f = (b & c) | ((~b) & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | ((~d) & c);
            g = (5 * i + 1) & 0xf;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) & 0xf;
        } else {
            f = c ^ (b | (~d));
            g = (7 * i) & 0xf;
        }

        tmp = d;
        d = c;
        c = b;
        b = b + rol32(a + f + k[i] + m[g], s[i]);
        a = tmp;
    }

    a += aa;
    b += bb;
    c += cc;
    d += dd;
    octeon_hsh_set32(crypto->hsh_iv, 0, bswap32(a));
    octeon_hsh_set32(crypto->hsh_iv, 1, bswap32(b));
    octeon_hsh_set32(crypto->hsh_iv, 2, bswap32(c));
    octeon_hsh_set32(crypto->hsh_iv, 3, bswap32(d));
}

static void octeon_sha1_transform(MIPSOcteonCryptoState *crypto)
{
    uint32_t w[80];
    uint32_t a, b, c, d, e;
    uint32_t orig[5];
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = octeon_hsh_get32(crypto->hsh_dat, i);
    }
    for (i = 16; i < 80; i++) {
        w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    for (i = 0; i < 5; i++) {
        orig[i] = octeon_hsh_get32(crypto->hsh_iv, i);
    }
    a = orig[0];
    b = orig[1];
    c = orig[2];
    d = orig[3];
    e = orig[4];

    for (i = 0; i < 80; i++) {
        uint32_t f, k, temp;

        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5a827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ed9eba1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdc;
        } else {
            f = b ^ c ^ d;
            k = 0xca62c1d6;
        }

        temp = rol32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol32(b, 30);
        b = a;
        a = temp;
    }

    orig[0] += a;
    orig[1] += b;
    orig[2] += c;
    orig[3] += d;
    orig[4] += e;
    for (i = 0; i < 5; i++) {
        octeon_hsh_set32(crypto->hsh_iv, i, orig[i]);
    }
}

static void octeon_sha256_transform(MIPSOcteonCryptoState *crypto)
{
    static const uint32_t k[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
    };
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t orig[8];
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = octeon_hsh_get32(crypto->hsh_dat, i);
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = ror32(w[i - 15], 7) ^
                      ror32(w[i - 15], 18) ^
                      (w[i - 15] >> 3);
        uint32_t s1 = ror32(w[i - 2], 17) ^
                      ror32(w[i - 2], 19) ^
                      (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    for (i = 0; i < 8; i++) {
        orig[i] = octeon_hsh_get32(crypto->hsh_iv, i);
    }
    a = orig[0];
    b = orig[1];
    c = orig[2];
    d = orig[3];
    e = orig[4];
    f = orig[5];
    g = orig[6];
    h = orig[7];

    for (i = 0; i < 64; i++) {
        uint32_t s1 = ror32(e, 6) ^
                      ror32(e, 11) ^
                      ror32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + k[i] + w[i];
        uint32_t s0 = ror32(a, 2) ^
                      ror32(a, 13) ^
                      ror32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    orig[0] += a;
    orig[1] += b;
    orig[2] += c;
    orig[3] += d;
    orig[4] += e;
    orig[5] += f;
    orig[6] += g;
    orig[7] += h;
    for (i = 0; i < 8; i++) {
        octeon_hsh_set32(crypto->hsh_iv, i, orig[i]);
    }
}

static void octeon_sha512_transform(MIPSOcteonCryptoState *crypto)
{
    static const uint64_t k[80] = {
        0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL,
        0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
        0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
        0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
        0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
        0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
        0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL,
        0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
        0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
        0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
        0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL,
        0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
        0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL,
        0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
        0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
        0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
        0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL,
        0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
        0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL,
        0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
        0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
        0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
        0xd192e819d6ef5218ULL, 0xd69906245565a910ULL,
        0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
        0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
        0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
        0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
        0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
        0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL,
        0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
        0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL,
        0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
        0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
        0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
        0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
        0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
        0x28db77f523047d84ULL, 0x32caab7b40c72493ULL,
        0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
        0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
        0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
    };
    uint64_t w[80];
    uint64_t a, b, c, d, e, f, g, h;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = crypto->hsh_dat[i];
    }
    for (i = 16; i < 80; i++) {
        uint64_t s0 = ror64(w[i - 15], 1) ^
                      ror64(w[i - 15], 8) ^
                      (w[i - 15] >> 7);
        uint64_t s1 = ror64(w[i - 2], 19) ^
                      ror64(w[i - 2], 61) ^
                      (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = crypto->hsh_iv[0];
    b = crypto->hsh_iv[1];
    c = crypto->hsh_iv[2];
    d = crypto->hsh_iv[3];
    e = crypto->hsh_iv[4];
    f = crypto->hsh_iv[5];
    g = crypto->hsh_iv[6];
    h = crypto->hsh_iv[7];

    for (i = 0; i < 80; i++) {
        uint64_t s0 = ror64(a, 28) ^
                      ror64(a, 34) ^
                      ror64(a, 39);
        uint64_t s1 = ror64(e, 14) ^
                      ror64(e, 18) ^
                      ror64(e, 41);
        uint64_t ch = (e & f) ^ ((~e) & g);
        uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint64_t temp1 = h + s1 + ch + k[i] + w[i];
        uint64_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    crypto->hsh_iv[0] += a;
    crypto->hsh_iv[1] += b;
    crypto->hsh_iv[2] += c;
    crypto->hsh_iv[3] += d;
    crypto->hsh_iv[4] += e;
    crypto->hsh_iv[5] += f;
    crypto->hsh_iv[6] += g;
    crypto->hsh_iv[7] += h;
}

static const uint64_t octeon_sha3_round_constants[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL,
    0x800000000000808aULL, 0x8000000080008000ULL,
    0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008aULL, 0x0000000000000088ULL,
    0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL,
    0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL,
    0x0000000080000001ULL, 0x8000000080008008ULL,
};

static const uint8_t octeon_sha3_rotation_constants[24] = {
     1,  3,  6, 10, 15, 21, 28, 36, 45, 55,  2, 14,
    27, 41, 56,  8, 25, 43, 62, 18, 39, 61, 20, 44,
};

static const uint8_t octeon_sha3_pi_lanes[24] = {
    10,  7, 11, 17, 18,  3,  5, 16,  8, 21, 24,  4,
    15, 23, 19, 13, 12,  2, 20, 14, 22,  9,  6,  1,
};

static uint64_t octeon_sha3_reg_to_lane(uint64_t value)
{
    /*
     * The COP2 register interface is consumed by big-endian MIPS code as
     * 64-bit register values, while Keccak lanes are byte-little-endian.
     */
    return bswap64(value);
}

static uint64_t octeon_sha3_lane_to_reg(uint64_t value)
{
    return bswap64(value);
}

static void octeon_sha3_permute(MIPSOcteonCryptoState *crypto)
{
    uint64_t state[25];

    for (int i = 0; i < 25; i++) {
        state[i] = octeon_sha3_reg_to_lane(crypto->sha3_dat[i]);
    }

    for (int round = 0; round < 24; round++) {
        uint64_t bc[5];
        uint64_t temp;

        for (int x = 0; x < 5; x++) {
            bc[x] = state[x] ^ state[5 + x] ^ state[10 + x] ^
                    state[15 + x] ^ state[20 + x];
        }
        for (int x = 0; x < 5; x++) {
            temp = bc[(x + 4) % 5] ^ rol64(bc[(x + 1) % 5], 1);
            for (int y = 0; y < 25; y += 5) {
                state[y + x] ^= temp;
            }
        }

        temp = state[1];
        for (int i = 0; i < 24; i++) {
            uint64_t next = state[octeon_sha3_pi_lanes[i]];

            state[octeon_sha3_pi_lanes[i]] =
                rol64(temp, octeon_sha3_rotation_constants[i]);
            temp = next;
        }

        for (int y = 0; y < 25; y += 5) {
            for (int x = 0; x < 5; x++) {
                bc[x] = state[y + x];
            }
            for (int x = 0; x < 5; x++) {
                state[y + x] = bc[x] ^ ((~bc[(x + 1) % 5]) & bc[(x + 2) % 5]);
            }
        }

        state[0] ^= octeon_sha3_round_constants[round];
    }

    for (int i = 0; i < 25; i++) {
        crypto->sha3_dat[i] = octeon_sha3_lane_to_reg(state[i]);
    }
}

static uint32_t octeon_crypto_hi32(uint64_t value)
{
    return value >> 32;
}

static uint32_t octeon_crypto_lo32(uint64_t value)
{
    return value;
}

static uint64_t octeon_crypto_pack32(uint32_t hi, uint32_t lo)
{
    return ((uint64_t)hi << 32) | lo;
}

static const uint8_t octeon_zuc_s0[256] = {
    0x3e, 0x72, 0x5b, 0x47, 0xca, 0xe0, 0x00, 0x33,
    0x04, 0xd1, 0x54, 0x98, 0x09, 0xb9, 0x6d, 0xcb,
    0x7b, 0x1b, 0xf9, 0x32, 0xaf, 0x9d, 0x6a, 0xa5,
    0xb8, 0x2d, 0xfc, 0x1d, 0x08, 0x53, 0x03, 0x90,
    0x4d, 0x4e, 0x84, 0x99, 0xe4, 0xce, 0xd9, 0x91,
    0xdd, 0xb6, 0x85, 0x48, 0x8b, 0x29, 0x6e, 0xac,
    0xcd, 0xc1, 0xf8, 0x1e, 0x73, 0x43, 0x69, 0xc6,
    0xb5, 0xbd, 0xfd, 0x39, 0x63, 0x20, 0xd4, 0x38,
    0x76, 0x7d, 0xb2, 0xa7, 0xcf, 0xed, 0x57, 0xc5,
    0xf3, 0x2c, 0xbb, 0x14, 0x21, 0x06, 0x55, 0x9b,
    0xe3, 0xef, 0x5e, 0x31, 0x4f, 0x7f, 0x5a, 0xa4,
    0x0d, 0x82, 0x51, 0x49, 0x5f, 0xba, 0x58, 0x1c,
    0x4a, 0x16, 0xd5, 0x17, 0xa8, 0x92, 0x24, 0x1f,
    0x8c, 0xff, 0xd8, 0xae, 0x2e, 0x01, 0xd3, 0xad,
    0x3b, 0x4b, 0xda, 0x46, 0xeb, 0xc9, 0xde, 0x9a,
    0x8f, 0x87, 0xd7, 0x3a, 0x80, 0x6f, 0x2f, 0xc8,
    0xb1, 0xb4, 0x37, 0xf7, 0x0a, 0x22, 0x13, 0x28,
    0x7c, 0xcc, 0x3c, 0x89, 0xc7, 0xc3, 0x96, 0x56,
    0x07, 0xbf, 0x7e, 0xf0, 0x0b, 0x2b, 0x97, 0x52,
    0x35, 0x41, 0x79, 0x61, 0xa6, 0x4c, 0x10, 0xfe,
    0xbc, 0x26, 0x95, 0x88, 0x8a, 0xb0, 0xa3, 0xfb,
    0xc0, 0x18, 0x94, 0xf2, 0xe1, 0xe5, 0xe9, 0x5d,
    0xd0, 0xdc, 0x11, 0x66, 0x64, 0x5c, 0xec, 0x59,
    0x42, 0x75, 0x12, 0xf5, 0x74, 0x9c, 0xaa, 0x23,
    0x0e, 0x86, 0xab, 0xbe, 0x2a, 0x02, 0xe7, 0x67,
    0xe6, 0x44, 0xa2, 0x6c, 0xc2, 0x93, 0x9f, 0xf1,
    0xf6, 0xfa, 0x36, 0xd2, 0x50, 0x68, 0x9e, 0x62,
    0x71, 0x15, 0x3d, 0xd6, 0x40, 0xc4, 0xe2, 0x0f,
    0x8e, 0x83, 0x77, 0x6b, 0x25, 0x05, 0x3f, 0x0c,
    0x30, 0xea, 0x70, 0xb7, 0xa1, 0xe8, 0xa9, 0x65,
    0x8d, 0x27, 0x1a, 0xdb, 0x81, 0xb3, 0xa0, 0xf4,
    0x45, 0x7a, 0x19, 0xdf, 0xee, 0x78, 0x34, 0x60,
};

static const uint8_t octeon_zuc_s1[256] = {
    0x55, 0xc2, 0x63, 0x71, 0x3b, 0xc8, 0x47, 0x86,
    0x9f, 0x3c, 0xda, 0x5b, 0x29, 0xaa, 0xfd, 0x77,
    0x8c, 0xc5, 0x94, 0x0c, 0xa6, 0x1a, 0x13, 0x00,
    0xe3, 0xa8, 0x16, 0x72, 0x40, 0xf9, 0xf8, 0x42,
    0x44, 0x26, 0x68, 0x96, 0x81, 0xd9, 0x45, 0x3e,
    0x10, 0x76, 0xc6, 0xa7, 0x8b, 0x39, 0x43, 0xe1,
    0x3a, 0xb5, 0x56, 0x2a, 0xc0, 0x6d, 0xb3, 0x05,
    0x22, 0x66, 0xbf, 0xdc, 0x0b, 0xfa, 0x62, 0x48,
    0xdd, 0x20, 0x11, 0x06, 0x36, 0xc9, 0xc1, 0xcf,
    0xf6, 0x27, 0x52, 0xbb, 0x69, 0xf5, 0xd4, 0x87,
    0x7f, 0x84, 0x4c, 0xd2, 0x9c, 0x57, 0xa4, 0xbc,
    0x4f, 0x9a, 0xdf, 0xfe, 0xd6, 0x8d, 0x7a, 0xeb,
    0x2b, 0x53, 0xd8, 0x5c, 0xa1, 0x14, 0x17, 0xfb,
    0x23, 0xd5, 0x7d, 0x30, 0x67, 0x73, 0x08, 0x09,
    0xee, 0xb7, 0x70, 0x3f, 0x61, 0xb2, 0x19, 0x8e,
    0x4e, 0xe5, 0x4b, 0x93, 0x8f, 0x5d, 0xdb, 0xa9,
    0xad, 0xf1, 0xae, 0x2e, 0xcb, 0x0d, 0xfc, 0xf4,
    0x2d, 0x46, 0x6e, 0x1d, 0x97, 0xe8, 0xd1, 0xe9,
    0x4d, 0x37, 0xa5, 0x75, 0x5e, 0x83, 0x9e, 0xab,
    0x82, 0x9d, 0xb9, 0x1c, 0xe0, 0xcd, 0x49, 0x89,
    0x01, 0xb6, 0xbd, 0x58, 0x24, 0xa2, 0x5f, 0x38,
    0x78, 0x99, 0x15, 0x90, 0x50, 0xb8, 0x95, 0xe4,
    0xd0, 0x91, 0xc7, 0xce, 0xed, 0x0f, 0xb4, 0x6f,
    0xa0, 0xcc, 0xf0, 0x02, 0x4a, 0x79, 0xc3, 0xde,
    0xa3, 0xef, 0xea, 0x51, 0xe6, 0x6b, 0x18, 0xec,
    0x1b, 0x2c, 0x80, 0xf7, 0x74, 0xe7, 0xff, 0x21,
    0x5a, 0x6a, 0x54, 0x1e, 0x41, 0x31, 0x92, 0x35,
    0xc4, 0x33, 0x07, 0x0a, 0xba, 0x7e, 0x0e, 0x34,
    0x88, 0xb1, 0x98, 0x7c, 0xf3, 0x3d, 0x60, 0x6c,
    0x7b, 0xca, 0xd3, 0x1f, 0x32, 0x65, 0x04, 0x28,
    0x64, 0xbe, 0x85, 0x9b, 0x2f, 0x59, 0x8a, 0xd7,
    0xb0, 0x25, 0xac, 0xaf, 0x12, 0x03, 0xe2, 0xf2,
};

static uint32_t octeon_zuc_addm(uint32_t a, uint32_t b)
{
    uint32_t c = a + b;

    c = (c & 0x7fffffffU) + (c >> 31);
    return c ? c : 0x7fffffffU;
}

static uint32_t octeon_zuc_mul_by_pow2(uint32_t v, unsigned int shift)
{
    return ((v << shift) | (v >> (31 - shift))) & 0x7fffffffU;
}

static uint32_t octeon_zuc_make_u32(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) |
           ((uint32_t)c << 8) | d;
}

static uint64_t octeon_zuc_pack_pair(uint32_t hi, uint32_t lo)
{
    return ((uint64_t)hi << 32) | lo;
}

static uint32_t octeon_zuc_lfsr(const MIPSOcteonCryptoState *crypto,
                                unsigned int index)
{
    uint64_t pair = crypto->hsh_dat[index / 2];

    return index & 1 ? octeon_crypto_lo32(pair) : octeon_crypto_hi32(pair);
}

static void octeon_zuc_set_lfsr(MIPSOcteonCryptoState *crypto,
                                unsigned int index, uint32_t value)
{
    uint32_t hi = octeon_crypto_hi32(crypto->hsh_dat[index / 2]);
    uint32_t lo = octeon_crypto_lo32(crypto->hsh_dat[index / 2]);

    value &= 0x7fffffffU;
    if (index & 1) {
        lo = value;
    } else {
        hi = value;
    }
    crypto->hsh_dat[index / 2] = octeon_zuc_pack_pair(hi, lo);
}

static uint32_t octeon_zuc_fsm(const MIPSOcteonCryptoState *crypto,
                               unsigned int index)
{
    g_assert(index < 2);
    return crypto->hsh_iv[1 + index];
}

static void octeon_zuc_set_fsm(MIPSOcteonCryptoState *crypto,
                               unsigned int index, uint32_t value)
{
    g_assert(index < 2);
    crypto->hsh_iv[1 + index] = value;
}

static uint32_t octeon_zuc_window(const MIPSOcteonCryptoState *crypto,
                                  unsigned int index)
{
    uint64_t pair = crypto->hsh_iv[0];

    switch (index) {
    case 0:
        return octeon_crypto_hi32(pair);
    case 1:
        return octeon_crypto_lo32(pair);
    default:
        g_assert_not_reached();
    }
}

static void octeon_zuc_set_window_pair(MIPSOcteonCryptoState *crypto,
                                       uint32_t hi, uint32_t lo)
{
    crypto->hsh_iv[0] = octeon_zuc_pack_pair(hi, lo);
}

static uint32_t octeon_zuc_tresult(const MIPSOcteonCryptoState *crypto)
{
    return crypto->hsh_iv[3];
}

static void octeon_zuc_set_tresult(MIPSOcteonCryptoState *crypto,
                                   uint32_t value)
{
    crypto->hsh_iv[3] = value;
}

static void octeon_zuc_bit_reorganization(const MIPSOcteonCryptoState *crypto,
                                          uint32_t x[4])
{
    x[0] = ((octeon_zuc_lfsr(crypto, 15) & 0x7fff8000U) << 1) |
            (octeon_zuc_lfsr(crypto, 14) & 0xffffU);
    x[1] = ((octeon_zuc_lfsr(crypto, 11) & 0xffffU) << 16) |
            (octeon_zuc_lfsr(crypto, 9) >> 15);
    x[2] = ((octeon_zuc_lfsr(crypto, 7) & 0xffffU) << 16) |
            (octeon_zuc_lfsr(crypto, 5) >> 15);
    x[3] = ((octeon_zuc_lfsr(crypto, 2) & 0xffffU) << 16) |
            (octeon_zuc_lfsr(crypto, 0) >> 15);
}

static uint32_t octeon_zuc_l1(uint32_t x)
{
    return x ^ rol32(x, 2) ^ rol32(x, 10) ^ rol32(x, 18) ^ rol32(x, 24);
}

static uint32_t octeon_zuc_l2(uint32_t x)
{
    return x ^ rol32(x, 8) ^ rol32(x, 14) ^ rol32(x, 22) ^ rol32(x, 30);
}

static uint32_t octeon_zuc_f(MIPSOcteonCryptoState *crypto, const uint32_t x[4])
{
    uint32_t fsm0 = octeon_zuc_fsm(crypto, 0);
    uint32_t fsm1 = octeon_zuc_fsm(crypto, 1);
    uint32_t w = (x[0] ^ fsm0) + fsm1;
    uint32_t w1 = fsm0 + x[1];
    uint32_t w2 = fsm1 ^ x[2];
    uint32_t u = octeon_zuc_l1((w1 << 16) | (w2 >> 16));
    uint32_t v = octeon_zuc_l2((w2 << 16) | (w1 >> 16));

    octeon_zuc_set_fsm(crypto, 0,
                       octeon_zuc_make_u32(octeon_zuc_s0[u >> 24],
                                           octeon_zuc_s1[(uint8_t)(u >> 16)],
                                           octeon_zuc_s0[(uint8_t)(u >> 8)],
                                           octeon_zuc_s1[(uint8_t)u]));
    octeon_zuc_set_fsm(crypto, 1,
                       octeon_zuc_make_u32(octeon_zuc_s0[v >> 24],
                                           octeon_zuc_s1[(uint8_t)(v >> 16)],
                                           octeon_zuc_s0[(uint8_t)(v >> 8)],
                                           octeon_zuc_s1[(uint8_t)v]));
    return w;
}

static void octeon_zuc_lfsr_step(MIPSOcteonCryptoState *crypto,
                                 bool init_mode, uint32_t u)
{
    uint32_t lfsr[16];
    uint32_t f;

    for (int i = 0; i < 16; i++) {
        lfsr[i] = octeon_zuc_lfsr(crypto, i);
    }

    f = lfsr[0];
    f = octeon_zuc_addm(f, octeon_zuc_mul_by_pow2(lfsr[0], 8));
    f = octeon_zuc_addm(f, octeon_zuc_mul_by_pow2(lfsr[4], 20));
    f = octeon_zuc_addm(f, octeon_zuc_mul_by_pow2(lfsr[10], 21));
    f = octeon_zuc_addm(f, octeon_zuc_mul_by_pow2(lfsr[13], 17));
    f = octeon_zuc_addm(f, octeon_zuc_mul_by_pow2(lfsr[15], 15));
    if (init_mode) {
        f = octeon_zuc_addm(f, u);
    }

    for (int i = 0; i < 15; i++) {
        octeon_zuc_set_lfsr(crypto, i, lfsr[i + 1]);
    }
    octeon_zuc_set_lfsr(crypto, 15, f);
}

static uint32_t octeon_zuc_generate_word(MIPSOcteonCryptoState *crypto)
{
    uint32_t x[4];
    uint32_t z;

    octeon_zuc_bit_reorganization(crypto, x);
    z = octeon_zuc_f(crypto, x) ^ x[3];
    octeon_zuc_lfsr_step(crypto, false, 0);
    return z;
}

static void octeon_zuc_fill_window_pair(MIPSOcteonCryptoState *crypto)
{
    uint32_t z0 = octeon_zuc_generate_word(crypto);
    uint32_t z1 = octeon_zuc_generate_word(crypto);

    octeon_zuc_set_window_pair(crypto, z0, z1);
}

static uint32_t
octeon_zuc_window_word(const MIPSOcteonCryptoState *crypto, unsigned int bit,
                       uint32_t z2)
{
    if (bit == 0) {
        return octeon_zuc_window(crypto, 0);
    }
    if (bit < 32) {
        return (octeon_zuc_window(crypto, 0) << bit) |
               (octeon_zuc_window(crypto, 1) >> (32 - bit));
    }
    if (bit == 32) {
        return octeon_zuc_window(crypto, 1);
    }
    return (octeon_zuc_window(crypto, 1) << (bit - 32)) |
           (z2 >> (64 - bit));
}

static void octeon_zuc_advance_window(MIPSOcteonCryptoState *crypto,
                                      uint32_t z2)
{
    uint32_t z3 = octeon_zuc_generate_word(crypto);

    octeon_zuc_set_window_pair(crypto, z2, z3);
}

static void octeon_zuc_start(MIPSOcteonCryptoState *crypto, uint64_t data)
{
    uint32_t x[4];

    for (int i = 0; i < 14; i++) {
        octeon_zuc_set_lfsr(crypto, i, octeon_zuc_lfsr(crypto, i));
    }
    octeon_zuc_set_lfsr(crypto, 14, data >> 32);
    octeon_zuc_set_lfsr(crypto, 15, data);
    octeon_zuc_set_fsm(crypto, 0, 0);
    octeon_zuc_set_fsm(crypto, 1, 0);
    octeon_zuc_set_tresult(crypto, 0);

    for (int i = 0; i < 32; i++) {
        octeon_zuc_bit_reorganization(crypto, x);
        octeon_zuc_lfsr_step(crypto, true, octeon_zuc_f(crypto, x) >> 1);
    }

    octeon_zuc_bit_reorganization(crypto, x);
    (void)octeon_zuc_f(crypto, x);
    octeon_zuc_lfsr_step(crypto, false, 0);
    octeon_zuc_fill_window_pair(crypto);
}

static void octeon_zuc_more(MIPSOcteonCryptoState *crypto, uint64_t data)
{
    uint32_t t = octeon_zuc_tresult(crypto);
    uint32_t z2 = octeon_zuc_generate_word(crypto);

    for (unsigned int bit = 0; bit < 64; bit++) {
        if ((data >> (63 - bit)) & 1) {
            t ^= octeon_zuc_window_word(crypto, bit, z2);
        }
    }
    octeon_zuc_set_tresult(crypto, t);
    octeon_zuc_advance_window(crypto, z2);
}

static const uint8_t octeon_snow3g_sr[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5,
    0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
    0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc,
    0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
    0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
    0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b,
    0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85,
    0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
    0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17,
    0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
    0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
    0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9,
    0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6,
    0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
    0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94,
    0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68,
    0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

static const uint8_t octeon_snow3g_sq[256] = {
    0x25, 0x24, 0x73, 0x67, 0xd7, 0xae, 0x5c, 0x30,
    0xa4, 0xee, 0x6e, 0xcb, 0x7d, 0xb5, 0x82, 0xdb,
    0xe4, 0x8e, 0x48, 0x49, 0x4f, 0x5d, 0x6a, 0x78,
    0x70, 0x88, 0xe8, 0x5f, 0x5e, 0x84, 0x65, 0xe2,
    0xd8, 0xe9, 0xcc, 0xed, 0x40, 0x2f, 0x11, 0x28,
    0x57, 0xd2, 0xac, 0xe3, 0x4a, 0x15, 0x1b, 0xb9,
    0xb2, 0x80, 0x85, 0xa6, 0x2e, 0x02, 0x47, 0x29,
    0x07, 0x4b, 0x0e, 0xc1, 0x51, 0xaa, 0x89, 0xd4,
    0xca, 0x01, 0x46, 0xb3, 0xef, 0xdd, 0x44, 0x7b,
    0xc2, 0x7f, 0xbe, 0xc3, 0x9f, 0x20, 0x4c, 0x64,
    0x83, 0xa2, 0x68, 0x42, 0x13, 0xb4, 0x41, 0xcd,
    0xba, 0xc6, 0xbb, 0x6d, 0x4d, 0x71, 0x21, 0xf4,
    0x8d, 0xb0, 0xe5, 0x93, 0xfe, 0x8f, 0xe6, 0xcf,
    0x43, 0x45, 0x31, 0x22, 0x37, 0x36, 0x96, 0xfa,
    0xbc, 0x0f, 0x08, 0x52, 0x1d, 0x55, 0x1a, 0xc5,
    0x4e, 0x23, 0x69, 0x7a, 0x92, 0xff, 0x5b, 0x5a,
    0xeb, 0x9a, 0x1c, 0xa9, 0xd1, 0x7e, 0x0d, 0xfc,
    0x50, 0x8a, 0xb6, 0x62, 0xf5, 0x0a, 0xf8, 0xdc,
    0x03, 0x3c, 0x0c, 0x39, 0xf1, 0xb8, 0xf3, 0x3d,
    0xf2, 0xd5, 0x97, 0x66, 0x81, 0x32, 0xa0, 0x00,
    0x06, 0xce, 0xf6, 0xea, 0xb7, 0x17, 0xf7, 0x8c,
    0x79, 0xd6, 0xa7, 0xbf, 0x8b, 0x3f, 0x1f, 0x53,
    0x63, 0x75, 0x35, 0x2c, 0x60, 0xfd, 0x27, 0xd3,
    0x94, 0xa5, 0x7c, 0xa1, 0x05, 0x58, 0x2d, 0xbd,
    0xd9, 0xc7, 0xaf, 0x6b, 0x54, 0x0b, 0xe0, 0x38,
    0x04, 0xc8, 0x9d, 0xe7, 0x14, 0xb1, 0x87, 0x9c,
    0xdf, 0x6f, 0xf9, 0xda, 0x2a, 0xc4, 0x59, 0x16,
    0x74, 0x91, 0xab, 0x26, 0x61, 0x76, 0x34, 0x2b,
    0xad, 0x99, 0xfb, 0x72, 0xec, 0x33, 0x12, 0xde,
    0x98, 0x3b, 0xc0, 0x9b, 0x3e, 0x18, 0x10, 0x3a,
    0x56, 0xe1, 0x77, 0xc9, 0x1e, 0x9e, 0x95, 0xa3,
    0x90, 0x19, 0xa8, 0x6c, 0x09, 0xd0, 0xf0, 0x86,
};

static uint8_t octeon_snow3g_mulx(uint8_t v, uint8_t c)
{
    return (v & 0x80) ? ((v << 1) ^ c) : (v << 1);
}

static uint8_t octeon_snow3g_mulxpow(uint8_t v, unsigned int n, uint8_t c)
{
    while (n-- > 0) {
        v = octeon_snow3g_mulx(v, c);
    }
    return v;
}

static uint32_t octeon_snow3g_pack32(uint8_t b0, uint8_t b1,
                                     uint8_t b2, uint8_t b3)
{
    return ((uint32_t)b0 << 24)
         | ((uint32_t)b1 << 16)
         | ((uint32_t)b2 << 8)
         | b3;
}

static uint32_t octeon_snow3g_mulalpha(uint8_t c)
{
    return octeon_snow3g_pack32(octeon_snow3g_mulxpow(c,  23, 0xa9),
                                octeon_snow3g_mulxpow(c, 245, 0xa9),
                                octeon_snow3g_mulxpow(c,  48, 0xa9),
                                octeon_snow3g_mulxpow(c, 239, 0xa9));
}

static uint32_t octeon_snow3g_divalpha(uint8_t c)
{
    return octeon_snow3g_pack32(octeon_snow3g_mulxpow(c, 16, 0xa9),
                                octeon_snow3g_mulxpow(c, 39, 0xa9),
                                octeon_snow3g_mulxpow(c,  6, 0xa9),
                                octeon_snow3g_mulxpow(c, 64, 0xa9));
}

static uint32_t octeon_snow3g_s1(uint32_t w)
{
    uint8_t x0 = octeon_snow3g_sr[w >> 24];
    uint8_t x1 = octeon_snow3g_sr[(uint8_t)(w >> 16)];
    uint8_t x2 = octeon_snow3g_sr[(uint8_t)(w >> 8)];
    uint8_t x3 = octeon_snow3g_sr[(uint8_t)w];
    uint8_t r0 = octeon_snow3g_mulx(x0, 0x1b) ^ x1 ^ x2 ^
                 octeon_snow3g_mulx(x3, 0x1b) ^ x3;
    uint8_t r1 = octeon_snow3g_mulx(x0, 0x1b) ^ x0 ^
                 octeon_snow3g_mulx(x1, 0x1b) ^ x2 ^ x3;
    uint8_t r2 = x0 ^ octeon_snow3g_mulx(x1, 0x1b) ^ x1 ^
                 octeon_snow3g_mulx(x2, 0x1b) ^ x3;
    uint8_t r3 = x0 ^ x1 ^ octeon_snow3g_mulx(x2, 0x1b) ^ x2 ^
                 octeon_snow3g_mulx(x3, 0x1b);

    return octeon_snow3g_pack32(r0, r1, r2, r3);
}

static uint32_t octeon_snow3g_s2(uint32_t w)
{
    uint8_t x0 = octeon_snow3g_sq[w >> 24];
    uint8_t x1 = octeon_snow3g_sq[(uint8_t)(w >> 16)];
    uint8_t x2 = octeon_snow3g_sq[(uint8_t)(w >> 8)];
    uint8_t x3 = octeon_snow3g_sq[(uint8_t)w];
    uint8_t r0 = octeon_snow3g_mulx(x0, 0x69) ^ x1 ^ x2 ^
                 octeon_snow3g_mulx(x3, 0x69) ^ x3;
    uint8_t r1 = octeon_snow3g_mulx(x0, 0x69) ^ x0 ^
                 octeon_snow3g_mulx(x1, 0x69) ^ x2 ^ x3;
    uint8_t r2 = x0 ^ octeon_snow3g_mulx(x1, 0x69) ^ x1 ^
                 octeon_snow3g_mulx(x2, 0x69) ^ x3;
    uint8_t r3 = x0 ^ x1 ^ octeon_snow3g_mulx(x2, 0x69) ^ x2 ^
                 octeon_snow3g_mulx(x3, 0x69);

    return octeon_snow3g_pack32(r0, r1, r2, r3);
}

static uint32_t octeon_snow3g_lfsr(const MIPSOcteonCryptoState *crypto,
                                   unsigned int index)
{
    uint64_t pair = crypto->hsh_dat[index / 2];

    return index & 1 ? octeon_crypto_lo32(pair) : octeon_crypto_hi32(pair);
}

static void octeon_snow3g_set_lfsr(MIPSOcteonCryptoState *crypto,
                                   unsigned int index, uint32_t value)
{
    uint32_t hi = octeon_crypto_hi32(crypto->hsh_dat[index / 2]);
    uint32_t lo = octeon_crypto_lo32(crypto->hsh_dat[index / 2]);

    if (index & 1) {
        lo = value;
    } else {
        hi = value;
    }
    crypto->hsh_dat[index / 2] = octeon_crypto_pack32(hi, lo);
}

static uint32_t octeon_snow3g_fsm(const MIPSOcteonCryptoState *crypto,
                                  unsigned int index)
{
    return crypto->hsh_iv[1 + index];
}

static void octeon_snow3g_set_fsm(MIPSOcteonCryptoState *crypto,
                                  unsigned int index, uint32_t value)
{
    crypto->hsh_iv[1 + index] = value;
}

static uint32_t octeon_snow3g_clock_fsm(MIPSOcteonCryptoState *crypto)
{
    uint32_t fsm0 = octeon_snow3g_fsm(crypto, 0);
    uint32_t fsm1 = octeon_snow3g_fsm(crypto, 1);
    uint32_t fsm2 = octeon_snow3g_fsm(crypto, 2);
    uint32_t f = (uint32_t)(octeon_snow3g_lfsr(crypto, 15) + fsm0) ^ fsm1;
    uint32_t r = (uint32_t)(fsm1 + (fsm2 ^ octeon_snow3g_lfsr(crypto, 5)));

    octeon_snow3g_set_fsm(crypto, 2, octeon_snow3g_s2(fsm1));
    octeon_snow3g_set_fsm(crypto, 1, octeon_snow3g_s1(fsm0));
    octeon_snow3g_set_fsm(crypto, 0, r);
    return f;
}

static void octeon_snow3g_clock_lfsr(MIPSOcteonCryptoState *crypto,
                                     bool init_mode, uint32_t f)
{
    uint32_t lfsr[16];
    uint32_t s0;
    uint32_t s11;
    uint32_t v;
    int i;

    for (i = 0; i < 16; i++) {
        lfsr[i] = octeon_snow3g_lfsr(crypto, i);
    }

    s0 = lfsr[0];
    s11 = lfsr[11];
    v = (s0 << 8) ^ octeon_snow3g_mulalpha(s0 >> 24) ^
        lfsr[2] ^ (s11 >> 8) ^ octeon_snow3g_divalpha((uint8_t)s11);

    if (init_mode) {
        v ^= f;
    }

    for (i = 0; i < 15; i++) {
        octeon_snow3g_set_lfsr(crypto, i, lfsr[i + 1]);
    }
    octeon_snow3g_set_lfsr(crypto, 15, v);
}

static uint32_t octeon_snow3g_generate_word(MIPSOcteonCryptoState *crypto)
{
    uint32_t f = octeon_snow3g_clock_fsm(crypto);
    uint32_t z = f ^ octeon_snow3g_lfsr(crypto, 0);

    octeon_snow3g_clock_lfsr(crypto, false, 0);
    return z;
}

static void octeon_snow3g_queue_result(MIPSOcteonCryptoState *crypto)
{
    uint32_t z0 = octeon_snow3g_generate_word(crypto);
    uint32_t z1 = octeon_snow3g_generate_word(crypto);

    crypto->hsh_iv[0] = octeon_crypto_pack32(z0, z1);
}

static void octeon_snow3g_start(MIPSOcteonCryptoState *crypto, uint64_t data)
{
    int i;

    for (i = 0; i < 14; i++) {
        octeon_snow3g_set_lfsr(crypto, i, octeon_snow3g_lfsr(crypto, i));
    }
    octeon_snow3g_set_lfsr(crypto, 14, data >> 32);
    octeon_snow3g_set_lfsr(crypto, 15, data);
    for (i = 0; i < 3; i++) {
        octeon_snow3g_set_fsm(crypto, i, 0);
    }

    for (i = 0; i < 32; i++) {
        uint32_t f = octeon_snow3g_clock_fsm(crypto);

        octeon_snow3g_clock_lfsr(crypto, true, f);
    }

    (void)octeon_snow3g_clock_fsm(crypto);
    octeon_snow3g_clock_lfsr(crypto, false, 0);
    octeon_snow3g_queue_result(crypto);
}

static void octeon_snow3g_more(MIPSOcteonCryptoState *crypto)
{
    octeon_snow3g_queue_result(crypto);
}

static int octeon_aes_key_bits(const MIPSOcteonCryptoState *crypto)
{
    enum {
        OCTEON_AES_KEYLEN_128 = 1,
        OCTEON_AES_KEYLEN_192 = 2,
        OCTEON_AES_KEYLEN_256 = 3,
    };

    switch (crypto->aes_keylen) {
    case OCTEON_AES_KEYLEN_128:
        return 128;
    case OCTEON_AES_KEYLEN_192:
        return 192;
    case OCTEON_AES_KEYLEN_256:
        return 256;
    default:
        return 0;
    }
}

static void octeon_aes_load_key(const MIPSOcteonCryptoState *crypto,
                                uint8_t *key, size_t keylen)
{
    stq_be_p(key, crypto->aes_key[0]);
    stq_be_p(key + 8, crypto->aes_key[1]);
    if (keylen > 16) {
        stq_be_p(key + 16, crypto->aes_key[2]);
    }
    if (keylen > 24) {
        stq_be_p(key + 24, crypto->aes_key[3]);
    }
}

static void octeon_aes_load_block(const uint64_t regs[2], uint8_t *block)
{
    stq_be_p(block, regs[0]);
    stq_be_p(block + 8, regs[1]);
}

static void octeon_aes_store_block(uint64_t regs[2], const uint8_t *block)
{
    regs[0] = ldq_be_p(block);
    regs[1] = ldq_be_p(block + 8);
}

static void octeon_aes_encrypt_common(MIPSOcteonCryptoState *crypto, bool cbc)
{
    AES_KEY key;
    uint8_t in[16];
    uint8_t out[16];
    uint8_t iv[16];
    uint8_t raw_key[32] = {};
    int bits = octeon_aes_key_bits(crypto);

    if (!bits) {
        return;
    }

    octeon_aes_load_key(crypto, raw_key, bits / 8);
    octeon_aes_load_block(crypto->aes_resinp, in);
    if (cbc) {
        int i;

        octeon_aes_load_block(crypto->aes_iv, iv);
        for (i = 0; i < sizeof(in); i++) {
            in[i] ^= iv[i];
        }
    }

    AES_set_encrypt_key(raw_key, bits, &key);
    AES_encrypt(in, out, &key);
    octeon_aes_store_block(crypto->aes_resinp, out);
    if (cbc) {
        octeon_aes_store_block(crypto->aes_iv, out);
    }
}

static void octeon_aes_decrypt_common(MIPSOcteonCryptoState *crypto, bool cbc)
{
    AES_KEY key;
    uint8_t in[16];
    uint8_t out[16];
    uint8_t iv[16];
    uint8_t next_iv[16];
    uint8_t raw_key[32] = {};
    int bits = octeon_aes_key_bits(crypto);
    int i;

    if (!bits) {
        return;
    }

    octeon_aes_load_key(crypto, raw_key, bits / 8);
    octeon_aes_load_block(crypto->aes_resinp, in);
    if (cbc) {
        memcpy(next_iv, in, sizeof(next_iv));
        octeon_aes_load_block(crypto->aes_iv, iv);
    }

    AES_set_decrypt_key(raw_key, bits, &key);
    AES_decrypt(in, out, &key);
    if (cbc) {
        for (i = 0; i < sizeof(out); i++) {
            out[i] ^= iv[i];
        }
    }

    octeon_aes_store_block(crypto->aes_resinp, out);
    if (cbc) {
        octeon_aes_store_block(crypto->aes_iv, next_iv);
    }
}

void helper_octeon_cp2_mt_aes_enc_cbc1(CPUMIPSState *env, uint64_t value)
{
    MIPSOcteonCryptoState *crypto = &env->octeon_crypto;

    crypto->aes_resinp[1] = value;
    octeon_aes_encrypt_common(crypto, true);
}

void helper_octeon_cp2_mt_aes_enc1(CPUMIPSState *env, uint64_t value)
{
    MIPSOcteonCryptoState *crypto = &env->octeon_crypto;

    crypto->aes_resinp[1] = value;
    octeon_aes_encrypt_common(crypto, false);
}

void helper_octeon_cp2_mt_aes_dec_cbc1(CPUMIPSState *env, uint64_t value)
{
    MIPSOcteonCryptoState *crypto = &env->octeon_crypto;

    crypto->aes_resinp[1] = value;
    octeon_aes_decrypt_common(crypto, true);
}

void helper_octeon_cp2_mt_aes_dec1(CPUMIPSState *env, uint64_t value)
{
    MIPSOcteonCryptoState *crypto = &env->octeon_crypto;

    crypto->aes_resinp[1] = value;
    octeon_aes_decrypt_common(crypto, false);
}

static uint32_t octeon_sms4_t(uint32_t x)
{
    x = sm4_subword(x);
    return x ^ rol32(x, 2) ^ rol32(x, 10) ^ rol32(x, 18) ^ rol32(x, 24);
}

static uint32_t octeon_sms4_t_key(uint32_t x)
{
    x = sm4_subword(x);
    return x ^ rol32(x, 13) ^ rol32(x, 23);
}

static void octeon_sms4_expand_key(const uint8_t *key, uint32_t round_keys[32])
{
    static const uint32_t fk[4] = {
        0xa3b1bac6U, 0x56aa3350U, 0x677d9197U, 0xb27022dcU,
    };
    uint32_t k[36];

    for (int i = 0; i < 4; i++) {
        k[i] = ldl_be_p(key + i * 4) ^ fk[i];
    }
    for (int i = 0; i < 32; i++) {
        k[i + 4] = k[i] ^ octeon_sms4_t_key(k[i + 1] ^ k[i + 2] ^
                                            k[i + 3] ^ sm4_ck[i]);
        round_keys[i] = k[i + 4];
    }
}

static void octeon_sms4_crypt_block(const uint8_t *in, uint8_t *out,
                                    const uint32_t round_keys[32],
                                    bool encrypt)
{
    uint32_t x[36];

    for (int i = 0; i < 4; i++) {
        x[i] = ldl_be_p(in + i * 4);
    }
    for (int i = 0; i < 32; i++) {
        uint32_t rk = round_keys[encrypt ? i : 31 - i];

        x[i + 4] = x[i] ^ octeon_sms4_t(x[i + 1] ^ x[i + 2] ^
                                        x[i + 3] ^ rk);
    }
    stl_be_p(out, x[35]);
    stl_be_p(out + 4, x[34]);
    stl_be_p(out + 8, x[33]);
    stl_be_p(out + 12, x[32]);
}

static void octeon_sms4_crypt_common(MIPSOcteonCryptoState *crypto,
                                     bool encrypt, bool cbc)
{
    uint8_t key[16];
    uint8_t in[16];
    uint8_t out[16];
    uint8_t iv[16];
    uint8_t next_iv[16];
    uint32_t round_keys[32];

    /*
     * SMS4 aliases the AES state onto the RESINP, IV, and KEY banks,
     * with only the operation selectors remaining distinct.
     */
    octeon_aes_load_key(crypto, key, sizeof(key));
    octeon_aes_load_block(crypto->aes_resinp, in);
    if (cbc) {
        octeon_aes_load_block(crypto->aes_iv, iv);
        if (encrypt) {
            for (int i = 0; i < sizeof(in); i++) {
                in[i] ^= iv[i];
            }
        } else {
            memcpy(next_iv, in, sizeof(next_iv));
        }
    }

    octeon_sms4_expand_key(key, round_keys);
    octeon_sms4_crypt_block(in, out, round_keys, encrypt);
    if (cbc && !encrypt) {
        for (int i = 0; i < sizeof(out); i++) {
            out[i] ^= iv[i];
        }
    }

    octeon_aes_store_block(crypto->aes_resinp, out);
    if (cbc) {
        octeon_aes_store_block(crypto->aes_iv, encrypt ? out : next_iv);
    }
}

void helper_octeon_cp2_mt_sms4_enc_cbc1(CPUMIPSState *env, uint64_t value)
{
    MIPSOcteonCryptoState *crypto = &env->octeon_crypto;

    crypto->aes_resinp[1] = value;
    octeon_sms4_crypt_common(crypto, true, true);
}

void helper_octeon_cp2_mt_sms4_enc1(CPUMIPSState *env, uint64_t value)
{
    MIPSOcteonCryptoState *crypto = &env->octeon_crypto;

    crypto->aes_resinp[1] = value;
    octeon_sms4_crypt_common(crypto, true, false);
}

void helper_octeon_cp2_mt_sms4_dec_cbc1(CPUMIPSState *env, uint64_t value)
{
    MIPSOcteonCryptoState *crypto = &env->octeon_crypto;

    crypto->aes_resinp[1] = value;
    octeon_sms4_crypt_common(crypto, false, true);
}

void helper_octeon_cp2_mt_sms4_dec1(CPUMIPSState *env, uint64_t value)
{
    MIPSOcteonCryptoState *crypto = &env->octeon_crypto;

    crypto->aes_resinp[1] = value;
    octeon_sms4_crypt_common(crypto, false, false);
}

static const uint8_t octeon_des_ip[64] = {
    58, 50, 42, 34, 26, 18, 10,  2,
    60, 52, 44, 36, 28, 20, 12,  4,
    62, 54, 46, 38, 30, 22, 14,  6,
    64, 56, 48, 40, 32, 24, 16,  8,
    57, 49, 41, 33, 25, 17,  9,  1,
    59, 51, 43, 35, 27, 19, 11,  3,
    61, 53, 45, 37, 29, 21, 13,  5,
    63, 55, 47, 39, 31, 23, 15,  7,
};

static const uint8_t octeon_des_fp[64] = {
    40,  8, 48, 16, 56, 24, 64, 32,
    39,  7, 47, 15, 55, 23, 63, 31,
    38,  6, 46, 14, 54, 22, 62, 30,
    37,  5, 45, 13, 53, 21, 61, 29,
    36,  4, 44, 12, 52, 20, 60, 28,
    35,  3, 43, 11, 51, 19, 59, 27,
    34,  2, 42, 10, 50, 18, 58, 26,
    33,  1, 41,  9, 49, 17, 57, 25,
};

static const uint8_t octeon_des_e[48] = {
    32,  1,  2,  3,  4,  5,
     4,  5,  6,  7,  8,  9,
     8,  9, 10, 11, 12, 13,
    12, 13, 14, 15, 16, 17,
    16, 17, 18, 19, 20, 21,
    20, 21, 22, 23, 24, 25,
    24, 25, 26, 27, 28, 29,
    28, 29, 30, 31, 32,  1,
};

static const uint8_t octeon_des_p[32] = {
    16,  7, 20, 21, 29, 12, 28, 17,
     1, 15, 23, 26,  5, 18, 31, 10,
     2,  8, 24, 14, 32, 27,  3,  9,
    19, 13, 30,  6, 22, 11,  4, 25,
};

static const uint8_t octeon_des_pc1[56] = {
    57, 49, 41, 33, 25, 17,  9,
     1, 58, 50, 42, 34, 26, 18,
    10,  2, 59, 51, 43, 35, 27,
    19, 11,  3, 60, 52, 44, 36,
    63, 55, 47, 39, 31, 23, 15,
     7, 62, 54, 46, 38, 30, 22,
    14,  6, 61, 53, 45, 37, 29,
    21, 13,  5, 28, 20, 12,  4,
};

static const uint8_t octeon_des_pc2[48] = {
    14, 17, 11, 24,  1,  5,
     3, 28, 15,  6, 21, 10,
    23, 19, 12,  4, 26,  8,
    16,  7, 27, 20, 13,  2,
    41, 52, 31, 37, 47, 55,
    30, 40, 51, 45, 33, 48,
    44, 49, 39, 56, 34, 53,
    46, 42, 50, 36, 29, 32,
};

static const uint8_t octeon_des_rotations[16] = {
    1, 1, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 1,
};

static const uint8_t octeon_des_sboxes[8][64] = {
    {
        14, 4, 13, 1,  2, 15, 11, 8,  3, 10, 6, 12, 5, 9, 0, 7,
         0, 15, 7, 4, 14,  2, 13, 1, 10,  6, 12, 11, 9, 5, 3, 8,
         4, 1, 14, 8, 13,  6,  2, 11, 15, 12, 9,  7, 3, 10, 5, 0,
        15, 12, 8, 2,  4,  9,  1, 7,  5, 11, 3, 14, 10, 0, 6, 13,
    },
    {
        15, 1,  8, 14, 6, 11, 3, 4,  9, 7, 2, 13, 12, 0, 5, 10,
         3, 13, 4, 7, 15, 2,  8, 14, 12, 0, 1, 10,  6, 9, 11, 5,
         0, 14, 7, 11, 10, 4, 13, 1,  5, 8, 12, 6,  9, 3,  2, 15,
        13, 8, 10, 1,  3, 15, 4, 2, 11, 6, 7, 12,  0, 5, 14, 9,
    },
    {
        10, 0,  9, 14, 6, 3, 15, 5,  1, 13, 12, 7, 11, 4, 2, 8,
        13, 7,  0, 9,  3, 4, 6,  10, 2, 8,  5, 14, 12, 11, 15, 1,
        13, 6,  4, 9,  8, 15, 3, 0, 11, 1,  2, 12,  5, 10, 14, 7,
         1, 10, 13, 0,  6, 9, 8, 7,  4, 15, 14, 3, 11, 5,  2, 12,
    },
    {
         7, 13, 14, 3,  0, 6, 9, 10, 1, 2, 8,  5, 11, 12, 4, 15,
        13, 8,  11, 5,  6, 15, 0, 3,  4, 7, 2, 12, 1,  10, 14, 9,
        10, 6,  9,  0, 12, 11, 7, 13, 15, 1, 3, 14, 5,  2,  8,  4,
         3, 15, 0,  6, 10, 1, 13, 8,  9, 4, 5, 11, 12, 7,  2,  14,
    },
    {
         2, 12, 4,  1,  7, 10, 11, 6,  8, 5, 3, 15, 13, 0, 14, 9,
        14, 11, 2,  12, 4, 7,  13, 1,  5, 0, 15, 10, 3,  9, 8,  6,
         4, 2,  1,  11, 10, 13, 7, 8, 15, 9, 12, 5,  6,  3, 0, 14,
        11, 8,  12, 7,  1, 14, 2, 13, 6, 15, 0,  9, 10, 4, 5,  3,
    },
    {
        12, 1,  10, 15, 9, 2,  6, 8,  0, 13, 3, 4, 14, 7, 5, 11,
        10, 15, 4,  2,  7, 12, 9, 5,  6, 1,  13, 14, 0, 11, 3, 8,
         9, 14, 15, 5,  2, 8,  12, 3,  7, 0,  4, 10, 1, 13, 11, 6,
         4, 3,  2,  12, 9, 5,  15, 10, 11, 14, 1, 7,  6, 0,  8, 13,
    },
    {
         4, 11, 2, 14, 15, 0, 8, 13, 3, 12, 9, 7,  5, 10, 6, 1,
        13, 0,  11, 7,  4, 9, 1, 10, 14, 3, 5, 12, 2, 15, 8, 6,
         1, 4,  11, 13, 12, 3, 7, 14, 10, 15, 6, 8,  0, 5,  9, 2,
         6, 11, 13, 8,  1, 4, 10, 7,  9, 5,  0, 15, 14, 2,  3, 12,
    },
    {
        13, 2,  8, 4,  6, 15, 11, 1, 10, 9, 3, 14, 5, 0, 12, 7,
         1, 15, 13, 8, 10, 3,  7,  4, 12, 5, 6, 11, 0, 14, 9,  2,
         7, 11, 4,  1,  9, 12, 14, 2, 0,  6, 10, 13, 15, 3, 5, 8,
         2, 1,  14, 7,  4, 10, 8,  13, 15, 12, 9, 0,  3,  5, 6, 11,
    },
};

static const uint8_t octeon_kasumi_s7[128] = {
     54,  50,  62,  56,  22,  34,  94,  96,  38,   6,  63,  93,   2,  18,
    123,  33,  55, 113,  39, 114,  21,  67,  65,  12,  47,  73,  46,  27,
     25, 111, 124,  81,  53,   9, 121,  79,  52,  60,  58,  48, 101, 127,
     40, 120, 104,  70,  71,  43,  20, 122,  72,  61,  23, 109,  13, 100,
     77,   1,  16,   7,  82,  10, 105,  98, 117, 116,  76,  11,  89, 106,
      0, 125, 118,  99,  86,  69,  30,  57, 126,  87, 112,  51,  17,   5,
     95,  14,  90,  84,  91,   8,  35, 103,  32,  97,  28,  66, 102,  31,
     26,  45,  75,   4,  85,  92,  37,  74,  80,  49,  68,  29, 115,  44,
     64, 107, 108,  24, 110,  83,  36,  78,  42,  19,  15,  41,  88, 119,
     59,   3,
};

static const uint16_t octeon_kasumi_s9[512] = {
    167, 239, 161, 379, 391, 334,   9, 338,  38, 226,  48, 358, 452, 385,
     90, 397, 183, 253, 147, 331, 415, 340,  51, 362, 306, 500, 262,  82,
    216, 159, 356, 177, 175, 241, 489,  37, 206,  17,   0, 333,  44, 254,
    378,  58, 143, 220,  81, 400,  95,   3, 315, 245,  54, 235, 218, 405,
    472, 264, 172, 494, 371, 290, 399,  76, 165, 197, 395, 121, 257, 480,
    423, 212, 240,  28, 462, 176, 406, 507, 288, 223, 501, 407, 249, 265,
     89, 186, 221, 428, 164,  74, 440, 196, 458, 421, 350, 163, 232, 158,
    134, 354,  13, 250, 491, 142, 191,  69, 193, 425, 152, 227, 366, 135,
    344, 300, 276, 242, 437, 320, 113, 278,  11, 243,  87, 317,  36,  93,
    496,  27, 487, 446, 482,  41,  68, 156, 457, 131, 326, 403, 339,  20,
     39, 115, 442, 124, 475, 384, 508,  53, 112, 170, 479, 151, 126, 169,
     73, 268, 279, 321, 168, 364, 363, 292,  46, 499, 393, 327, 324,  24,
    456, 267, 157, 460, 488, 426, 309, 229, 439, 506, 208, 271, 349, 401,
    434, 236,  16, 209, 359,  52,  56, 120, 199, 277, 465, 416, 252, 287,
    246,   6,  83, 305, 420, 345, 153, 502,  65,  61, 244, 282, 173, 222,
    418,  67, 386, 368, 261, 101, 476, 291, 195, 430,  49,  79, 166, 330,
    280, 383, 373, 128, 382, 408, 155, 495, 367, 388, 274, 107, 459, 417,
     62, 454, 132, 225, 203, 316, 234,  14, 301,  91, 503, 286, 424, 211,
    347, 307, 140, 374,  35, 103, 125, 427,  19, 214, 453, 146, 498, 314,
    444, 230, 256, 329, 198, 285,  50, 116,  78, 410,  10, 205, 510, 171,
    231,  45, 139, 467,  29,  86, 505,  32,  72,  26, 342, 150, 313, 490,
    431, 238, 411, 325, 149, 473,  40, 119, 174, 355, 185, 233, 389,  71,
    448, 273, 372,  55, 110, 178, 322,  12, 469, 392, 369, 190,   1, 109,
    375, 137, 181,  88,  75, 308, 260, 484,  98, 272, 370, 275, 412, 111,
    336, 318,   4, 504, 492, 259, 304,  77, 337, 435,  21, 357, 303, 332,
    483,  18,  47,  85,  25, 497, 474, 289, 100, 269, 296, 478, 270, 106,
     31, 104, 433,  84, 414, 486, 394,  96,  99, 154, 511, 148, 413, 361,
    409, 255, 162, 215, 302, 201, 266, 351, 343, 144, 441, 365, 108, 298,
    251,  34, 182, 509, 138, 210, 335, 133, 311, 352, 328, 141, 396, 346,
    123, 319, 450, 281, 429, 228, 443, 481,  92, 404, 485, 422, 248, 297,
     23, 213, 130, 466,  22, 217, 283,  70, 294, 360, 419, 127, 312, 377,
      7, 468, 194,   2, 117, 295, 463, 258, 224, 447, 247, 187,  80, 398,
    284, 353, 105, 390, 299, 471, 470, 184,  57, 200, 348,  63, 204, 188,
     33, 451,  97,  30, 310, 219,  94, 160, 129, 493,  64, 179, 263, 102,
    189, 207, 114, 402, 438, 477, 387, 122, 192,  42, 381,   5, 145, 118,
    180, 449, 293, 323, 136, 380,  43,  66,  60, 455, 341, 445, 202, 432,
      8, 237,  15, 376, 436, 464,  59, 461,
};

static const uint16_t octeon_kasumi_constants[8] = {
    0x0123, 0x4567, 0x89ab, 0xcdef, 0xfedc, 0xba98, 0x7654, 0x3210,
};

typedef struct OcteonKasumiSubkeys {
    uint16_t kli1[8];
    uint16_t kli2[8];
    uint16_t koi1[8];
    uint16_t koi2[8];
    uint16_t koi3[8];
    uint16_t kii1[8];
    uint16_t kii2[8];
    uint16_t kii3[8];
} OcteonKasumiSubkeys;

static uint64_t octeon_des_permute(uint64_t input, const uint8_t *table,
                                   size_t output_bits, size_t input_bits)
{
    uint64_t out = 0;

    for (size_t i = 0; i < output_bits; i++) {
        unsigned src = table[i] - 1;

        out = (out << 1) | ((input >> (input_bits - 1 - src)) & 1);
    }
    return out;
}

static uint32_t octeon_des_rotate28(uint32_t v, unsigned shift)
{
    return ((v << shift) | (v >> (28 - shift))) & 0x0fffffffU;
}

static void octeon_des_expand_subkeys(uint64_t key, uint64_t subkeys[16])
{
    uint64_t permuted = octeon_des_permute(key, octeon_des_pc1,
                                           ARRAY_SIZE(octeon_des_pc1), 64);
    uint32_t c = (permuted >> 28) & 0x0fffffffU;
    uint32_t d = permuted & 0x0fffffffU;

    for (int i = 0; i < 16; i++) {
        c = octeon_des_rotate28(c, octeon_des_rotations[i]);
        d = octeon_des_rotate28(d, octeon_des_rotations[i]);
        subkeys[i] = octeon_des_permute(((uint64_t)c << 28) | d,
                                        octeon_des_pc2,
                                        ARRAY_SIZE(octeon_des_pc2), 56);
    }
}

static uint32_t octeon_des_f(uint32_t r, uint64_t subkey)
{
    uint64_t expanded = octeon_des_permute(r, octeon_des_e,
                                           ARRAY_SIZE(octeon_des_e), 32);
    uint32_t out = 0;

    expanded ^= subkey;
    for (int i = 0; i < 8; i++) {
        uint8_t sextet = (expanded >> (42 - i * 6)) & 0x3f;
        uint8_t row = ((sextet & 0x20) >> 4) | (sextet & 0x01);
        uint8_t col = (sextet >> 1) & 0x0f;

        out = (out << 4) | octeon_des_sboxes[i][row * 16 + col];
    }

    return octeon_des_permute(out, octeon_des_p, ARRAY_SIZE(octeon_des_p), 32);
}

static uint64_t octeon_des_block_crypt(uint64_t block, uint64_t key,
                                       bool encrypt)
{
    uint64_t subkeys[16];
    uint64_t permuted = octeon_des_permute(block, octeon_des_ip,
                                           ARRAY_SIZE(octeon_des_ip), 64);
    uint32_t l = permuted >> 32;
    uint32_t r = permuted;

    octeon_des_expand_subkeys(key, subkeys);

    for (int i = 0; i < 16; i++) {
        uint32_t next = l ^ octeon_des_f(r, subkeys[encrypt ? i : 15 - i]);

        l = r;
        r = next;
    }

    return octeon_des_permute(((uint64_t)r << 32) | l,
                              octeon_des_fp, ARRAY_SIZE(octeon_des_fp), 64);
}

static uint64_t octeon_3des_block_crypt(uint64_t block, const uint64_t keys[3],
                                        bool encrypt)
{
    if (encrypt) {
        block = octeon_des_block_crypt(block, keys[0], true);
        block = octeon_des_block_crypt(block, keys[1], false);
        block = octeon_des_block_crypt(block, keys[2], true);
    } else {
        block = octeon_des_block_crypt(block, keys[2], false);
        block = octeon_des_block_crypt(block, keys[1], true);
        block = octeon_des_block_crypt(block, keys[0], false);
    }
    return block;
}

static void octeon_3des_crypt_common(MIPSOcteonCryptoState *crypto,
                                     uint64_t input_reg,
                                     bool encrypt, bool cbc)
{
    const uint64_t keys[3] = {
        crypto->des3_key[0],
        crypto->des3_key[1],
        crypto->des3_key[2],
    };
    uint64_t block = input_reg;

    if (cbc) {
        if (encrypt) {
            block ^= crypto->des3_iv;
            block = octeon_3des_block_crypt(block, keys, true);
            crypto->des3_iv = block;
        } else {
            block = octeon_3des_block_crypt(block, keys, false);
            block ^= crypto->des3_iv;
            crypto->des3_iv = input_reg;
        }
    } else {
        block = octeon_3des_block_crypt(block, keys, encrypt);
    }

    crypto->des3_result = block;
}

static uint16_t octeon_rol16(uint16_t value, unsigned int bits)
{
    return (value << bits) | (value >> (16 - bits));
}

static void octeon_kasumi_key_schedule(const uint64_t key_regs[2],
                                       OcteonKasumiSubkeys *subkeys)
{
    uint16_t key[8];
    uint16_t key_prime[8];

    key[0] = key_regs[0] >> 48;
    key[1] = key_regs[0] >> 32;
    key[2] = key_regs[0] >> 16;
    key[3] = key_regs[0];
    key[4] = key_regs[1] >> 48;
    key[5] = key_regs[1] >> 32;
    key[6] = key_regs[1] >> 16;
    key[7] = key_regs[1];

    for (int i = 0; i < 8; i++) {
        key_prime[i] = key[i] ^ octeon_kasumi_constants[i];
    }

    for (int i = 0; i < 8; i++) {
        subkeys->kli1[i] = octeon_rol16(key[i], 1);
        subkeys->kli2[i] = key_prime[(i + 2) & 7];
        subkeys->koi1[i] = octeon_rol16(key[(i + 1) & 7], 5);
        subkeys->koi2[i] = octeon_rol16(key[(i + 5) & 7], 8);
        subkeys->koi3[i] = octeon_rol16(key[(i + 6) & 7], 13);
        subkeys->kii1[i] = key_prime[(i + 4) & 7];
        subkeys->kii2[i] = key_prime[(i + 3) & 7];
        subkeys->kii3[i] = key_prime[(i + 7) & 7];
    }
}

static uint16_t octeon_kasumi_fi(uint16_t in, uint16_t subkey)
{
    uint16_t nine = in >> 7;
    uint16_t seven = in & 0x7f;

    nine = octeon_kasumi_s9[nine] ^ seven;
    seven = octeon_kasumi_s7[seven] ^ (nine & 0x7f);
    seven ^= subkey >> 9;
    nine ^= subkey & 0x1ff;
    nine = octeon_kasumi_s9[nine] ^ seven;
    seven = octeon_kasumi_s7[seven] ^ (nine & 0x7f);
    return (seven << 9) | nine;
}

static uint32_t octeon_kasumi_fo(uint32_t in, int index,
                                 const OcteonKasumiSubkeys *subkeys)
{
    uint16_t left = in >> 16;
    uint16_t right = in;

    left ^= subkeys->koi1[index];
    left = octeon_kasumi_fi(left, subkeys->kii1[index]);
    left ^= right;
    right ^= subkeys->koi2[index];
    right = octeon_kasumi_fi(right, subkeys->kii2[index]);
    right ^= left;
    left ^= subkeys->koi3[index];
    left = octeon_kasumi_fi(left, subkeys->kii3[index]);
    left ^= right;

    return ((uint32_t)right << 16) | left;
}

static uint32_t octeon_kasumi_fl(uint32_t in, int index,
                                 const OcteonKasumiSubkeys *subkeys)
{
    uint16_t left = in >> 16;
    uint16_t right = in;
    uint16_t a = left & subkeys->kli1[index];
    uint16_t b;

    right ^= octeon_rol16(a, 1);
    b = right | subkeys->kli2[index];
    left ^= octeon_rol16(b, 1);
    return ((uint32_t)left << 16) | right;
}

static uint64_t octeon_kasumi_block_encrypt(uint64_t block,
                                            const uint64_t key_regs[2])
{
    OcteonKasumiSubkeys subkeys;
    uint32_t left = block >> 32;
    uint32_t right = block;

    octeon_kasumi_key_schedule(key_regs, &subkeys);

    for (int i = 0; i < 8; ) {
        uint32_t temp = octeon_kasumi_fl(left, i, &subkeys);

        temp = octeon_kasumi_fo(temp, i++, &subkeys);
        right ^= temp;
        temp = octeon_kasumi_fo(right, i, &subkeys);
        temp = octeon_kasumi_fl(temp, i++, &subkeys);
        left ^= temp;
    }

    return ((uint64_t)left << 32) | right;
}

static void octeon_kasumi_crypt_common(MIPSOcteonCryptoState *crypto,
                                       uint64_t input_reg, bool cbc)
{
    const uint64_t key_regs[2] = {
        crypto->des3_key[0],
        crypto->des3_key[1],
    };
    uint64_t block = input_reg;

    if (cbc) {
        block ^= crypto->des3_iv;
    }

    block = octeon_kasumi_block_encrypt(block, key_regs);
    if (cbc) {
        crypto->des3_iv = block;
    }
    crypto->des3_result = block;
}

void helper_octeon_cp2_mt_des3_enc_cbc(CPUMIPSState *env, uint64_t value)
{
    octeon_3des_crypt_common(&env->octeon_crypto, value, true, true);
}

void helper_octeon_cp2_mt_kas_enc_cbc(CPUMIPSState *env, uint64_t value)
{
    octeon_kasumi_crypt_common(&env->octeon_crypto, value, true);
}

void helper_octeon_cp2_mt_des3_enc(CPUMIPSState *env, uint64_t value)
{
    octeon_3des_crypt_common(&env->octeon_crypto, value, true, false);
}

void helper_octeon_cp2_mt_kas_enc(CPUMIPSState *env, uint64_t value)
{
    octeon_kasumi_crypt_common(&env->octeon_crypto, value, false);
}

void helper_octeon_cp2_mt_des3_dec_cbc(CPUMIPSState *env, uint64_t value)
{
    octeon_3des_crypt_common(&env->octeon_crypto, value, false, true);
}

void helper_octeon_cp2_mt_des3_dec(CPUMIPSState *env, uint64_t value)
{
    octeon_3des_crypt_common(&env->octeon_crypto, value, false, false);
}

static const uint8_t camellia_sbox1[256] = {
    112, 130,  44, 236, 179,  39, 192, 229, 228, 133,  87,  53, 234,  12,
    174,  65,  35, 239, 107, 147,  69,  25, 165,  33, 237,  14,  79,  78,
     29, 101, 146, 189, 134, 184, 175, 143, 124, 235,  31, 206,  62,  48,
    220,  95,  94, 197,  11,  26, 166, 225,  57, 202, 213,  71,  93,  61,
    217,   1,  90, 214,  81,  86, 108,  77, 139,  13, 154, 102, 251, 204,
    176,  45, 116,  18,  43,  32, 240, 177, 132, 153, 223,  76, 203, 194,
     52, 126, 118,   5, 109, 183, 169,  49, 209,  23,   4, 215,  20,  88,
     58,  97, 222,  27,  17,  28,  50,  15, 156,  22,  83,  24, 242,  34,
    254,  68, 207, 178, 195, 181, 122, 145,  36,   8, 232, 168,  96, 252,
    105,  80, 170, 208, 160, 125, 161, 137,  98, 151,  84,  91,  30, 149,
    224, 255, 100, 210,  16, 196,   0,  72, 163, 247, 117, 219, 138,   3,
    230, 218,   9,  63, 221, 148, 135,  92, 131,   2, 205,  74, 144,  51,
    115, 103, 246, 243, 157, 127, 191, 226,  82, 155, 216,  38, 200,  55,
    198,  59, 129, 150, 111,  75,  19, 190,  99,  46, 233, 121, 167, 140,
    159, 110, 188, 142,  41, 245, 249, 182,  47, 253, 180,  89, 120, 152,
      6, 106, 231,  70, 113, 186, 212,  37, 171,  66, 136, 162, 141, 250,
    114,   7, 185,  85, 248, 238, 172,  10,  54,  73,  42, 104,  60,  56,
    241, 164,  64,  40, 211, 123, 187, 201,  67, 193,  21, 227, 173, 244,
    119, 199, 128, 158,
};

static uint8_t camellia_rotl8(uint8_t v, unsigned int shift)
{
    return (v << shift) | (v >> (8 - shift));
}

static uint8_t camellia_sbox2(uint8_t x)
{
    return camellia_rotl8(camellia_sbox1[x], 1);
}

static uint8_t camellia_sbox3(uint8_t x)
{
    return camellia_rotl8(camellia_sbox1[x], 7);
}

static uint8_t camellia_sbox4(uint8_t x)
{
    return camellia_sbox1[camellia_rotl8(x, 1)];
}

static uint64_t camellia_f(uint64_t input, uint64_t key)
{
    uint64_t x = input ^ key;
    uint8_t t1 = camellia_sbox1[x >> 56];
    uint8_t t2 = camellia_sbox2((x >> 48) & 0xff);
    uint8_t t3 = camellia_sbox3((x >> 40) & 0xff);
    uint8_t t4 = camellia_sbox4((x >> 32) & 0xff);
    uint8_t t5 = camellia_sbox2((x >> 24) & 0xff);
    uint8_t t6 = camellia_sbox3((x >> 16) & 0xff);
    uint8_t t7 = camellia_sbox4((x >> 8) & 0xff);
    uint8_t t8 = camellia_sbox1[x & 0xff];
    uint8_t y1 = t1 ^ t3 ^ t4 ^ t6 ^ t7 ^ t8;
    uint8_t y2 = t1 ^ t2 ^ t4 ^ t5 ^ t7 ^ t8;
    uint8_t y3 = t1 ^ t2 ^ t3 ^ t5 ^ t6 ^ t8;
    uint8_t y4 = t2 ^ t3 ^ t4 ^ t5 ^ t6 ^ t7;
    uint8_t y5 = t1 ^ t2 ^ t6 ^ t7 ^ t8;
    uint8_t y6 = t2 ^ t3 ^ t5 ^ t7 ^ t8;
    uint8_t y7 = t3 ^ t4 ^ t5 ^ t6 ^ t8;
    uint8_t y8 = t1 ^ t4 ^ t5 ^ t6 ^ t7;

    return ((uint64_t)y1 << 56) | ((uint64_t)y2 << 48) |
           ((uint64_t)y3 << 40) | ((uint64_t)y4 << 32) |
           ((uint64_t)y5 << 24) | ((uint64_t)y6 << 16) |
           ((uint64_t)y7 << 8) | y8;
}

static uint64_t camellia_fl(uint64_t input, uint64_t key)
{
    uint32_t x1 = input >> 32;
    uint32_t x2 = input;
    uint32_t k1 = key >> 32;
    uint32_t k2 = key;

    x2 ^= rol32(x1 & k1, 1);
    x1 ^= x2 | k2;
    return ((uint64_t)x1 << 32) | x2;
}

static uint64_t camellia_flinv(uint64_t input, uint64_t key)
{
    uint32_t y1 = input >> 32;
    uint32_t y2 = input;
    uint32_t k1 = key >> 32;
    uint32_t k2 = key;

    y1 ^= y2 | k2;
    y2 ^= rol32(y1 & k1, 1);
    return ((uint64_t)y1 << 32) | y2;
}

static void octeon_camellia_round(MIPSOcteonCryptoState *crypto, uint64_t key)
{
    uint64_t left = crypto->aes_resinp[0];
    uint64_t right = crypto->aes_resinp[1];

    crypto->aes_resinp[0] = right ^ camellia_f(left, key);
    crypto->aes_resinp[1] = left;
}

static void octeon_camellia_fl_layer(MIPSOcteonCryptoState *crypto,
                                     uint64_t key, bool inverse)
{
    uint64_t state = crypto->aes_resinp[inverse ? 1 : 0];

    crypto->aes_resinp[inverse ? 1 : 0] = inverse ?
        camellia_flinv(state, key) :
        camellia_fl(state, key);
}

void helper_octeon_cp2_mt_camellia_fl(CPUMIPSState *env, uint64_t value)
{
    octeon_camellia_fl_layer(&env->octeon_crypto, value, false);
}

void helper_octeon_cp2_mt_camellia_flinv(CPUMIPSState *env, uint64_t value)
{
    octeon_camellia_fl_layer(&env->octeon_crypto, value, true);
}

void helper_octeon_cp2_mt_camellia_round(CPUMIPSState *env, uint64_t value)
{
    octeon_camellia_round(&env->octeon_crypto, value);
}

void helper_octeon_cp2_mt_snow3g_start(CPUMIPSState *env, uint64_t value)
{
    octeon_snow3g_start(&env->octeon_crypto, value);
}

void helper_octeon_cp2_mt_snow3g_more(CPUMIPSState *env, uint64_t value)
{
    (void)value;
    octeon_snow3g_more(&env->octeon_crypto);
}

void helper_octeon_cp2_mt_zuc_start(CPUMIPSState *env, uint64_t value)
{
    octeon_zuc_start(&env->octeon_crypto, value);
}

void helper_octeon_cp2_mt_zuc_more(CPUMIPSState *env, uint64_t value)
{
    octeon_zuc_more(&env->octeon_crypto, value);
}

void helper_octeon_cp2_mt_hsh_startsha1_compat(CPUMIPSState *env,
                                               uint64_t value)
{
    octeon_hsh_set_pair(env->octeon_crypto.hsh_dat, 7, value);
    octeon_sha1_transform(&env->octeon_crypto);
}

void helper_octeon_cp2_mt_hsh_startmd5(CPUMIPSState *env, uint64_t value)
{
    octeon_hsh_set_pair(env->octeon_crypto.hsh_dat, 7, value);
    octeon_md5_transform(&env->octeon_crypto);
}

void helper_octeon_cp2_mt_hsh_startsha256(CPUMIPSState *env, uint64_t value)
{
    octeon_hsh_set_pair(env->octeon_crypto.hsh_dat, 7, value);
    octeon_sha256_transform(&env->octeon_crypto);
}

void helper_octeon_cp2_mt_hsh_startsha(CPUMIPSState *env, uint64_t value)
{
    octeon_hsh_set_pair(env->octeon_crypto.hsh_dat, 7, value);
    octeon_sha1_transform(&env->octeon_crypto);
}

void helper_octeon_cp2_mt_hsh_startsha512(CPUMIPSState *env, uint64_t value)
{
    MIPSOcteonCryptoState *crypto = &env->octeon_crypto;

    crypto->hsh_dat[15] = value;
    octeon_sha512_transform(crypto);
}

uint64_t helper_octeon_cp2_mf_crc_iv_reflect(CPUMIPSState *env)
{
    return octeon_crc_reflect32_by_byte(env->octeon_crypto.crc_iv);
}

static void octeon_gfm_xormul1_common(MIPSOcteonCryptoState *crypto,
                                      uint64_t value)
{
    crypto->gfm_resinp[1] ^= value;
    if (crypto->gfm_poly <= 0xff && crypto->gfm_mul[1] == 0 &&
        crypto->gfm_resinp[0] == 0) {
        octeon_gfm_mul64_uia2(crypto->gfm_resinp, crypto->gfm_mul,
                              crypto->gfm_poly, crypto->gfm_resinp);
    } else {
        octeon_gfm_mul(crypto->gfm_resinp, crypto->gfm_mul, crypto->gfm_poly,
                       crypto->gfm_resinp);
    }
}

void helper_octeon_cp2_mt_gfm_xormul1_reflect(CPUMIPSState *env,
                                              uint64_t value)
{
    MIPSOcteonCryptoState *crypto = &env->octeon_crypto;

    octeon_gfm_xormul1_common(crypto, revbit64(value));
}

void helper_octeon_cp2_mt_gfm_xormul1(CPUMIPSState *env, uint64_t value)
{
    MIPSOcteonCryptoState *crypto = &env->octeon_crypto;

    octeon_gfm_xormul1_common(crypto, value);
}

void helper_octeon_cp2_mt_sha3_startop(CPUMIPSState *env)
{
    octeon_sha3_permute(&env->octeon_crypto);
}

void helper_octeon_cp2_mt_crc_write_iv_reflect(CPUMIPSState *env,
                                               uint64_t value)
{
    env->octeon_crypto.crc_iv =
        octeon_crc_reflect32_by_byte((uint32_t)value);
}

void helper_octeon_cp2_mt_crc_write_polynomial_reflect(CPUMIPSState *env,
                                                       uint64_t value)
{
    env->octeon_crypto.crc_poly =
        octeon_crc_reflect32_by_byte((uint32_t)value);
}

void helper_octeon_cp2_mt_crc_write_byte(CPUMIPSState *env, uint64_t value)
{
    octeon_crc_update_normal(&env->octeon_crypto, value, 1);
}

void helper_octeon_cp2_mt_crc_write_half(CPUMIPSState *env, uint64_t value)
{
    octeon_crc_update_normal(&env->octeon_crypto, value, 2);
}

void helper_octeon_cp2_mt_crc_write_word(CPUMIPSState *env, uint64_t value)
{
    octeon_crc_update_normal(&env->octeon_crypto, value, 4);
}

void helper_octeon_cp2_mt_crc_write_dword(CPUMIPSState *env, uint64_t value)
{
    octeon_crc_update_normal(&env->octeon_crypto, value, 8);
}

void helper_octeon_cp2_mt_crc_write_var(CPUMIPSState *env, uint64_t value)
{
    MIPSOcteonCryptoState *crypto = &env->octeon_crypto;

    octeon_crc_update_normal(crypto, value, MIN(8U, crypto->crc_len & 0xf));
}

void helper_octeon_cp2_mt_crc_write_byte_reflect(CPUMIPSState *env,
                                                 uint64_t value)
{
    octeon_crc_update_reflect(&env->octeon_crypto, value, 1);
}

void helper_octeon_cp2_mt_crc_write_half_reflect(CPUMIPSState *env,
                                                 uint64_t value)
{
    octeon_crc_update_reflect(&env->octeon_crypto, value, 2);
}

void helper_octeon_cp2_mt_crc_write_word_reflect(CPUMIPSState *env,
                                                 uint64_t value)
{
    octeon_crc_update_reflect(&env->octeon_crypto, value, 4);
}

void helper_octeon_cp2_mt_crc_write_dword_reflect(CPUMIPSState *env,
                                                  uint64_t value)
{
    octeon_crc_update_reflect(&env->octeon_crypto, value, 8);
}

void helper_octeon_cp2_mt_crc_write_var_reflect(CPUMIPSState *env,
                                                uint64_t value)
{
    MIPSOcteonCryptoState *crypto = &env->octeon_crypto;

    octeon_crc_update_reflect(crypto, value, MIN(8U, crypto->crc_len & 0xf));
}

void helper_octeon_cp2_mt_llm_read_addr0(CPUMIPSState *env, uint64_t value)
{
    octeon_llm_read(&env->octeon_crypto, 0, value, false);
}

void helper_octeon_cp2_mt_llm_write_addr0(CPUMIPSState *env, uint64_t value)
{
    octeon_llm_write(&env->octeon_crypto, 0, value, false);
}

void helper_octeon_cp2_mt_llm_read64_addr0(CPUMIPSState *env, uint64_t value)
{
    octeon_llm_read(&env->octeon_crypto, 0, value, true);
}

void helper_octeon_cp2_mt_llm_write64_addr0(CPUMIPSState *env, uint64_t value)
{
    octeon_llm_write(&env->octeon_crypto, 0, value, true);
}

void helper_octeon_cp2_mt_llm_read_addr1(CPUMIPSState *env, uint64_t value)
{
    octeon_llm_read(&env->octeon_crypto, 1, value, false);
}

void helper_octeon_cp2_mt_llm_write_addr1(CPUMIPSState *env, uint64_t value)
{
    octeon_llm_write(&env->octeon_crypto, 1, value, false);
}

void helper_octeon_cp2_mt_llm_read64_addr1(CPUMIPSState *env, uint64_t value)
{
    octeon_llm_read(&env->octeon_crypto, 1, value, true);
}

void helper_octeon_cp2_mt_llm_write64_addr1(CPUMIPSState *env, uint64_t value)
{
    octeon_llm_write(&env->octeon_crypto, 1, value, true);
}
