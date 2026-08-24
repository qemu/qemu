/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * s390 cpacf sha256
 *
 * Authors:
 *   Harald Freudenberger <freude@linux.ibm.com>
 *
 * The sha256 implementation here is more or less a copy-and-paste
 * from Jason A. Donenfeld's implementation of sha 512 with adaptions
 * for sha 256.
 */

#include "qemu/osdep.h"
#include "s390x-internal.h"
#include "tcg_s390x.h"
#include "exec/helper-proto.h"
#include "accel/tcg/cpu-ldst-common.h"
#include "accel/tcg/cpu-mmu-index.h"
#include "target/s390x/tcg/cpacf-arch.h"
#include "target/s390x/tcg/cpacf.h"
#include "target/s390x/tcg/crypto_helper.h"

static uint32_t R(uint32_t x, int c)
{
    return (x >> c) | (x << (32 - c));
}
static uint32_t Ch(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (~x & z);
}
static uint32_t Maj(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}
static uint32_t Sigma0(uint32_t x)
{
    return R(x, 2) ^ R(x, 13) ^ R(x, 22);
}
static uint32_t Sigma1(uint32_t x)
{
    return R(x, 6) ^ R(x, 11) ^ R(x, 25);
}
static uint32_t sigma0(uint32_t x)
{
    return R(x, 7) ^ R(x, 18) ^ (x >> 3);
}
static uint32_t sigma1(uint32_t x)
{
    return R(x, 17) ^ R(x, 19) ^ (x >> 10);
}

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

/* a is icv/ocv, w is a single message block. w will get reused internally. */
static void sha256_bda(uint32_t a[8], uint32_t w[16])
{
    uint32_t t, z[8], b[8];
    int i, j;

    memcpy(z, a, sizeof(z));
    for (i = 0; i < 64; i++) {
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

/* a is icv/ocv, w is a single message block that needs be32 conversion. */
static void sha256_bda_be32(uint32_t a[8], uint32_t w[16])
{
    uint32_t t[16];
    int i;

    for (i = 0; i < 16; i++) {
        t[i] = be32_to_cpu(w[i]);
    }
    sha256_bda(a, t);
}

int cpacf_sha256(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                 uint64_t param_addr, uint64_t *message_reg, uint64_t *len_reg,
                 uint32_t type)
{
    enum { MAX_BLOCKS_PER_RUN = 128 }; /* 128 * 64 = 8K */
    uint64_t len = *len_reg, processed = 0;
    int message_reg_len = 64;
    uint32_t a[8];

    g_assert(type == S390_FEAT_TYPE_KIMD || type == S390_FEAT_TYPE_KLMD);

    if (!(env->psw.mask & PSW_MASK_64)) {
        len = (uint32_t)len;
        message_reg_len = (env->psw.mask & PSW_MASK_32) ? 32 : 24;
    }

    /* KIMD: length has to be properly aligned. */
    if (type == S390_FEAT_TYPE_KIMD && !QEMU_IS_ALIGNED(len, 64)) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* read icv (8 * u32) */
    read_guest_wrap_u32(env, mmu_idx, ra, param_addr, a, 8);

    /* Process full blocks first. */
    for (; len >= 64; len -= 64, processed += 64) {
        uint32_t w[16];

        if (processed >= MAX_BLOCKS_PER_RUN * 64) {
            break;
        }

        /* read sha256 block (16 * u32) */
        read_guest_wrap_u32(env, mmu_idx, ra, *message_reg + processed, w, 16);
        sha256_bda(a, w);
    }

    /* KLMD: Process partial/empty block last. */
    if (type == S390_FEAT_TYPE_KLMD && len < 64) {
        uint8_t x[64];

        /* Read the remainder of the message. */
        read_guest_wrap_u8(env, mmu_idx, ra, *message_reg + processed, x, len);

        /* Pad the remainder with zero and set the top bit. */
        memset(x + len, 0, 64 - len);
        x[len] = 0x80;

        /*
         * Place the MBL either into this block (if there is space left),
         * or use an additional one.
         */
        if (len < 56) {
            read_guest_wrap_u8(env, mmu_idx, ra, param_addr + 32, x + 56, 8);
        }
        sha256_bda_be32(a, (uint32_t *)x);

        if (len >= 56) {
            memset(x, 0, 56);
            read_guest_wrap_u8(env, mmu_idx, ra, param_addr + 32, x + 56, 8);
            sha256_bda_be32(a, (uint32_t *)x);
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
    write_guest_wrap_u32(env, mmu_idx, ra, param_addr, a, 8);
    *message_reg = deposit64(*message_reg, 0, message_reg_len,
                             *message_reg + processed);
    *len_reg -= processed;
    return !len ? 0 : 3;
}
