/*
 * xxHash - Fast Hash algorithm
 * Copyright (C) 2012-2016, Yann Collet
 *
 * BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * + Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * + Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You can contact the author at :
 * - xxHash source repository : https://github.com/Cyan4973/xxHash
 */

#ifndef QEMU_XXHASH_H
#define QEMU_XXHASH_H

#include "qemu/bitops.h"

#define PRIME32_1   2654435761U
#define PRIME32_2   2246822519U
#define PRIME32_3   3266489917U
#define PRIME32_4    668265263U
#define PRIME32_5    374761393U

#define QEMU_XXHASH_SEED 1

/*
 * xxhash32, customized for input variables that are not guaranteed to be
 * contiguous in memory.
 */
static inline uint32_t qemu_xxhash8(uint64_t ab, uint64_t cd, uint64_t ef,
                                    uint32_t g, uint32_t h)
{
    uint32_t v1 = QEMU_XXHASH_SEED + PRIME32_1 + PRIME32_2;
    uint32_t v2 = QEMU_XXHASH_SEED + PRIME32_2;
    uint32_t v3 = QEMU_XXHASH_SEED + 0;
    uint32_t v4 = QEMU_XXHASH_SEED - PRIME32_1;
    uint32_t a = ab;
    uint32_t b = ab >> 32;
    uint32_t c = cd;
    uint32_t d = cd >> 32;
    uint32_t e = ef;
    uint32_t f = ef >> 32;
    uint32_t h32;

    v1 += a * PRIME32_2;
    v1 = rol32(v1, 13);
    v1 *= PRIME32_1;

    v2 += b * PRIME32_2;
    v2 = rol32(v2, 13);
    v2 *= PRIME32_1;

    v3 += c * PRIME32_2;
    v3 = rol32(v3, 13);
    v3 *= PRIME32_1;

    v4 += d * PRIME32_2;
    v4 = rol32(v4, 13);
    v4 *= PRIME32_1;

    h32 = rol32(v1, 1) + rol32(v2, 7) + rol32(v3, 12) + rol32(v4, 18);
    h32 += 28;

    h32 += e * PRIME32_3;
    h32  = rol32(h32, 17) * PRIME32_4;

    h32 += f * PRIME32_3;
    h32  = rol32(h32, 17) * PRIME32_4;

    h32 += g * PRIME32_3;
    h32  = rol32(h32, 17) * PRIME32_4;

    h32 += h * PRIME32_3;
    h32  = rol32(h32, 17) * PRIME32_4;

    h32 ^= h32 >> 15;
    h32 *= PRIME32_2;
    h32 ^= h32 >> 13;
    h32 *= PRIME32_3;
    h32 ^= h32 >> 16;

    return h32;
}

static inline uint32_t qemu_xxhash2(uint64_t ab)
{
    return qemu_xxhash8(ab, 0, 0, 0, 0);
}

static inline uint32_t qemu_xxhash4(uint64_t ab, uint64_t cd)
{
    return qemu_xxhash8(ab, cd, 0, 0, 0);
}

static inline uint32_t qemu_xxhash5(uint64_t ab, uint64_t cd, uint32_t e)
{
    return qemu_xxhash8(ab, cd, 0, e, 0);
}

static inline uint32_t qemu_xxhash6(uint64_t ab, uint64_t cd, uint32_t e,
                                    uint32_t f)
{
    return qemu_xxhash8(ab, cd, 0, e, f);
}

static inline uint32_t qemu_xxhash7(uint64_t ab, uint64_t cd, uint64_t ef,
                                    uint32_t g)
{
    return qemu_xxhash8(ab, cd, ef, g, 0);
}

/*
 * Component parts of the XXH64 algorithm from
 * https://github.com/Cyan4973/xxHash/blob/v0.8.0/xxhash.h
 *
 * The complete algorithm looks like
 *
 *  i = 0;
 *  if (len >= 32) {
 *      v1 = seed + XXH_PRIME64_1 + XXH_PRIME64_2;
 *      v2 = seed + XXH_PRIME64_2;
 *      v3 = seed + 0;
 *      v4 = seed - XXH_PRIME64_1;
 *      do {
 *          v1 = XXH64_round(v1, get64bits(input + i));
 *          v2 = XXH64_round(v2, get64bits(input + i + 8));
 *          v3 = XXH64_round(v3, get64bits(input + i + 16));
 *          v4 = XXH64_round(v4, get64bits(input + i + 24));
 *      } while ((i += 32) <= len);
 *      h64 = XXH64_mergerounds(v1, v2, v3, v4);
 *  } else {
 *      h64 = seed + XXH_PRIME64_5;
 *  }
 *  h64 += len;
 *
 *  for (; i + 8 <= len; i += 8) {
 *      h64 ^= XXH64_round(0, get64bits(input + i));
 *      h64 = rol64(h64, 27) * XXH_PRIME64_1 + XXH_PRIME64_4;
 *  }
 *  for (; i + 4 <= len; i += 4) {
 *      h64 ^= get32bits(input + i) * PRIME64_1;
 *      h64 = rol64(h64, 23) * XXH_PRIME64_2 + XXH_PRIME64_3;
 *  }
 *  for (; i < len; i += 1) {
 *      h64 ^= get8bits(input + i) * XXH_PRIME64_5;
 *      h64 = rol64(h64, 11) * XXH_PRIME64_1;
 *  }
 *
 *  return XXH64_avalanche(h64)
 *
 * Exposing the pieces instead allows for simplified usage when
 * the length is a known constant and the inputs are in registers.
 */
#define XXH_PRIME64_1   0x9E3779B185EBCA87ULL
#define XXH_PRIME64_2   0xC2B2AE3D27D4EB4FULL
#define XXH_PRIME64_3   0x165667B19E3779F9ULL
#define XXH_PRIME64_4   0x85EBCA77C2B2AE63ULL
#define XXH_PRIME64_5   0x27D4EB2F165667C5ULL

static inline uint64_t XXH64_round(uint64_t acc, uint64_t input)
{
    return rol64(acc + input * XXH_PRIME64_2, 31) * XXH_PRIME64_1;
}

static inline uint64_t XXH64_mergeround(uint64_t acc, uint64_t val)
{
    return (acc ^ XXH64_round(0, val)) * XXH_PRIME64_1 + XXH_PRIME64_4;
}

static inline uint64_t XXH64_mergerounds(uint64_t v1, uint64_t v2,
                                         uint64_t v3, uint64_t v4)
{
    uint64_t h64;

    h64 = rol64(v1, 1) + rol64(v2, 7) + rol64(v3, 12) + rol64(v4, 18);
    h64 = XXH64_mergeround(h64, v1);
    h64 = XXH64_mergeround(h64, v2);
    h64 = XXH64_mergeround(h64, v3);
    h64 = XXH64_mergeround(h64, v4);

    return h64;
}

static inline uint64_t XXH64_avalanche(uint64_t h64)
{
    h64 ^= h64 >> 33;
    h64 *= XXH_PRIME64_2;
    h64 ^= h64 >> 29;
    h64 *= XXH_PRIME64_3;
    h64 ^= h64 >> 32;
    return h64;
}

static inline uint64_t qemu_xxhash64_4(uint64_t a, uint64_t b,
                                       uint64_t c, uint64_t d)
{
    uint64_t v1 = QEMU_XXHASH_SEED + XXH_PRIME64_1 + XXH_PRIME64_2;
    uint64_t v2 = QEMU_XXHASH_SEED + XXH_PRIME64_2;
    uint64_t v3 = QEMU_XXHASH_SEED + 0;
    uint64_t v4 = QEMU_XXHASH_SEED - XXH_PRIME64_1;

    v1 = XXH64_round(v1, a);
    v2 = XXH64_round(v2, b);
    v3 = XXH64_round(v3, c);
    v4 = XXH64_round(v4, d);

    return XXH64_avalanche(XXH64_mergerounds(v1, v2, v3, v4));
}

#endif /* QEMU_XXHASH_H */
