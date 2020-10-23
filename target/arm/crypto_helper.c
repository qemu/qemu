/*
 * crypto_helper.c - emulate v8 Crypto Extensions instructions
 *
 * Copyright (C) 2013 - 2018 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "qemu/osdep.h"

#include "cpu.h"
#include "exec/helper-proto.h"
#include "tcg/tcg-gvec-desc.h"
#include "crypto/aes.h"
#include "vec_internal.h"

union CRYPTO_STATE {
    uint8_t    bytes[16];
    uint32_t   words[4];
    uint64_t   l[2];
};

#ifdef HOST_WORDS_BIGENDIAN
#define CR_ST_BYTE(state, i)   ((state).bytes[(15 - (i)) ^ 8])
#define CR_ST_WORD(state, i)   ((state).words[(3 - (i)) ^ 2])
#else
#define CR_ST_BYTE(state, i)   ((state).bytes[i])
#define CR_ST_WORD(state, i)   ((state).words[i])
#endif

/*
 * The caller has not been converted to full gvec, and so only
 * modifies the low 16 bytes of the vector register.
 */
static void clear_tail_16(void *vd, uint32_t desc)
{
    int opr_sz = simd_oprsz(desc);
    int max_sz = simd_maxsz(desc);

    assert(opr_sz == 16);
    clear_tail(vd, opr_sz, max_sz);
}

static void do_crypto_aese(uint64_t *rd, uint64_t *rn,
                           uint64_t *rm, bool decrypt)
{
    static uint8_t const * const sbox[2] = { AES_sbox, AES_isbox };
    static uint8_t const * const shift[2] = { AES_shifts, AES_ishifts };
    union CRYPTO_STATE rk = { .l = { rm[0], rm[1] } };
    union CRYPTO_STATE st = { .l = { rn[0], rn[1] } };
    int i;

    /* xor state vector with round key */
    rk.l[0] ^= st.l[0];
    rk.l[1] ^= st.l[1];

    /* combine ShiftRows operation and sbox substitution */
    for (i = 0; i < 16; i++) {
        CR_ST_BYTE(st, i) = sbox[decrypt][CR_ST_BYTE(rk, shift[decrypt][i])];
    }

    rd[0] = st.l[0];
    rd[1] = st.l[1];
}

void HELPER(crypto_aese)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    bool decrypt = simd_data(desc);

    for (i = 0; i < opr_sz; i += 16) {
        do_crypto_aese(vd + i, vn + i, vm + i, decrypt);
    }
    clear_tail(vd, opr_sz, simd_maxsz(desc));
}

static void do_crypto_aesmc(uint64_t *rd, uint64_t *rm, bool decrypt)
{
    static uint32_t const mc[][256] = { {
        /* MixColumns lookup table */
        0x00000000, 0x03010102, 0x06020204, 0x05030306,
        0x0c040408, 0x0f05050a, 0x0a06060c, 0x0907070e,
        0x18080810, 0x1b090912, 0x1e0a0a14, 0x1d0b0b16,
        0x140c0c18, 0x170d0d1a, 0x120e0e1c, 0x110f0f1e,
        0x30101020, 0x33111122, 0x36121224, 0x35131326,
        0x3c141428, 0x3f15152a, 0x3a16162c, 0x3917172e,
        0x28181830, 0x2b191932, 0x2e1a1a34, 0x2d1b1b36,
        0x241c1c38, 0x271d1d3a, 0x221e1e3c, 0x211f1f3e,
        0x60202040, 0x63212142, 0x66222244, 0x65232346,
        0x6c242448, 0x6f25254a, 0x6a26264c, 0x6927274e,
        0x78282850, 0x7b292952, 0x7e2a2a54, 0x7d2b2b56,
        0x742c2c58, 0x772d2d5a, 0x722e2e5c, 0x712f2f5e,
        0x50303060, 0x53313162, 0x56323264, 0x55333366,
        0x5c343468, 0x5f35356a, 0x5a36366c, 0x5937376e,
        0x48383870, 0x4b393972, 0x4e3a3a74, 0x4d3b3b76,
        0x443c3c78, 0x473d3d7a, 0x423e3e7c, 0x413f3f7e,
        0xc0404080, 0xc3414182, 0xc6424284, 0xc5434386,
        0xcc444488, 0xcf45458a, 0xca46468c, 0xc947478e,
        0xd8484890, 0xdb494992, 0xde4a4a94, 0xdd4b4b96,
        0xd44c4c98, 0xd74d4d9a, 0xd24e4e9c, 0xd14f4f9e,
        0xf05050a0, 0xf35151a2, 0xf65252a4, 0xf55353a6,
        0xfc5454a8, 0xff5555aa, 0xfa5656ac, 0xf95757ae,
        0xe85858b0, 0xeb5959b2, 0xee5a5ab4, 0xed5b5bb6,
        0xe45c5cb8, 0xe75d5dba, 0xe25e5ebc, 0xe15f5fbe,
        0xa06060c0, 0xa36161c2, 0xa66262c4, 0xa56363c6,
        0xac6464c8, 0xaf6565ca, 0xaa6666cc, 0xa96767ce,
        0xb86868d0, 0xbb6969d2, 0xbe6a6ad4, 0xbd6b6bd6,
        0xb46c6cd8, 0xb76d6dda, 0xb26e6edc, 0xb16f6fde,
        0x907070e0, 0x937171e2, 0x967272e4, 0x957373e6,
        0x9c7474e8, 0x9f7575ea, 0x9a7676ec, 0x997777ee,
        0x887878f0, 0x8b7979f2, 0x8e7a7af4, 0x8d7b7bf6,
        0x847c7cf8, 0x877d7dfa, 0x827e7efc, 0x817f7ffe,
        0x9b80801b, 0x98818119, 0x9d82821f, 0x9e83831d,
        0x97848413, 0x94858511, 0x91868617, 0x92878715,
        0x8388880b, 0x80898909, 0x858a8a0f, 0x868b8b0d,
        0x8f8c8c03, 0x8c8d8d01, 0x898e8e07, 0x8a8f8f05,
        0xab90903b, 0xa8919139, 0xad92923f, 0xae93933d,
        0xa7949433, 0xa4959531, 0xa1969637, 0xa2979735,
        0xb398982b, 0xb0999929, 0xb59a9a2f, 0xb69b9b2d,
        0xbf9c9c23, 0xbc9d9d21, 0xb99e9e27, 0xba9f9f25,
        0xfba0a05b, 0xf8a1a159, 0xfda2a25f, 0xfea3a35d,
        0xf7a4a453, 0xf4a5a551, 0xf1a6a657, 0xf2a7a755,
        0xe3a8a84b, 0xe0a9a949, 0xe5aaaa4f, 0xe6abab4d,
        0xefacac43, 0xecadad41, 0xe9aeae47, 0xeaafaf45,
        0xcbb0b07b, 0xc8b1b179, 0xcdb2b27f, 0xceb3b37d,
        0xc7b4b473, 0xc4b5b571, 0xc1b6b677, 0xc2b7b775,
        0xd3b8b86b, 0xd0b9b969, 0xd5baba6f, 0xd6bbbb6d,
        0xdfbcbc63, 0xdcbdbd61, 0xd9bebe67, 0xdabfbf65,
        0x5bc0c09b, 0x58c1c199, 0x5dc2c29f, 0x5ec3c39d,
        0x57c4c493, 0x54c5c591, 0x51c6c697, 0x52c7c795,
        0x43c8c88b, 0x40c9c989, 0x45caca8f, 0x46cbcb8d,
        0x4fcccc83, 0x4ccdcd81, 0x49cece87, 0x4acfcf85,
        0x6bd0d0bb, 0x68d1d1b9, 0x6dd2d2bf, 0x6ed3d3bd,
        0x67d4d4b3, 0x64d5d5b1, 0x61d6d6b7, 0x62d7d7b5,
        0x73d8d8ab, 0x70d9d9a9, 0x75dadaaf, 0x76dbdbad,
        0x7fdcdca3, 0x7cdddda1, 0x79dedea7, 0x7adfdfa5,
        0x3be0e0db, 0x38e1e1d9, 0x3de2e2df, 0x3ee3e3dd,
        0x37e4e4d3, 0x34e5e5d1, 0x31e6e6d7, 0x32e7e7d5,
        0x23e8e8cb, 0x20e9e9c9, 0x25eaeacf, 0x26ebebcd,
        0x2fececc3, 0x2cededc1, 0x29eeeec7, 0x2aefefc5,
        0x0bf0f0fb, 0x08f1f1f9, 0x0df2f2ff, 0x0ef3f3fd,
        0x07f4f4f3, 0x04f5f5f1, 0x01f6f6f7, 0x02f7f7f5,
        0x13f8f8eb, 0x10f9f9e9, 0x15fafaef, 0x16fbfbed,
        0x1ffcfce3, 0x1cfdfde1, 0x19fefee7, 0x1affffe5,
    }, {
        /* Inverse MixColumns lookup table */
        0x00000000, 0x0b0d090e, 0x161a121c, 0x1d171b12,
        0x2c342438, 0x27392d36, 0x3a2e3624, 0x31233f2a,
        0x58684870, 0x5365417e, 0x4e725a6c, 0x457f5362,
        0x745c6c48, 0x7f516546, 0x62467e54, 0x694b775a,
        0xb0d090e0, 0xbbdd99ee, 0xa6ca82fc, 0xadc78bf2,
        0x9ce4b4d8, 0x97e9bdd6, 0x8afea6c4, 0x81f3afca,
        0xe8b8d890, 0xe3b5d19e, 0xfea2ca8c, 0xf5afc382,
        0xc48cfca8, 0xcf81f5a6, 0xd296eeb4, 0xd99be7ba,
        0x7bbb3bdb, 0x70b632d5, 0x6da129c7, 0x66ac20c9,
        0x578f1fe3, 0x5c8216ed, 0x41950dff, 0x4a9804f1,
        0x23d373ab, 0x28de7aa5, 0x35c961b7, 0x3ec468b9,
        0x0fe75793, 0x04ea5e9d, 0x19fd458f, 0x12f04c81,
        0xcb6bab3b, 0xc066a235, 0xdd71b927, 0xd67cb029,
        0xe75f8f03, 0xec52860d, 0xf1459d1f, 0xfa489411,
        0x9303e34b, 0x980eea45, 0x8519f157, 0x8e14f859,
        0xbf37c773, 0xb43ace7d, 0xa92dd56f, 0xa220dc61,
        0xf66d76ad, 0xfd607fa3, 0xe07764b1, 0xeb7a6dbf,
        0xda595295, 0xd1545b9b, 0xcc434089, 0xc74e4987,
        0xae053edd, 0xa50837d3, 0xb81f2cc1, 0xb31225cf,
        0x82311ae5, 0x893c13eb, 0x942b08f9, 0x9f2601f7,
        0x46bde64d, 0x4db0ef43, 0x50a7f451, 0x5baafd5f,
        0x6a89c275, 0x6184cb7b, 0x7c93d069, 0x779ed967,
        0x1ed5ae3d, 0x15d8a733, 0x08cfbc21, 0x03c2b52f,
        0x32e18a05, 0x39ec830b, 0x24fb9819, 0x2ff69117,
        0x8dd64d76, 0x86db4478, 0x9bcc5f6a, 0x90c15664,
        0xa1e2694e, 0xaaef6040, 0xb7f87b52, 0xbcf5725c,
        0xd5be0506, 0xdeb30c08, 0xc3a4171a, 0xc8a91e14,
        0xf98a213e, 0xf2872830, 0xef903322, 0xe49d3a2c,
        0x3d06dd96, 0x360bd498, 0x2b1ccf8a, 0x2011c684,
        0x1132f9ae, 0x1a3ff0a0, 0x0728ebb2, 0x0c25e2bc,
        0x656e95e6, 0x6e639ce8, 0x737487fa, 0x78798ef4,
        0x495ab1de, 0x4257b8d0, 0x5f40a3c2, 0x544daacc,
        0xf7daec41, 0xfcd7e54f, 0xe1c0fe5d, 0xeacdf753,
        0xdbeec879, 0xd0e3c177, 0xcdf4da65, 0xc6f9d36b,
        0xafb2a431, 0xa4bfad3f, 0xb9a8b62d, 0xb2a5bf23,
        0x83868009, 0x888b8907, 0x959c9215, 0x9e919b1b,
        0x470a7ca1, 0x4c0775af, 0x51106ebd, 0x5a1d67b3,
        0x6b3e5899, 0x60335197, 0x7d244a85, 0x7629438b,
        0x1f6234d1, 0x146f3ddf, 0x097826cd, 0x02752fc3,
        0x335610e9, 0x385b19e7, 0x254c02f5, 0x2e410bfb,
        0x8c61d79a, 0x876cde94, 0x9a7bc586, 0x9176cc88,
        0xa055f3a2, 0xab58faac, 0xb64fe1be, 0xbd42e8b0,
        0xd4099fea, 0xdf0496e4, 0xc2138df6, 0xc91e84f8,
        0xf83dbbd2, 0xf330b2dc, 0xee27a9ce, 0xe52aa0c0,
        0x3cb1477a, 0x37bc4e74, 0x2aab5566, 0x21a65c68,
        0x10856342, 0x1b886a4c, 0x069f715e, 0x0d927850,
        0x64d90f0a, 0x6fd40604, 0x72c31d16, 0x79ce1418,
        0x48ed2b32, 0x43e0223c, 0x5ef7392e, 0x55fa3020,
        0x01b79aec, 0x0aba93e2, 0x17ad88f0, 0x1ca081fe,
        0x2d83bed4, 0x268eb7da, 0x3b99acc8, 0x3094a5c6,
        0x59dfd29c, 0x52d2db92, 0x4fc5c080, 0x44c8c98e,
        0x75ebf6a4, 0x7ee6ffaa, 0x63f1e4b8, 0x68fcedb6,
        0xb1670a0c, 0xba6a0302, 0xa77d1810, 0xac70111e,
        0x9d532e34, 0x965e273a, 0x8b493c28, 0x80443526,
        0xe90f427c, 0xe2024b72, 0xff155060, 0xf418596e,
        0xc53b6644, 0xce366f4a, 0xd3217458, 0xd82c7d56,
        0x7a0ca137, 0x7101a839, 0x6c16b32b, 0x671bba25,
        0x5638850f, 0x5d358c01, 0x40229713, 0x4b2f9e1d,
        0x2264e947, 0x2969e049, 0x347efb5b, 0x3f73f255,
        0x0e50cd7f, 0x055dc471, 0x184adf63, 0x1347d66d,
        0xcadc31d7, 0xc1d138d9, 0xdcc623cb, 0xd7cb2ac5,
        0xe6e815ef, 0xede51ce1, 0xf0f207f3, 0xfbff0efd,
        0x92b479a7, 0x99b970a9, 0x84ae6bbb, 0x8fa362b5,
        0xbe805d9f, 0xb58d5491, 0xa89a4f83, 0xa397468d,
    } };

    union CRYPTO_STATE st = { .l = { rm[0], rm[1] } };
    int i;

    for (i = 0; i < 16; i += 4) {
        CR_ST_WORD(st, i >> 2) =
            mc[decrypt][CR_ST_BYTE(st, i)] ^
            rol32(mc[decrypt][CR_ST_BYTE(st, i + 1)], 8) ^
            rol32(mc[decrypt][CR_ST_BYTE(st, i + 2)], 16) ^
            rol32(mc[decrypt][CR_ST_BYTE(st, i + 3)], 24);
    }

    rd[0] = st.l[0];
    rd[1] = st.l[1];
}

void HELPER(crypto_aesmc)(void *vd, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    bool decrypt = simd_data(desc);

    for (i = 0; i < opr_sz; i += 16) {
        do_crypto_aesmc(vd + i, vm + i, decrypt);
    }
    clear_tail(vd, opr_sz, simd_maxsz(desc));
}

/*
 * SHA-1 logical functions
 */

static uint32_t cho(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & (y ^ z)) ^ z;
}

static uint32_t par(uint32_t x, uint32_t y, uint32_t z)
{
    return x ^ y ^ z;
}

static uint32_t maj(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) | ((x | y) & z);
}

void HELPER(crypto_sha1su0)(void *vd, void *vn, void *vm, uint32_t desc)
{
    uint64_t *d = vd, *n = vn, *m = vm;
    uint64_t d0, d1;

    d0 = d[1] ^ d[0] ^ m[0];
    d1 = n[0] ^ d[1] ^ m[1];
    d[0] = d0;
    d[1] = d1;

    clear_tail_16(vd, desc);
}

static inline void crypto_sha1_3reg(uint64_t *rd, uint64_t *rn,
                                    uint64_t *rm, uint32_t desc,
                                    uint32_t (*fn)(union CRYPTO_STATE *d))
{
    union CRYPTO_STATE d = { .l = { rd[0], rd[1] } };
    union CRYPTO_STATE n = { .l = { rn[0], rn[1] } };
    union CRYPTO_STATE m = { .l = { rm[0], rm[1] } };
    int i;

    for (i = 0; i < 4; i++) {
        uint32_t t = fn(&d);

        t += rol32(CR_ST_WORD(d, 0), 5) + CR_ST_WORD(n, 0)
             + CR_ST_WORD(m, i);

        CR_ST_WORD(n, 0) = CR_ST_WORD(d, 3);
        CR_ST_WORD(d, 3) = CR_ST_WORD(d, 2);
        CR_ST_WORD(d, 2) = ror32(CR_ST_WORD(d, 1), 2);
        CR_ST_WORD(d, 1) = CR_ST_WORD(d, 0);
        CR_ST_WORD(d, 0) = t;
    }
    rd[0] = d.l[0];
    rd[1] = d.l[1];

    clear_tail_16(rd, desc);
}

static uint32_t do_sha1c(union CRYPTO_STATE *d)
{
    return cho(CR_ST_WORD(*d, 1), CR_ST_WORD(*d, 2), CR_ST_WORD(*d, 3));
}

void HELPER(crypto_sha1c)(void *vd, void *vn, void *vm, uint32_t desc)
{
    crypto_sha1_3reg(vd, vn, vm, desc, do_sha1c);
}

static uint32_t do_sha1p(union CRYPTO_STATE *d)
{
    return par(CR_ST_WORD(*d, 1), CR_ST_WORD(*d, 2), CR_ST_WORD(*d, 3));
}

void HELPER(crypto_sha1p)(void *vd, void *vn, void *vm, uint32_t desc)
{
    crypto_sha1_3reg(vd, vn, vm, desc, do_sha1p);
}

static uint32_t do_sha1m(union CRYPTO_STATE *d)
{
    return maj(CR_ST_WORD(*d, 1), CR_ST_WORD(*d, 2), CR_ST_WORD(*d, 3));
}

void HELPER(crypto_sha1m)(void *vd, void *vn, void *vm, uint32_t desc)
{
    crypto_sha1_3reg(vd, vn, vm, desc, do_sha1m);
}

void HELPER(crypto_sha1h)(void *vd, void *vm, uint32_t desc)
{
    uint64_t *rd = vd;
    uint64_t *rm = vm;
    union CRYPTO_STATE m = { .l = { rm[0], rm[1] } };

    CR_ST_WORD(m, 0) = ror32(CR_ST_WORD(m, 0), 2);
    CR_ST_WORD(m, 1) = CR_ST_WORD(m, 2) = CR_ST_WORD(m, 3) = 0;

    rd[0] = m.l[0];
    rd[1] = m.l[1];

    clear_tail_16(vd, desc);
}

void HELPER(crypto_sha1su1)(void *vd, void *vm, uint32_t desc)
{
    uint64_t *rd = vd;
    uint64_t *rm = vm;
    union CRYPTO_STATE d = { .l = { rd[0], rd[1] } };
    union CRYPTO_STATE m = { .l = { rm[0], rm[1] } };

    CR_ST_WORD(d, 0) = rol32(CR_ST_WORD(d, 0) ^ CR_ST_WORD(m, 1), 1);
    CR_ST_WORD(d, 1) = rol32(CR_ST_WORD(d, 1) ^ CR_ST_WORD(m, 2), 1);
    CR_ST_WORD(d, 2) = rol32(CR_ST_WORD(d, 2) ^ CR_ST_WORD(m, 3), 1);
    CR_ST_WORD(d, 3) = rol32(CR_ST_WORD(d, 3) ^ CR_ST_WORD(d, 0), 1);

    rd[0] = d.l[0];
    rd[1] = d.l[1];

    clear_tail_16(vd, desc);
}

/*
 * The SHA-256 logical functions, according to
 * http://csrc.nist.gov/groups/STM/cavp/documents/shs/sha256-384-512.pdf
 */

static uint32_t S0(uint32_t x)
{
    return ror32(x, 2) ^ ror32(x, 13) ^ ror32(x, 22);
}

static uint32_t S1(uint32_t x)
{
    return ror32(x, 6) ^ ror32(x, 11) ^ ror32(x, 25);
}

static uint32_t s0(uint32_t x)
{
    return ror32(x, 7) ^ ror32(x, 18) ^ (x >> 3);
}

static uint32_t s1(uint32_t x)
{
    return ror32(x, 17) ^ ror32(x, 19) ^ (x >> 10);
}

void HELPER(crypto_sha256h)(void *vd, void *vn, void *vm, uint32_t desc)
{
    uint64_t *rd = vd;
    uint64_t *rn = vn;
    uint64_t *rm = vm;
    union CRYPTO_STATE d = { .l = { rd[0], rd[1] } };
    union CRYPTO_STATE n = { .l = { rn[0], rn[1] } };
    union CRYPTO_STATE m = { .l = { rm[0], rm[1] } };
    int i;

    for (i = 0; i < 4; i++) {
        uint32_t t = cho(CR_ST_WORD(n, 0), CR_ST_WORD(n, 1), CR_ST_WORD(n, 2))
                     + CR_ST_WORD(n, 3) + S1(CR_ST_WORD(n, 0))
                     + CR_ST_WORD(m, i);

        CR_ST_WORD(n, 3) = CR_ST_WORD(n, 2);
        CR_ST_WORD(n, 2) = CR_ST_WORD(n, 1);
        CR_ST_WORD(n, 1) = CR_ST_WORD(n, 0);
        CR_ST_WORD(n, 0) = CR_ST_WORD(d, 3) + t;

        t += maj(CR_ST_WORD(d, 0), CR_ST_WORD(d, 1), CR_ST_WORD(d, 2))
             + S0(CR_ST_WORD(d, 0));

        CR_ST_WORD(d, 3) = CR_ST_WORD(d, 2);
        CR_ST_WORD(d, 2) = CR_ST_WORD(d, 1);
        CR_ST_WORD(d, 1) = CR_ST_WORD(d, 0);
        CR_ST_WORD(d, 0) = t;
    }

    rd[0] = d.l[0];
    rd[1] = d.l[1];

    clear_tail_16(vd, desc);
}

void HELPER(crypto_sha256h2)(void *vd, void *vn, void *vm, uint32_t desc)
{
    uint64_t *rd = vd;
    uint64_t *rn = vn;
    uint64_t *rm = vm;
    union CRYPTO_STATE d = { .l = { rd[0], rd[1] } };
    union CRYPTO_STATE n = { .l = { rn[0], rn[1] } };
    union CRYPTO_STATE m = { .l = { rm[0], rm[1] } };
    int i;

    for (i = 0; i < 4; i++) {
        uint32_t t = cho(CR_ST_WORD(d, 0), CR_ST_WORD(d, 1), CR_ST_WORD(d, 2))
                     + CR_ST_WORD(d, 3) + S1(CR_ST_WORD(d, 0))
                     + CR_ST_WORD(m, i);

        CR_ST_WORD(d, 3) = CR_ST_WORD(d, 2);
        CR_ST_WORD(d, 2) = CR_ST_WORD(d, 1);
        CR_ST_WORD(d, 1) = CR_ST_WORD(d, 0);
        CR_ST_WORD(d, 0) = CR_ST_WORD(n, 3 - i) + t;
    }

    rd[0] = d.l[0];
    rd[1] = d.l[1];

    clear_tail_16(vd, desc);
}

void HELPER(crypto_sha256su0)(void *vd, void *vm, uint32_t desc)
{
    uint64_t *rd = vd;
    uint64_t *rm = vm;
    union CRYPTO_STATE d = { .l = { rd[0], rd[1] } };
    union CRYPTO_STATE m = { .l = { rm[0], rm[1] } };

    CR_ST_WORD(d, 0) += s0(CR_ST_WORD(d, 1));
    CR_ST_WORD(d, 1) += s0(CR_ST_WORD(d, 2));
    CR_ST_WORD(d, 2) += s0(CR_ST_WORD(d, 3));
    CR_ST_WORD(d, 3) += s0(CR_ST_WORD(m, 0));

    rd[0] = d.l[0];
    rd[1] = d.l[1];

    clear_tail_16(vd, desc);
}

void HELPER(crypto_sha256su1)(void *vd, void *vn, void *vm, uint32_t desc)
{
    uint64_t *rd = vd;
    uint64_t *rn = vn;
    uint64_t *rm = vm;
    union CRYPTO_STATE d = { .l = { rd[0], rd[1] } };
    union CRYPTO_STATE n = { .l = { rn[0], rn[1] } };
    union CRYPTO_STATE m = { .l = { rm[0], rm[1] } };

    CR_ST_WORD(d, 0) += s1(CR_ST_WORD(m, 2)) + CR_ST_WORD(n, 1);
    CR_ST_WORD(d, 1) += s1(CR_ST_WORD(m, 3)) + CR_ST_WORD(n, 2);
    CR_ST_WORD(d, 2) += s1(CR_ST_WORD(d, 0)) + CR_ST_WORD(n, 3);
    CR_ST_WORD(d, 3) += s1(CR_ST_WORD(d, 1)) + CR_ST_WORD(m, 0);

    rd[0] = d.l[0];
    rd[1] = d.l[1];

    clear_tail_16(vd, desc);
}

/*
 * The SHA-512 logical functions (same as above but using 64-bit operands)
 */

static uint64_t cho512(uint64_t x, uint64_t y, uint64_t z)
{
    return (x & (y ^ z)) ^ z;
}

static uint64_t maj512(uint64_t x, uint64_t y, uint64_t z)
{
    return (x & y) | ((x | y) & z);
}

static uint64_t S0_512(uint64_t x)
{
    return ror64(x, 28) ^ ror64(x, 34) ^ ror64(x, 39);
}

static uint64_t S1_512(uint64_t x)
{
    return ror64(x, 14) ^ ror64(x, 18) ^ ror64(x, 41);
}

static uint64_t s0_512(uint64_t x)
{
    return ror64(x, 1) ^ ror64(x, 8) ^ (x >> 7);
}

static uint64_t s1_512(uint64_t x)
{
    return ror64(x, 19) ^ ror64(x, 61) ^ (x >> 6);
}

void HELPER(crypto_sha512h)(void *vd, void *vn, void *vm, uint32_t desc)
{
    uint64_t *rd = vd;
    uint64_t *rn = vn;
    uint64_t *rm = vm;
    uint64_t d0 = rd[0];
    uint64_t d1 = rd[1];

    d1 += S1_512(rm[1]) + cho512(rm[1], rn[0], rn[1]);
    d0 += S1_512(d1 + rm[0]) + cho512(d1 + rm[0], rm[1], rn[0]);

    rd[0] = d0;
    rd[1] = d1;

    clear_tail_16(vd, desc);
}

void HELPER(crypto_sha512h2)(void *vd, void *vn, void *vm, uint32_t desc)
{
    uint64_t *rd = vd;
    uint64_t *rn = vn;
    uint64_t *rm = vm;
    uint64_t d0 = rd[0];
    uint64_t d1 = rd[1];

    d1 += S0_512(rm[0]) + maj512(rn[0], rm[1], rm[0]);
    d0 += S0_512(d1) + maj512(d1, rm[0], rm[1]);

    rd[0] = d0;
    rd[1] = d1;

    clear_tail_16(vd, desc);
}

void HELPER(crypto_sha512su0)(void *vd, void *vn, uint32_t desc)
{
    uint64_t *rd = vd;
    uint64_t *rn = vn;
    uint64_t d0 = rd[0];
    uint64_t d1 = rd[1];

    d0 += s0_512(rd[1]);
    d1 += s0_512(rn[0]);

    rd[0] = d0;
    rd[1] = d1;

    clear_tail_16(vd, desc);
}

void HELPER(crypto_sha512su1)(void *vd, void *vn, void *vm, uint32_t desc)
{
    uint64_t *rd = vd;
    uint64_t *rn = vn;
    uint64_t *rm = vm;

    rd[0] += s1_512(rn[0]) + rm[0];
    rd[1] += s1_512(rn[1]) + rm[1];

    clear_tail_16(vd, desc);
}

void HELPER(crypto_sm3partw1)(void *vd, void *vn, void *vm, uint32_t desc)
{
    uint64_t *rd = vd;
    uint64_t *rn = vn;
    uint64_t *rm = vm;
    union CRYPTO_STATE d = { .l = { rd[0], rd[1] } };
    union CRYPTO_STATE n = { .l = { rn[0], rn[1] } };
    union CRYPTO_STATE m = { .l = { rm[0], rm[1] } };
    uint32_t t;

    t = CR_ST_WORD(d, 0) ^ CR_ST_WORD(n, 0) ^ ror32(CR_ST_WORD(m, 1), 17);
    CR_ST_WORD(d, 0) = t ^ ror32(t, 17) ^ ror32(t, 9);

    t = CR_ST_WORD(d, 1) ^ CR_ST_WORD(n, 1) ^ ror32(CR_ST_WORD(m, 2), 17);
    CR_ST_WORD(d, 1) = t ^ ror32(t, 17) ^ ror32(t, 9);

    t = CR_ST_WORD(d, 2) ^ CR_ST_WORD(n, 2) ^ ror32(CR_ST_WORD(m, 3), 17);
    CR_ST_WORD(d, 2) = t ^ ror32(t, 17) ^ ror32(t, 9);

    t = CR_ST_WORD(d, 3) ^ CR_ST_WORD(n, 3) ^ ror32(CR_ST_WORD(d, 0), 17);
    CR_ST_WORD(d, 3) = t ^ ror32(t, 17) ^ ror32(t, 9);

    rd[0] = d.l[0];
    rd[1] = d.l[1];

    clear_tail_16(vd, desc);
}

void HELPER(crypto_sm3partw2)(void *vd, void *vn, void *vm, uint32_t desc)
{
    uint64_t *rd = vd;
    uint64_t *rn = vn;
    uint64_t *rm = vm;
    union CRYPTO_STATE d = { .l = { rd[0], rd[1] } };
    union CRYPTO_STATE n = { .l = { rn[0], rn[1] } };
    union CRYPTO_STATE m = { .l = { rm[0], rm[1] } };
    uint32_t t = CR_ST_WORD(n, 0) ^ ror32(CR_ST_WORD(m, 0), 25);

    CR_ST_WORD(d, 0) ^= t;
    CR_ST_WORD(d, 1) ^= CR_ST_WORD(n, 1) ^ ror32(CR_ST_WORD(m, 1), 25);
    CR_ST_WORD(d, 2) ^= CR_ST_WORD(n, 2) ^ ror32(CR_ST_WORD(m, 2), 25);
    CR_ST_WORD(d, 3) ^= CR_ST_WORD(n, 3) ^ ror32(CR_ST_WORD(m, 3), 25) ^
                        ror32(t, 17) ^ ror32(t, 2) ^ ror32(t, 26);

    rd[0] = d.l[0];
    rd[1] = d.l[1];

    clear_tail_16(vd, desc);
}

static inline void QEMU_ALWAYS_INLINE
crypto_sm3tt(uint64_t *rd, uint64_t *rn, uint64_t *rm,
             uint32_t desc, uint32_t opcode)
{
    union CRYPTO_STATE d = { .l = { rd[0], rd[1] } };
    union CRYPTO_STATE n = { .l = { rn[0], rn[1] } };
    union CRYPTO_STATE m = { .l = { rm[0], rm[1] } };
    uint32_t imm2 = simd_data(desc);
    uint32_t t;

    assert(imm2 < 4);

    if (opcode == 0 || opcode == 2) {
        /* SM3TT1A, SM3TT2A */
        t = par(CR_ST_WORD(d, 3), CR_ST_WORD(d, 2), CR_ST_WORD(d, 1));
    } else if (opcode == 1) {
        /* SM3TT1B */
        t = maj(CR_ST_WORD(d, 3), CR_ST_WORD(d, 2), CR_ST_WORD(d, 1));
    } else if (opcode == 3) {
        /* SM3TT2B */
        t = cho(CR_ST_WORD(d, 3), CR_ST_WORD(d, 2), CR_ST_WORD(d, 1));
    } else {
        qemu_build_not_reached();
    }

    t += CR_ST_WORD(d, 0) + CR_ST_WORD(m, imm2);

    CR_ST_WORD(d, 0) = CR_ST_WORD(d, 1);

    if (opcode < 2) {
        /* SM3TT1A, SM3TT1B */
        t += CR_ST_WORD(n, 3) ^ ror32(CR_ST_WORD(d, 3), 20);

        CR_ST_WORD(d, 1) = ror32(CR_ST_WORD(d, 2), 23);
    } else {
        /* SM3TT2A, SM3TT2B */
        t += CR_ST_WORD(n, 3);
        t ^= rol32(t, 9) ^ rol32(t, 17);

        CR_ST_WORD(d, 1) = ror32(CR_ST_WORD(d, 2), 13);
    }

    CR_ST_WORD(d, 2) = CR_ST_WORD(d, 3);
    CR_ST_WORD(d, 3) = t;

    rd[0] = d.l[0];
    rd[1] = d.l[1];

    clear_tail_16(rd, desc);
}

#define DO_SM3TT(NAME, OPCODE) \
    void HELPER(NAME)(void *vd, void *vn, void *vm, uint32_t desc) \
    { crypto_sm3tt(vd, vn, vm, desc, OPCODE); }

DO_SM3TT(crypto_sm3tt1a, 0)
DO_SM3TT(crypto_sm3tt1b, 1)
DO_SM3TT(crypto_sm3tt2a, 2)
DO_SM3TT(crypto_sm3tt2b, 3)

#undef DO_SM3TT

static uint8_t const sm4_sbox[] = {
    0xd6, 0x90, 0xe9, 0xfe, 0xcc, 0xe1, 0x3d, 0xb7,
    0x16, 0xb6, 0x14, 0xc2, 0x28, 0xfb, 0x2c, 0x05,
    0x2b, 0x67, 0x9a, 0x76, 0x2a, 0xbe, 0x04, 0xc3,
    0xaa, 0x44, 0x13, 0x26, 0x49, 0x86, 0x06, 0x99,
    0x9c, 0x42, 0x50, 0xf4, 0x91, 0xef, 0x98, 0x7a,
    0x33, 0x54, 0x0b, 0x43, 0xed, 0xcf, 0xac, 0x62,
    0xe4, 0xb3, 0x1c, 0xa9, 0xc9, 0x08, 0xe8, 0x95,
    0x80, 0xdf, 0x94, 0xfa, 0x75, 0x8f, 0x3f, 0xa6,
    0x47, 0x07, 0xa7, 0xfc, 0xf3, 0x73, 0x17, 0xba,
    0x83, 0x59, 0x3c, 0x19, 0xe6, 0x85, 0x4f, 0xa8,
    0x68, 0x6b, 0x81, 0xb2, 0x71, 0x64, 0xda, 0x8b,
    0xf8, 0xeb, 0x0f, 0x4b, 0x70, 0x56, 0x9d, 0x35,
    0x1e, 0x24, 0x0e, 0x5e, 0x63, 0x58, 0xd1, 0xa2,
    0x25, 0x22, 0x7c, 0x3b, 0x01, 0x21, 0x78, 0x87,
    0xd4, 0x00, 0x46, 0x57, 0x9f, 0xd3, 0x27, 0x52,
    0x4c, 0x36, 0x02, 0xe7, 0xa0, 0xc4, 0xc8, 0x9e,
    0xea, 0xbf, 0x8a, 0xd2, 0x40, 0xc7, 0x38, 0xb5,
    0xa3, 0xf7, 0xf2, 0xce, 0xf9, 0x61, 0x15, 0xa1,
    0xe0, 0xae, 0x5d, 0xa4, 0x9b, 0x34, 0x1a, 0x55,
    0xad, 0x93, 0x32, 0x30, 0xf5, 0x8c, 0xb1, 0xe3,
    0x1d, 0xf6, 0xe2, 0x2e, 0x82, 0x66, 0xca, 0x60,
    0xc0, 0x29, 0x23, 0xab, 0x0d, 0x53, 0x4e, 0x6f,
    0xd5, 0xdb, 0x37, 0x45, 0xde, 0xfd, 0x8e, 0x2f,
    0x03, 0xff, 0x6a, 0x72, 0x6d, 0x6c, 0x5b, 0x51,
    0x8d, 0x1b, 0xaf, 0x92, 0xbb, 0xdd, 0xbc, 0x7f,
    0x11, 0xd9, 0x5c, 0x41, 0x1f, 0x10, 0x5a, 0xd8,
    0x0a, 0xc1, 0x31, 0x88, 0xa5, 0xcd, 0x7b, 0xbd,
    0x2d, 0x74, 0xd0, 0x12, 0xb8, 0xe5, 0xb4, 0xb0,
    0x89, 0x69, 0x97, 0x4a, 0x0c, 0x96, 0x77, 0x7e,
    0x65, 0xb9, 0xf1, 0x09, 0xc5, 0x6e, 0xc6, 0x84,
    0x18, 0xf0, 0x7d, 0xec, 0x3a, 0xdc, 0x4d, 0x20,
    0x79, 0xee, 0x5f, 0x3e, 0xd7, 0xcb, 0x39, 0x48,
};

static void do_crypto_sm4e(uint64_t *rd, uint64_t *rn, uint64_t *rm)
{
    union CRYPTO_STATE d = { .l = { rn[0], rn[1] } };
    union CRYPTO_STATE n = { .l = { rm[0], rm[1] } };
    uint32_t t, i;

    for (i = 0; i < 4; i++) {
        t = CR_ST_WORD(d, (i + 1) % 4) ^
            CR_ST_WORD(d, (i + 2) % 4) ^
            CR_ST_WORD(d, (i + 3) % 4) ^
            CR_ST_WORD(n, i);

        t = sm4_sbox[t & 0xff] |
            sm4_sbox[(t >> 8) & 0xff] << 8 |
            sm4_sbox[(t >> 16) & 0xff] << 16 |
            sm4_sbox[(t >> 24) & 0xff] << 24;

        CR_ST_WORD(d, i) ^= t ^ rol32(t, 2) ^ rol32(t, 10) ^ rol32(t, 18) ^
                            rol32(t, 24);
    }

    rd[0] = d.l[0];
    rd[1] = d.l[1];
}

void HELPER(crypto_sm4e)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);

    for (i = 0; i < opr_sz; i += 16) {
        do_crypto_sm4e(vd + i, vn + i, vm + i);
    }
    clear_tail(vd, opr_sz, simd_maxsz(desc));
}

static void do_crypto_sm4ekey(uint64_t *rd, uint64_t *rn, uint64_t *rm)
{
    union CRYPTO_STATE d;
    union CRYPTO_STATE n = { .l = { rn[0], rn[1] } };
    union CRYPTO_STATE m = { .l = { rm[0], rm[1] } };
    uint32_t t, i;

    d = n;
    for (i = 0; i < 4; i++) {
        t = CR_ST_WORD(d, (i + 1) % 4) ^
            CR_ST_WORD(d, (i + 2) % 4) ^
            CR_ST_WORD(d, (i + 3) % 4) ^
            CR_ST_WORD(m, i);

        t = sm4_sbox[t & 0xff] |
            sm4_sbox[(t >> 8) & 0xff] << 8 |
            sm4_sbox[(t >> 16) & 0xff] << 16 |
            sm4_sbox[(t >> 24) & 0xff] << 24;

        CR_ST_WORD(d, i) ^= t ^ rol32(t, 13) ^ rol32(t, 23);
    }

    rd[0] = d.l[0];
    rd[1] = d.l[1];
}

void HELPER(crypto_sm4ekey)(void *vd, void *vn, void* vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);

    for (i = 0; i < opr_sz; i += 16) {
        do_crypto_sm4ekey(vd + i, vn + i, vm + i);
    }
    clear_tail(vd, opr_sz, simd_maxsz(desc));
}

void HELPER(crypto_rax1)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    uint64_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 8; ++i) {
        d[i] = n[i] ^ rol64(m[i], 1);
    }
    clear_tail(vd, opr_sz, simd_maxsz(desc));
}
