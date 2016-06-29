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

#ifndef EXEC_TB_HASH_XX_H
#define EXEC_TB_HASH_XX_H

#include "qemu/bitops.h"

#define PRIME32_1   2654435761U
#define PRIME32_2   2246822519U
#define PRIME32_3   3266489917U
#define PRIME32_4    668265263U
#define PRIME32_5    374761393U

#define TB_HASH_XX_SEED 1

/*
 * xxhash32, customized for input variables that are not guaranteed to be
 * contiguous in memory.
 */
static inline
uint32_t tb_hash_func5(uint64_t a0, uint64_t b0, uint32_t e)
{
    uint32_t v1 = TB_HASH_XX_SEED + PRIME32_1 + PRIME32_2;
    uint32_t v2 = TB_HASH_XX_SEED + PRIME32_2;
    uint32_t v3 = TB_HASH_XX_SEED + 0;
    uint32_t v4 = TB_HASH_XX_SEED - PRIME32_1;
    uint32_t a = a0 >> 32;
    uint32_t b = a0;
    uint32_t c = b0 >> 32;
    uint32_t d = b0;
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
    h32 += 20;

    h32 += e * PRIME32_3;
    h32  = rol32(h32, 17) * PRIME32_4;

    h32 ^= h32 >> 15;
    h32 *= PRIME32_2;
    h32 ^= h32 >> 13;
    h32 *= PRIME32_3;
    h32 ^= h32 >> 16;

    return h32;
}

#endif /* EXEC_TB_HASH_XX_H */
