/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * buffer_is_zero acceleration, aarch64 version.
 */

#ifdef __ARM_NEON
#include <arm_neon.h>

/*
 * Helper for preventing the compiler from reassociating
 * chains of binary vector operations.
 */
#define REASSOC_BARRIER(vec0, vec1) asm("" : "+w"(vec0), "+w"(vec1))

static bool buffer_is_zero_simd(const void *buf, size_t len)
{
    uint32x4_t t0, t1, t2, t3;

    /* Align head/tail to 16-byte boundaries.  */
    const uint32x4_t *p = QEMU_ALIGN_PTR_DOWN(buf + 16, 16);
    const uint32x4_t *e = QEMU_ALIGN_PTR_DOWN(buf + len - 1, 16);

    /* Unaligned loads at head/tail.  */
    t0 = vld1q_u32(buf) | vld1q_u32(buf + len - 16);

    /* Collect a partial block at tail end.  */
    t1 = e[-7] | e[-6];
    t2 = e[-5] | e[-4];
    t3 = e[-3] | e[-2];
    t0 |= e[-1];
    REASSOC_BARRIER(t0, t1);
    REASSOC_BARRIER(t2, t3);
    t0 |= t1;
    t2 |= t3;
    REASSOC_BARRIER(t0, t2);
    t0 |= t2;

    /*
     * Loop over complete 128-byte blocks.
     * With the head and tail removed, e - p >= 14, so the loop
     * must iterate at least once.
     */
    do {
        /*
         * Reduce via UMAXV.  Whatever the actual result,
         * it will only be zero if all input bytes are zero.
         */
        if (unlikely(vmaxvq_u32(t0) != 0)) {
            return false;
        }

        t0 = p[0] | p[1];
        t1 = p[2] | p[3];
        t2 = p[4] | p[5];
        t3 = p[6] | p[7];
        REASSOC_BARRIER(t0, t1);
        REASSOC_BARRIER(t2, t3);
        t0 |= t1;
        t2 |= t3;
        REASSOC_BARRIER(t0, t2);
        t0 |= t2;
        p += 8;
    } while (p < e - 7);

    return vmaxvq_u32(t0) == 0;
}

static biz_accel_fn const accel_table[] = {
    buffer_is_zero_int_ge256,
    buffer_is_zero_simd,
};

#define best_accel() 1
#else
# include "host/include/generic/host/bufferiszero.c.inc"
#endif
