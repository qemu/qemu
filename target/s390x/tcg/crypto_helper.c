/*
 *  s390x crypto helpers
 *
 *  Copyright (C) 2022 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 *  Copyright (c) 2017 Red Hat Inc
 *
 *  Authors:
 *   David Hildenbrand <david@redhat.com>
 *   Jason A. Donenfeld <Jason@zx2c4.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/guest-random.h"
#include "s390x-internal.h"
#include "tcg_s390x.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"

static uint64_t R(uint64_t x, int c)
{
    return (x >> c) | (x << (64 - c));
}
static uint64_t Ch(uint64_t x, uint64_t y, uint64_t z)
{
    return (x & y) ^ (~x & z);
}
static uint64_t Maj(uint64_t x, uint64_t y, uint64_t z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}
static uint64_t Sigma0(uint64_t x)
{
    return R(x, 28) ^ R(x, 34) ^ R(x, 39);
}
static uint64_t Sigma1(uint64_t x)
{
    return R(x, 14) ^ R(x, 18) ^ R(x, 41);
}
static uint64_t sigma0(uint64_t x)
{
    return R(x, 1) ^ R(x, 8) ^ (x >> 7);
}
static uint64_t sigma1(uint64_t x)
{
    return R(x, 19) ^ R(x, 61) ^ (x >> 6);
}

static const uint64_t K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL,
    0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
    0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL,
    0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
    0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
    0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL,
    0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL,
    0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL,
    0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL,
    0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
    0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
    0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL,
    0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
    0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL,
    0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL,
    0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
    0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL,
    0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
    0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
    0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

/* a is icv/ocv, w is a single message block. w will get reused internally. */
static void sha512_bda(uint64_t a[8], uint64_t w[16])
{
    uint64_t t, z[8], b[8];
    int i, j;

    memcpy(z, a, sizeof(z));
    for (i = 0; i < 80; i++) {
        memcpy(b, a, sizeof(b));

        t = a[7] + Sigma1(a[4]) + Ch(a[4], a[5], a[6]) + K[i] + w[i % 16];
        b[7] = t + Sigma0(a[0]) + Maj(a[0], a[1], a[2]);
        b[3] += t;
        for (j = 0; j < 8; ++j) {
            a[(j + 1) % 8] = b[j];
        }
        if (i % 16 == 15) {
            for (j = 0; j < 16; ++j) {
                w[j] += w[(j + 9) % 16] + sigma0(w[(j + 1) % 16]) +
                        sigma1(w[(j + 14) % 16]);
            }
        }
    }

    for (i = 0; i < 8; i++) {
        a[i] += z[i];
    }
}

/* a is icv/ocv, w is a single message block that needs be64 conversion. */
static void sha512_bda_be64(uint64_t a[8], uint64_t w[16])
{
    uint64_t t[16];
    int i;

    for (i = 0; i < 16; i++) {
        t[i] = be64_to_cpu(w[i]);
    }
    sha512_bda(a, t);
}

static void sha512_read_icv(CPUS390XState *env, uint64_t addr,
                            uint64_t a[8], uintptr_t ra)
{
    int i;

    for (i = 0; i < 8; i++, addr += 8) {
        addr = wrap_address(env, addr);
        a[i] = cpu_ldq_be_data_ra(env, addr, ra);
    }
}

static void sha512_write_ocv(CPUS390XState *env, uint64_t addr,
                             uint64_t a[8], uintptr_t ra)
{
    int i;

    for (i = 0; i < 8; i++, addr += 8) {
        addr = wrap_address(env, addr);
        cpu_stq_be_data_ra(env, addr, a[i], ra);
    }
}

static void sha512_read_block(CPUS390XState *env, uint64_t addr,
                              uint64_t a[16], uintptr_t ra)
{
    int i;

    for (i = 0; i < 16; i++, addr += 8) {
        addr = wrap_address(env, addr);
        a[i] = cpu_ldq_be_data_ra(env, addr, ra);
    }
}

static void sha512_read_mbl_be64(CPUS390XState *env, uint64_t addr,
                                 uint8_t a[16], uintptr_t ra)
{
    int i;

    for (i = 0; i < 16; i++, addr += 1) {
        addr = wrap_address(env, addr);
        a[i] = cpu_ldub_data_ra(env, addr, ra);
    }
}

static int cpacf_sha512(CPUS390XState *env, uintptr_t ra, uint64_t param_addr,
                      uint64_t *message_reg, uint64_t *len_reg, uint32_t type)
{
    enum { MAX_BLOCKS_PER_RUN = 64 }; /* Arbitrary: keep interactivity. */
    uint64_t len = *len_reg, a[8], processed = 0;
    int i, message_reg_len = 64;

    g_assert(type == S390_FEAT_TYPE_KIMD || type == S390_FEAT_TYPE_KLMD);

    if (!(env->psw.mask & PSW_MASK_64)) {
        len = (uint32_t)len;
        message_reg_len = (env->psw.mask & PSW_MASK_32) ? 32 : 24;
    }

    /* KIMD: length has to be properly aligned. */
    if (type == S390_FEAT_TYPE_KIMD && !QEMU_IS_ALIGNED(len, 128)) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    sha512_read_icv(env, param_addr, a, ra);

    /* Process full blocks first. */
    for (; len >= 128; len -= 128, processed += 128) {
        uint64_t w[16];

        if (processed >= MAX_BLOCKS_PER_RUN * 128) {
            break;
        }

        sha512_read_block(env, *message_reg + processed, w, ra);
        sha512_bda(a, w);
    }

    /* KLMD: Process partial/empty block last. */
    if (type == S390_FEAT_TYPE_KLMD && len < 128) {
        uint8_t x[128];

        /* Read the remainder of the message byte-per-byte. */
        for (i = 0; i < len; i++) {
            uint64_t addr = wrap_address(env, *message_reg + processed + i);

            x[i] = cpu_ldub_data_ra(env, addr, ra);
        }
        /* Pad the remainder with zero and set the top bit. */
        memset(x + len, 0, 128 - len);
        x[len] = 128;

        /*
         * Place the MBL either into this block (if there is space left),
         * or use an additional one.
         */
        if (len < 112) {
            sha512_read_mbl_be64(env, param_addr + 64, x + 112, ra);
        }
        sha512_bda_be64(a, (uint64_t *)x);

        if (len >= 112) {
            memset(x, 0, 112);
            sha512_read_mbl_be64(env, param_addr + 64, x + 112, ra);
            sha512_bda_be64(a, (uint64_t *)x);
        }

        processed += len;
        len = 0;
    }

    /*
     * Modify memory after we read all inputs and modify registers only after
     * writing memory succeeded.
     *
     * TODO: if writing fails halfway through (e.g., when crossing page
     * boundaries), we're in trouble. We'd need something like access_prepare().
     */
    sha512_write_ocv(env, param_addr, a, ra);
    *message_reg = deposit64(*message_reg, 0, message_reg_len,
                             *message_reg + processed);
    *len_reg -= processed;
    return !len ? 0 : 3;
}

static void fill_buf_random(CPUS390XState *env, uintptr_t ra,
                            uint64_t *buf_reg, uint64_t *len_reg)
{
    uint8_t tmp[256];
    uint64_t len = *len_reg;
    int buf_reg_len = 64;

    if (!(env->psw.mask & PSW_MASK_64)) {
        len = (uint32_t)len;
        buf_reg_len = (env->psw.mask & PSW_MASK_32) ? 32 : 24;
    }

    while (len) {
        size_t block = MIN(len, sizeof(tmp));

        qemu_guest_getrandom_nofail(tmp, block);
        for (size_t i = 0; i < block; ++i) {
            cpu_stb_data_ra(env, wrap_address(env, *buf_reg), tmp[i], ra);
            *buf_reg = deposit64(*buf_reg, 0, buf_reg_len, *buf_reg + 1);
            --*len_reg;
        }
        len -= block;
    }
}

uint32_t HELPER(msa)(CPUS390XState *env, uint32_t r1, uint32_t r2, uint32_t r3,
                     uint32_t type)
{
    const uintptr_t ra = GETPC();
    const uint8_t mod = env->regs[0] & 0x80ULL;
    const uint8_t fc = env->regs[0] & 0x7fULL;
    uint8_t subfunc[16] = { 0 };
    uint64_t param_addr;
    int i;

    switch (type) {
    case S390_FEAT_TYPE_KMAC:
    case S390_FEAT_TYPE_KIMD:
    case S390_FEAT_TYPE_KLMD:
    case S390_FEAT_TYPE_PCKMO:
    case S390_FEAT_TYPE_PCC:
        if (mod) {
            tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        }
        break;
    }

    s390_get_feat_block(type, subfunc);
    if (!test_be_bit(fc, subfunc)) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    switch (fc) {
    case 0: /* query subfunction */
        for (i = 0; i < 16; i++) {
            param_addr = wrap_address(env, env->regs[1] + i);
            cpu_stb_data_ra(env, param_addr, subfunc[i], ra);
        }
        break;
    case 3: /* CPACF_*_SHA_512 */
        return cpacf_sha512(env, ra, env->regs[1], &env->regs[r2],
                            &env->regs[r2 + 1], type);
    case 114: /* CPACF_PRNO_TRNG */
        fill_buf_random(env, ra, &env->regs[r1], &env->regs[r1 + 1]);
        fill_buf_random(env, ra, &env->regs[r2], &env->regs[r2 + 1]);
        break;
    default:
        /* we don't implement any other subfunction yet */
        g_assert_not_reached();
    }

    return 0;
}
