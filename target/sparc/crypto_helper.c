/*
 * SPARC crypto instructions for QEMU
 *
 * Based on Oracle SPARC Architecture 2011
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "exec/helper-proto.h"
#include "crypto/aes-round.h"
#include "crypto/aes.h"
#include "qemu/bswap.h"
#include "qemu/crc32c.h"
#include "cpu.h"

/*
 * SPARC four-operand AES round (Section 7.3).
 * s2:s3 holds the state; half selects output columns 0-1 or 2-3, and
 * key supplies their round key. dec selects decryption; last omits
 * MixColumns/InvMixColumns. Return the selected 64-bit half.
 *
 * Note all 64-bit values (state and key) hold AES bytes in big-endian
 * order, i.e. the most significant byte is the first state byte.
 */
static uint64_t aes_round(uint64_t key, uint64_t s2, uint64_t s3,
                          int half, bool dec, bool last)
{
    AESState st, rk, r;
    const unsigned first = HOST_BIG_ENDIAN ? 0 : 1;
    const unsigned out = first ^ half;

    st.d[first] = s2;       /* columns 0-1 */
    st.d[first ^ 1] = s3;   /* columns 2-3 */
    rk.d[out] = key;
    rk.d[out ^ 1] = 0;

    if (dec) {
        if (last) {
            aesdec_ISB_ISR_AK(&r, &st, &rk, true);
        } else {
            aesdec_ISB_ISR_AK_IMC(&r, &st, &rk, true);
        }
    } else {
        if (last) {
            aesenc_SB_SR_AK(&r, &st, &rk, true);
        } else {
            aesenc_SB_SR_MC_AK(&r, &st, &rk, true);
        }
    }

    return r.d[out];
}

uint64_t helper_aes_eround01(uint64_t key, uint64_t s2, uint64_t s3)
{
    return aes_round(key, s2, s3, 0, false, false);
}

uint64_t helper_aes_eround23(uint64_t key, uint64_t s2, uint64_t s3)
{
    return aes_round(key, s2, s3, 1, false, false);
}

uint64_t helper_aes_dround01(uint64_t key, uint64_t s2, uint64_t s3)
{
    return aes_round(key, s2, s3, 0, true, false);
}

uint64_t helper_aes_dround23(uint64_t key, uint64_t s2, uint64_t s3)
{
    return aes_round(key, s2, s3, 1, true, false);
}

uint64_t helper_aes_eround01_l(uint64_t key, uint64_t s2, uint64_t s3)
{
    return aes_round(key, s2, s3, 0, false, true);
}

uint64_t helper_aes_eround23_l(uint64_t key, uint64_t s2, uint64_t s3)
{
    return aes_round(key, s2, s3, 1, false, true);
}

uint64_t helper_aes_dround01_l(uint64_t key, uint64_t s2, uint64_t s3)
{
    return aes_round(key, s2, s3, 0, true, true);
}

uint64_t helper_aes_dround23_l(uint64_t key, uint64_t s2, uint64_t s3)
{
    return aes_round(key, s2, s3, 1, true, true);
}

/* Combine the transformed word with the two words in rs1. */
static uint64_t aes_kexpand(uint64_t rs1, uint32_t t)
{
    uint32_t hi = (rs1 >> 32) ^ t;
    uint32_t lo = (uint32_t)rs1 ^ hi;

    return deposit64(lo, 32, 32, hi);
}

/* Apply the AES S-box to each byte, preserving byte positions. */
static uint32_t aes_subword(uint32_t x)
{
    return ((uint32_t)AES_sbox[x >> 24] << 24) |
           ((uint32_t)AES_sbox[(x >> 16) & 0xff] << 16) |
           ((uint32_t)AES_sbox[(x >> 8) & 0xff] << 8) |
           (uint32_t)AES_sbox[x & 0xff];
}

/*
 * AES Cryptographic Operations 4-operand (Section 7.3)
 * AES key expansion with RotWord, SubWord, and RCON.
 */
uint64_t helper_aes_kexpand1(uint64_t rs1, uint64_t rs2, uint32_t imm)
{
    static const uint8_t rcon[10] = {
        0x01, 0x02, 0x04, 0x08, 0x10,
        0x20, 0x40, 0x80, 0x1b, 0x36,
    };
    uint32_t t = rol32((uint32_t)rs2, 8);

    t = aes_subword(t);

    if (imm < ARRAY_SIZE(rcon)) {
        t ^= (uint32_t)rcon[imm] << 24;
    }

    return aes_kexpand(rs1, t);
}

/*
 * AES Cryptographic Operations 3-operand (Section 7.4)
 * AES key expansion with SubWord, without RotWord or RCON.
 */
uint64_t helper_aes_kexpand0(uint64_t rs1, uint64_t rs2)
{
    return aes_kexpand(rs1, aes_subword(rs2));
}

/*
 * AES Cryptographic Operations 3-operand (Section 7.4)
 * AES key expansion without SubWord, RotWord, or RCON.
 */
uint64_t helper_aes_kexpand2(uint64_t rs1, uint64_t rs2)
{
    return aes_kexpand(rs1, (uint32_t)rs2);
}

/*
 * CRC32C Operation 3-operand (Section 7.19)
 * Accumulate CRC32C over the eight bytes of rs2, most significant first.
 */
uint64_t helper_crc32c(uint64_t rs1, uint64_t rs2)
{
    uint8_t buf[8];
    /* IV and result are byte-swapped in the register. */
    uint32_t crc = bswap32((uint32_t)rs1);

    stq_be_p(buf, rs2);

    /* Undo crc32c()'s final inversion for the instruction's raw result. */
    crc = crc32c(crc, buf, sizeof(buf)) ^ UINT32_MAX;

    /* Result lives in the low 32 bits; upper half reads as zero. */
    return (uint64_t)bswap32(crc);
}

/*
 * Compress one SHA-1 block from f8-f23 into f0-f4.
 * Clear f5 to match SPARC M7 behavior.
 */
void helper_sha1(CPUSPARCState *env)
{
    uint32_t a = env->fpr[0].l.upper;
    uint32_t b = env->fpr[0].l.lower;
    uint32_t c = env->fpr[1].l.upper;
    uint32_t d = env->fpr[1].l.lower;
    uint32_t e = env->fpr[2].l.upper;
    uint32_t w[80];

    /* Load 16 message words; each fpr[] entry holds two words. */
    for (int i = 0; i < 8; i++) {
        w[2 * i] = env->fpr[4 + i].l.upper;
        w[2 * i + 1] = env->fpr[4 + i].l.lower;
    }

    /* Expand the message into the 80-word round schedule. */
    for (int i = 16; i < 80; i++) {
        w[i] = rol32(w[i - 3] ^ w[i - 8] ^
                     w[i - 14] ^ w[i - 16], 1);
    }

    /* Each group of 20 rounds uses its own function and constant. */
    for (int i = 0; i < 80; i++) {
        uint32_t f, k, t;

        if (i < 20) {
            f = (b & c) | (~b & d);          /* Choose */
            k = 0x5a827999;
        } else if (i < 40) {
            f = b ^ c ^ d;                  /* Parity */
            k = 0x6ed9eba1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d); /* Majority */
            k = 0x8f1bbcdc;
        } else {
            f = b ^ c ^ d;                  /* Parity */
            k = 0xca62c1d6;
        }

        /* Advance the five working words, with arithmetic modulo 2^32. */
        t = rol32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol32(b, 30);
        b = a;
        a = t;
    }

    /* Accumulate the hash in f0-f4 and clear the unused f5 word. */
    env->fpr[0].l.upper += a;
    env->fpr[0].l.lower += b;
    env->fpr[1].l.upper += c;
    env->fpr[1].l.lower += d;
    env->fpr[2].l.upper += e;
    env->fpr[2].l.lower = 0;
}

/* SHA-2 Boolean functions. */
static inline uint64_t sha_ch(uint64_t x, uint64_t y, uint64_t z)
{
    return (x & y) ^ (~x & z);
}

static inline uint64_t sha_maj(uint64_t x, uint64_t y, uint64_t z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static const uint64_t sha512_k[80] = {
    UINT64_C(0x428a2f98d728ae22), UINT64_C(0x7137449123ef65cd),
    UINT64_C(0xb5c0fbcfec4d3b2f), UINT64_C(0xe9b5dba58189dbbc),
    UINT64_C(0x3956c25bf348b538), UINT64_C(0x59f111f1b605d019),
    UINT64_C(0x923f82a4af194f9b), UINT64_C(0xab1c5ed5da6d8118),
    UINT64_C(0xd807aa98a3030242), UINT64_C(0x12835b0145706fbe),
    UINT64_C(0x243185be4ee4b28c), UINT64_C(0x550c7dc3d5ffb4e2),
    UINT64_C(0x72be5d74f27b896f), UINT64_C(0x80deb1fe3b1696b1),
    UINT64_C(0x9bdc06a725c71235), UINT64_C(0xc19bf174cf692694),
    UINT64_C(0xe49b69c19ef14ad2), UINT64_C(0xefbe4786384f25e3),
    UINT64_C(0x0fc19dc68b8cd5b5), UINT64_C(0x240ca1cc77ac9c65),
    UINT64_C(0x2de92c6f592b0275), UINT64_C(0x4a7484aa6ea6e483),
    UINT64_C(0x5cb0a9dcbd41fbd4), UINT64_C(0x76f988da831153b5),
    UINT64_C(0x983e5152ee66dfab), UINT64_C(0xa831c66d2db43210),
    UINT64_C(0xb00327c898fb213f), UINT64_C(0xbf597fc7beef0ee4),
    UINT64_C(0xc6e00bf33da88fc2), UINT64_C(0xd5a79147930aa725),
    UINT64_C(0x06ca6351e003826f), UINT64_C(0x142929670a0e6e70),
    UINT64_C(0x27b70a8546d22ffc), UINT64_C(0x2e1b21385c26c926),
    UINT64_C(0x4d2c6dfc5ac42aed), UINT64_C(0x53380d139d95b3df),
    UINT64_C(0x650a73548baf63de), UINT64_C(0x766a0abb3c77b2a8),
    UINT64_C(0x81c2c92e47edaee6), UINT64_C(0x92722c851482353b),
    UINT64_C(0xa2bfe8a14cf10364), UINT64_C(0xa81a664bbc423001),
    UINT64_C(0xc24b8b70d0f89791), UINT64_C(0xc76c51a30654be30),
    UINT64_C(0xd192e819d6ef5218), UINT64_C(0xd69906245565a910),
    UINT64_C(0xf40e35855771202a), UINT64_C(0x106aa07032bbd1b8),
    UINT64_C(0x19a4c116b8d2d0c8), UINT64_C(0x1e376c085141ab53),
    UINT64_C(0x2748774cdf8eeb99), UINT64_C(0x34b0bcb5e19b48a8),
    UINT64_C(0x391c0cb3c5c95a63), UINT64_C(0x4ed8aa4ae3418acb),
    UINT64_C(0x5b9cca4f7763e373), UINT64_C(0x682e6ff3d6b2b8a3),
    UINT64_C(0x748f82ee5defb2fc), UINT64_C(0x78a5636f43172f60),
    UINT64_C(0x84c87814a1f0ab72), UINT64_C(0x8cc702081a6439ec),
    UINT64_C(0x90befffa23631e28), UINT64_C(0xa4506cebde82bde9),
    UINT64_C(0xbef9a3f7b2c67915), UINT64_C(0xc67178f2e372532b),
    UINT64_C(0xca273eceea26619c), UINT64_C(0xd186b8c721c0c207),
    UINT64_C(0xeada7dd6cde0eb1e), UINT64_C(0xf57d4f7fee6ed178),
    UINT64_C(0x06f067aa72176fba), UINT64_C(0x0a637dc5a2c898a6),
    UINT64_C(0x113f9804bef90dae), UINT64_C(0x1b710b35131c471b),
    UINT64_C(0x28db77f523047d84), UINT64_C(0x32caab7b40c72493),
    UINT64_C(0x3c9ebe0a15c9bebc), UINT64_C(0x431d67c49c100d4c),
    UINT64_C(0x4cc5d4becb3e42b6), UINT64_C(0x597f299cfc657e2a),
    UINT64_C(0x5fcb6fab3ad6faec), UINT64_C(0x6c44198c4a475817),
};

/* Compress 16 numeric message words into the supplied SHA-256 state. */
static void sha256_compress(uint32_t state[8], const uint32_t block[16])
{
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    uint32_t w[64];

    /* Expand the message into the 64-word round schedule. */
    memcpy(w, block, 16 * sizeof(*w));
    for (int i = 16; i < 64; i++) {
        uint32_t x = w[i - 15], y = w[i - 2];
        uint32_t s0 = ror32(x, 7) ^ ror32(x, 18) ^ (x >> 3);
        uint32_t s1 = ror32(y, 17) ^ ror32(y, 19) ^ (y >> 10);

        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    /* Perform 64 rounds with arithmetic modulo 2^32. */
    for (int i = 0; i < 64; i++) {
        uint32_t s0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
        uint32_t s1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);

        /* sha_ch/sha_maj are shared with SHA-512; narrow to 32 bits. */
        uint32_t t1 = h + s1 + (uint32_t)sha_ch(e, f, g) +
                      sha256_k[i] + w[i];
        uint32_t t2 = s0 + (uint32_t)sha_maj(a, b, c);

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    /* Add the working words to the original state. */
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

/* Compress 16 numeric message words into the supplied SHA-512 state. */
static void sha512_compress(uint64_t state[8], const uint64_t block[16])
{
    uint64_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint64_t e = state[4], f = state[5], g = state[6], h = state[7];
    uint64_t w[80];

    /* Expand the message into the 80-word round schedule. */
    memcpy(w, block, 16 * sizeof(*w));
    for (int i = 16; i < 80; i++) {
        uint64_t x = w[i - 15], y = w[i - 2];
        uint64_t s0 = ror64(x, 1) ^ ror64(x, 8) ^ (x >> 7);
        uint64_t s1 = ror64(y, 19) ^ ror64(y, 61) ^ (y >> 6);

        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    /* Perform 80 rounds with arithmetic modulo 2^64. */
    for (int i = 0; i < 80; i++) {
        uint64_t s0 = ror64(a, 28) ^ ror64(a, 34) ^ ror64(a, 39);
        uint64_t s1 = ror64(e, 14) ^ ror64(e, 18) ^ ror64(e, 41);
        uint64_t t1 = h + s1 + sha_ch(e, f, g) + sha512_k[i] + w[i];
        uint64_t t2 = s0 + sha_maj(a, b, c);

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    /* Add the working words to the original state. */
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

/* Compress one SHA-256 block from f8-f23 into f0-f7. */
void helper_sha256(CPUSPARCState *env)
{
    uint32_t state[8], block[16];

    /* Each fpr[] entry holds two architectural 32-bit registers. */
    for (int i = 0; i < 4; i++) {
        state[2 * i] = env->fpr[i].l.upper;
        state[2 * i + 1] = env->fpr[i].l.lower;
    }
    for (int i = 0; i < 8; i++) {
        block[2 * i] = env->fpr[4 + i].l.upper;
        block[2 * i + 1] = env->fpr[4 + i].l.lower;
    }

    sha256_compress(state, block);

    for (int i = 0; i < 4; i++) {
        env->fpr[i].l.upper = state[2 * i];
        env->fpr[i].l.lower = state[2 * i + 1];
    }
}

/*
 * Compress one SHA-512 block from FD16, FD18, ..., FD46
 * into FD0, FD2, ..., FD14.
 */
void helper_sha512(CPUSPARCState *env)
{
    uint64_t state[8], block[16];

    /* Each fpr[] entry holds one architectural 64-bit register. */
    for (int i = 0; i < 8; i++) {
        state[i] = env->fpr[i].ll;
    }
    for (int i = 0; i < 16; i++) {
        block[i] = env->fpr[8 + i].ll;
    }

    sha512_compress(state, block);

    for (int i = 0; i < 8; i++) {
        env->fpr[i].ll = state[i];
    }
}
