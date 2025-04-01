/*
 * ARM SVE Operations
 *
 * Copyright (c) 2018 Linaro, Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "exec/exec-all.h"
#include "exec/page-protection.h"
#include "exec/helper-proto.h"
#include "exec/target_page.h"
#include "exec/tlb-flags.h"
#include "tcg/tcg-gvec-desc.h"
#include "fpu/softfloat.h"
#include "tcg/tcg.h"
#include "vec_internal.h"
#include "sve_ldst_internal.h"
#include "accel/tcg/cpu-ldst.h"
#include "accel/tcg/cpu-ops.h"
#ifdef CONFIG_USER_ONLY
#include "user/page-protection.h"
#endif


/* Return a value for NZCV as per the ARM PredTest pseudofunction.
 *
 * The return value has bit 31 set if N is set, bit 1 set if Z is clear,
 * and bit 0 set if C is set.  Compare the definitions of these variables
 * within CPUARMState.
 */

/* For no G bits set, NZCV = C.  */
#define PREDTEST_INIT  1

/* This is an iterative function, called for each Pd and Pg word
 * moving forward.
 */
static uint32_t iter_predtest_fwd(uint64_t d, uint64_t g, uint32_t flags)
{
    if (likely(g)) {
        /* Compute N from first D & G.
           Use bit 2 to signal first G bit seen.  */
        if (!(flags & 4)) {
            flags |= ((d & (g & -g)) != 0) << 31;
            flags |= 4;
        }

        /* Accumulate Z from each D & G.  */
        flags |= ((d & g) != 0) << 1;

        /* Compute C from last !(D & G).  Replace previous.  */
        flags = deposit32(flags, 0, 1, (d & pow2floor(g)) == 0);
    }
    return flags;
}

/* This is an iterative function, called for each Pd and Pg word
 * moving backward.
 */
static uint32_t iter_predtest_bwd(uint64_t d, uint64_t g, uint32_t flags)
{
    if (likely(g)) {
        /* Compute C from first (i.e last) !(D & G).
           Use bit 2 to signal first G bit seen.  */
        if (!(flags & 4)) {
            flags += 4 - 1; /* add bit 2, subtract C from PREDTEST_INIT */
            flags |= (d & pow2floor(g)) == 0;
        }

        /* Accumulate Z from each D & G.  */
        flags |= ((d & g) != 0) << 1;

        /* Compute N from last (i.e first) D & G.  Replace previous.  */
        flags = deposit32(flags, 31, 1, (d & (g & -g)) != 0);
    }
    return flags;
}

/* The same for a single word predicate.  */
uint32_t HELPER(sve_predtest1)(uint64_t d, uint64_t g)
{
    return iter_predtest_fwd(d, g, PREDTEST_INIT);
}

/* The same for a multi-word predicate.  */
uint32_t HELPER(sve_predtest)(void *vd, void *vg, uint32_t words)
{
    uint32_t flags = PREDTEST_INIT;
    uint64_t *d = vd, *g = vg;
    uintptr_t i = 0;

    do {
        flags = iter_predtest_fwd(d[i], g[i], flags);
    } while (++i < words);

    return flags;
}

/* Similarly for single word elements.  */
static inline uint64_t expand_pred_s(uint8_t byte)
{
    static const uint64_t word[] = {
        [0x01] = 0x00000000ffffffffull,
        [0x10] = 0xffffffff00000000ull,
        [0x11] = 0xffffffffffffffffull,
    };
    return word[byte & 0x11];
}

#define LOGICAL_PPPP(NAME, FUNC) \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *vg, uint32_t desc)  \
{                                                                         \
    uintptr_t opr_sz = simd_oprsz(desc);                                  \
    uint64_t *d = vd, *n = vn, *m = vm, *g = vg;                          \
    uintptr_t i;                                                          \
    for (i = 0; i < opr_sz / 8; ++i) {                                    \
        d[i] = FUNC(n[i], m[i], g[i]);                                    \
    }                                                                     \
}

#define DO_AND(N, M, G)  (((N) & (M)) & (G))
#define DO_BIC(N, M, G)  (((N) & ~(M)) & (G))
#define DO_EOR(N, M, G)  (((N) ^ (M)) & (G))
#define DO_ORR(N, M, G)  (((N) | (M)) & (G))
#define DO_ORN(N, M, G)  (((N) | ~(M)) & (G))
#define DO_NOR(N, M, G)  (~((N) | (M)) & (G))
#define DO_NAND(N, M, G) (~((N) & (M)) & (G))
#define DO_SEL(N, M, G)  (((N) & (G)) | ((M) & ~(G)))

LOGICAL_PPPP(sve_and_pppp, DO_AND)
LOGICAL_PPPP(sve_bic_pppp, DO_BIC)
LOGICAL_PPPP(sve_eor_pppp, DO_EOR)
LOGICAL_PPPP(sve_sel_pppp, DO_SEL)
LOGICAL_PPPP(sve_orr_pppp, DO_ORR)
LOGICAL_PPPP(sve_orn_pppp, DO_ORN)
LOGICAL_PPPP(sve_nor_pppp, DO_NOR)
LOGICAL_PPPP(sve_nand_pppp, DO_NAND)

#undef DO_AND
#undef DO_BIC
#undef DO_EOR
#undef DO_ORR
#undef DO_ORN
#undef DO_NOR
#undef DO_NAND
#undef DO_SEL
#undef LOGICAL_PPPP

/* Fully general three-operand expander, controlled by a predicate.
 * This is complicated by the host-endian storage of the register file.
 */
/* ??? I don't expect the compiler could ever vectorize this itself.
 * With some tables we can convert bit masks to byte masks, and with
 * extra care wrt byte/word ordering we could use gcc generic vectors
 * and do 16 bytes at a time.
 */
#define DO_ZPZZ(NAME, TYPE, H, OP)                                       \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *vg, uint32_t desc) \
{                                                                       \
    intptr_t i, opr_sz = simd_oprsz(desc);                              \
    for (i = 0; i < opr_sz; ) {                                         \
        uint16_t pg = *(uint16_t *)(vg + H1_2(i >> 3));                 \
        do {                                                            \
            if (pg & 1) {                                               \
                TYPE nn = *(TYPE *)(vn + H(i));                         \
                TYPE mm = *(TYPE *)(vm + H(i));                         \
                *(TYPE *)(vd + H(i)) = OP(nn, mm);                      \
            }                                                           \
            i += sizeof(TYPE), pg >>= sizeof(TYPE);                     \
        } while (i & 15);                                               \
    }                                                                   \
}

/* Similarly, specialized for 64-bit operands.  */
#define DO_ZPZZ_D(NAME, TYPE, OP)                                \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *vg, uint32_t desc) \
{                                                               \
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;                  \
    TYPE *d = vd, *n = vn, *m = vm;                             \
    uint8_t *pg = vg;                                           \
    for (i = 0; i < opr_sz; i += 1) {                           \
        if (pg[H1(i)] & 1) {                                    \
            TYPE nn = n[i], mm = m[i];                          \
            d[i] = OP(nn, mm);                                  \
        }                                                       \
    }                                                           \
}

#define DO_AND(N, M)  (N & M)
#define DO_EOR(N, M)  (N ^ M)
#define DO_ORR(N, M)  (N | M)
#define DO_BIC(N, M)  (N & ~M)
#define DO_ADD(N, M)  (N + M)
#define DO_SUB(N, M)  (N - M)
#define DO_MAX(N, M)  ((N) >= (M) ? (N) : (M))
#define DO_MIN(N, M)  ((N) >= (M) ? (M) : (N))
#define DO_ABD(N, M)  ((N) >= (M) ? (N) - (M) : (M) - (N))
#define DO_MUL(N, M)  (N * M)


/*
 * We must avoid the C undefined behaviour cases: division by
 * zero and signed division of INT_MIN by -1. Both of these
 * have architecturally defined required results for Arm.
 * We special case all signed divisions by -1 to avoid having
 * to deduce the minimum integer for the type involved.
 */
#define DO_SDIV(N, M) (unlikely(M == 0) ? 0 : unlikely(M == -1) ? -N : N / M)
#define DO_UDIV(N, M) (unlikely(M == 0) ? 0 : N / M)

DO_ZPZZ(sve_and_zpzz_b, uint8_t, H1, DO_AND)
DO_ZPZZ(sve_and_zpzz_h, uint16_t, H1_2, DO_AND)
DO_ZPZZ(sve_and_zpzz_s, uint32_t, H1_4, DO_AND)
DO_ZPZZ_D(sve_and_zpzz_d, uint64_t, DO_AND)

DO_ZPZZ(sve_orr_zpzz_b, uint8_t, H1, DO_ORR)
DO_ZPZZ(sve_orr_zpzz_h, uint16_t, H1_2, DO_ORR)
DO_ZPZZ(sve_orr_zpzz_s, uint32_t, H1_4, DO_ORR)
DO_ZPZZ_D(sve_orr_zpzz_d, uint64_t, DO_ORR)

DO_ZPZZ(sve_eor_zpzz_b, uint8_t, H1, DO_EOR)
DO_ZPZZ(sve_eor_zpzz_h, uint16_t, H1_2, DO_EOR)
DO_ZPZZ(sve_eor_zpzz_s, uint32_t, H1_4, DO_EOR)
DO_ZPZZ_D(sve_eor_zpzz_d, uint64_t, DO_EOR)

DO_ZPZZ(sve_bic_zpzz_b, uint8_t, H1, DO_BIC)
DO_ZPZZ(sve_bic_zpzz_h, uint16_t, H1_2, DO_BIC)
DO_ZPZZ(sve_bic_zpzz_s, uint32_t, H1_4, DO_BIC)
DO_ZPZZ_D(sve_bic_zpzz_d, uint64_t, DO_BIC)

DO_ZPZZ(sve_add_zpzz_b, uint8_t, H1, DO_ADD)
DO_ZPZZ(sve_add_zpzz_h, uint16_t, H1_2, DO_ADD)
DO_ZPZZ(sve_add_zpzz_s, uint32_t, H1_4, DO_ADD)
DO_ZPZZ_D(sve_add_zpzz_d, uint64_t, DO_ADD)

DO_ZPZZ(sve_sub_zpzz_b, uint8_t, H1, DO_SUB)
DO_ZPZZ(sve_sub_zpzz_h, uint16_t, H1_2, DO_SUB)
DO_ZPZZ(sve_sub_zpzz_s, uint32_t, H1_4, DO_SUB)
DO_ZPZZ_D(sve_sub_zpzz_d, uint64_t, DO_SUB)

DO_ZPZZ(sve_smax_zpzz_b, int8_t, H1, DO_MAX)
DO_ZPZZ(sve_smax_zpzz_h, int16_t, H1_2, DO_MAX)
DO_ZPZZ(sve_smax_zpzz_s, int32_t, H1_4, DO_MAX)
DO_ZPZZ_D(sve_smax_zpzz_d, int64_t, DO_MAX)

DO_ZPZZ(sve_umax_zpzz_b, uint8_t, H1, DO_MAX)
DO_ZPZZ(sve_umax_zpzz_h, uint16_t, H1_2, DO_MAX)
DO_ZPZZ(sve_umax_zpzz_s, uint32_t, H1_4, DO_MAX)
DO_ZPZZ_D(sve_umax_zpzz_d, uint64_t, DO_MAX)

DO_ZPZZ(sve_smin_zpzz_b, int8_t,  H1, DO_MIN)
DO_ZPZZ(sve_smin_zpzz_h, int16_t,  H1_2, DO_MIN)
DO_ZPZZ(sve_smin_zpzz_s, int32_t,  H1_4, DO_MIN)
DO_ZPZZ_D(sve_smin_zpzz_d, int64_t,  DO_MIN)

DO_ZPZZ(sve_umin_zpzz_b, uint8_t, H1, DO_MIN)
DO_ZPZZ(sve_umin_zpzz_h, uint16_t, H1_2, DO_MIN)
DO_ZPZZ(sve_umin_zpzz_s, uint32_t, H1_4, DO_MIN)
DO_ZPZZ_D(sve_umin_zpzz_d, uint64_t, DO_MIN)

DO_ZPZZ(sve_sabd_zpzz_b, int8_t,  H1, DO_ABD)
DO_ZPZZ(sve_sabd_zpzz_h, int16_t,  H1_2, DO_ABD)
DO_ZPZZ(sve_sabd_zpzz_s, int32_t,  H1_4, DO_ABD)
DO_ZPZZ_D(sve_sabd_zpzz_d, int64_t,  DO_ABD)

DO_ZPZZ(sve_uabd_zpzz_b, uint8_t, H1, DO_ABD)
DO_ZPZZ(sve_uabd_zpzz_h, uint16_t, H1_2, DO_ABD)
DO_ZPZZ(sve_uabd_zpzz_s, uint32_t, H1_4, DO_ABD)
DO_ZPZZ_D(sve_uabd_zpzz_d, uint64_t, DO_ABD)

/* Because the computation type is at least twice as large as required,
   these work for both signed and unsigned source types.  */
static inline uint8_t do_mulh_b(int32_t n, int32_t m)
{
    return (n * m) >> 8;
}

static inline uint16_t do_mulh_h(int32_t n, int32_t m)
{
    return (n * m) >> 16;
}

static inline uint32_t do_mulh_s(int64_t n, int64_t m)
{
    return (n * m) >> 32;
}

static inline uint64_t do_smulh_d(uint64_t n, uint64_t m)
{
    uint64_t lo, hi;
    muls64(&lo, &hi, n, m);
    return hi;
}

static inline uint64_t do_umulh_d(uint64_t n, uint64_t m)
{
    uint64_t lo, hi;
    mulu64(&lo, &hi, n, m);
    return hi;
}

DO_ZPZZ(sve_mul_zpzz_b, uint8_t, H1, DO_MUL)
DO_ZPZZ(sve_mul_zpzz_h, uint16_t, H1_2, DO_MUL)
DO_ZPZZ(sve_mul_zpzz_s, uint32_t, H1_4, DO_MUL)
DO_ZPZZ_D(sve_mul_zpzz_d, uint64_t, DO_MUL)

DO_ZPZZ(sve_smulh_zpzz_b, int8_t, H1, do_mulh_b)
DO_ZPZZ(sve_smulh_zpzz_h, int16_t, H1_2, do_mulh_h)
DO_ZPZZ(sve_smulh_zpzz_s, int32_t, H1_4, do_mulh_s)
DO_ZPZZ_D(sve_smulh_zpzz_d, uint64_t, do_smulh_d)

DO_ZPZZ(sve_umulh_zpzz_b, uint8_t, H1, do_mulh_b)
DO_ZPZZ(sve_umulh_zpzz_h, uint16_t, H1_2, do_mulh_h)
DO_ZPZZ(sve_umulh_zpzz_s, uint32_t, H1_4, do_mulh_s)
DO_ZPZZ_D(sve_umulh_zpzz_d, uint64_t, do_umulh_d)

DO_ZPZZ(sve_sdiv_zpzz_s, int32_t, H1_4, DO_SDIV)
DO_ZPZZ_D(sve_sdiv_zpzz_d, int64_t, DO_SDIV)

DO_ZPZZ(sve_udiv_zpzz_s, uint32_t, H1_4, DO_UDIV)
DO_ZPZZ_D(sve_udiv_zpzz_d, uint64_t, DO_UDIV)

/* Note that all bits of the shift are significant
   and not modulo the element size.  */
#define DO_ASR(N, M)  (N >> MIN(M, sizeof(N) * 8 - 1))
#define DO_LSR(N, M)  (M < sizeof(N) * 8 ? N >> M : 0)
#define DO_LSL(N, M)  (M < sizeof(N) * 8 ? N << M : 0)

DO_ZPZZ(sve_asr_zpzz_b, int8_t, H1, DO_ASR)
DO_ZPZZ(sve_lsr_zpzz_b, uint8_t, H1_2, DO_LSR)
DO_ZPZZ(sve_lsl_zpzz_b, uint8_t, H1_4, DO_LSL)

DO_ZPZZ(sve_asr_zpzz_h, int16_t, H1, DO_ASR)
DO_ZPZZ(sve_lsr_zpzz_h, uint16_t, H1_2, DO_LSR)
DO_ZPZZ(sve_lsl_zpzz_h, uint16_t, H1_4, DO_LSL)

DO_ZPZZ(sve_asr_zpzz_s, int32_t, H1, DO_ASR)
DO_ZPZZ(sve_lsr_zpzz_s, uint32_t, H1_2, DO_LSR)
DO_ZPZZ(sve_lsl_zpzz_s, uint32_t, H1_4, DO_LSL)

DO_ZPZZ_D(sve_asr_zpzz_d, int64_t, DO_ASR)
DO_ZPZZ_D(sve_lsr_zpzz_d, uint64_t, DO_LSR)
DO_ZPZZ_D(sve_lsl_zpzz_d, uint64_t, DO_LSL)

static inline uint16_t do_sadalp_h(int16_t n, int16_t m)
{
    int8_t n1 = n, n2 = n >> 8;
    return m + n1 + n2;
}

static inline uint32_t do_sadalp_s(int32_t n, int32_t m)
{
    int16_t n1 = n, n2 = n >> 16;
    return m + n1 + n2;
}

static inline uint64_t do_sadalp_d(int64_t n, int64_t m)
{
    int32_t n1 = n, n2 = n >> 32;
    return m + n1 + n2;
}

DO_ZPZZ(sve2_sadalp_zpzz_h, int16_t, H1_2, do_sadalp_h)
DO_ZPZZ(sve2_sadalp_zpzz_s, int32_t, H1_4, do_sadalp_s)
DO_ZPZZ_D(sve2_sadalp_zpzz_d, int64_t, do_sadalp_d)

static inline uint16_t do_uadalp_h(uint16_t n, uint16_t m)
{
    uint8_t n1 = n, n2 = n >> 8;
    return m + n1 + n2;
}

static inline uint32_t do_uadalp_s(uint32_t n, uint32_t m)
{
    uint16_t n1 = n, n2 = n >> 16;
    return m + n1 + n2;
}

static inline uint64_t do_uadalp_d(uint64_t n, uint64_t m)
{
    uint32_t n1 = n, n2 = n >> 32;
    return m + n1 + n2;
}

DO_ZPZZ(sve2_uadalp_zpzz_h, uint16_t, H1_2, do_uadalp_h)
DO_ZPZZ(sve2_uadalp_zpzz_s, uint32_t, H1_4, do_uadalp_s)
DO_ZPZZ_D(sve2_uadalp_zpzz_d, uint64_t, do_uadalp_d)

#define do_srshl_b(n, m)  do_sqrshl_bhs(n, m, 8, true, NULL)
#define do_srshl_h(n, m)  do_sqrshl_bhs(n, m, 16, true, NULL)
#define do_srshl_s(n, m)  do_sqrshl_bhs(n, m, 32, true, NULL)
#define do_srshl_d(n, m)  do_sqrshl_d(n, m, true, NULL)

DO_ZPZZ(sve2_srshl_zpzz_b, int8_t, H1, do_srshl_b)
DO_ZPZZ(sve2_srshl_zpzz_h, int16_t, H1_2, do_srshl_h)
DO_ZPZZ(sve2_srshl_zpzz_s, int32_t, H1_4, do_srshl_s)
DO_ZPZZ_D(sve2_srshl_zpzz_d, int64_t, do_srshl_d)

#define do_urshl_b(n, m)  do_uqrshl_bhs(n, (int8_t)m, 8, true, NULL)
#define do_urshl_h(n, m)  do_uqrshl_bhs(n, (int16_t)m, 16, true, NULL)
#define do_urshl_s(n, m)  do_uqrshl_bhs(n, m, 32, true, NULL)
#define do_urshl_d(n, m)  do_uqrshl_d(n, m, true, NULL)

DO_ZPZZ(sve2_urshl_zpzz_b, uint8_t, H1, do_urshl_b)
DO_ZPZZ(sve2_urshl_zpzz_h, uint16_t, H1_2, do_urshl_h)
DO_ZPZZ(sve2_urshl_zpzz_s, uint32_t, H1_4, do_urshl_s)
DO_ZPZZ_D(sve2_urshl_zpzz_d, uint64_t, do_urshl_d)

/*
 * Unlike the NEON and AdvSIMD versions, there is no QC bit to set.
 * We pass in a pointer to a dummy saturation field to trigger
 * the saturating arithmetic but discard the information about
 * whether it has occurred.
 */
#define do_sqshl_b(n, m) \
   ({ uint32_t discard; do_sqrshl_bhs(n, m, 8, false, &discard); })
#define do_sqshl_h(n, m) \
   ({ uint32_t discard; do_sqrshl_bhs(n, m, 16, false, &discard); })
#define do_sqshl_s(n, m) \
   ({ uint32_t discard; do_sqrshl_bhs(n, m, 32, false, &discard); })
#define do_sqshl_d(n, m) \
   ({ uint32_t discard; do_sqrshl_d(n, m, false, &discard); })

DO_ZPZZ(sve2_sqshl_zpzz_b, int8_t, H1_2, do_sqshl_b)
DO_ZPZZ(sve2_sqshl_zpzz_h, int16_t, H1_2, do_sqshl_h)
DO_ZPZZ(sve2_sqshl_zpzz_s, int32_t, H1_4, do_sqshl_s)
DO_ZPZZ_D(sve2_sqshl_zpzz_d, int64_t, do_sqshl_d)

#define do_uqshl_b(n, m) \
   ({ uint32_t discard; do_uqrshl_bhs(n, (int8_t)m, 8, false, &discard); })
#define do_uqshl_h(n, m) \
   ({ uint32_t discard; do_uqrshl_bhs(n, (int16_t)m, 16, false, &discard); })
#define do_uqshl_s(n, m) \
   ({ uint32_t discard; do_uqrshl_bhs(n, m, 32, false, &discard); })
#define do_uqshl_d(n, m) \
   ({ uint32_t discard; do_uqrshl_d(n, m, false, &discard); })

DO_ZPZZ(sve2_uqshl_zpzz_b, uint8_t, H1_2, do_uqshl_b)
DO_ZPZZ(sve2_uqshl_zpzz_h, uint16_t, H1_2, do_uqshl_h)
DO_ZPZZ(sve2_uqshl_zpzz_s, uint32_t, H1_4, do_uqshl_s)
DO_ZPZZ_D(sve2_uqshl_zpzz_d, uint64_t, do_uqshl_d)

#define do_sqrshl_b(n, m) \
   ({ uint32_t discard; do_sqrshl_bhs(n, m, 8, true, &discard); })
#define do_sqrshl_h(n, m) \
   ({ uint32_t discard; do_sqrshl_bhs(n, m, 16, true, &discard); })
#define do_sqrshl_s(n, m) \
   ({ uint32_t discard; do_sqrshl_bhs(n, m, 32, true, &discard); })
#define do_sqrshl_d(n, m) \
   ({ uint32_t discard; do_sqrshl_d(n, m, true, &discard); })

DO_ZPZZ(sve2_sqrshl_zpzz_b, int8_t, H1_2, do_sqrshl_b)
DO_ZPZZ(sve2_sqrshl_zpzz_h, int16_t, H1_2, do_sqrshl_h)
DO_ZPZZ(sve2_sqrshl_zpzz_s, int32_t, H1_4, do_sqrshl_s)
DO_ZPZZ_D(sve2_sqrshl_zpzz_d, int64_t, do_sqrshl_d)

#undef do_sqrshl_d

#define do_uqrshl_b(n, m) \
   ({ uint32_t discard; do_uqrshl_bhs(n, (int8_t)m, 8, true, &discard); })
#define do_uqrshl_h(n, m) \
   ({ uint32_t discard; do_uqrshl_bhs(n, (int16_t)m, 16, true, &discard); })
#define do_uqrshl_s(n, m) \
   ({ uint32_t discard; do_uqrshl_bhs(n, m, 32, true, &discard); })
#define do_uqrshl_d(n, m) \
   ({ uint32_t discard; do_uqrshl_d(n, m, true, &discard); })

DO_ZPZZ(sve2_uqrshl_zpzz_b, uint8_t, H1_2, do_uqrshl_b)
DO_ZPZZ(sve2_uqrshl_zpzz_h, uint16_t, H1_2, do_uqrshl_h)
DO_ZPZZ(sve2_uqrshl_zpzz_s, uint32_t, H1_4, do_uqrshl_s)
DO_ZPZZ_D(sve2_uqrshl_zpzz_d, uint64_t, do_uqrshl_d)

#undef do_uqrshl_d

#define DO_HADD_BHS(n, m)  (((int64_t)n + m) >> 1)
#define DO_HADD_D(n, m)    ((n >> 1) + (m >> 1) + (n & m & 1))

DO_ZPZZ(sve2_shadd_zpzz_b, int8_t, H1, DO_HADD_BHS)
DO_ZPZZ(sve2_shadd_zpzz_h, int16_t, H1_2, DO_HADD_BHS)
DO_ZPZZ(sve2_shadd_zpzz_s, int32_t, H1_4, DO_HADD_BHS)
DO_ZPZZ_D(sve2_shadd_zpzz_d, int64_t, DO_HADD_D)

DO_ZPZZ(sve2_uhadd_zpzz_b, uint8_t, H1, DO_HADD_BHS)
DO_ZPZZ(sve2_uhadd_zpzz_h, uint16_t, H1_2, DO_HADD_BHS)
DO_ZPZZ(sve2_uhadd_zpzz_s, uint32_t, H1_4, DO_HADD_BHS)
DO_ZPZZ_D(sve2_uhadd_zpzz_d, uint64_t, DO_HADD_D)

#define DO_RHADD_BHS(n, m)  (((int64_t)n + m + 1) >> 1)
#define DO_RHADD_D(n, m)    ((n >> 1) + (m >> 1) + ((n | m) & 1))

DO_ZPZZ(sve2_srhadd_zpzz_b, int8_t, H1, DO_RHADD_BHS)
DO_ZPZZ(sve2_srhadd_zpzz_h, int16_t, H1_2, DO_RHADD_BHS)
DO_ZPZZ(sve2_srhadd_zpzz_s, int32_t, H1_4, DO_RHADD_BHS)
DO_ZPZZ_D(sve2_srhadd_zpzz_d, int64_t, DO_RHADD_D)

DO_ZPZZ(sve2_urhadd_zpzz_b, uint8_t, H1, DO_RHADD_BHS)
DO_ZPZZ(sve2_urhadd_zpzz_h, uint16_t, H1_2, DO_RHADD_BHS)
DO_ZPZZ(sve2_urhadd_zpzz_s, uint32_t, H1_4, DO_RHADD_BHS)
DO_ZPZZ_D(sve2_urhadd_zpzz_d, uint64_t, DO_RHADD_D)

#define DO_HSUB_BHS(n, m)  (((int64_t)n - m) >> 1)
#define DO_HSUB_D(n, m)    ((n >> 1) - (m >> 1) - (~n & m & 1))

DO_ZPZZ(sve2_shsub_zpzz_b, int8_t, H1, DO_HSUB_BHS)
DO_ZPZZ(sve2_shsub_zpzz_h, int16_t, H1_2, DO_HSUB_BHS)
DO_ZPZZ(sve2_shsub_zpzz_s, int32_t, H1_4, DO_HSUB_BHS)
DO_ZPZZ_D(sve2_shsub_zpzz_d, int64_t, DO_HSUB_D)

DO_ZPZZ(sve2_uhsub_zpzz_b, uint8_t, H1, DO_HSUB_BHS)
DO_ZPZZ(sve2_uhsub_zpzz_h, uint16_t, H1_2, DO_HSUB_BHS)
DO_ZPZZ(sve2_uhsub_zpzz_s, uint32_t, H1_4, DO_HSUB_BHS)
DO_ZPZZ_D(sve2_uhsub_zpzz_d, uint64_t, DO_HSUB_D)

static inline int32_t do_sat_bhs(int64_t val, int64_t min, int64_t max)
{
    return val >= max ? max : val <= min ? min : val;
}

#define DO_SQADD_B(n, m) do_sat_bhs((int64_t)n + m, INT8_MIN, INT8_MAX)
#define DO_SQADD_H(n, m) do_sat_bhs((int64_t)n + m, INT16_MIN, INT16_MAX)
#define DO_SQADD_S(n, m) do_sat_bhs((int64_t)n + m, INT32_MIN, INT32_MAX)

static inline int64_t do_sqadd_d(int64_t n, int64_t m)
{
    int64_t r = n + m;
    if (((r ^ n) & ~(n ^ m)) < 0) {
        /* Signed overflow.  */
        return r < 0 ? INT64_MAX : INT64_MIN;
    }
    return r;
}

DO_ZPZZ(sve2_sqadd_zpzz_b, int8_t, H1, DO_SQADD_B)
DO_ZPZZ(sve2_sqadd_zpzz_h, int16_t, H1_2, DO_SQADD_H)
DO_ZPZZ(sve2_sqadd_zpzz_s, int32_t, H1_4, DO_SQADD_S)
DO_ZPZZ_D(sve2_sqadd_zpzz_d, int64_t, do_sqadd_d)

#define DO_UQADD_B(n, m) do_sat_bhs((int64_t)n + m, 0, UINT8_MAX)
#define DO_UQADD_H(n, m) do_sat_bhs((int64_t)n + m, 0, UINT16_MAX)
#define DO_UQADD_S(n, m) do_sat_bhs((int64_t)n + m, 0, UINT32_MAX)

static inline uint64_t do_uqadd_d(uint64_t n, uint64_t m)
{
    uint64_t r = n + m;
    return r < n ? UINT64_MAX : r;
}

DO_ZPZZ(sve2_uqadd_zpzz_b, uint8_t, H1, DO_UQADD_B)
DO_ZPZZ(sve2_uqadd_zpzz_h, uint16_t, H1_2, DO_UQADD_H)
DO_ZPZZ(sve2_uqadd_zpzz_s, uint32_t, H1_4, DO_UQADD_S)
DO_ZPZZ_D(sve2_uqadd_zpzz_d, uint64_t, do_uqadd_d)

#define DO_SQSUB_B(n, m) do_sat_bhs((int64_t)n - m, INT8_MIN, INT8_MAX)
#define DO_SQSUB_H(n, m) do_sat_bhs((int64_t)n - m, INT16_MIN, INT16_MAX)
#define DO_SQSUB_S(n, m) do_sat_bhs((int64_t)n - m, INT32_MIN, INT32_MAX)

static inline int64_t do_sqsub_d(int64_t n, int64_t m)
{
    int64_t r = n - m;
    if (((r ^ n) & (n ^ m)) < 0) {
        /* Signed overflow.  */
        return r < 0 ? INT64_MAX : INT64_MIN;
    }
    return r;
}

DO_ZPZZ(sve2_sqsub_zpzz_b, int8_t, H1, DO_SQSUB_B)
DO_ZPZZ(sve2_sqsub_zpzz_h, int16_t, H1_2, DO_SQSUB_H)
DO_ZPZZ(sve2_sqsub_zpzz_s, int32_t, H1_4, DO_SQSUB_S)
DO_ZPZZ_D(sve2_sqsub_zpzz_d, int64_t, do_sqsub_d)

#define DO_UQSUB_B(n, m) do_sat_bhs((int64_t)n - m, 0, UINT8_MAX)
#define DO_UQSUB_H(n, m) do_sat_bhs((int64_t)n - m, 0, UINT16_MAX)
#define DO_UQSUB_S(n, m) do_sat_bhs((int64_t)n - m, 0, UINT32_MAX)

static inline uint64_t do_uqsub_d(uint64_t n, uint64_t m)
{
    return n > m ? n - m : 0;
}

DO_ZPZZ(sve2_uqsub_zpzz_b, uint8_t, H1, DO_UQSUB_B)
DO_ZPZZ(sve2_uqsub_zpzz_h, uint16_t, H1_2, DO_UQSUB_H)
DO_ZPZZ(sve2_uqsub_zpzz_s, uint32_t, H1_4, DO_UQSUB_S)
DO_ZPZZ_D(sve2_uqsub_zpzz_d, uint64_t, do_uqsub_d)

#define DO_SUQADD_B(n, m) \
    do_sat_bhs((int64_t)(int8_t)n + m, INT8_MIN, INT8_MAX)
#define DO_SUQADD_H(n, m) \
    do_sat_bhs((int64_t)(int16_t)n + m, INT16_MIN, INT16_MAX)
#define DO_SUQADD_S(n, m) \
    do_sat_bhs((int64_t)(int32_t)n + m, INT32_MIN, INT32_MAX)

static inline int64_t do_suqadd_d(int64_t n, uint64_t m)
{
    uint64_t r = n + m;

    if (n < 0) {
        /* Note that m - abs(n) cannot underflow. */
        if (r > INT64_MAX) {
            /* Result is either very large positive or negative. */
            if (m > -n) {
                /* m > abs(n), so r is a very large positive. */
                return INT64_MAX;
            }
            /* Result is negative. */
        }
    } else {
        /* Both inputs are positive: check for overflow.  */
        if (r < m || r > INT64_MAX) {
            return INT64_MAX;
        }
    }
    return r;
}

DO_ZPZZ(sve2_suqadd_zpzz_b, uint8_t, H1, DO_SUQADD_B)
DO_ZPZZ(sve2_suqadd_zpzz_h, uint16_t, H1_2, DO_SUQADD_H)
DO_ZPZZ(sve2_suqadd_zpzz_s, uint32_t, H1_4, DO_SUQADD_S)
DO_ZPZZ_D(sve2_suqadd_zpzz_d, uint64_t, do_suqadd_d)

#define DO_USQADD_B(n, m) \
    do_sat_bhs((int64_t)n + (int8_t)m, 0, UINT8_MAX)
#define DO_USQADD_H(n, m) \
    do_sat_bhs((int64_t)n + (int16_t)m, 0, UINT16_MAX)
#define DO_USQADD_S(n, m) \
    do_sat_bhs((int64_t)n + (int32_t)m, 0, UINT32_MAX)

static inline uint64_t do_usqadd_d(uint64_t n, int64_t m)
{
    uint64_t r = n + m;

    if (m < 0) {
        return n < -m ? 0 : r;
    }
    return r < n ? UINT64_MAX : r;
}

DO_ZPZZ(sve2_usqadd_zpzz_b, uint8_t, H1, DO_USQADD_B)
DO_ZPZZ(sve2_usqadd_zpzz_h, uint16_t, H1_2, DO_USQADD_H)
DO_ZPZZ(sve2_usqadd_zpzz_s, uint32_t, H1_4, DO_USQADD_S)
DO_ZPZZ_D(sve2_usqadd_zpzz_d, uint64_t, do_usqadd_d)

#undef DO_ZPZZ
#undef DO_ZPZZ_D

/*
 * Three operand expander, operating on element pairs.
 * If the slot I is even, the elements from from VN {I, I+1}.
 * If the slot I is odd, the elements from from VM {I-1, I}.
 * Load all of the input elements in each pair before overwriting output.
 */
#define DO_ZPZZ_PAIR(NAME, TYPE, H, OP) \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *vg, uint32_t desc) \
{                                                               \
    intptr_t i, opr_sz = simd_oprsz(desc);                      \
    for (i = 0; i < opr_sz; ) {                                 \
        uint16_t pg = *(uint16_t *)(vg + H1_2(i >> 3));         \
        do {                                                    \
            TYPE n0 = *(TYPE *)(vn + H(i));                     \
            TYPE m0 = *(TYPE *)(vm + H(i));                     \
            TYPE n1 = *(TYPE *)(vn + H(i + sizeof(TYPE)));      \
            TYPE m1 = *(TYPE *)(vm + H(i + sizeof(TYPE)));      \
            if (pg & 1) {                                       \
                *(TYPE *)(vd + H(i)) = OP(n0, n1);              \
            }                                                   \
            i += sizeof(TYPE), pg >>= sizeof(TYPE);             \
            if (pg & 1) {                                       \
                *(TYPE *)(vd + H(i)) = OP(m0, m1);              \
            }                                                   \
            i += sizeof(TYPE), pg >>= sizeof(TYPE);             \
        } while (i & 15);                                       \
    }                                                           \
}

/* Similarly, specialized for 64-bit operands.  */
#define DO_ZPZZ_PAIR_D(NAME, TYPE, OP) \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *vg, uint32_t desc) \
{                                                               \
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;                  \
    TYPE *d = vd, *n = vn, *m = vm;                             \
    uint8_t *pg = vg;                                           \
    for (i = 0; i < opr_sz; i += 2) {                           \
        TYPE n0 = n[i], n1 = n[i + 1];                          \
        TYPE m0 = m[i], m1 = m[i + 1];                          \
        if (pg[H1(i)] & 1) {                                    \
            d[i] = OP(n0, n1);                                  \
        }                                                       \
        if (pg[H1(i + 1)] & 1) {                                \
            d[i + 1] = OP(m0, m1);                              \
        }                                                       \
    }                                                           \
}

DO_ZPZZ_PAIR(sve2_addp_zpzz_b, uint8_t, H1, DO_ADD)
DO_ZPZZ_PAIR(sve2_addp_zpzz_h, uint16_t, H1_2, DO_ADD)
DO_ZPZZ_PAIR(sve2_addp_zpzz_s, uint32_t, H1_4, DO_ADD)
DO_ZPZZ_PAIR_D(sve2_addp_zpzz_d, uint64_t, DO_ADD)

DO_ZPZZ_PAIR(sve2_umaxp_zpzz_b, uint8_t, H1, DO_MAX)
DO_ZPZZ_PAIR(sve2_umaxp_zpzz_h, uint16_t, H1_2, DO_MAX)
DO_ZPZZ_PAIR(sve2_umaxp_zpzz_s, uint32_t, H1_4, DO_MAX)
DO_ZPZZ_PAIR_D(sve2_umaxp_zpzz_d, uint64_t, DO_MAX)

DO_ZPZZ_PAIR(sve2_uminp_zpzz_b, uint8_t, H1, DO_MIN)
DO_ZPZZ_PAIR(sve2_uminp_zpzz_h, uint16_t, H1_2, DO_MIN)
DO_ZPZZ_PAIR(sve2_uminp_zpzz_s, uint32_t, H1_4, DO_MIN)
DO_ZPZZ_PAIR_D(sve2_uminp_zpzz_d, uint64_t, DO_MIN)

DO_ZPZZ_PAIR(sve2_smaxp_zpzz_b, int8_t, H1, DO_MAX)
DO_ZPZZ_PAIR(sve2_smaxp_zpzz_h, int16_t, H1_2, DO_MAX)
DO_ZPZZ_PAIR(sve2_smaxp_zpzz_s, int32_t, H1_4, DO_MAX)
DO_ZPZZ_PAIR_D(sve2_smaxp_zpzz_d, int64_t, DO_MAX)

DO_ZPZZ_PAIR(sve2_sminp_zpzz_b, int8_t, H1, DO_MIN)
DO_ZPZZ_PAIR(sve2_sminp_zpzz_h, int16_t, H1_2, DO_MIN)
DO_ZPZZ_PAIR(sve2_sminp_zpzz_s, int32_t, H1_4, DO_MIN)
DO_ZPZZ_PAIR_D(sve2_sminp_zpzz_d, int64_t, DO_MIN)

#undef DO_ZPZZ_PAIR
#undef DO_ZPZZ_PAIR_D

#define DO_ZPZZ_PAIR_FP(NAME, TYPE, H, OP)                              \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *vg,               \
                  float_status *status, uint32_t desc)                  \
{                                                                       \
    intptr_t i, opr_sz = simd_oprsz(desc);                              \
    for (i = 0; i < opr_sz; ) {                                         \
        uint16_t pg = *(uint16_t *)(vg + H1_2(i >> 3));                 \
        do {                                                            \
            TYPE n0 = *(TYPE *)(vn + H(i));                             \
            TYPE m0 = *(TYPE *)(vm + H(i));                             \
            TYPE n1 = *(TYPE *)(vn + H(i + sizeof(TYPE)));              \
            TYPE m1 = *(TYPE *)(vm + H(i + sizeof(TYPE)));              \
            if (pg & 1) {                                               \
                *(TYPE *)(vd + H(i)) = OP(n0, n1, status);              \
            }                                                           \
            i += sizeof(TYPE), pg >>= sizeof(TYPE);                     \
            if (pg & 1) {                                               \
                *(TYPE *)(vd + H(i)) = OP(m0, m1, status);              \
            }                                                           \
            i += sizeof(TYPE), pg >>= sizeof(TYPE);                     \
        } while (i & 15);                                               \
    }                                                                   \
}

DO_ZPZZ_PAIR_FP(sve2_faddp_zpzz_h, float16, H1_2, float16_add)
DO_ZPZZ_PAIR_FP(sve2_faddp_zpzz_s, float32, H1_4, float32_add)
DO_ZPZZ_PAIR_FP(sve2_faddp_zpzz_d, float64, H1_8, float64_add)

DO_ZPZZ_PAIR_FP(sve2_fmaxnmp_zpzz_h, float16, H1_2, float16_maxnum)
DO_ZPZZ_PAIR_FP(sve2_fmaxnmp_zpzz_s, float32, H1_4, float32_maxnum)
DO_ZPZZ_PAIR_FP(sve2_fmaxnmp_zpzz_d, float64, H1_8, float64_maxnum)

DO_ZPZZ_PAIR_FP(sve2_fminnmp_zpzz_h, float16, H1_2, float16_minnum)
DO_ZPZZ_PAIR_FP(sve2_fminnmp_zpzz_s, float32, H1_4, float32_minnum)
DO_ZPZZ_PAIR_FP(sve2_fminnmp_zpzz_d, float64, H1_8, float64_minnum)

DO_ZPZZ_PAIR_FP(sve2_fmaxp_zpzz_h, float16, H1_2, float16_max)
DO_ZPZZ_PAIR_FP(sve2_fmaxp_zpzz_s, float32, H1_4, float32_max)
DO_ZPZZ_PAIR_FP(sve2_fmaxp_zpzz_d, float64, H1_8, float64_max)

DO_ZPZZ_PAIR_FP(sve2_fminp_zpzz_h, float16, H1_2, float16_min)
DO_ZPZZ_PAIR_FP(sve2_fminp_zpzz_s, float32, H1_4, float32_min)
DO_ZPZZ_PAIR_FP(sve2_fminp_zpzz_d, float64, H1_8, float64_min)

#undef DO_ZPZZ_PAIR_FP

/* Three-operand expander, controlled by a predicate, in which the
 * third operand is "wide".  That is, for D = N op M, the same 64-bit
 * value of M is used with all of the narrower values of N.
 */
#define DO_ZPZW(NAME, TYPE, TYPEW, H, OP)                               \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *vg, uint32_t desc) \
{                                                                       \
    intptr_t i, opr_sz = simd_oprsz(desc);                              \
    for (i = 0; i < opr_sz; ) {                                         \
        uint8_t pg = *(uint8_t *)(vg + H1(i >> 3));                     \
        TYPEW mm = *(TYPEW *)(vm + i);                                  \
        do {                                                            \
            if (pg & 1) {                                               \
                TYPE nn = *(TYPE *)(vn + H(i));                         \
                *(TYPE *)(vd + H(i)) = OP(nn, mm);                      \
            }                                                           \
            i += sizeof(TYPE), pg >>= sizeof(TYPE);                     \
        } while (i & 7);                                                \
    }                                                                   \
}

DO_ZPZW(sve_asr_zpzw_b, int8_t, uint64_t, H1, DO_ASR)
DO_ZPZW(sve_lsr_zpzw_b, uint8_t, uint64_t, H1, DO_LSR)
DO_ZPZW(sve_lsl_zpzw_b, uint8_t, uint64_t, H1, DO_LSL)

DO_ZPZW(sve_asr_zpzw_h, int16_t, uint64_t, H1_2, DO_ASR)
DO_ZPZW(sve_lsr_zpzw_h, uint16_t, uint64_t, H1_2, DO_LSR)
DO_ZPZW(sve_lsl_zpzw_h, uint16_t, uint64_t, H1_2, DO_LSL)

DO_ZPZW(sve_asr_zpzw_s, int32_t, uint64_t, H1_4, DO_ASR)
DO_ZPZW(sve_lsr_zpzw_s, uint32_t, uint64_t, H1_4, DO_LSR)
DO_ZPZW(sve_lsl_zpzw_s, uint32_t, uint64_t, H1_4, DO_LSL)

#undef DO_ZPZW

/* Fully general two-operand expander, controlled by a predicate.
 */
#define DO_ZPZ(NAME, TYPE, H, OP)                               \
void HELPER(NAME)(void *vd, void *vn, void *vg, uint32_t desc)  \
{                                                               \
    intptr_t i, opr_sz = simd_oprsz(desc);                      \
    for (i = 0; i < opr_sz; ) {                                 \
        uint16_t pg = *(uint16_t *)(vg + H1_2(i >> 3));         \
        do {                                                    \
            if (pg & 1) {                                       \
                TYPE nn = *(TYPE *)(vn + H(i));                 \
                *(TYPE *)(vd + H(i)) = OP(nn);                  \
            }                                                   \
            i += sizeof(TYPE), pg >>= sizeof(TYPE);             \
        } while (i & 15);                                       \
    }                                                           \
}

/* Similarly, specialized for 64-bit operands.  */
#define DO_ZPZ_D(NAME, TYPE, OP)                                \
void HELPER(NAME)(void *vd, void *vn, void *vg, uint32_t desc)  \
{                                                               \
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;                  \
    TYPE *d = vd, *n = vn;                                      \
    uint8_t *pg = vg;                                           \
    for (i = 0; i < opr_sz; i += 1) {                           \
        if (pg[H1(i)] & 1) {                                    \
            TYPE nn = n[i];                                     \
            d[i] = OP(nn);                                      \
        }                                                       \
    }                                                           \
}

#define DO_CLS_B(N)   (clrsb32(N) - 24)
#define DO_CLS_H(N)   (clrsb32(N) - 16)

DO_ZPZ(sve_cls_b, int8_t, H1, DO_CLS_B)
DO_ZPZ(sve_cls_h, int16_t, H1_2, DO_CLS_H)
DO_ZPZ(sve_cls_s, int32_t, H1_4, clrsb32)
DO_ZPZ_D(sve_cls_d, int64_t, clrsb64)

#define DO_CLZ_B(N)   (clz32(N) - 24)
#define DO_CLZ_H(N)   (clz32(N) - 16)

DO_ZPZ(sve_clz_b, uint8_t, H1, DO_CLZ_B)
DO_ZPZ(sve_clz_h, uint16_t, H1_2, DO_CLZ_H)
DO_ZPZ(sve_clz_s, uint32_t, H1_4, clz32)
DO_ZPZ_D(sve_clz_d, uint64_t, clz64)

DO_ZPZ(sve_cnt_zpz_b, uint8_t, H1, ctpop8)
DO_ZPZ(sve_cnt_zpz_h, uint16_t, H1_2, ctpop16)
DO_ZPZ(sve_cnt_zpz_s, uint32_t, H1_4, ctpop32)
DO_ZPZ_D(sve_cnt_zpz_d, uint64_t, ctpop64)

#define DO_CNOT(N)    (N == 0)

DO_ZPZ(sve_cnot_b, uint8_t, H1, DO_CNOT)
DO_ZPZ(sve_cnot_h, uint16_t, H1_2, DO_CNOT)
DO_ZPZ(sve_cnot_s, uint32_t, H1_4, DO_CNOT)
DO_ZPZ_D(sve_cnot_d, uint64_t, DO_CNOT)

#define DO_FABS(N)    (N & ((__typeof(N))-1 >> 1))

DO_ZPZ(sve_fabs_h, uint16_t, H1_2, DO_FABS)
DO_ZPZ(sve_fabs_s, uint32_t, H1_4, DO_FABS)
DO_ZPZ_D(sve_fabs_d, uint64_t, DO_FABS)

#define DO_AH_FABS_H(N) (float16_is_any_nan(N) ? (N) : DO_FABS(N))
#define DO_AH_FABS_S(N) (float32_is_any_nan(N) ? (N) : DO_FABS(N))
#define DO_AH_FABS_D(N) (float64_is_any_nan(N) ? (N) : DO_FABS(N))

DO_ZPZ(sve_ah_fabs_h, uint16_t, H1_2, DO_AH_FABS_H)
DO_ZPZ(sve_ah_fabs_s, uint32_t, H1_4, DO_AH_FABS_S)
DO_ZPZ_D(sve_ah_fabs_d, uint64_t, DO_AH_FABS_D)

#define DO_FNEG(N)    (N ^ ~((__typeof(N))-1 >> 1))

DO_ZPZ(sve_fneg_h, uint16_t, H1_2, DO_FNEG)
DO_ZPZ(sve_fneg_s, uint32_t, H1_4, DO_FNEG)
DO_ZPZ_D(sve_fneg_d, uint64_t, DO_FNEG)

#define DO_AH_FNEG_H(N) (float16_is_any_nan(N) ? (N) : DO_FNEG(N))
#define DO_AH_FNEG_S(N) (float32_is_any_nan(N) ? (N) : DO_FNEG(N))
#define DO_AH_FNEG_D(N) (float64_is_any_nan(N) ? (N) : DO_FNEG(N))

DO_ZPZ(sve_ah_fneg_h, uint16_t, H1_2, DO_AH_FNEG_H)
DO_ZPZ(sve_ah_fneg_s, uint32_t, H1_4, DO_AH_FNEG_S)
DO_ZPZ_D(sve_ah_fneg_d, uint64_t, DO_AH_FNEG_D)

#define DO_NOT(N)    (~N)

DO_ZPZ(sve_not_zpz_b, uint8_t, H1, DO_NOT)
DO_ZPZ(sve_not_zpz_h, uint16_t, H1_2, DO_NOT)
DO_ZPZ(sve_not_zpz_s, uint32_t, H1_4, DO_NOT)
DO_ZPZ_D(sve_not_zpz_d, uint64_t, DO_NOT)

#define DO_SXTB(N)    ((int8_t)N)
#define DO_SXTH(N)    ((int16_t)N)
#define DO_SXTS(N)    ((int32_t)N)
#define DO_UXTB(N)    ((uint8_t)N)
#define DO_UXTH(N)    ((uint16_t)N)
#define DO_UXTS(N)    ((uint32_t)N)

DO_ZPZ(sve_sxtb_h, uint16_t, H1_2, DO_SXTB)
DO_ZPZ(sve_sxtb_s, uint32_t, H1_4, DO_SXTB)
DO_ZPZ(sve_sxth_s, uint32_t, H1_4, DO_SXTH)
DO_ZPZ_D(sve_sxtb_d, uint64_t, DO_SXTB)
DO_ZPZ_D(sve_sxth_d, uint64_t, DO_SXTH)
DO_ZPZ_D(sve_sxtw_d, uint64_t, DO_SXTS)

DO_ZPZ(sve_uxtb_h, uint16_t, H1_2, DO_UXTB)
DO_ZPZ(sve_uxtb_s, uint32_t, H1_4, DO_UXTB)
DO_ZPZ(sve_uxth_s, uint32_t, H1_4, DO_UXTH)
DO_ZPZ_D(sve_uxtb_d, uint64_t, DO_UXTB)
DO_ZPZ_D(sve_uxth_d, uint64_t, DO_UXTH)
DO_ZPZ_D(sve_uxtw_d, uint64_t, DO_UXTS)

#define DO_ABS(N)    (N < 0 ? -N : N)

DO_ZPZ(sve_abs_b, int8_t, H1, DO_ABS)
DO_ZPZ(sve_abs_h, int16_t, H1_2, DO_ABS)
DO_ZPZ(sve_abs_s, int32_t, H1_4, DO_ABS)
DO_ZPZ_D(sve_abs_d, int64_t, DO_ABS)

#define DO_NEG(N)    (-N)

DO_ZPZ(sve_neg_b, uint8_t, H1, DO_NEG)
DO_ZPZ(sve_neg_h, uint16_t, H1_2, DO_NEG)
DO_ZPZ(sve_neg_s, uint32_t, H1_4, DO_NEG)
DO_ZPZ_D(sve_neg_d, uint64_t, DO_NEG)

DO_ZPZ(sve_revb_h, uint16_t, H1_2, bswap16)
DO_ZPZ(sve_revb_s, uint32_t, H1_4, bswap32)
DO_ZPZ_D(sve_revb_d, uint64_t, bswap64)

DO_ZPZ(sve_revh_s, uint32_t, H1_4, hswap32)
DO_ZPZ_D(sve_revh_d, uint64_t, hswap64)

DO_ZPZ_D(sve_revw_d, uint64_t, wswap64)

void HELPER(sme_revd_q)(void *vd, void *vn, void *vg, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd, *n = vn;
    uint8_t *pg = vg;

    for (i = 0; i < opr_sz; i += 2) {
        if (pg[H1(i)] & 1) {
            uint64_t n0 = n[i + 0];
            uint64_t n1 = n[i + 1];
            d[i + 0] = n1;
            d[i + 1] = n0;
        }
    }
}

DO_ZPZ(sve_rbit_b, uint8_t, H1, revbit8)
DO_ZPZ(sve_rbit_h, uint16_t, H1_2, revbit16)
DO_ZPZ(sve_rbit_s, uint32_t, H1_4, revbit32)
DO_ZPZ_D(sve_rbit_d, uint64_t, revbit64)

#define DO_SQABS(X) \
    ({ __typeof(X) x_ = (X), min_ = 1ull << (sizeof(X) * 8 - 1); \
       x_ >= 0 ? x_ : x_ == min_ ? -min_ - 1 : -x_; })

DO_ZPZ(sve2_sqabs_b, int8_t, H1, DO_SQABS)
DO_ZPZ(sve2_sqabs_h, int16_t, H1_2, DO_SQABS)
DO_ZPZ(sve2_sqabs_s, int32_t, H1_4, DO_SQABS)
DO_ZPZ_D(sve2_sqabs_d, int64_t, DO_SQABS)

#define DO_SQNEG(X) \
    ({ __typeof(X) x_ = (X), min_ = 1ull << (sizeof(X) * 8 - 1); \
       x_ == min_ ? -min_ - 1 : -x_; })

DO_ZPZ(sve2_sqneg_b, uint8_t, H1, DO_SQNEG)
DO_ZPZ(sve2_sqneg_h, uint16_t, H1_2, DO_SQNEG)
DO_ZPZ(sve2_sqneg_s, uint32_t, H1_4, DO_SQNEG)
DO_ZPZ_D(sve2_sqneg_d, uint64_t, DO_SQNEG)

DO_ZPZ(sve2_urecpe_s, uint32_t, H1_4, helper_recpe_u32)
DO_ZPZ(sve2_ursqrte_s, uint32_t, H1_4, helper_rsqrte_u32)

/* Three-operand expander, unpredicated, in which the third operand is "wide".
 */
#define DO_ZZW(NAME, TYPE, TYPEW, H, OP)                       \
void HELPER(NAME)(void *vd, void *vn, void *vm, uint32_t desc) \
{                                                              \
    intptr_t i, opr_sz = simd_oprsz(desc);                     \
    for (i = 0; i < opr_sz; ) {                                \
        TYPEW mm = *(TYPEW *)(vm + i);                         \
        do {                                                   \
            TYPE nn = *(TYPE *)(vn + H(i));                    \
            *(TYPE *)(vd + H(i)) = OP(nn, mm);                 \
            i += sizeof(TYPE);                                 \
        } while (i & 7);                                       \
    }                                                          \
}

DO_ZZW(sve_asr_zzw_b, int8_t, uint64_t, H1, DO_ASR)
DO_ZZW(sve_lsr_zzw_b, uint8_t, uint64_t, H1, DO_LSR)
DO_ZZW(sve_lsl_zzw_b, uint8_t, uint64_t, H1, DO_LSL)

DO_ZZW(sve_asr_zzw_h, int16_t, uint64_t, H1_2, DO_ASR)
DO_ZZW(sve_lsr_zzw_h, uint16_t, uint64_t, H1_2, DO_LSR)
DO_ZZW(sve_lsl_zzw_h, uint16_t, uint64_t, H1_2, DO_LSL)

DO_ZZW(sve_asr_zzw_s, int32_t, uint64_t, H1_4, DO_ASR)
DO_ZZW(sve_lsr_zzw_s, uint32_t, uint64_t, H1_4, DO_LSR)
DO_ZZW(sve_lsl_zzw_s, uint32_t, uint64_t, H1_4, DO_LSL)

#undef DO_ZZW

#undef DO_CLS_B
#undef DO_CLS_H
#undef DO_CLZ_B
#undef DO_CLZ_H
#undef DO_CNOT
#undef DO_FABS
#undef DO_FNEG
#undef DO_ABS
#undef DO_NEG
#undef DO_ZPZ
#undef DO_ZPZ_D

/*
 * Three-operand expander, unpredicated, in which the two inputs are
 * selected from the top or bottom half of the wide column.
 */
#define DO_ZZZ_TB(NAME, TYPEW, TYPEN, HW, HN, OP) \
void HELPER(NAME)(void *vd, void *vn, void *vm, uint32_t desc)          \
{                                                                       \
    intptr_t i, opr_sz = simd_oprsz(desc);                              \
    int sel1 = extract32(desc, SIMD_DATA_SHIFT, 1) * sizeof(TYPEN);     \
    int sel2 = extract32(desc, SIMD_DATA_SHIFT + 1, 1) * sizeof(TYPEN); \
    for (i = 0; i < opr_sz; i += sizeof(TYPEW)) {                       \
        TYPEW nn = *(TYPEN *)(vn + HN(i + sel1));                       \
        TYPEW mm = *(TYPEN *)(vm + HN(i + sel2));                       \
        *(TYPEW *)(vd + HW(i)) = OP(nn, mm);                            \
    }                                                                   \
}

DO_ZZZ_TB(sve2_saddl_h, int16_t, int8_t, H1_2, H1, DO_ADD)
DO_ZZZ_TB(sve2_saddl_s, int32_t, int16_t, H1_4, H1_2, DO_ADD)
DO_ZZZ_TB(sve2_saddl_d, int64_t, int32_t, H1_8, H1_4, DO_ADD)

DO_ZZZ_TB(sve2_ssubl_h, int16_t, int8_t, H1_2, H1, DO_SUB)
DO_ZZZ_TB(sve2_ssubl_s, int32_t, int16_t, H1_4, H1_2, DO_SUB)
DO_ZZZ_TB(sve2_ssubl_d, int64_t, int32_t, H1_8, H1_4, DO_SUB)

DO_ZZZ_TB(sve2_sabdl_h, int16_t, int8_t, H1_2, H1, DO_ABD)
DO_ZZZ_TB(sve2_sabdl_s, int32_t, int16_t, H1_4, H1_2, DO_ABD)
DO_ZZZ_TB(sve2_sabdl_d, int64_t, int32_t, H1_8, H1_4, DO_ABD)

DO_ZZZ_TB(sve2_uaddl_h, uint16_t, uint8_t, H1_2, H1, DO_ADD)
DO_ZZZ_TB(sve2_uaddl_s, uint32_t, uint16_t, H1_4, H1_2, DO_ADD)
DO_ZZZ_TB(sve2_uaddl_d, uint64_t, uint32_t, H1_8, H1_4, DO_ADD)

DO_ZZZ_TB(sve2_usubl_h, uint16_t, uint8_t, H1_2, H1, DO_SUB)
DO_ZZZ_TB(sve2_usubl_s, uint32_t, uint16_t, H1_4, H1_2, DO_SUB)
DO_ZZZ_TB(sve2_usubl_d, uint64_t, uint32_t, H1_8, H1_4, DO_SUB)

DO_ZZZ_TB(sve2_uabdl_h, uint16_t, uint8_t, H1_2, H1, DO_ABD)
DO_ZZZ_TB(sve2_uabdl_s, uint32_t, uint16_t, H1_4, H1_2, DO_ABD)
DO_ZZZ_TB(sve2_uabdl_d, uint64_t, uint32_t, H1_8, H1_4, DO_ABD)

DO_ZZZ_TB(sve2_smull_zzz_h, int16_t, int8_t, H1_2, H1, DO_MUL)
DO_ZZZ_TB(sve2_smull_zzz_s, int32_t, int16_t, H1_4, H1_2, DO_MUL)
DO_ZZZ_TB(sve2_smull_zzz_d, int64_t, int32_t, H1_8, H1_4, DO_MUL)

DO_ZZZ_TB(sve2_umull_zzz_h, uint16_t, uint8_t, H1_2, H1, DO_MUL)
DO_ZZZ_TB(sve2_umull_zzz_s, uint32_t, uint16_t, H1_4, H1_2, DO_MUL)
DO_ZZZ_TB(sve2_umull_zzz_d, uint64_t, uint32_t, H1_8, H1_4, DO_MUL)

/* Note that the multiply cannot overflow, but the doubling can. */
static inline int16_t do_sqdmull_h(int16_t n, int16_t m)
{
    int16_t val = n * m;
    return DO_SQADD_H(val, val);
}

static inline int32_t do_sqdmull_s(int32_t n, int32_t m)
{
    int32_t val = n * m;
    return DO_SQADD_S(val, val);
}

static inline int64_t do_sqdmull_d(int64_t n, int64_t m)
{
    int64_t val = n * m;
    return do_sqadd_d(val, val);
}

DO_ZZZ_TB(sve2_sqdmull_zzz_h, int16_t, int8_t, H1_2, H1, do_sqdmull_h)
DO_ZZZ_TB(sve2_sqdmull_zzz_s, int32_t, int16_t, H1_4, H1_2, do_sqdmull_s)
DO_ZZZ_TB(sve2_sqdmull_zzz_d, int64_t, int32_t, H1_8, H1_4, do_sqdmull_d)

#undef DO_ZZZ_TB

#define DO_ZZZ_WTB(NAME, TYPEW, TYPEN, HW, HN, OP) \
void HELPER(NAME)(void *vd, void *vn, void *vm, uint32_t desc) \
{                                                              \
    intptr_t i, opr_sz = simd_oprsz(desc);                     \
    int sel2 = extract32(desc, SIMD_DATA_SHIFT, 1) * sizeof(TYPEN); \
    for (i = 0; i < opr_sz; i += sizeof(TYPEW)) {              \
        TYPEW nn = *(TYPEW *)(vn + HW(i));                     \
        TYPEW mm = *(TYPEN *)(vm + HN(i + sel2));              \
        *(TYPEW *)(vd + HW(i)) = OP(nn, mm);                   \
    }                                                          \
}

DO_ZZZ_WTB(sve2_saddw_h, int16_t, int8_t, H1_2, H1, DO_ADD)
DO_ZZZ_WTB(sve2_saddw_s, int32_t, int16_t, H1_4, H1_2, DO_ADD)
DO_ZZZ_WTB(sve2_saddw_d, int64_t, int32_t, H1_8, H1_4, DO_ADD)

DO_ZZZ_WTB(sve2_ssubw_h, int16_t, int8_t, H1_2, H1, DO_SUB)
DO_ZZZ_WTB(sve2_ssubw_s, int32_t, int16_t, H1_4, H1_2, DO_SUB)
DO_ZZZ_WTB(sve2_ssubw_d, int64_t, int32_t, H1_8, H1_4, DO_SUB)

DO_ZZZ_WTB(sve2_uaddw_h, uint16_t, uint8_t, H1_2, H1, DO_ADD)
DO_ZZZ_WTB(sve2_uaddw_s, uint32_t, uint16_t, H1_4, H1_2, DO_ADD)
DO_ZZZ_WTB(sve2_uaddw_d, uint64_t, uint32_t, H1_8, H1_4, DO_ADD)

DO_ZZZ_WTB(sve2_usubw_h, uint16_t, uint8_t, H1_2, H1, DO_SUB)
DO_ZZZ_WTB(sve2_usubw_s, uint32_t, uint16_t, H1_4, H1_2, DO_SUB)
DO_ZZZ_WTB(sve2_usubw_d, uint64_t, uint32_t, H1_8, H1_4, DO_SUB)

#undef DO_ZZZ_WTB

#define DO_ZZZ_NTB(NAME, TYPE, H, OP)                                   \
void HELPER(NAME)(void *vd, void *vn, void *vm, uint32_t desc)          \
{                                                                       \
    intptr_t i, opr_sz = simd_oprsz(desc);                              \
    intptr_t sel1 = extract32(desc, SIMD_DATA_SHIFT, 1) * sizeof(TYPE); \
    intptr_t sel2 = extract32(desc, SIMD_DATA_SHIFT + 1, 1) * sizeof(TYPE); \
    for (i = 0; i < opr_sz; i += 2 * sizeof(TYPE)) {                    \
        TYPE nn = *(TYPE *)(vn + H(i + sel1));                          \
        TYPE mm = *(TYPE *)(vm + H(i + sel2));                          \
        *(TYPE *)(vd + H(i + sel1)) = OP(nn, mm);                       \
    }                                                                   \
}

DO_ZZZ_NTB(sve2_eoril_b, uint8_t, H1, DO_EOR)
DO_ZZZ_NTB(sve2_eoril_h, uint16_t, H1_2, DO_EOR)
DO_ZZZ_NTB(sve2_eoril_s, uint32_t, H1_4, DO_EOR)
DO_ZZZ_NTB(sve2_eoril_d, uint64_t, H1_8, DO_EOR)

#undef DO_ZZZ_NTB

#define DO_ZZZW_ACC(NAME, TYPEW, TYPEN, HW, HN, OP) \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *va, uint32_t desc) \
{                                                               \
    intptr_t i, opr_sz = simd_oprsz(desc);                      \
    intptr_t sel1 = simd_data(desc) * sizeof(TYPEN);            \
    for (i = 0; i < opr_sz; i += sizeof(TYPEW)) {               \
        TYPEW nn = *(TYPEN *)(vn + HN(i + sel1));               \
        TYPEW mm = *(TYPEN *)(vm + HN(i + sel1));               \
        TYPEW aa = *(TYPEW *)(va + HW(i));                      \
        *(TYPEW *)(vd + HW(i)) = OP(nn, mm) + aa;               \
    }                                                           \
}

DO_ZZZW_ACC(sve2_sabal_h, int16_t, int8_t, H1_2, H1, DO_ABD)
DO_ZZZW_ACC(sve2_sabal_s, int32_t, int16_t, H1_4, H1_2, DO_ABD)
DO_ZZZW_ACC(sve2_sabal_d, int64_t, int32_t, H1_8, H1_4, DO_ABD)

DO_ZZZW_ACC(sve2_uabal_h, uint16_t, uint8_t, H1_2, H1, DO_ABD)
DO_ZZZW_ACC(sve2_uabal_s, uint32_t, uint16_t, H1_4, H1_2, DO_ABD)
DO_ZZZW_ACC(sve2_uabal_d, uint64_t, uint32_t, H1_8, H1_4, DO_ABD)

DO_ZZZW_ACC(sve2_smlal_zzzw_h, int16_t, int8_t, H1_2, H1, DO_MUL)
DO_ZZZW_ACC(sve2_smlal_zzzw_s, int32_t, int16_t, H1_4, H1_2, DO_MUL)
DO_ZZZW_ACC(sve2_smlal_zzzw_d, int64_t, int32_t, H1_8, H1_4, DO_MUL)

DO_ZZZW_ACC(sve2_umlal_zzzw_h, uint16_t, uint8_t, H1_2, H1, DO_MUL)
DO_ZZZW_ACC(sve2_umlal_zzzw_s, uint32_t, uint16_t, H1_4, H1_2, DO_MUL)
DO_ZZZW_ACC(sve2_umlal_zzzw_d, uint64_t, uint32_t, H1_8, H1_4, DO_MUL)

#define DO_NMUL(N, M)  -(N * M)

DO_ZZZW_ACC(sve2_smlsl_zzzw_h, int16_t, int8_t, H1_2, H1, DO_NMUL)
DO_ZZZW_ACC(sve2_smlsl_zzzw_s, int32_t, int16_t, H1_4, H1_2, DO_NMUL)
DO_ZZZW_ACC(sve2_smlsl_zzzw_d, int64_t, int32_t, H1_8, H1_4, DO_NMUL)

DO_ZZZW_ACC(sve2_umlsl_zzzw_h, uint16_t, uint8_t, H1_2, H1, DO_NMUL)
DO_ZZZW_ACC(sve2_umlsl_zzzw_s, uint32_t, uint16_t, H1_4, H1_2, DO_NMUL)
DO_ZZZW_ACC(sve2_umlsl_zzzw_d, uint64_t, uint32_t, H1_8, H1_4, DO_NMUL)

#undef DO_ZZZW_ACC

#define DO_XTNB(NAME, TYPE, OP) \
void HELPER(NAME)(void *vd, void *vn, uint32_t desc)         \
{                                                            \
    intptr_t i, opr_sz = simd_oprsz(desc);                   \
    for (i = 0; i < opr_sz; i += sizeof(TYPE)) {             \
        TYPE nn = *(TYPE *)(vn + i);                         \
        nn = OP(nn) & MAKE_64BIT_MASK(0, sizeof(TYPE) * 4);  \
        *(TYPE *)(vd + i) = nn;                              \
    }                                                        \
}

#define DO_XTNT(NAME, TYPE, TYPEN, H, OP)                               \
void HELPER(NAME)(void *vd, void *vn, uint32_t desc)                    \
{                                                                       \
    intptr_t i, opr_sz = simd_oprsz(desc), odd = H(sizeof(TYPEN));      \
    for (i = 0; i < opr_sz; i += sizeof(TYPE)) {                        \
        TYPE nn = *(TYPE *)(vn + i);                                    \
        *(TYPEN *)(vd + i + odd) = OP(nn);                              \
    }                                                                   \
}

#define DO_SQXTN_H(n)  do_sat_bhs(n, INT8_MIN, INT8_MAX)
#define DO_SQXTN_S(n)  do_sat_bhs(n, INT16_MIN, INT16_MAX)
#define DO_SQXTN_D(n)  do_sat_bhs(n, INT32_MIN, INT32_MAX)

DO_XTNB(sve2_sqxtnb_h, int16_t, DO_SQXTN_H)
DO_XTNB(sve2_sqxtnb_s, int32_t, DO_SQXTN_S)
DO_XTNB(sve2_sqxtnb_d, int64_t, DO_SQXTN_D)

DO_XTNT(sve2_sqxtnt_h, int16_t, int8_t, H1, DO_SQXTN_H)
DO_XTNT(sve2_sqxtnt_s, int32_t, int16_t, H1_2, DO_SQXTN_S)
DO_XTNT(sve2_sqxtnt_d, int64_t, int32_t, H1_4, DO_SQXTN_D)

#define DO_UQXTN_H(n)  do_sat_bhs(n, 0, UINT8_MAX)
#define DO_UQXTN_S(n)  do_sat_bhs(n, 0, UINT16_MAX)
#define DO_UQXTN_D(n)  do_sat_bhs(n, 0, UINT32_MAX)

DO_XTNB(sve2_uqxtnb_h, uint16_t, DO_UQXTN_H)
DO_XTNB(sve2_uqxtnb_s, uint32_t, DO_UQXTN_S)
DO_XTNB(sve2_uqxtnb_d, uint64_t, DO_UQXTN_D)

DO_XTNT(sve2_uqxtnt_h, uint16_t, uint8_t, H1, DO_UQXTN_H)
DO_XTNT(sve2_uqxtnt_s, uint32_t, uint16_t, H1_2, DO_UQXTN_S)
DO_XTNT(sve2_uqxtnt_d, uint64_t, uint32_t, H1_4, DO_UQXTN_D)

DO_XTNB(sve2_sqxtunb_h, int16_t, DO_UQXTN_H)
DO_XTNB(sve2_sqxtunb_s, int32_t, DO_UQXTN_S)
DO_XTNB(sve2_sqxtunb_d, int64_t, DO_UQXTN_D)

DO_XTNT(sve2_sqxtunt_h, int16_t, int8_t, H1, DO_UQXTN_H)
DO_XTNT(sve2_sqxtunt_s, int32_t, int16_t, H1_2, DO_UQXTN_S)
DO_XTNT(sve2_sqxtunt_d, int64_t, int32_t, H1_4, DO_UQXTN_D)

#undef DO_XTNB
#undef DO_XTNT

void HELPER(sve2_adcl_s)(void *vd, void *vn, void *vm, void *va, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int sel = H4(extract32(desc, SIMD_DATA_SHIFT, 1));
    uint32_t inv = -extract32(desc, SIMD_DATA_SHIFT + 1, 1);
    uint32_t *a = va, *n = vn;
    uint64_t *d = vd, *m = vm;

    for (i = 0; i < opr_sz / 8; ++i) {
        uint32_t e1 = a[2 * i + H4(0)];
        uint32_t e2 = n[2 * i + sel] ^ inv;
        uint64_t c = extract64(m[i], 32, 1);
        /* Compute and store the entire 33-bit result at once. */
        d[i] = c + e1 + e2;
    }
}

void HELPER(sve2_adcl_d)(void *vd, void *vn, void *vm, void *va, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int sel = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint64_t inv = -(uint64_t)extract32(desc, SIMD_DATA_SHIFT + 1, 1);
    uint64_t *d = vd, *a = va, *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 8; i += 2) {
        Int128 e1 = int128_make64(a[i]);
        Int128 e2 = int128_make64(n[i + sel] ^ inv);
        Int128 c = int128_make64(m[i + 1] & 1);
        Int128 r = int128_add(int128_add(e1, e2), c);
        d[i + 0] = int128_getlo(r);
        d[i + 1] = int128_gethi(r);
    }
}

#define DO_SQDMLAL(NAME, TYPEW, TYPEN, HW, HN, DMUL_OP, SUM_OP) \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *va, uint32_t desc) \
{                                                                       \
    intptr_t i, opr_sz = simd_oprsz(desc);                              \
    int sel1 = extract32(desc, SIMD_DATA_SHIFT, 1) * sizeof(TYPEN);     \
    int sel2 = extract32(desc, SIMD_DATA_SHIFT + 1, 1) * sizeof(TYPEN); \
    for (i = 0; i < opr_sz; i += sizeof(TYPEW)) {                       \
        TYPEW nn = *(TYPEN *)(vn + HN(i + sel1));                       \
        TYPEW mm = *(TYPEN *)(vm + HN(i + sel2));                       \
        TYPEW aa = *(TYPEW *)(va + HW(i));                              \
        *(TYPEW *)(vd + HW(i)) = SUM_OP(aa, DMUL_OP(nn, mm));           \
    }                                                                   \
}

DO_SQDMLAL(sve2_sqdmlal_zzzw_h, int16_t, int8_t, H1_2, H1,
           do_sqdmull_h, DO_SQADD_H)
DO_SQDMLAL(sve2_sqdmlal_zzzw_s, int32_t, int16_t, H1_4, H1_2,
           do_sqdmull_s, DO_SQADD_S)
DO_SQDMLAL(sve2_sqdmlal_zzzw_d, int64_t, int32_t, H1_8, H1_4,
           do_sqdmull_d, do_sqadd_d)

DO_SQDMLAL(sve2_sqdmlsl_zzzw_h, int16_t, int8_t, H1_2, H1,
           do_sqdmull_h, DO_SQSUB_H)
DO_SQDMLAL(sve2_sqdmlsl_zzzw_s, int32_t, int16_t, H1_4, H1_2,
           do_sqdmull_s, DO_SQSUB_S)
DO_SQDMLAL(sve2_sqdmlsl_zzzw_d, int64_t, int32_t, H1_8, H1_4,
           do_sqdmull_d, do_sqsub_d)

#undef DO_SQDMLAL

#define DO_CMLA_FUNC(NAME, TYPE, H, OP) \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *va, uint32_t desc) \
{                                                               \
    intptr_t i, opr_sz = simd_oprsz(desc) / sizeof(TYPE);       \
    int rot = simd_data(desc);                                  \
    int sel_a = rot & 1, sel_b = sel_a ^ 1;                     \
    bool sub_r = rot == 1 || rot == 2;                          \
    bool sub_i = rot >= 2;                                      \
    TYPE *d = vd, *n = vn, *m = vm, *a = va;                    \
    for (i = 0; i < opr_sz; i += 2) {                           \
        TYPE elt1_a = n[H(i + sel_a)];                          \
        TYPE elt2_a = m[H(i + sel_a)];                          \
        TYPE elt2_b = m[H(i + sel_b)];                          \
        d[H(i)] = OP(elt1_a, elt2_a, a[H(i)], sub_r);           \
        d[H(i + 1)] = OP(elt1_a, elt2_b, a[H(i + 1)], sub_i);   \
    }                                                           \
}

#define DO_CMLA(N, M, A, S) (A + (N * M) * (S ? -1 : 1))

DO_CMLA_FUNC(sve2_cmla_zzzz_b, uint8_t, H1, DO_CMLA)
DO_CMLA_FUNC(sve2_cmla_zzzz_h, uint16_t, H2, DO_CMLA)
DO_CMLA_FUNC(sve2_cmla_zzzz_s, uint32_t, H4, DO_CMLA)
DO_CMLA_FUNC(sve2_cmla_zzzz_d, uint64_t, H8, DO_CMLA)

#define DO_SQRDMLAH_B(N, M, A, S) \
    do_sqrdmlah_b(N, M, A, S, true)
#define DO_SQRDMLAH_H(N, M, A, S) \
    ({ uint32_t discard; do_sqrdmlah_h(N, M, A, S, true, &discard); })
#define DO_SQRDMLAH_S(N, M, A, S) \
    ({ uint32_t discard; do_sqrdmlah_s(N, M, A, S, true, &discard); })
#define DO_SQRDMLAH_D(N, M, A, S) \
    do_sqrdmlah_d(N, M, A, S, true)

DO_CMLA_FUNC(sve2_sqrdcmlah_zzzz_b, int8_t, H1, DO_SQRDMLAH_B)
DO_CMLA_FUNC(sve2_sqrdcmlah_zzzz_h, int16_t, H2, DO_SQRDMLAH_H)
DO_CMLA_FUNC(sve2_sqrdcmlah_zzzz_s, int32_t, H4, DO_SQRDMLAH_S)
DO_CMLA_FUNC(sve2_sqrdcmlah_zzzz_d, int64_t, H8, DO_SQRDMLAH_D)

#define DO_CMLA_IDX_FUNC(NAME, TYPE, H, OP) \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *va, uint32_t desc)    \
{                                                                           \
    intptr_t i, j, oprsz = simd_oprsz(desc);                                \
    int rot = extract32(desc, SIMD_DATA_SHIFT, 2);                          \
    int idx = extract32(desc, SIMD_DATA_SHIFT + 2, 2) * 2;                  \
    int sel_a = rot & 1, sel_b = sel_a ^ 1;                                 \
    bool sub_r = rot == 1 || rot == 2;                                      \
    bool sub_i = rot >= 2;                                                  \
    TYPE *d = vd, *n = vn, *m = vm, *a = va;                                \
    for (i = 0; i < oprsz / sizeof(TYPE); i += 16 / sizeof(TYPE)) {         \
        TYPE elt2_a = m[H(i + idx + sel_a)];                                \
        TYPE elt2_b = m[H(i + idx + sel_b)];                                \
        for (j = 0; j < 16 / sizeof(TYPE); j += 2) {                        \
            TYPE elt1_a = n[H(i + j + sel_a)];                              \
            d[H2(i + j)] = OP(elt1_a, elt2_a, a[H(i + j)], sub_r);          \
            d[H2(i + j + 1)] = OP(elt1_a, elt2_b, a[H(i + j + 1)], sub_i);  \
        }                                                                   \
    }                                                                       \
}

DO_CMLA_IDX_FUNC(sve2_cmla_idx_h, int16_t, H2, DO_CMLA)
DO_CMLA_IDX_FUNC(sve2_cmla_idx_s, int32_t, H4, DO_CMLA)

DO_CMLA_IDX_FUNC(sve2_sqrdcmlah_idx_h, int16_t, H2, DO_SQRDMLAH_H)
DO_CMLA_IDX_FUNC(sve2_sqrdcmlah_idx_s, int32_t, H4, DO_SQRDMLAH_S)

#undef DO_CMLA
#undef DO_CMLA_FUNC
#undef DO_CMLA_IDX_FUNC
#undef DO_SQRDMLAH_B
#undef DO_SQRDMLAH_H
#undef DO_SQRDMLAH_S
#undef DO_SQRDMLAH_D

/* Note N and M are 4 elements bundled into one unit. */
static int32_t do_cdot_s(uint32_t n, uint32_t m, int32_t a,
                         int sel_a, int sel_b, int sub_i)
{
    for (int i = 0; i <= 1; i++) {
        int32_t elt1_r = (int8_t)(n >> (16 * i));
        int32_t elt1_i = (int8_t)(n >> (16 * i + 8));
        int32_t elt2_a = (int8_t)(m >> (16 * i + 8 * sel_a));
        int32_t elt2_b = (int8_t)(m >> (16 * i + 8 * sel_b));

        a += elt1_r * elt2_a + elt1_i * elt2_b * sub_i;
    }
    return a;
}

static int64_t do_cdot_d(uint64_t n, uint64_t m, int64_t a,
                         int sel_a, int sel_b, int sub_i)
{
    for (int i = 0; i <= 1; i++) {
        int64_t elt1_r = (int16_t)(n >> (32 * i + 0));
        int64_t elt1_i = (int16_t)(n >> (32 * i + 16));
        int64_t elt2_a = (int16_t)(m >> (32 * i + 16 * sel_a));
        int64_t elt2_b = (int16_t)(m >> (32 * i + 16 * sel_b));

        a += elt1_r * elt2_a + elt1_i * elt2_b * sub_i;
    }
    return a;
}

void HELPER(sve2_cdot_zzzz_s)(void *vd, void *vn, void *vm,
                              void *va, uint32_t desc)
{
    int opr_sz = simd_oprsz(desc);
    int rot = simd_data(desc);
    int sel_a = rot & 1;
    int sel_b = sel_a ^ 1;
    int sub_i = (rot == 0 || rot == 3 ? -1 : 1);
    uint32_t *d = vd, *n = vn, *m = vm, *a = va;

    for (int e = 0; e < opr_sz / 4; e++) {
        d[e] = do_cdot_s(n[e], m[e], a[e], sel_a, sel_b, sub_i);
    }
}

void HELPER(sve2_cdot_zzzz_d)(void *vd, void *vn, void *vm,
                              void *va, uint32_t desc)
{
    int opr_sz = simd_oprsz(desc);
    int rot = simd_data(desc);
    int sel_a = rot & 1;
    int sel_b = sel_a ^ 1;
    int sub_i = (rot == 0 || rot == 3 ? -1 : 1);
    uint64_t *d = vd, *n = vn, *m = vm, *a = va;

    for (int e = 0; e < opr_sz / 8; e++) {
        d[e] = do_cdot_d(n[e], m[e], a[e], sel_a, sel_b, sub_i);
    }
}

void HELPER(sve2_cdot_idx_s)(void *vd, void *vn, void *vm,
                             void *va, uint32_t desc)
{
    int opr_sz = simd_oprsz(desc);
    int rot = extract32(desc, SIMD_DATA_SHIFT, 2);
    int idx = H4(extract32(desc, SIMD_DATA_SHIFT + 2, 2));
    int sel_a = rot & 1;
    int sel_b = sel_a ^ 1;
    int sub_i = (rot == 0 || rot == 3 ? -1 : 1);
    uint32_t *d = vd, *n = vn, *m = vm, *a = va;

    for (int seg = 0; seg < opr_sz / 4; seg += 4) {
        uint32_t seg_m = m[seg + idx];
        for (int e = 0; e < 4; e++) {
            d[seg + e] = do_cdot_s(n[seg + e], seg_m, a[seg + e],
                                   sel_a, sel_b, sub_i);
        }
    }
}

void HELPER(sve2_cdot_idx_d)(void *vd, void *vn, void *vm,
                             void *va, uint32_t desc)
{
    int seg, opr_sz = simd_oprsz(desc);
    int rot = extract32(desc, SIMD_DATA_SHIFT, 2);
    int idx = extract32(desc, SIMD_DATA_SHIFT + 2, 2);
    int sel_a = rot & 1;
    int sel_b = sel_a ^ 1;
    int sub_i = (rot == 0 || rot == 3 ? -1 : 1);
    uint64_t *d = vd, *n = vn, *m = vm, *a = va;

    for (seg = 0; seg < opr_sz / 8; seg += 2) {
        uint64_t seg_m = m[seg + idx];
        for (int e = 0; e < 2; e++) {
            d[seg + e] = do_cdot_d(n[seg + e], seg_m, a[seg + e],
                                   sel_a, sel_b, sub_i);
        }
    }
}

#define DO_ZZXZ(NAME, TYPE, H, OP) \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *va, uint32_t desc) \
{                                                                       \
    intptr_t oprsz = simd_oprsz(desc), segment = 16 / sizeof(TYPE);     \
    intptr_t i, j, idx = simd_data(desc);                               \
    TYPE *d = vd, *a = va, *n = vn, *m = (TYPE *)vm + H(idx);           \
    for (i = 0; i < oprsz / sizeof(TYPE); i += segment) {               \
        TYPE mm = m[i];                                                 \
        for (j = 0; j < segment; j++) {                                 \
            d[i + j] = OP(n[i + j], mm, a[i + j]);                      \
        }                                                               \
    }                                                                   \
}

#define DO_SQRDMLAH_H(N, M, A) \
    ({ uint32_t discard; do_sqrdmlah_h(N, M, A, false, true, &discard); })
#define DO_SQRDMLAH_S(N, M, A) \
    ({ uint32_t discard; do_sqrdmlah_s(N, M, A, false, true, &discard); })
#define DO_SQRDMLAH_D(N, M, A) do_sqrdmlah_d(N, M, A, false, true)

DO_ZZXZ(sve2_sqrdmlah_idx_h, int16_t, H2, DO_SQRDMLAH_H)
DO_ZZXZ(sve2_sqrdmlah_idx_s, int32_t, H4, DO_SQRDMLAH_S)
DO_ZZXZ(sve2_sqrdmlah_idx_d, int64_t, H8, DO_SQRDMLAH_D)

#define DO_SQRDMLSH_H(N, M, A) \
    ({ uint32_t discard; do_sqrdmlah_h(N, M, A, true, true, &discard); })
#define DO_SQRDMLSH_S(N, M, A) \
    ({ uint32_t discard; do_sqrdmlah_s(N, M, A, true, true, &discard); })
#define DO_SQRDMLSH_D(N, M, A) do_sqrdmlah_d(N, M, A, true, true)

DO_ZZXZ(sve2_sqrdmlsh_idx_h, int16_t, H2, DO_SQRDMLSH_H)
DO_ZZXZ(sve2_sqrdmlsh_idx_s, int32_t, H4, DO_SQRDMLSH_S)
DO_ZZXZ(sve2_sqrdmlsh_idx_d, int64_t, H8, DO_SQRDMLSH_D)

#undef DO_ZZXZ

#define DO_ZZXW(NAME, TYPEW, TYPEN, HW, HN, OP) \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *va, uint32_t desc)  \
{                                                                         \
    intptr_t i, j, oprsz = simd_oprsz(desc);                              \
    intptr_t sel = extract32(desc, SIMD_DATA_SHIFT, 1) * sizeof(TYPEN);   \
    intptr_t idx = extract32(desc, SIMD_DATA_SHIFT + 1, 3) * sizeof(TYPEN); \
    for (i = 0; i < oprsz; i += 16) {                                     \
        TYPEW mm = *(TYPEN *)(vm + HN(i + idx));                          \
        for (j = 0; j < 16; j += sizeof(TYPEW)) {                         \
            TYPEW nn = *(TYPEN *)(vn + HN(i + j + sel));                  \
            TYPEW aa = *(TYPEW *)(va + HW(i + j));                        \
            *(TYPEW *)(vd + HW(i + j)) = OP(nn, mm, aa);                  \
        }                                                                 \
    }                                                                     \
}

#define DO_MLA(N, M, A)  (A + N * M)

DO_ZZXW(sve2_smlal_idx_s, int32_t, int16_t, H1_4, H1_2, DO_MLA)
DO_ZZXW(sve2_smlal_idx_d, int64_t, int32_t, H1_8, H1_4, DO_MLA)
DO_ZZXW(sve2_umlal_idx_s, uint32_t, uint16_t, H1_4, H1_2, DO_MLA)
DO_ZZXW(sve2_umlal_idx_d, uint64_t, uint32_t, H1_8, H1_4, DO_MLA)

#define DO_MLS(N, M, A)  (A - N * M)

DO_ZZXW(sve2_smlsl_idx_s, int32_t, int16_t, H1_4, H1_2, DO_MLS)
DO_ZZXW(sve2_smlsl_idx_d, int64_t, int32_t, H1_8, H1_4, DO_MLS)
DO_ZZXW(sve2_umlsl_idx_s, uint32_t, uint16_t, H1_4, H1_2, DO_MLS)
DO_ZZXW(sve2_umlsl_idx_d, uint64_t, uint32_t, H1_8, H1_4, DO_MLS)

#define DO_SQDMLAL_S(N, M, A)  DO_SQADD_S(A, do_sqdmull_s(N, M))
#define DO_SQDMLAL_D(N, M, A)  do_sqadd_d(A, do_sqdmull_d(N, M))

DO_ZZXW(sve2_sqdmlal_idx_s, int32_t, int16_t, H1_4, H1_2, DO_SQDMLAL_S)
DO_ZZXW(sve2_sqdmlal_idx_d, int64_t, int32_t, H1_8, H1_4, DO_SQDMLAL_D)

#define DO_SQDMLSL_S(N, M, A)  DO_SQSUB_S(A, do_sqdmull_s(N, M))
#define DO_SQDMLSL_D(N, M, A)  do_sqsub_d(A, do_sqdmull_d(N, M))

DO_ZZXW(sve2_sqdmlsl_idx_s, int32_t, int16_t, H1_4, H1_2, DO_SQDMLSL_S)
DO_ZZXW(sve2_sqdmlsl_idx_d, int64_t, int32_t, H1_8, H1_4, DO_SQDMLSL_D)

#undef DO_MLA
#undef DO_MLS
#undef DO_ZZXW

#define DO_ZZX(NAME, TYPEW, TYPEN, HW, HN, OP) \
void HELPER(NAME)(void *vd, void *vn, void *vm, uint32_t desc)            \
{                                                                         \
    intptr_t i, j, oprsz = simd_oprsz(desc);                              \
    intptr_t sel = extract32(desc, SIMD_DATA_SHIFT, 1) * sizeof(TYPEN);   \
    intptr_t idx = extract32(desc, SIMD_DATA_SHIFT + 1, 3) * sizeof(TYPEN); \
    for (i = 0; i < oprsz; i += 16) {                                     \
        TYPEW mm = *(TYPEN *)(vm + HN(i + idx));                          \
        for (j = 0; j < 16; j += sizeof(TYPEW)) {                         \
            TYPEW nn = *(TYPEN *)(vn + HN(i + j + sel));                  \
            *(TYPEW *)(vd + HW(i + j)) = OP(nn, mm);                      \
        }                                                                 \
    }                                                                     \
}

DO_ZZX(sve2_sqdmull_idx_s, int32_t, int16_t, H1_4, H1_2, do_sqdmull_s)
DO_ZZX(sve2_sqdmull_idx_d, int64_t, int32_t, H1_8, H1_4, do_sqdmull_d)

DO_ZZX(sve2_smull_idx_s, int32_t, int16_t, H1_4, H1_2, DO_MUL)
DO_ZZX(sve2_smull_idx_d, int64_t, int32_t, H1_8, H1_4, DO_MUL)

DO_ZZX(sve2_umull_idx_s, uint32_t, uint16_t, H1_4, H1_2, DO_MUL)
DO_ZZX(sve2_umull_idx_d, uint64_t, uint32_t, H1_8, H1_4, DO_MUL)

#undef DO_ZZX

#define DO_BITPERM(NAME, TYPE, OP) \
void HELPER(NAME)(void *vd, void *vn, void *vm, uint32_t desc) \
{                                                              \
    intptr_t i, opr_sz = simd_oprsz(desc);                     \
    for (i = 0; i < opr_sz; i += sizeof(TYPE)) {               \
        TYPE nn = *(TYPE *)(vn + i);                           \
        TYPE mm = *(TYPE *)(vm + i);                           \
        *(TYPE *)(vd + i) = OP(nn, mm, sizeof(TYPE) * 8);      \
    }                                                          \
}

static uint64_t bitextract(uint64_t data, uint64_t mask, int n)
{
    uint64_t res = 0;
    int db, rb = 0;

    for (db = 0; db < n; ++db) {
        if ((mask >> db) & 1) {
            res |= ((data >> db) & 1) << rb;
            ++rb;
        }
    }
    return res;
}

DO_BITPERM(sve2_bext_b, uint8_t, bitextract)
DO_BITPERM(sve2_bext_h, uint16_t, bitextract)
DO_BITPERM(sve2_bext_s, uint32_t, bitextract)
DO_BITPERM(sve2_bext_d, uint64_t, bitextract)

static uint64_t bitdeposit(uint64_t data, uint64_t mask, int n)
{
    uint64_t res = 0;
    int rb, db = 0;

    for (rb = 0; rb < n; ++rb) {
        if ((mask >> rb) & 1) {
            res |= ((data >> db) & 1) << rb;
            ++db;
        }
    }
    return res;
}

DO_BITPERM(sve2_bdep_b, uint8_t, bitdeposit)
DO_BITPERM(sve2_bdep_h, uint16_t, bitdeposit)
DO_BITPERM(sve2_bdep_s, uint32_t, bitdeposit)
DO_BITPERM(sve2_bdep_d, uint64_t, bitdeposit)

static uint64_t bitgroup(uint64_t data, uint64_t mask, int n)
{
    uint64_t resm = 0, resu = 0;
    int db, rbm = 0, rbu = 0;

    for (db = 0; db < n; ++db) {
        uint64_t val = (data >> db) & 1;
        if ((mask >> db) & 1) {
            resm |= val << rbm++;
        } else {
            resu |= val << rbu++;
        }
    }

    return resm | (resu << rbm);
}

DO_BITPERM(sve2_bgrp_b, uint8_t, bitgroup)
DO_BITPERM(sve2_bgrp_h, uint16_t, bitgroup)
DO_BITPERM(sve2_bgrp_s, uint32_t, bitgroup)
DO_BITPERM(sve2_bgrp_d, uint64_t, bitgroup)

#undef DO_BITPERM

#define DO_CADD(NAME, TYPE, H, ADD_OP, SUB_OP)                  \
void HELPER(NAME)(void *vd, void *vn, void *vm, uint32_t desc)  \
{                                                               \
    intptr_t i, opr_sz = simd_oprsz(desc);                      \
    int sub_r = simd_data(desc);                                \
    if (sub_r) {                                                \
        for (i = 0; i < opr_sz; i += 2 * sizeof(TYPE)) {        \
            TYPE acc_r = *(TYPE *)(vn + H(i));                  \
            TYPE acc_i = *(TYPE *)(vn + H(i + sizeof(TYPE)));   \
            TYPE el2_r = *(TYPE *)(vm + H(i));                  \
            TYPE el2_i = *(TYPE *)(vm + H(i + sizeof(TYPE)));   \
            acc_r = ADD_OP(acc_r, el2_i);                       \
            acc_i = SUB_OP(acc_i, el2_r);                       \
            *(TYPE *)(vd + H(i)) = acc_r;                       \
            *(TYPE *)(vd + H(i + sizeof(TYPE))) = acc_i;        \
        }                                                       \
    } else {                                                    \
        for (i = 0; i < opr_sz; i += 2 * sizeof(TYPE)) {        \
            TYPE acc_r = *(TYPE *)(vn + H(i));                  \
            TYPE acc_i = *(TYPE *)(vn + H(i + sizeof(TYPE)));   \
            TYPE el2_r = *(TYPE *)(vm + H(i));                  \
            TYPE el2_i = *(TYPE *)(vm + H(i + sizeof(TYPE)));   \
            acc_r = SUB_OP(acc_r, el2_i);                       \
            acc_i = ADD_OP(acc_i, el2_r);                       \
            *(TYPE *)(vd + H(i)) = acc_r;                       \
            *(TYPE *)(vd + H(i + sizeof(TYPE))) = acc_i;        \
        }                                                       \
    }                                                           \
}

DO_CADD(sve2_cadd_b, int8_t, H1, DO_ADD, DO_SUB)
DO_CADD(sve2_cadd_h, int16_t, H1_2, DO_ADD, DO_SUB)
DO_CADD(sve2_cadd_s, int32_t, H1_4, DO_ADD, DO_SUB)
DO_CADD(sve2_cadd_d, int64_t, H1_8, DO_ADD, DO_SUB)

DO_CADD(sve2_sqcadd_b, int8_t, H1, DO_SQADD_B, DO_SQSUB_B)
DO_CADD(sve2_sqcadd_h, int16_t, H1_2, DO_SQADD_H, DO_SQSUB_H)
DO_CADD(sve2_sqcadd_s, int32_t, H1_4, DO_SQADD_S, DO_SQSUB_S)
DO_CADD(sve2_sqcadd_d, int64_t, H1_8, do_sqadd_d, do_sqsub_d)

#undef DO_CADD

#define DO_ZZI_SHLL(NAME, TYPEW, TYPEN, HW, HN) \
void HELPER(NAME)(void *vd, void *vn, uint32_t desc)           \
{                                                              \
    intptr_t i, opr_sz = simd_oprsz(desc);                     \
    intptr_t sel = (simd_data(desc) & 1) * sizeof(TYPEN);      \
    int shift = simd_data(desc) >> 1;                          \
    for (i = 0; i < opr_sz; i += sizeof(TYPEW)) {              \
        TYPEW nn = *(TYPEN *)(vn + HN(i + sel));               \
        *(TYPEW *)(vd + HW(i)) = nn << shift;                  \
    }                                                          \
}

DO_ZZI_SHLL(sve2_sshll_h, int16_t, int8_t, H1_2, H1)
DO_ZZI_SHLL(sve2_sshll_s, int32_t, int16_t, H1_4, H1_2)
DO_ZZI_SHLL(sve2_sshll_d, int64_t, int32_t, H1_8, H1_4)

DO_ZZI_SHLL(sve2_ushll_h, uint16_t, uint8_t, H1_2, H1)
DO_ZZI_SHLL(sve2_ushll_s, uint32_t, uint16_t, H1_4, H1_2)
DO_ZZI_SHLL(sve2_ushll_d, uint64_t, uint32_t, H1_8, H1_4)

#undef DO_ZZI_SHLL

/* Two-operand reduction expander, controlled by a predicate.
 * The difference between TYPERED and TYPERET has to do with
 * sign-extension.  E.g. for SMAX, TYPERED must be signed,
 * but TYPERET must be unsigned so that e.g. a 32-bit value
 * is not sign-extended to the ABI uint64_t return type.
 */
/* ??? If we were to vectorize this by hand the reduction ordering
 * would change.  For integer operands, this is perfectly fine.
 */
#define DO_VPZ(NAME, TYPEELT, TYPERED, TYPERET, H, INIT, OP) \
uint64_t HELPER(NAME)(void *vn, void *vg, uint32_t desc)   \
{                                                          \
    intptr_t i, opr_sz = simd_oprsz(desc);                 \
    TYPERED ret = INIT;                                    \
    for (i = 0; i < opr_sz; ) {                            \
        uint16_t pg = *(uint16_t *)(vg + H1_2(i >> 3));    \
        do {                                               \
            if (pg & 1) {                                  \
                TYPEELT nn = *(TYPEELT *)(vn + H(i));      \
                ret = OP(ret, nn);                         \
            }                                              \
            i += sizeof(TYPEELT), pg >>= sizeof(TYPEELT);  \
        } while (i & 15);                                  \
    }                                                      \
    return (TYPERET)ret;                                   \
}

#define DO_VPZ_D(NAME, TYPEE, TYPER, INIT, OP)             \
uint64_t HELPER(NAME)(void *vn, void *vg, uint32_t desc)   \
{                                                          \
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;             \
    TYPEE *n = vn;                                         \
    uint8_t *pg = vg;                                      \
    TYPER ret = INIT;                                      \
    for (i = 0; i < opr_sz; i += 1) {                      \
        if (pg[H1(i)] & 1) {                               \
            TYPEE nn = n[i];                               \
            ret = OP(ret, nn);                             \
        }                                                  \
    }                                                      \
    return ret;                                            \
}

DO_VPZ(sve_orv_b, uint8_t, uint8_t, uint8_t, H1, 0, DO_ORR)
DO_VPZ(sve_orv_h, uint16_t, uint16_t, uint16_t, H1_2, 0, DO_ORR)
DO_VPZ(sve_orv_s, uint32_t, uint32_t, uint32_t, H1_4, 0, DO_ORR)
DO_VPZ_D(sve_orv_d, uint64_t, uint64_t, 0, DO_ORR)

DO_VPZ(sve_eorv_b, uint8_t, uint8_t, uint8_t, H1, 0, DO_EOR)
DO_VPZ(sve_eorv_h, uint16_t, uint16_t, uint16_t, H1_2, 0, DO_EOR)
DO_VPZ(sve_eorv_s, uint32_t, uint32_t, uint32_t, H1_4, 0, DO_EOR)
DO_VPZ_D(sve_eorv_d, uint64_t, uint64_t, 0, DO_EOR)

DO_VPZ(sve_andv_b, uint8_t, uint8_t, uint8_t, H1, -1, DO_AND)
DO_VPZ(sve_andv_h, uint16_t, uint16_t, uint16_t, H1_2, -1, DO_AND)
DO_VPZ(sve_andv_s, uint32_t, uint32_t, uint32_t, H1_4, -1, DO_AND)
DO_VPZ_D(sve_andv_d, uint64_t, uint64_t, -1, DO_AND)

DO_VPZ(sve_saddv_b, int8_t, uint64_t, uint64_t, H1, 0, DO_ADD)
DO_VPZ(sve_saddv_h, int16_t, uint64_t, uint64_t, H1_2, 0, DO_ADD)
DO_VPZ(sve_saddv_s, int32_t, uint64_t, uint64_t, H1_4, 0, DO_ADD)

DO_VPZ(sve_uaddv_b, uint8_t, uint64_t, uint64_t, H1, 0, DO_ADD)
DO_VPZ(sve_uaddv_h, uint16_t, uint64_t, uint64_t, H1_2, 0, DO_ADD)
DO_VPZ(sve_uaddv_s, uint32_t, uint64_t, uint64_t, H1_4, 0, DO_ADD)
DO_VPZ_D(sve_uaddv_d, uint64_t, uint64_t, 0, DO_ADD)

DO_VPZ(sve_smaxv_b, int8_t, int8_t, uint8_t, H1, INT8_MIN, DO_MAX)
DO_VPZ(sve_smaxv_h, int16_t, int16_t, uint16_t, H1_2, INT16_MIN, DO_MAX)
DO_VPZ(sve_smaxv_s, int32_t, int32_t, uint32_t, H1_4, INT32_MIN, DO_MAX)
DO_VPZ_D(sve_smaxv_d, int64_t, int64_t, INT64_MIN, DO_MAX)

DO_VPZ(sve_umaxv_b, uint8_t, uint8_t, uint8_t, H1, 0, DO_MAX)
DO_VPZ(sve_umaxv_h, uint16_t, uint16_t, uint16_t, H1_2, 0, DO_MAX)
DO_VPZ(sve_umaxv_s, uint32_t, uint32_t, uint32_t, H1_4, 0, DO_MAX)
DO_VPZ_D(sve_umaxv_d, uint64_t, uint64_t, 0, DO_MAX)

DO_VPZ(sve_sminv_b, int8_t, int8_t, uint8_t, H1, INT8_MAX, DO_MIN)
DO_VPZ(sve_sminv_h, int16_t, int16_t, uint16_t, H1_2, INT16_MAX, DO_MIN)
DO_VPZ(sve_sminv_s, int32_t, int32_t, uint32_t, H1_4, INT32_MAX, DO_MIN)
DO_VPZ_D(sve_sminv_d, int64_t, int64_t, INT64_MAX, DO_MIN)

DO_VPZ(sve_uminv_b, uint8_t, uint8_t, uint8_t, H1, -1, DO_MIN)
DO_VPZ(sve_uminv_h, uint16_t, uint16_t, uint16_t, H1_2, -1, DO_MIN)
DO_VPZ(sve_uminv_s, uint32_t, uint32_t, uint32_t, H1_4, -1, DO_MIN)
DO_VPZ_D(sve_uminv_d, uint64_t, uint64_t, -1, DO_MIN)

#undef DO_VPZ
#undef DO_VPZ_D

/* Two vector operand, one scalar operand, unpredicated.  */
#define DO_ZZI(NAME, TYPE, OP)                                       \
void HELPER(NAME)(void *vd, void *vn, uint64_t s64, uint32_t desc)   \
{                                                                    \
    intptr_t i, opr_sz = simd_oprsz(desc) / sizeof(TYPE);            \
    TYPE s = s64, *d = vd, *n = vn;                                  \
    for (i = 0; i < opr_sz; ++i) {                                   \
        d[i] = OP(n[i], s);                                          \
    }                                                                \
}

#define DO_SUBR(X, Y)   (Y - X)

DO_ZZI(sve_subri_b, uint8_t, DO_SUBR)
DO_ZZI(sve_subri_h, uint16_t, DO_SUBR)
DO_ZZI(sve_subri_s, uint32_t, DO_SUBR)
DO_ZZI(sve_subri_d, uint64_t, DO_SUBR)

DO_ZZI(sve_smaxi_b, int8_t, DO_MAX)
DO_ZZI(sve_smaxi_h, int16_t, DO_MAX)
DO_ZZI(sve_smaxi_s, int32_t, DO_MAX)
DO_ZZI(sve_smaxi_d, int64_t, DO_MAX)

DO_ZZI(sve_smini_b, int8_t, DO_MIN)
DO_ZZI(sve_smini_h, int16_t, DO_MIN)
DO_ZZI(sve_smini_s, int32_t, DO_MIN)
DO_ZZI(sve_smini_d, int64_t, DO_MIN)

DO_ZZI(sve_umaxi_b, uint8_t, DO_MAX)
DO_ZZI(sve_umaxi_h, uint16_t, DO_MAX)
DO_ZZI(sve_umaxi_s, uint32_t, DO_MAX)
DO_ZZI(sve_umaxi_d, uint64_t, DO_MAX)

DO_ZZI(sve_umini_b, uint8_t, DO_MIN)
DO_ZZI(sve_umini_h, uint16_t, DO_MIN)
DO_ZZI(sve_umini_s, uint32_t, DO_MIN)
DO_ZZI(sve_umini_d, uint64_t, DO_MIN)

#undef DO_ZZI

#undef DO_AND
#undef DO_ORR
#undef DO_EOR
#undef DO_BIC
#undef DO_ADD
#undef DO_SUB
#undef DO_MAX
#undef DO_MIN
#undef DO_ABD
#undef DO_MUL
#undef DO_DIV
#undef DO_ASR
#undef DO_LSR
#undef DO_LSL
#undef DO_SUBR

/* Similar to the ARM LastActiveElement pseudocode function, except the
   result is multiplied by the element size.  This includes the not found
   indication; e.g. not found for esz=3 is -8.  */
static intptr_t last_active_element(uint64_t *g, intptr_t words, intptr_t esz)
{
    uint64_t mask = pred_esz_masks[esz];
    intptr_t i = words;

    do {
        uint64_t this_g = g[--i] & mask;
        if (this_g) {
            return i * 64 + (63 - clz64(this_g));
        }
    } while (i > 0);
    return (intptr_t)-1 << esz;
}

uint32_t HELPER(sve_pfirst)(void *vd, void *vg, uint32_t pred_desc)
{
    intptr_t words = DIV_ROUND_UP(FIELD_EX32(pred_desc, PREDDESC, OPRSZ), 8);
    uint32_t flags = PREDTEST_INIT;
    uint64_t *d = vd, *g = vg;
    intptr_t i = 0;

    do {
        uint64_t this_d = d[i];
        uint64_t this_g = g[i];

        if (this_g) {
            if (!(flags & 4)) {
                /* Set in D the first bit of G.  */
                this_d |= this_g & -this_g;
                d[i] = this_d;
            }
            flags = iter_predtest_fwd(this_d, this_g, flags);
        }
    } while (++i < words);

    return flags;
}

uint32_t HELPER(sve_pnext)(void *vd, void *vg, uint32_t pred_desc)
{
    intptr_t words = DIV_ROUND_UP(FIELD_EX32(pred_desc, PREDDESC, OPRSZ), 8);
    intptr_t esz = FIELD_EX32(pred_desc, PREDDESC, ESZ);
    uint32_t flags = PREDTEST_INIT;
    uint64_t *d = vd, *g = vg, esz_mask;
    intptr_t i, next;

    next = last_active_element(vd, words, esz) + (1 << esz);
    esz_mask = pred_esz_masks[esz];

    /* Similar to the pseudocode for pnext, but scaled by ESZ
       so that we find the correct bit.  */
    if (next < words * 64) {
        uint64_t mask = -1;

        if (next & 63) {
            mask = ~((1ull << (next & 63)) - 1);
            next &= -64;
        }
        do {
            uint64_t this_g = g[next / 64] & esz_mask & mask;
            if (this_g != 0) {
                next = (next & -64) + ctz64(this_g);
                break;
            }
            next += 64;
            mask = -1;
        } while (next < words * 64);
    }

    i = 0;
    do {
        uint64_t this_d = 0;
        if (i == next / 64) {
            this_d = 1ull << (next & 63);
        }
        d[i] = this_d;
        flags = iter_predtest_fwd(this_d, g[i] & esz_mask, flags);
    } while (++i < words);

    return flags;
}

/*
 * Copy Zn into Zd, and store zero into inactive elements.
 * If inv, store zeros into the active elements.
 */
void HELPER(sve_movz_b)(void *vd, void *vn, void *vg, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t inv = -(uint64_t)(simd_data(desc) & 1);
    uint64_t *d = vd, *n = vn;
    uint8_t *pg = vg;

    for (i = 0; i < opr_sz; i += 1) {
        d[i] = n[i] & (expand_pred_b(pg[H1(i)]) ^ inv);
    }
}

void HELPER(sve_movz_h)(void *vd, void *vn, void *vg, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t inv = -(uint64_t)(simd_data(desc) & 1);
    uint64_t *d = vd, *n = vn;
    uint8_t *pg = vg;

    for (i = 0; i < opr_sz; i += 1) {
        d[i] = n[i] & (expand_pred_h(pg[H1(i)]) ^ inv);
    }
}

void HELPER(sve_movz_s)(void *vd, void *vn, void *vg, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t inv = -(uint64_t)(simd_data(desc) & 1);
    uint64_t *d = vd, *n = vn;
    uint8_t *pg = vg;

    for (i = 0; i < opr_sz; i += 1) {
        d[i] = n[i] & (expand_pred_s(pg[H1(i)]) ^ inv);
    }
}

void HELPER(sve_movz_d)(void *vd, void *vn, void *vg, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd, *n = vn;
    uint8_t *pg = vg;
    uint8_t inv = simd_data(desc);

    for (i = 0; i < opr_sz; i += 1) {
        d[i] = n[i] & -(uint64_t)((pg[H1(i)] ^ inv) & 1);
    }
}

/* Three-operand expander, immediate operand, controlled by a predicate.
 */
#define DO_ZPZI(NAME, TYPE, H, OP)                              \
void HELPER(NAME)(void *vd, void *vn, void *vg, uint32_t desc)  \
{                                                               \
    intptr_t i, opr_sz = simd_oprsz(desc);                      \
    TYPE imm = simd_data(desc);                                 \
    for (i = 0; i < opr_sz; ) {                                 \
        uint16_t pg = *(uint16_t *)(vg + H1_2(i >> 3));         \
        do {                                                    \
            if (pg & 1) {                                       \
                TYPE nn = *(TYPE *)(vn + H(i));                 \
                *(TYPE *)(vd + H(i)) = OP(nn, imm);             \
            }                                                   \
            i += sizeof(TYPE), pg >>= sizeof(TYPE);             \
        } while (i & 15);                                       \
    }                                                           \
}

/* Similarly, specialized for 64-bit operands.  */
#define DO_ZPZI_D(NAME, TYPE, OP)                               \
void HELPER(NAME)(void *vd, void *vn, void *vg, uint32_t desc)  \
{                                                               \
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;                  \
    TYPE *d = vd, *n = vn;                                      \
    TYPE imm = simd_data(desc);                                 \
    uint8_t *pg = vg;                                           \
    for (i = 0; i < opr_sz; i += 1) {                           \
        if (pg[H1(i)] & 1) {                                    \
            TYPE nn = n[i];                                     \
            d[i] = OP(nn, imm);                                 \
        }                                                       \
    }                                                           \
}

#define DO_SHR(N, M)  (N >> M)
#define DO_SHL(N, M)  (N << M)

/* Arithmetic shift right for division.  This rounds negative numbers
   toward zero as per signed division.  Therefore before shifting,
   when N is negative, add 2**M-1.  */
#define DO_ASRD(N, M) ((N + (N < 0 ? ((__typeof(N))1 << M) - 1 : 0)) >> M)

static inline uint64_t do_urshr(uint64_t x, unsigned sh)
{
    if (likely(sh < 64)) {
        return (x >> sh) + ((x >> (sh - 1)) & 1);
    } else if (sh == 64) {
        return x >> 63;
    } else {
        return 0;
    }
}

static inline int64_t do_srshr(int64_t x, unsigned sh)
{
    if (likely(sh < 64)) {
        return (x >> sh) + ((x >> (sh - 1)) & 1);
    } else {
        /* Rounding the sign bit always produces 0. */
        return 0;
    }
}

DO_ZPZI(sve_asr_zpzi_b, int8_t, H1, DO_SHR)
DO_ZPZI(sve_asr_zpzi_h, int16_t, H1_2, DO_SHR)
DO_ZPZI(sve_asr_zpzi_s, int32_t, H1_4, DO_SHR)
DO_ZPZI_D(sve_asr_zpzi_d, int64_t, DO_SHR)

DO_ZPZI(sve_lsr_zpzi_b, uint8_t, H1, DO_SHR)
DO_ZPZI(sve_lsr_zpzi_h, uint16_t, H1_2, DO_SHR)
DO_ZPZI(sve_lsr_zpzi_s, uint32_t, H1_4, DO_SHR)
DO_ZPZI_D(sve_lsr_zpzi_d, uint64_t, DO_SHR)

DO_ZPZI(sve_lsl_zpzi_b, uint8_t, H1, DO_SHL)
DO_ZPZI(sve_lsl_zpzi_h, uint16_t, H1_2, DO_SHL)
DO_ZPZI(sve_lsl_zpzi_s, uint32_t, H1_4, DO_SHL)
DO_ZPZI_D(sve_lsl_zpzi_d, uint64_t, DO_SHL)

DO_ZPZI(sve_asrd_b, int8_t, H1, DO_ASRD)
DO_ZPZI(sve_asrd_h, int16_t, H1_2, DO_ASRD)
DO_ZPZI(sve_asrd_s, int32_t, H1_4, DO_ASRD)
DO_ZPZI_D(sve_asrd_d, int64_t, DO_ASRD)

/* SVE2 bitwise shift by immediate */
DO_ZPZI(sve2_sqshl_zpzi_b, int8_t, H1, do_sqshl_b)
DO_ZPZI(sve2_sqshl_zpzi_h, int16_t, H1_2, do_sqshl_h)
DO_ZPZI(sve2_sqshl_zpzi_s, int32_t, H1_4, do_sqshl_s)
DO_ZPZI_D(sve2_sqshl_zpzi_d, int64_t, do_sqshl_d)

DO_ZPZI(sve2_uqshl_zpzi_b, uint8_t, H1, do_uqshl_b)
DO_ZPZI(sve2_uqshl_zpzi_h, uint16_t, H1_2, do_uqshl_h)
DO_ZPZI(sve2_uqshl_zpzi_s, uint32_t, H1_4, do_uqshl_s)
DO_ZPZI_D(sve2_uqshl_zpzi_d, uint64_t, do_uqshl_d)

DO_ZPZI(sve2_srshr_b, int8_t, H1, do_srshr)
DO_ZPZI(sve2_srshr_h, int16_t, H1_2, do_srshr)
DO_ZPZI(sve2_srshr_s, int32_t, H1_4, do_srshr)
DO_ZPZI_D(sve2_srshr_d, int64_t, do_srshr)

DO_ZPZI(sve2_urshr_b, uint8_t, H1, do_urshr)
DO_ZPZI(sve2_urshr_h, uint16_t, H1_2, do_urshr)
DO_ZPZI(sve2_urshr_s, uint32_t, H1_4, do_urshr)
DO_ZPZI_D(sve2_urshr_d, uint64_t, do_urshr)

#define do_suqrshl_b(n, m) \
   ({ uint32_t discard; do_suqrshl_bhs(n, (int8_t)m, 8, false, &discard); })
#define do_suqrshl_h(n, m) \
   ({ uint32_t discard; do_suqrshl_bhs(n, (int16_t)m, 16, false, &discard); })
#define do_suqrshl_s(n, m) \
   ({ uint32_t discard; do_suqrshl_bhs(n, m, 32, false, &discard); })
#define do_suqrshl_d(n, m) \
   ({ uint32_t discard; do_suqrshl_d(n, m, false, &discard); })

DO_ZPZI(sve2_sqshlu_b, int8_t, H1, do_suqrshl_b)
DO_ZPZI(sve2_sqshlu_h, int16_t, H1_2, do_suqrshl_h)
DO_ZPZI(sve2_sqshlu_s, int32_t, H1_4, do_suqrshl_s)
DO_ZPZI_D(sve2_sqshlu_d, int64_t, do_suqrshl_d)

#undef DO_ASRD
#undef DO_ZPZI
#undef DO_ZPZI_D

#define DO_SHRNB(NAME, TYPEW, TYPEN, OP) \
void HELPER(NAME)(void *vd, void *vn, uint32_t desc)         \
{                                                            \
    intptr_t i, opr_sz = simd_oprsz(desc);                   \
    int shift = simd_data(desc);                             \
    for (i = 0; i < opr_sz; i += sizeof(TYPEW)) {            \
        TYPEW nn = *(TYPEW *)(vn + i);                       \
        *(TYPEW *)(vd + i) = (TYPEN)OP(nn, shift);           \
    }                                                        \
}

#define DO_SHRNT(NAME, TYPEW, TYPEN, HW, HN, OP)                  \
void HELPER(NAME)(void *vd, void *vn, uint32_t desc)              \
{                                                                 \
    intptr_t i, opr_sz = simd_oprsz(desc);                        \
    int shift = simd_data(desc);                                  \
    for (i = 0; i < opr_sz; i += sizeof(TYPEW)) {                 \
        TYPEW nn = *(TYPEW *)(vn + HW(i));                        \
        *(TYPEN *)(vd + HN(i + sizeof(TYPEN))) = OP(nn, shift);   \
    }                                                             \
}

DO_SHRNB(sve2_shrnb_h, uint16_t, uint8_t, DO_SHR)
DO_SHRNB(sve2_shrnb_s, uint32_t, uint16_t, DO_SHR)
DO_SHRNB(sve2_shrnb_d, uint64_t, uint32_t, DO_SHR)

DO_SHRNT(sve2_shrnt_h, uint16_t, uint8_t, H1_2, H1, DO_SHR)
DO_SHRNT(sve2_shrnt_s, uint32_t, uint16_t, H1_4, H1_2, DO_SHR)
DO_SHRNT(sve2_shrnt_d, uint64_t, uint32_t, H1_8, H1_4, DO_SHR)

DO_SHRNB(sve2_rshrnb_h, uint16_t, uint8_t, do_urshr)
DO_SHRNB(sve2_rshrnb_s, uint32_t, uint16_t, do_urshr)
DO_SHRNB(sve2_rshrnb_d, uint64_t, uint32_t, do_urshr)

DO_SHRNT(sve2_rshrnt_h, uint16_t, uint8_t, H1_2, H1, do_urshr)
DO_SHRNT(sve2_rshrnt_s, uint32_t, uint16_t, H1_4, H1_2, do_urshr)
DO_SHRNT(sve2_rshrnt_d, uint64_t, uint32_t, H1_8, H1_4, do_urshr)

#define DO_SQSHRUN_H(x, sh) do_sat_bhs((int64_t)(x) >> sh, 0, UINT8_MAX)
#define DO_SQSHRUN_S(x, sh) do_sat_bhs((int64_t)(x) >> sh, 0, UINT16_MAX)
#define DO_SQSHRUN_D(x, sh) \
    do_sat_bhs((int64_t)(x) >> (sh < 64 ? sh : 63), 0, UINT32_MAX)

DO_SHRNB(sve2_sqshrunb_h, int16_t, uint8_t, DO_SQSHRUN_H)
DO_SHRNB(sve2_sqshrunb_s, int32_t, uint16_t, DO_SQSHRUN_S)
DO_SHRNB(sve2_sqshrunb_d, int64_t, uint32_t, DO_SQSHRUN_D)

DO_SHRNT(sve2_sqshrunt_h, int16_t, uint8_t, H1_2, H1, DO_SQSHRUN_H)
DO_SHRNT(sve2_sqshrunt_s, int32_t, uint16_t, H1_4, H1_2, DO_SQSHRUN_S)
DO_SHRNT(sve2_sqshrunt_d, int64_t, uint32_t, H1_8, H1_4, DO_SQSHRUN_D)

#define DO_SQRSHRUN_H(x, sh) do_sat_bhs(do_srshr(x, sh), 0, UINT8_MAX)
#define DO_SQRSHRUN_S(x, sh) do_sat_bhs(do_srshr(x, sh), 0, UINT16_MAX)
#define DO_SQRSHRUN_D(x, sh) do_sat_bhs(do_srshr(x, sh), 0, UINT32_MAX)

DO_SHRNB(sve2_sqrshrunb_h, int16_t, uint8_t, DO_SQRSHRUN_H)
DO_SHRNB(sve2_sqrshrunb_s, int32_t, uint16_t, DO_SQRSHRUN_S)
DO_SHRNB(sve2_sqrshrunb_d, int64_t, uint32_t, DO_SQRSHRUN_D)

DO_SHRNT(sve2_sqrshrunt_h, int16_t, uint8_t, H1_2, H1, DO_SQRSHRUN_H)
DO_SHRNT(sve2_sqrshrunt_s, int32_t, uint16_t, H1_4, H1_2, DO_SQRSHRUN_S)
DO_SHRNT(sve2_sqrshrunt_d, int64_t, uint32_t, H1_8, H1_4, DO_SQRSHRUN_D)

#define DO_SQSHRN_H(x, sh) do_sat_bhs(x >> sh, INT8_MIN, INT8_MAX)
#define DO_SQSHRN_S(x, sh) do_sat_bhs(x >> sh, INT16_MIN, INT16_MAX)
#define DO_SQSHRN_D(x, sh) do_sat_bhs(x >> sh, INT32_MIN, INT32_MAX)

DO_SHRNB(sve2_sqshrnb_h, int16_t, uint8_t, DO_SQSHRN_H)
DO_SHRNB(sve2_sqshrnb_s, int32_t, uint16_t, DO_SQSHRN_S)
DO_SHRNB(sve2_sqshrnb_d, int64_t, uint32_t, DO_SQSHRN_D)

DO_SHRNT(sve2_sqshrnt_h, int16_t, uint8_t, H1_2, H1, DO_SQSHRN_H)
DO_SHRNT(sve2_sqshrnt_s, int32_t, uint16_t, H1_4, H1_2, DO_SQSHRN_S)
DO_SHRNT(sve2_sqshrnt_d, int64_t, uint32_t, H1_8, H1_4, DO_SQSHRN_D)

#define DO_SQRSHRN_H(x, sh) do_sat_bhs(do_srshr(x, sh), INT8_MIN, INT8_MAX)
#define DO_SQRSHRN_S(x, sh) do_sat_bhs(do_srshr(x, sh), INT16_MIN, INT16_MAX)
#define DO_SQRSHRN_D(x, sh) do_sat_bhs(do_srshr(x, sh), INT32_MIN, INT32_MAX)

DO_SHRNB(sve2_sqrshrnb_h, int16_t, uint8_t, DO_SQRSHRN_H)
DO_SHRNB(sve2_sqrshrnb_s, int32_t, uint16_t, DO_SQRSHRN_S)
DO_SHRNB(sve2_sqrshrnb_d, int64_t, uint32_t, DO_SQRSHRN_D)

DO_SHRNT(sve2_sqrshrnt_h, int16_t, uint8_t, H1_2, H1, DO_SQRSHRN_H)
DO_SHRNT(sve2_sqrshrnt_s, int32_t, uint16_t, H1_4, H1_2, DO_SQRSHRN_S)
DO_SHRNT(sve2_sqrshrnt_d, int64_t, uint32_t, H1_8, H1_4, DO_SQRSHRN_D)

#define DO_UQSHRN_H(x, sh) MIN(x >> sh, UINT8_MAX)
#define DO_UQSHRN_S(x, sh) MIN(x >> sh, UINT16_MAX)
#define DO_UQSHRN_D(x, sh) MIN(x >> sh, UINT32_MAX)

DO_SHRNB(sve2_uqshrnb_h, uint16_t, uint8_t, DO_UQSHRN_H)
DO_SHRNB(sve2_uqshrnb_s, uint32_t, uint16_t, DO_UQSHRN_S)
DO_SHRNB(sve2_uqshrnb_d, uint64_t, uint32_t, DO_UQSHRN_D)

DO_SHRNT(sve2_uqshrnt_h, uint16_t, uint8_t, H1_2, H1, DO_UQSHRN_H)
DO_SHRNT(sve2_uqshrnt_s, uint32_t, uint16_t, H1_4, H1_2, DO_UQSHRN_S)
DO_SHRNT(sve2_uqshrnt_d, uint64_t, uint32_t, H1_8, H1_4, DO_UQSHRN_D)

#define DO_UQRSHRN_H(x, sh) MIN(do_urshr(x, sh), UINT8_MAX)
#define DO_UQRSHRN_S(x, sh) MIN(do_urshr(x, sh), UINT16_MAX)
#define DO_UQRSHRN_D(x, sh) MIN(do_urshr(x, sh), UINT32_MAX)

DO_SHRNB(sve2_uqrshrnb_h, uint16_t, uint8_t, DO_UQRSHRN_H)
DO_SHRNB(sve2_uqrshrnb_s, uint32_t, uint16_t, DO_UQRSHRN_S)
DO_SHRNB(sve2_uqrshrnb_d, uint64_t, uint32_t, DO_UQRSHRN_D)

DO_SHRNT(sve2_uqrshrnt_h, uint16_t, uint8_t, H1_2, H1, DO_UQRSHRN_H)
DO_SHRNT(sve2_uqrshrnt_s, uint32_t, uint16_t, H1_4, H1_2, DO_UQRSHRN_S)
DO_SHRNT(sve2_uqrshrnt_d, uint64_t, uint32_t, H1_8, H1_4, DO_UQRSHRN_D)

#undef DO_SHRNB
#undef DO_SHRNT

#define DO_BINOPNB(NAME, TYPEW, TYPEN, SHIFT, OP)                           \
void HELPER(NAME)(void *vd, void *vn, void *vm, uint32_t desc)              \
{                                                                           \
    intptr_t i, opr_sz = simd_oprsz(desc);                                  \
    for (i = 0; i < opr_sz; i += sizeof(TYPEW)) {                           \
        TYPEW nn = *(TYPEW *)(vn + i);                                      \
        TYPEW mm = *(TYPEW *)(vm + i);                                      \
        *(TYPEW *)(vd + i) = (TYPEN)OP(nn, mm, SHIFT);                      \
    }                                                                       \
}

#define DO_BINOPNT(NAME, TYPEW, TYPEN, SHIFT, HW, HN, OP)                   \
void HELPER(NAME)(void *vd, void *vn, void *vm, uint32_t desc)              \
{                                                                           \
    intptr_t i, opr_sz = simd_oprsz(desc);                                  \
    for (i = 0; i < opr_sz; i += sizeof(TYPEW)) {                           \
        TYPEW nn = *(TYPEW *)(vn + HW(i));                                  \
        TYPEW mm = *(TYPEW *)(vm + HW(i));                                  \
        *(TYPEN *)(vd + HN(i + sizeof(TYPEN))) = OP(nn, mm, SHIFT);         \
    }                                                                       \
}

#define DO_ADDHN(N, M, SH)  ((N + M) >> SH)
#define DO_RADDHN(N, M, SH) ((N + M + ((__typeof(N))1 << (SH - 1))) >> SH)
#define DO_SUBHN(N, M, SH)  ((N - M) >> SH)
#define DO_RSUBHN(N, M, SH) ((N - M + ((__typeof(N))1 << (SH - 1))) >> SH)

DO_BINOPNB(sve2_addhnb_h, uint16_t, uint8_t, 8, DO_ADDHN)
DO_BINOPNB(sve2_addhnb_s, uint32_t, uint16_t, 16, DO_ADDHN)
DO_BINOPNB(sve2_addhnb_d, uint64_t, uint32_t, 32, DO_ADDHN)

DO_BINOPNT(sve2_addhnt_h, uint16_t, uint8_t, 8, H1_2, H1, DO_ADDHN)
DO_BINOPNT(sve2_addhnt_s, uint32_t, uint16_t, 16, H1_4, H1_2, DO_ADDHN)
DO_BINOPNT(sve2_addhnt_d, uint64_t, uint32_t, 32, H1_8, H1_4, DO_ADDHN)

DO_BINOPNB(sve2_raddhnb_h, uint16_t, uint8_t, 8, DO_RADDHN)
DO_BINOPNB(sve2_raddhnb_s, uint32_t, uint16_t, 16, DO_RADDHN)
DO_BINOPNB(sve2_raddhnb_d, uint64_t, uint32_t, 32, DO_RADDHN)

DO_BINOPNT(sve2_raddhnt_h, uint16_t, uint8_t, 8, H1_2, H1, DO_RADDHN)
DO_BINOPNT(sve2_raddhnt_s, uint32_t, uint16_t, 16, H1_4, H1_2, DO_RADDHN)
DO_BINOPNT(sve2_raddhnt_d, uint64_t, uint32_t, 32, H1_8, H1_4, DO_RADDHN)

DO_BINOPNB(sve2_subhnb_h, uint16_t, uint8_t, 8, DO_SUBHN)
DO_BINOPNB(sve2_subhnb_s, uint32_t, uint16_t, 16, DO_SUBHN)
DO_BINOPNB(sve2_subhnb_d, uint64_t, uint32_t, 32, DO_SUBHN)

DO_BINOPNT(sve2_subhnt_h, uint16_t, uint8_t, 8, H1_2, H1, DO_SUBHN)
DO_BINOPNT(sve2_subhnt_s, uint32_t, uint16_t, 16, H1_4, H1_2, DO_SUBHN)
DO_BINOPNT(sve2_subhnt_d, uint64_t, uint32_t, 32, H1_8, H1_4, DO_SUBHN)

DO_BINOPNB(sve2_rsubhnb_h, uint16_t, uint8_t, 8, DO_RSUBHN)
DO_BINOPNB(sve2_rsubhnb_s, uint32_t, uint16_t, 16, DO_RSUBHN)
DO_BINOPNB(sve2_rsubhnb_d, uint64_t, uint32_t, 32, DO_RSUBHN)

DO_BINOPNT(sve2_rsubhnt_h, uint16_t, uint8_t, 8, H1_2, H1, DO_RSUBHN)
DO_BINOPNT(sve2_rsubhnt_s, uint32_t, uint16_t, 16, H1_4, H1_2, DO_RSUBHN)
DO_BINOPNT(sve2_rsubhnt_d, uint64_t, uint32_t, 32, H1_8, H1_4, DO_RSUBHN)

#undef DO_RSUBHN
#undef DO_SUBHN
#undef DO_RADDHN
#undef DO_ADDHN

#undef DO_BINOPNB

/* Fully general four-operand expander, controlled by a predicate.
 */
#define DO_ZPZZZ(NAME, TYPE, H, OP)                           \
void HELPER(NAME)(void *vd, void *va, void *vn, void *vm,     \
                  void *vg, uint32_t desc)                    \
{                                                             \
    intptr_t i, opr_sz = simd_oprsz(desc);                    \
    for (i = 0; i < opr_sz; ) {                               \
        uint16_t pg = *(uint16_t *)(vg + H1_2(i >> 3));       \
        do {                                                  \
            if (pg & 1) {                                     \
                TYPE nn = *(TYPE *)(vn + H(i));               \
                TYPE mm = *(TYPE *)(vm + H(i));               \
                TYPE aa = *(TYPE *)(va + H(i));               \
                *(TYPE *)(vd + H(i)) = OP(aa, nn, mm);        \
            }                                                 \
            i += sizeof(TYPE), pg >>= sizeof(TYPE);           \
        } while (i & 15);                                     \
    }                                                         \
}

/* Similarly, specialized for 64-bit operands.  */
#define DO_ZPZZZ_D(NAME, TYPE, OP)                            \
void HELPER(NAME)(void *vd, void *va, void *vn, void *vm,     \
                  void *vg, uint32_t desc)                    \
{                                                             \
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;                \
    TYPE *d = vd, *a = va, *n = vn, *m = vm;                  \
    uint8_t *pg = vg;                                         \
    for (i = 0; i < opr_sz; i += 1) {                         \
        if (pg[H1(i)] & 1) {                                  \
            TYPE aa = a[i], nn = n[i], mm = m[i];             \
            d[i] = OP(aa, nn, mm);                            \
        }                                                     \
    }                                                         \
}

#define DO_MLA(A, N, M)  (A + N * M)
#define DO_MLS(A, N, M)  (A - N * M)

DO_ZPZZZ(sve_mla_b, uint8_t, H1, DO_MLA)
DO_ZPZZZ(sve_mls_b, uint8_t, H1, DO_MLS)

DO_ZPZZZ(sve_mla_h, uint16_t, H1_2, DO_MLA)
DO_ZPZZZ(sve_mls_h, uint16_t, H1_2, DO_MLS)

DO_ZPZZZ(sve_mla_s, uint32_t, H1_4, DO_MLA)
DO_ZPZZZ(sve_mls_s, uint32_t, H1_4, DO_MLS)

DO_ZPZZZ_D(sve_mla_d, uint64_t, DO_MLA)
DO_ZPZZZ_D(sve_mls_d, uint64_t, DO_MLS)

#undef DO_MLA
#undef DO_MLS
#undef DO_ZPZZZ
#undef DO_ZPZZZ_D

void HELPER(sve_index_b)(void *vd, uint32_t start,
                         uint32_t incr, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    uint8_t *d = vd;
    for (i = 0; i < opr_sz; i += 1) {
        d[H1(i)] = start + i * incr;
    }
}

void HELPER(sve_index_h)(void *vd, uint32_t start,
                         uint32_t incr, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 2;
    uint16_t *d = vd;
    for (i = 0; i < opr_sz; i += 1) {
        d[H2(i)] = start + i * incr;
    }
}

void HELPER(sve_index_s)(void *vd, uint32_t start,
                         uint32_t incr, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 4;
    uint32_t *d = vd;
    for (i = 0; i < opr_sz; i += 1) {
        d[H4(i)] = start + i * incr;
    }
}

void HELPER(sve_index_d)(void *vd, uint64_t start,
                         uint64_t incr, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd;
    for (i = 0; i < opr_sz; i += 1) {
        d[i] = start + i * incr;
    }
}

void HELPER(sve_adr_p32)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 4;
    uint32_t sh = simd_data(desc);
    uint32_t *d = vd, *n = vn, *m = vm;
    for (i = 0; i < opr_sz; i += 1) {
        d[i] = n[i] + (m[i] << sh);
    }
}

void HELPER(sve_adr_p64)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t sh = simd_data(desc);
    uint64_t *d = vd, *n = vn, *m = vm;
    for (i = 0; i < opr_sz; i += 1) {
        d[i] = n[i] + (m[i] << sh);
    }
}

void HELPER(sve_adr_s32)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t sh = simd_data(desc);
    uint64_t *d = vd, *n = vn, *m = vm;
    for (i = 0; i < opr_sz; i += 1) {
        d[i] = n[i] + ((uint64_t)(int32_t)m[i] << sh);
    }
}

void HELPER(sve_adr_u32)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t sh = simd_data(desc);
    uint64_t *d = vd, *n = vn, *m = vm;
    for (i = 0; i < opr_sz; i += 1) {
        d[i] = n[i] + ((uint64_t)(uint32_t)m[i] << sh);
    }
}

void HELPER(sve_fexpa_h)(void *vd, void *vn, uint32_t desc)
{
    /* These constants are cut-and-paste directly from the ARM pseudocode.  */
    static const uint16_t coeff[] = {
        0x0000, 0x0016, 0x002d, 0x0045, 0x005d, 0x0075, 0x008e, 0x00a8,
        0x00c2, 0x00dc, 0x00f8, 0x0114, 0x0130, 0x014d, 0x016b, 0x0189,
        0x01a8, 0x01c8, 0x01e8, 0x0209, 0x022b, 0x024e, 0x0271, 0x0295,
        0x02ba, 0x02e0, 0x0306, 0x032e, 0x0356, 0x037f, 0x03a9, 0x03d4,
    };
    intptr_t i, opr_sz = simd_oprsz(desc) / 2;
    uint16_t *d = vd, *n = vn;

    for (i = 0; i < opr_sz; i++) {
        uint16_t nn = n[i];
        intptr_t idx = extract32(nn, 0, 5);
        uint16_t exp = extract32(nn, 5, 5);
        d[i] = coeff[idx] | (exp << 10);
    }
}

void HELPER(sve_fexpa_s)(void *vd, void *vn, uint32_t desc)
{
    /* These constants are cut-and-paste directly from the ARM pseudocode.  */
    static const uint32_t coeff[] = {
        0x000000, 0x0164d2, 0x02cd87, 0x043a29,
        0x05aac3, 0x071f62, 0x08980f, 0x0a14d5,
        0x0b95c2, 0x0d1adf, 0x0ea43a, 0x1031dc,
        0x11c3d3, 0x135a2b, 0x14f4f0, 0x16942d,
        0x1837f0, 0x19e046, 0x1b8d3a, 0x1d3eda,
        0x1ef532, 0x20b051, 0x227043, 0x243516,
        0x25fed7, 0x27cd94, 0x29a15b, 0x2b7a3a,
        0x2d583f, 0x2f3b79, 0x3123f6, 0x3311c4,
        0x3504f3, 0x36fd92, 0x38fbaf, 0x3aff5b,
        0x3d08a4, 0x3f179a, 0x412c4d, 0x4346cd,
        0x45672a, 0x478d75, 0x49b9be, 0x4bec15,
        0x4e248c, 0x506334, 0x52a81e, 0x54f35b,
        0x5744fd, 0x599d16, 0x5bfbb8, 0x5e60f5,
        0x60ccdf, 0x633f89, 0x65b907, 0x68396a,
        0x6ac0c7, 0x6d4f30, 0x6fe4ba, 0x728177,
        0x75257d, 0x77d0df, 0x7a83b3, 0x7d3e0c,
    };
    intptr_t i, opr_sz = simd_oprsz(desc) / 4;
    uint32_t *d = vd, *n = vn;

    for (i = 0; i < opr_sz; i++) {
        uint32_t nn = n[i];
        intptr_t idx = extract32(nn, 0, 6);
        uint32_t exp = extract32(nn, 6, 8);
        d[i] = coeff[idx] | (exp << 23);
    }
}

void HELPER(sve_fexpa_d)(void *vd, void *vn, uint32_t desc)
{
    /* These constants are cut-and-paste directly from the ARM pseudocode.  */
    static const uint64_t coeff[] = {
        0x0000000000000ull, 0x02C9A3E778061ull, 0x059B0D3158574ull,
        0x0874518759BC8ull, 0x0B5586CF9890Full, 0x0E3EC32D3D1A2ull,
        0x11301D0125B51ull, 0x1429AAEA92DE0ull, 0x172B83C7D517Bull,
        0x1A35BEB6FCB75ull, 0x1D4873168B9AAull, 0x2063B88628CD6ull,
        0x2387A6E756238ull, 0x26B4565E27CDDull, 0x29E9DF51FDEE1ull,
        0x2D285A6E4030Bull, 0x306FE0A31B715ull, 0x33C08B26416FFull,
        0x371A7373AA9CBull, 0x3A7DB34E59FF7ull, 0x3DEA64C123422ull,
        0x4160A21F72E2Aull, 0x44E086061892Dull, 0x486A2B5C13CD0ull,
        0x4BFDAD5362A27ull, 0x4F9B2769D2CA7ull, 0x5342B569D4F82ull,
        0x56F4736B527DAull, 0x5AB07DD485429ull, 0x5E76F15AD2148ull,
        0x6247EB03A5585ull, 0x6623882552225ull, 0x6A09E667F3BCDull,
        0x6DFB23C651A2Full, 0x71F75E8EC5F74ull, 0x75FEB564267C9ull,
        0x7A11473EB0187ull, 0x7E2F336CF4E62ull, 0x82589994CCE13ull,
        0x868D99B4492EDull, 0x8ACE5422AA0DBull, 0x8F1AE99157736ull,
        0x93737B0CDC5E5ull, 0x97D829FDE4E50ull, 0x9C49182A3F090ull,
        0xA0C667B5DE565ull, 0xA5503B23E255Dull, 0xA9E6B5579FDBFull,
        0xAE89F995AD3ADull, 0xB33A2B84F15FBull, 0xB7F76F2FB5E47ull,
        0xBCC1E904BC1D2ull, 0xC199BDD85529Cull, 0xC67F12E57D14Bull,
        0xCB720DCEF9069ull, 0xD072D4A07897Cull, 0xD5818DCFBA487ull,
        0xDA9E603DB3285ull, 0xDFC97337B9B5Full, 0xE502EE78B3FF6ull,
        0xEA4AFA2A490DAull, 0xEFA1BEE615A27ull, 0xF50765B6E4540ull,
        0xFA7C1819E90D8ull,
    };
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd, *n = vn;

    for (i = 0; i < opr_sz; i++) {
        uint64_t nn = n[i];
        intptr_t idx = extract32(nn, 0, 6);
        uint64_t exp = extract32(nn, 6, 11);
        d[i] = coeff[idx] | (exp << 52);
    }
}

void HELPER(sve_ftssel_h)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 2;
    bool fpcr_ah = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint16_t *d = vd, *n = vn, *m = vm;
    for (i = 0; i < opr_sz; i += 1) {
        uint16_t nn = n[i];
        uint16_t mm = m[i];
        if (mm & 1) {
            nn = float16_one;
        }
        if (mm & 2) {
            nn = float16_maybe_ah_chs(nn, fpcr_ah);
        }
        d[i] = nn;
    }
}

void HELPER(sve_ftssel_s)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 4;
    bool fpcr_ah = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint32_t *d = vd, *n = vn, *m = vm;
    for (i = 0; i < opr_sz; i += 1) {
        uint32_t nn = n[i];
        uint32_t mm = m[i];
        if (mm & 1) {
            nn = float32_one;
        }
        if (mm & 2) {
            nn = float32_maybe_ah_chs(nn, fpcr_ah);
        }
        d[i] = nn;
    }
}

void HELPER(sve_ftssel_d)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    bool fpcr_ah = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint64_t *d = vd, *n = vn, *m = vm;
    for (i = 0; i < opr_sz; i += 1) {
        uint64_t nn = n[i];
        uint64_t mm = m[i];
        if (mm & 1) {
            nn = float64_one;
        }
        if (mm & 2) {
            nn = float64_maybe_ah_chs(nn, fpcr_ah);
        }
        d[i] = nn;
    }
}

/*
 * Signed saturating addition with scalar operand.
 */

void HELPER(sve_sqaddi_b)(void *d, void *a, int32_t b, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz; i += sizeof(int8_t)) {
        *(int8_t *)(d + i) = DO_SQADD_B(b, *(int8_t *)(a + i));
    }
}

void HELPER(sve_sqaddi_h)(void *d, void *a, int32_t b, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz; i += sizeof(int16_t)) {
        *(int16_t *)(d + i) = DO_SQADD_H(b, *(int16_t *)(a + i));
    }
}

void HELPER(sve_sqaddi_s)(void *d, void *a, int64_t b, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz; i += sizeof(int32_t)) {
        *(int32_t *)(d + i) = DO_SQADD_S(b, *(int32_t *)(a + i));
    }
}

void HELPER(sve_sqaddi_d)(void *d, void *a, int64_t b, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz; i += sizeof(int64_t)) {
        *(int64_t *)(d + i) = do_sqadd_d(b, *(int64_t *)(a + i));
    }
}

/*
 * Unsigned saturating addition with scalar operand.
 */

void HELPER(sve_uqaddi_b)(void *d, void *a, int32_t b, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz; i += sizeof(uint8_t)) {
        *(uint8_t *)(d + i) = DO_UQADD_B(b, *(uint8_t *)(a + i));
    }
}

void HELPER(sve_uqaddi_h)(void *d, void *a, int32_t b, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz; i += sizeof(uint16_t)) {
        *(uint16_t *)(d + i) = DO_UQADD_H(b, *(uint16_t *)(a + i));
    }
}

void HELPER(sve_uqaddi_s)(void *d, void *a, int64_t b, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz; i += sizeof(uint32_t)) {
        *(uint32_t *)(d + i) = DO_UQADD_S(b, *(uint32_t *)(a + i));
    }
}

void HELPER(sve_uqaddi_d)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(uint64_t *)(d + i) = do_uqadd_d(b, *(uint64_t *)(a + i));
    }
}

void HELPER(sve_uqsubi_d)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        *(uint64_t *)(d + i) = do_uqsub_d(*(uint64_t *)(a + i), b);
    }
}

/* Two operand predicated copy immediate with merge.  All valid immediates
 * can fit within 17 signed bits in the simd_data field.
 */
void HELPER(sve_cpy_m_b)(void *vd, void *vn, void *vg,
                         uint64_t mm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd, *n = vn;
    uint8_t *pg = vg;

    mm = dup_const(MO_8, mm);
    for (i = 0; i < opr_sz; i += 1) {
        uint64_t nn = n[i];
        uint64_t pp = expand_pred_b(pg[H1(i)]);
        d[i] = (mm & pp) | (nn & ~pp);
    }
}

void HELPER(sve_cpy_m_h)(void *vd, void *vn, void *vg,
                         uint64_t mm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd, *n = vn;
    uint8_t *pg = vg;

    mm = dup_const(MO_16, mm);
    for (i = 0; i < opr_sz; i += 1) {
        uint64_t nn = n[i];
        uint64_t pp = expand_pred_h(pg[H1(i)]);
        d[i] = (mm & pp) | (nn & ~pp);
    }
}

void HELPER(sve_cpy_m_s)(void *vd, void *vn, void *vg,
                         uint64_t mm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd, *n = vn;
    uint8_t *pg = vg;

    mm = dup_const(MO_32, mm);
    for (i = 0; i < opr_sz; i += 1) {
        uint64_t nn = n[i];
        uint64_t pp = expand_pred_s(pg[H1(i)]);
        d[i] = (mm & pp) | (nn & ~pp);
    }
}

void HELPER(sve_cpy_m_d)(void *vd, void *vn, void *vg,
                         uint64_t mm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd, *n = vn;
    uint8_t *pg = vg;

    for (i = 0; i < opr_sz; i += 1) {
        uint64_t nn = n[i];
        d[i] = (pg[H1(i)] & 1 ? mm : nn);
    }
}

void HELPER(sve_cpy_z_b)(void *vd, void *vg, uint64_t val, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd;
    uint8_t *pg = vg;

    val = dup_const(MO_8, val);
    for (i = 0; i < opr_sz; i += 1) {
        d[i] = val & expand_pred_b(pg[H1(i)]);
    }
}

void HELPER(sve_cpy_z_h)(void *vd, void *vg, uint64_t val, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd;
    uint8_t *pg = vg;

    val = dup_const(MO_16, val);
    for (i = 0; i < opr_sz; i += 1) {
        d[i] = val & expand_pred_h(pg[H1(i)]);
    }
}

void HELPER(sve_cpy_z_s)(void *vd, void *vg, uint64_t val, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd;
    uint8_t *pg = vg;

    val = dup_const(MO_32, val);
    for (i = 0; i < opr_sz; i += 1) {
        d[i] = val & expand_pred_s(pg[H1(i)]);
    }
}

void HELPER(sve_cpy_z_d)(void *vd, void *vg, uint64_t val, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd;
    uint8_t *pg = vg;

    for (i = 0; i < opr_sz; i += 1) {
        d[i] = (pg[H1(i)] & 1 ? val : 0);
    }
}

/* Big-endian hosts need to frob the byte indices.  If the copy
 * happens to be 8-byte aligned, then no frobbing necessary.
 */
static void swap_memmove(void *vd, void *vs, size_t n)
{
    uintptr_t d = (uintptr_t)vd;
    uintptr_t s = (uintptr_t)vs;
    uintptr_t o = (d | s | n) & 7;
    size_t i;

#if !HOST_BIG_ENDIAN
    o = 0;
#endif
    switch (o) {
    case 0:
        memmove(vd, vs, n);
        break;

    case 4:
        if (d < s || d >= s + n) {
            for (i = 0; i < n; i += 4) {
                *(uint32_t *)H1_4(d + i) = *(uint32_t *)H1_4(s + i);
            }
        } else {
            for (i = n; i > 0; ) {
                i -= 4;
                *(uint32_t *)H1_4(d + i) = *(uint32_t *)H1_4(s + i);
            }
        }
        break;

    case 2:
    case 6:
        if (d < s || d >= s + n) {
            for (i = 0; i < n; i += 2) {
                *(uint16_t *)H1_2(d + i) = *(uint16_t *)H1_2(s + i);
            }
        } else {
            for (i = n; i > 0; ) {
                i -= 2;
                *(uint16_t *)H1_2(d + i) = *(uint16_t *)H1_2(s + i);
            }
        }
        break;

    default:
        if (d < s || d >= s + n) {
            for (i = 0; i < n; i++) {
                *(uint8_t *)H1(d + i) = *(uint8_t *)H1(s + i);
            }
        } else {
            for (i = n; i > 0; ) {
                i -= 1;
                *(uint8_t *)H1(d + i) = *(uint8_t *)H1(s + i);
            }
        }
        break;
    }
}

/* Similarly for memset of 0.  */
static void swap_memzero(void *vd, size_t n)
{
    uintptr_t d = (uintptr_t)vd;
    uintptr_t o = (d | n) & 7;
    size_t i;

    /* Usually, the first bit of a predicate is set, so N is 0.  */
    if (likely(n == 0)) {
        return;
    }

#if !HOST_BIG_ENDIAN
    o = 0;
#endif
    switch (o) {
    case 0:
        memset(vd, 0, n);
        break;

    case 4:
        for (i = 0; i < n; i += 4) {
            *(uint32_t *)H1_4(d + i) = 0;
        }
        break;

    case 2:
    case 6:
        for (i = 0; i < n; i += 2) {
            *(uint16_t *)H1_2(d + i) = 0;
        }
        break;

    default:
        for (i = 0; i < n; i++) {
            *(uint8_t *)H1(d + i) = 0;
        }
        break;
    }
}

void HELPER(sve_ext)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t opr_sz = simd_oprsz(desc);
    size_t n_ofs = simd_data(desc);
    size_t n_siz = opr_sz - n_ofs;

    if (vd != vm) {
        swap_memmove(vd, vn + n_ofs, n_siz);
        swap_memmove(vd + n_siz, vm, n_ofs);
    } else if (vd != vn) {
        swap_memmove(vd + n_siz, vd, n_ofs);
        swap_memmove(vd, vn + n_ofs, n_siz);
    } else {
        /* vd == vn == vm.  Need temp space.  */
        ARMVectorReg tmp;
        swap_memmove(&tmp, vm, n_ofs);
        swap_memmove(vd, vd + n_ofs, n_siz);
        memcpy(vd + n_siz, &tmp, n_ofs);
    }
}

#define DO_INSR(NAME, TYPE, H) \
void HELPER(NAME)(void *vd, void *vn, uint64_t val, uint32_t desc) \
{                                                                  \
    intptr_t opr_sz = simd_oprsz(desc);                            \
    swap_memmove(vd + sizeof(TYPE), vn, opr_sz - sizeof(TYPE));    \
    *(TYPE *)(vd + H(0)) = val;                                    \
}

DO_INSR(sve_insr_b, uint8_t, H1)
DO_INSR(sve_insr_h, uint16_t, H1_2)
DO_INSR(sve_insr_s, uint32_t, H1_4)
DO_INSR(sve_insr_d, uint64_t, H1_8)

#undef DO_INSR

void HELPER(sve_rev_b)(void *vd, void *vn, uint32_t desc)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc);
    for (i = 0, j = opr_sz - 8; i < opr_sz / 2; i += 8, j -= 8) {
        uint64_t f = *(uint64_t *)(vn + i);
        uint64_t b = *(uint64_t *)(vn + j);
        *(uint64_t *)(vd + i) = bswap64(b);
        *(uint64_t *)(vd + j) = bswap64(f);
    }
}

void HELPER(sve_rev_h)(void *vd, void *vn, uint32_t desc)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc);
    for (i = 0, j = opr_sz - 8; i < opr_sz / 2; i += 8, j -= 8) {
        uint64_t f = *(uint64_t *)(vn + i);
        uint64_t b = *(uint64_t *)(vn + j);
        *(uint64_t *)(vd + i) = hswap64(b);
        *(uint64_t *)(vd + j) = hswap64(f);
    }
}

void HELPER(sve_rev_s)(void *vd, void *vn, uint32_t desc)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc);
    for (i = 0, j = opr_sz - 8; i < opr_sz / 2; i += 8, j -= 8) {
        uint64_t f = *(uint64_t *)(vn + i);
        uint64_t b = *(uint64_t *)(vn + j);
        *(uint64_t *)(vd + i) = rol64(b, 32);
        *(uint64_t *)(vd + j) = rol64(f, 32);
    }
}

void HELPER(sve_rev_d)(void *vd, void *vn, uint32_t desc)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc);
    for (i = 0, j = opr_sz - 8; i < opr_sz / 2; i += 8, j -= 8) {
        uint64_t f = *(uint64_t *)(vn + i);
        uint64_t b = *(uint64_t *)(vn + j);
        *(uint64_t *)(vd + i) = b;
        *(uint64_t *)(vd + j) = f;
    }
}

typedef void tb_impl_fn(void *, void *, void *, void *, uintptr_t, bool);

static inline void do_tbl1(void *vd, void *vn, void *vm, uint32_t desc,
                           bool is_tbx, tb_impl_fn *fn)
{
    ARMVectorReg scratch;
    uintptr_t oprsz = simd_oprsz(desc);

    if (unlikely(vd == vn)) {
        vn = memcpy(&scratch, vn, oprsz);
    }

    fn(vd, vn, NULL, vm, oprsz, is_tbx);
}

static inline void do_tbl2(void *vd, void *vn0, void *vn1, void *vm,
                           uint32_t desc, bool is_tbx, tb_impl_fn *fn)
{
    ARMVectorReg scratch;
    uintptr_t oprsz = simd_oprsz(desc);

    if (unlikely(vd == vn0)) {
        vn0 = memcpy(&scratch, vn0, oprsz);
        if (vd == vn1) {
            vn1 = vn0;
        }
    } else if (unlikely(vd == vn1)) {
        vn1 = memcpy(&scratch, vn1, oprsz);
    }

    fn(vd, vn0, vn1, vm, oprsz, is_tbx);
}

#define DO_TB(SUFF, TYPE, H)                                            \
static inline void do_tb_##SUFF(void *vd, void *vt0, void *vt1,         \
                                void *vm, uintptr_t oprsz, bool is_tbx) \
{                                                                       \
    TYPE *d = vd, *tbl0 = vt0, *tbl1 = vt1, *indexes = vm;              \
    uintptr_t i, nelem = oprsz / sizeof(TYPE);                          \
    for (i = 0; i < nelem; ++i) {                                       \
        TYPE index = indexes[H1(i)], val = 0;                           \
        if (index < nelem) {                                            \
            val = tbl0[H(index)];                                       \
        } else {                                                        \
            index -= nelem;                                             \
            if (tbl1 && index < nelem) {                                \
                val = tbl1[H(index)];                                   \
            } else if (is_tbx) {                                        \
                continue;                                               \
            }                                                           \
        }                                                               \
        d[H(i)] = val;                                                  \
    }                                                                   \
}                                                                       \
void HELPER(sve_tbl_##SUFF)(void *vd, void *vn, void *vm, uint32_t desc) \
{                                                                       \
    do_tbl1(vd, vn, vm, desc, false, do_tb_##SUFF);                     \
}                                                                       \
void HELPER(sve2_tbl_##SUFF)(void *vd, void *vn0, void *vn1,            \
                             void *vm, uint32_t desc)                   \
{                                                                       \
    do_tbl2(vd, vn0, vn1, vm, desc, false, do_tb_##SUFF);               \
}                                                                       \
void HELPER(sve2_tbx_##SUFF)(void *vd, void *vn, void *vm, uint32_t desc) \
{                                                                       \
    do_tbl1(vd, vn, vm, desc, true, do_tb_##SUFF);                      \
}

DO_TB(b, uint8_t, H1)
DO_TB(h, uint16_t, H2)
DO_TB(s, uint32_t, H4)
DO_TB(d, uint64_t, H8)

#undef DO_TB

#define DO_UNPK(NAME, TYPED, TYPES, HD, HS) \
void HELPER(NAME)(void *vd, void *vn, uint32_t desc)           \
{                                                              \
    intptr_t i, opr_sz = simd_oprsz(desc);                     \
    TYPED *d = vd;                                             \
    TYPES *n = vn;                                             \
    ARMVectorReg tmp;                                          \
    if (unlikely(vn - vd < opr_sz)) {                          \
        n = memcpy(&tmp, n, opr_sz / 2);                       \
    }                                                          \
    for (i = 0; i < opr_sz / sizeof(TYPED); i++) {             \
        d[HD(i)] = n[HS(i)];                                   \
    }                                                          \
}

DO_UNPK(sve_sunpk_h, int16_t, int8_t, H2, H1)
DO_UNPK(sve_sunpk_s, int32_t, int16_t, H4, H2)
DO_UNPK(sve_sunpk_d, int64_t, int32_t, H8, H4)

DO_UNPK(sve_uunpk_h, uint16_t, uint8_t, H2, H1)
DO_UNPK(sve_uunpk_s, uint32_t, uint16_t, H4, H2)
DO_UNPK(sve_uunpk_d, uint64_t, uint32_t, H8, H4)

#undef DO_UNPK

/* Mask of bits included in the even numbered predicates of width esz.
 * We also use this for expand_bits/compress_bits, and so extend the
 * same pattern out to 16-bit units.
 */
static const uint64_t even_bit_esz_masks[5] = {
    0x5555555555555555ull,
    0x3333333333333333ull,
    0x0f0f0f0f0f0f0f0full,
    0x00ff00ff00ff00ffull,
    0x0000ffff0000ffffull,
};

/* Zero-extend units of 2**N bits to units of 2**(N+1) bits.
 * For N==0, this corresponds to the operation that in qemu/bitops.h
 * we call half_shuffle64; this algorithm is from Hacker's Delight,
 * section 7-2 Shuffling Bits.
 */
static uint64_t expand_bits(uint64_t x, int n)
{
    int i;

    x &= 0xffffffffu;
    for (i = 4; i >= n; i--) {
        int sh = 1 << i;
        x = ((x << sh) | x) & even_bit_esz_masks[i];
    }
    return x;
}

/* Compress units of 2**(N+1) bits to units of 2**N bits.
 * For N==0, this corresponds to the operation that in qemu/bitops.h
 * we call half_unshuffle64; this algorithm is from Hacker's Delight,
 * section 7-2 Shuffling Bits, where it is called an inverse half shuffle.
 */
static uint64_t compress_bits(uint64_t x, int n)
{
    int i;

    for (i = n; i <= 4; i++) {
        int sh = 1 << i;
        x &= even_bit_esz_masks[i];
        x = (x >> sh) | x;
    }
    return x & 0xffffffffu;
}

void HELPER(sve_zip_p)(void *vd, void *vn, void *vm, uint32_t pred_desc)
{
    intptr_t oprsz = FIELD_EX32(pred_desc, PREDDESC, OPRSZ);
    int esz = FIELD_EX32(pred_desc, PREDDESC, ESZ);
    intptr_t high = FIELD_EX32(pred_desc, PREDDESC, DATA);
    int esize = 1 << esz;
    uint64_t *d = vd;
    intptr_t i;

    if (oprsz <= 8) {
        uint64_t nn = *(uint64_t *)vn;
        uint64_t mm = *(uint64_t *)vm;
        int half = 4 * oprsz;

        nn = extract64(nn, high * half, half);
        mm = extract64(mm, high * half, half);
        nn = expand_bits(nn, esz);
        mm = expand_bits(mm, esz);
        d[0] = nn | (mm << esize);
    } else {
        ARMPredicateReg tmp;

        /* We produce output faster than we consume input.
           Therefore we must be mindful of possible overlap.  */
        if (vd == vn) {
            vn = memcpy(&tmp, vn, oprsz);
            if (vd == vm) {
                vm = vn;
            }
        } else if (vd == vm) {
            vm = memcpy(&tmp, vm, oprsz);
        }
        if (high) {
            high = oprsz >> 1;
        }

        if ((oprsz & 7) == 0) {
            uint32_t *n = vn, *m = vm;
            high >>= 2;

            for (i = 0; i < oprsz / 8; i++) {
                uint64_t nn = n[H4(high + i)];
                uint64_t mm = m[H4(high + i)];

                nn = expand_bits(nn, esz);
                mm = expand_bits(mm, esz);
                d[i] = nn | (mm << esize);
            }
        } else {
            uint8_t *n = vn, *m = vm;
            uint16_t *d16 = vd;

            for (i = 0; i < oprsz / 2; i++) {
                uint16_t nn = n[H1(high + i)];
                uint16_t mm = m[H1(high + i)];

                nn = expand_bits(nn, esz);
                mm = expand_bits(mm, esz);
                d16[H2(i)] = nn | (mm << esize);
            }
        }
    }
}

void HELPER(sve_uzp_p)(void *vd, void *vn, void *vm, uint32_t pred_desc)
{
    intptr_t oprsz = FIELD_EX32(pred_desc, PREDDESC, OPRSZ);
    int esz = FIELD_EX32(pred_desc, PREDDESC, ESZ);
    int odd = FIELD_EX32(pred_desc, PREDDESC, DATA) << esz;
    uint64_t *d = vd, *n = vn, *m = vm;
    uint64_t l, h;
    intptr_t i;

    if (oprsz <= 8) {
        l = compress_bits(n[0] >> odd, esz);
        h = compress_bits(m[0] >> odd, esz);
        d[0] = l | (h << (4 * oprsz));
    } else {
        ARMPredicateReg tmp_m;
        intptr_t oprsz_16 = oprsz / 16;

        if ((vm - vd) < (uintptr_t)oprsz) {
            m = memcpy(&tmp_m, vm, oprsz);
        }

        for (i = 0; i < oprsz_16; i++) {
            l = n[2 * i + 0];
            h = n[2 * i + 1];
            l = compress_bits(l >> odd, esz);
            h = compress_bits(h >> odd, esz);
            d[i] = l | (h << 32);
        }

        /*
         * For VL which is not a multiple of 512, the results from M do not
         * align nicely with the uint64_t for D.  Put the aligned results
         * from M into TMP_M and then copy it into place afterward.
         */
        if (oprsz & 15) {
            int final_shift = (oprsz & 15) * 2;

            l = n[2 * i + 0];
            h = n[2 * i + 1];
            l = compress_bits(l >> odd, esz);
            h = compress_bits(h >> odd, esz);
            d[i] = l | (h << final_shift);

            for (i = 0; i < oprsz_16; i++) {
                l = m[2 * i + 0];
                h = m[2 * i + 1];
                l = compress_bits(l >> odd, esz);
                h = compress_bits(h >> odd, esz);
                tmp_m.p[i] = l | (h << 32);
            }
            l = m[2 * i + 0];
            h = m[2 * i + 1];
            l = compress_bits(l >> odd, esz);
            h = compress_bits(h >> odd, esz);
            tmp_m.p[i] = l | (h << final_shift);

            swap_memmove(vd + oprsz / 2, &tmp_m, oprsz / 2);
        } else {
            for (i = 0; i < oprsz_16; i++) {
                l = m[2 * i + 0];
                h = m[2 * i + 1];
                l = compress_bits(l >> odd, esz);
                h = compress_bits(h >> odd, esz);
                d[oprsz_16 + i] = l | (h << 32);
            }
        }
    }
}

void HELPER(sve_trn_p)(void *vd, void *vn, void *vm, uint32_t pred_desc)
{
    intptr_t oprsz = FIELD_EX32(pred_desc, PREDDESC, OPRSZ);
    int esz = FIELD_EX32(pred_desc, PREDDESC, ESZ);
    int odd = FIELD_EX32(pred_desc, PREDDESC, DATA);
    uint64_t *d = vd, *n = vn, *m = vm;
    uint64_t mask;
    int shr, shl;
    intptr_t i;

    shl = 1 << esz;
    shr = 0;
    mask = even_bit_esz_masks[esz];
    if (odd) {
        mask <<= shl;
        shr = shl;
        shl = 0;
    }

    for (i = 0; i < DIV_ROUND_UP(oprsz, 8); i++) {
        uint64_t nn = (n[i] & mask) >> shr;
        uint64_t mm = (m[i] & mask) << shl;
        d[i] = nn + mm;
    }
}

/* Reverse units of 2**N bits.  */
static uint64_t reverse_bits_64(uint64_t x, int n)
{
    int i, sh;

    x = bswap64(x);
    for (i = 2, sh = 4; i >= n; i--, sh >>= 1) {
        uint64_t mask = even_bit_esz_masks[i];
        x = ((x & mask) << sh) | ((x >> sh) & mask);
    }
    return x;
}

static uint8_t reverse_bits_8(uint8_t x, int n)
{
    static const uint8_t mask[3] = { 0x55, 0x33, 0x0f };
    int i, sh;

    for (i = 2, sh = 4; i >= n; i--, sh >>= 1) {
        x = ((x & mask[i]) << sh) | ((x >> sh) & mask[i]);
    }
    return x;
}

void HELPER(sve_rev_p)(void *vd, void *vn, uint32_t pred_desc)
{
    intptr_t oprsz = FIELD_EX32(pred_desc, PREDDESC, OPRSZ);
    int esz = FIELD_EX32(pred_desc, PREDDESC, ESZ);
    intptr_t i, oprsz_2 = oprsz / 2;

    if (oprsz <= 8) {
        uint64_t l = *(uint64_t *)vn;
        l = reverse_bits_64(l << (64 - 8 * oprsz), esz);
        *(uint64_t *)vd = l;
    } else if ((oprsz & 15) == 0) {
        for (i = 0; i < oprsz_2; i += 8) {
            intptr_t ih = oprsz - 8 - i;
            uint64_t l = reverse_bits_64(*(uint64_t *)(vn + i), esz);
            uint64_t h = reverse_bits_64(*(uint64_t *)(vn + ih), esz);
            *(uint64_t *)(vd + i) = h;
            *(uint64_t *)(vd + ih) = l;
        }
    } else {
        for (i = 0; i < oprsz_2; i += 1) {
            intptr_t il = H1(i);
            intptr_t ih = H1(oprsz - 1 - i);
            uint8_t l = reverse_bits_8(*(uint8_t *)(vn + il), esz);
            uint8_t h = reverse_bits_8(*(uint8_t *)(vn + ih), esz);
            *(uint8_t *)(vd + il) = h;
            *(uint8_t *)(vd + ih) = l;
        }
    }
}

void HELPER(sve_punpk_p)(void *vd, void *vn, uint32_t pred_desc)
{
    intptr_t oprsz = FIELD_EX32(pred_desc, PREDDESC, OPRSZ);
    intptr_t high = FIELD_EX32(pred_desc, PREDDESC, DATA);
    uint64_t *d = vd;
    intptr_t i;

    if (oprsz <= 8) {
        uint64_t nn = *(uint64_t *)vn;
        int half = 4 * oprsz;

        nn = extract64(nn, high * half, half);
        nn = expand_bits(nn, 0);
        d[0] = nn;
    } else {
        ARMPredicateReg tmp_n;

        /* We produce output faster than we consume input.
           Therefore we must be mindful of possible overlap.  */
        if ((vn - vd) < (uintptr_t)oprsz) {
            vn = memcpy(&tmp_n, vn, oprsz);
        }
        if (high) {
            high = oprsz >> 1;
        }

        if ((oprsz & 7) == 0) {
            uint32_t *n = vn;
            high >>= 2;

            for (i = 0; i < oprsz / 8; i++) {
                uint64_t nn = n[H4(high + i)];
                d[i] = expand_bits(nn, 0);
            }
        } else {
            uint16_t *d16 = vd;
            uint8_t *n = vn;

            for (i = 0; i < oprsz / 2; i++) {
                uint16_t nn = n[H1(high + i)];
                d16[H2(i)] = expand_bits(nn, 0);
            }
        }
    }
}

#define DO_ZIP(NAME, TYPE, H) \
void HELPER(NAME)(void *vd, void *vn, void *vm, uint32_t desc)       \
{                                                                    \
    intptr_t oprsz = simd_oprsz(desc);                               \
    intptr_t odd_ofs = simd_data(desc);                              \
    intptr_t i, oprsz_2 = oprsz / 2;                                 \
    ARMVectorReg tmp_n, tmp_m;                                       \
    /* We produce output faster than we consume input.               \
       Therefore we must be mindful of possible overlap.  */         \
    if (unlikely((vn - vd) < (uintptr_t)oprsz)) {                    \
        vn = memcpy(&tmp_n, vn, oprsz);                              \
    }                                                                \
    if (unlikely((vm - vd) < (uintptr_t)oprsz)) {                    \
        vm = memcpy(&tmp_m, vm, oprsz);                              \
    }                                                                \
    for (i = 0; i < oprsz_2; i += sizeof(TYPE)) {                    \
        *(TYPE *)(vd + H(2 * i + 0)) = *(TYPE *)(vn + odd_ofs + H(i)); \
        *(TYPE *)(vd + H(2 * i + sizeof(TYPE))) =                    \
            *(TYPE *)(vm + odd_ofs + H(i));                          \
    }                                                                \
    if (sizeof(TYPE) == 16 && unlikely(oprsz & 16)) {                \
        memset(vd + oprsz - 16, 0, 16);                              \
    }                                                                \
}

DO_ZIP(sve_zip_b, uint8_t, H1)
DO_ZIP(sve_zip_h, uint16_t, H1_2)
DO_ZIP(sve_zip_s, uint32_t, H1_4)
DO_ZIP(sve_zip_d, uint64_t, H1_8)
DO_ZIP(sve2_zip_q, Int128, )

#define DO_UZP(NAME, TYPE, H) \
void HELPER(NAME)(void *vd, void *vn, void *vm, uint32_t desc)         \
{                                                                      \
    intptr_t oprsz = simd_oprsz(desc);                                 \
    intptr_t odd_ofs = simd_data(desc);                                \
    intptr_t i, p;                                                     \
    ARMVectorReg tmp_m;                                                \
    if (unlikely((vm - vd) < (uintptr_t)oprsz)) {                      \
        vm = memcpy(&tmp_m, vm, oprsz);                                \
    }                                                                  \
    i = 0, p = odd_ofs;                                                \
    do {                                                               \
        *(TYPE *)(vd + H(i)) = *(TYPE *)(vn + H(p));                   \
        i += sizeof(TYPE), p += 2 * sizeof(TYPE);                      \
    } while (p < oprsz);                                               \
    p -= oprsz;                                                        \
    do {                                                               \
        *(TYPE *)(vd + H(i)) = *(TYPE *)(vm + H(p));                   \
        i += sizeof(TYPE), p += 2 * sizeof(TYPE);                      \
    } while (p < oprsz);                                               \
    tcg_debug_assert(i == oprsz);                                      \
}

DO_UZP(sve_uzp_b, uint8_t, H1)
DO_UZP(sve_uzp_h, uint16_t, H1_2)
DO_UZP(sve_uzp_s, uint32_t, H1_4)
DO_UZP(sve_uzp_d, uint64_t, H1_8)
DO_UZP(sve2_uzp_q, Int128, )

#define DO_TRN(NAME, TYPE, H) \
void HELPER(NAME)(void *vd, void *vn, void *vm, uint32_t desc)         \
{                                                                      \
    intptr_t oprsz = simd_oprsz(desc);                                 \
    intptr_t odd_ofs = simd_data(desc);                                \
    intptr_t i;                                                        \
    for (i = 0; i < oprsz; i += 2 * sizeof(TYPE)) {                    \
        TYPE ae = *(TYPE *)(vn + H(i + odd_ofs));                      \
        TYPE be = *(TYPE *)(vm + H(i + odd_ofs));                      \
        *(TYPE *)(vd + H(i + 0)) = ae;                                 \
        *(TYPE *)(vd + H(i + sizeof(TYPE))) = be;                      \
    }                                                                  \
    if (sizeof(TYPE) == 16 && unlikely(oprsz & 16)) {                  \
        memset(vd + oprsz - 16, 0, 16);                                \
    }                                                                  \
}

DO_TRN(sve_trn_b, uint8_t, H1)
DO_TRN(sve_trn_h, uint16_t, H1_2)
DO_TRN(sve_trn_s, uint32_t, H1_4)
DO_TRN(sve_trn_d, uint64_t, H1_8)
DO_TRN(sve2_trn_q, Int128, )

#undef DO_ZIP
#undef DO_UZP
#undef DO_TRN

void HELPER(sve_compact_s)(void *vd, void *vn, void *vg, uint32_t desc)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc) / 4;
    uint32_t *d = vd, *n = vn;
    uint8_t *pg = vg;

    for (i = j = 0; i < opr_sz; i++) {
        if (pg[H1(i / 2)] & (i & 1 ? 0x10 : 0x01)) {
            d[H4(j)] = n[H4(i)];
            j++;
        }
    }
    for (; j < opr_sz; j++) {
        d[H4(j)] = 0;
    }
}

void HELPER(sve_compact_d)(void *vd, void *vn, void *vg, uint32_t desc)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd, *n = vn;
    uint8_t *pg = vg;

    for (i = j = 0; i < opr_sz; i++) {
        if (pg[H1(i)] & 1) {
            d[j] = n[i];
            j++;
        }
    }
    for (; j < opr_sz; j++) {
        d[j] = 0;
    }
}

/* Similar to the ARM LastActiveElement pseudocode function, except the
 * result is multiplied by the element size.  This includes the not found
 * indication; e.g. not found for esz=3 is -8.
 */
int32_t HELPER(sve_last_active_element)(void *vg, uint32_t pred_desc)
{
    intptr_t words = DIV_ROUND_UP(FIELD_EX32(pred_desc, PREDDESC, OPRSZ), 8);
    intptr_t esz = FIELD_EX32(pred_desc, PREDDESC, ESZ);

    return last_active_element(vg, words, esz);
}

void HELPER(sve_splice)(void *vd, void *vn, void *vm, void *vg, uint32_t desc)
{
    intptr_t opr_sz = simd_oprsz(desc) / 8;
    int esz = simd_data(desc);
    uint64_t pg, first_g, last_g, len, mask = pred_esz_masks[esz];
    intptr_t i, first_i, last_i;
    ARMVectorReg tmp;

    first_i = last_i = 0;
    first_g = last_g = 0;

    /* Find the extent of the active elements within VG.  */
    for (i = QEMU_ALIGN_UP(opr_sz, 8) - 8; i >= 0; i -= 8) {
        pg = *(uint64_t *)(vg + i) & mask;
        if (pg) {
            if (last_g == 0) {
                last_g = pg;
                last_i = i;
            }
            first_g = pg;
            first_i = i;
        }
    }

    len = 0;
    if (first_g != 0) {
        first_i = first_i * 8 + ctz64(first_g);
        last_i = last_i * 8 + 63 - clz64(last_g);
        len = last_i - first_i + (1 << esz);
        if (vd == vm) {
            vm = memcpy(&tmp, vm, opr_sz * 8);
        }
        swap_memmove(vd, vn + first_i, len);
    }
    swap_memmove(vd + len, vm, opr_sz * 8 - len);
}

void HELPER(sve_sel_zpzz_b)(void *vd, void *vn, void *vm,
                            void *vg, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd, *n = vn, *m = vm;
    uint8_t *pg = vg;

    for (i = 0; i < opr_sz; i += 1) {
        uint64_t nn = n[i], mm = m[i];
        uint64_t pp = expand_pred_b(pg[H1(i)]);
        d[i] = (nn & pp) | (mm & ~pp);
    }
}

void HELPER(sve_sel_zpzz_h)(void *vd, void *vn, void *vm,
                            void *vg, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd, *n = vn, *m = vm;
    uint8_t *pg = vg;

    for (i = 0; i < opr_sz; i += 1) {
        uint64_t nn = n[i], mm = m[i];
        uint64_t pp = expand_pred_h(pg[H1(i)]);
        d[i] = (nn & pp) | (mm & ~pp);
    }
}

void HELPER(sve_sel_zpzz_s)(void *vd, void *vn, void *vm,
                            void *vg, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd, *n = vn, *m = vm;
    uint8_t *pg = vg;

    for (i = 0; i < opr_sz; i += 1) {
        uint64_t nn = n[i], mm = m[i];
        uint64_t pp = expand_pred_s(pg[H1(i)]);
        d[i] = (nn & pp) | (mm & ~pp);
    }
}

void HELPER(sve_sel_zpzz_d)(void *vd, void *vn, void *vm,
                            void *vg, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd, *n = vn, *m = vm;
    uint8_t *pg = vg;

    for (i = 0; i < opr_sz; i += 1) {
        uint64_t nn = n[i], mm = m[i];
        d[i] = (pg[H1(i)] & 1 ? nn : mm);
    }
}

void HELPER(sve_sel_zpzz_q)(void *vd, void *vn, void *vm,
                            void *vg, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 16;
    Int128 *d = vd, *n = vn, *m = vm;
    uint16_t *pg = vg;

    for (i = 0; i < opr_sz; i += 1) {
        d[i] = (pg[H2(i)] & 1 ? n : m)[i];
    }
}

/* Two operand comparison controlled by a predicate.
 * ??? It is very tempting to want to be able to expand this inline
 * with x86 instructions, e.g.
 *
 *    vcmpeqw    zm, zn, %ymm0
 *    vpmovmskb  %ymm0, %eax
 *    and        $0x5555, %eax
 *    and        pg, %eax
 *
 * or even aarch64, e.g.
 *
 *    // mask = 4000 1000 0400 0100 0040 0010 0004 0001
 *    cmeq       v0.8h, zn, zm
 *    and        v0.8h, v0.8h, mask
 *    addv       h0, v0.8h
 *    and        v0.8b, pg
 *
 * However, coming up with an abstraction that allows vector inputs and
 * a scalar output, and also handles the byte-ordering of sub-uint64_t
 * scalar outputs, is tricky.
 */
#define DO_CMP_PPZZ(NAME, TYPE, OP, H, MASK)                                 \
uint32_t HELPER(NAME)(void *vd, void *vn, void *vm, void *vg, uint32_t desc) \
{                                                                            \
    intptr_t opr_sz = simd_oprsz(desc);                                      \
    uint32_t flags = PREDTEST_INIT;                                          \
    intptr_t i = opr_sz;                                                     \
    do {                                                                     \
        uint64_t out = 0, pg;                                                \
        do {                                                                 \
            i -= sizeof(TYPE), out <<= sizeof(TYPE);                         \
            TYPE nn = *(TYPE *)(vn + H(i));                                  \
            TYPE mm = *(TYPE *)(vm + H(i));                                  \
            out |= nn OP mm;                                                 \
        } while (i & 63);                                                    \
        pg = *(uint64_t *)(vg + (i >> 3)) & MASK;                            \
        out &= pg;                                                           \
        *(uint64_t *)(vd + (i >> 3)) = out;                                  \
        flags = iter_predtest_bwd(out, pg, flags);                           \
    } while (i > 0);                                                         \
    return flags;                                                            \
}

#define DO_CMP_PPZZ_B(NAME, TYPE, OP) \
    DO_CMP_PPZZ(NAME, TYPE, OP, H1,   0xffffffffffffffffull)
#define DO_CMP_PPZZ_H(NAME, TYPE, OP) \
    DO_CMP_PPZZ(NAME, TYPE, OP, H1_2, 0x5555555555555555ull)
#define DO_CMP_PPZZ_S(NAME, TYPE, OP) \
    DO_CMP_PPZZ(NAME, TYPE, OP, H1_4, 0x1111111111111111ull)
#define DO_CMP_PPZZ_D(NAME, TYPE, OP) \
    DO_CMP_PPZZ(NAME, TYPE, OP, H1_8, 0x0101010101010101ull)

DO_CMP_PPZZ_B(sve_cmpeq_ppzz_b, uint8_t,  ==)
DO_CMP_PPZZ_H(sve_cmpeq_ppzz_h, uint16_t, ==)
DO_CMP_PPZZ_S(sve_cmpeq_ppzz_s, uint32_t, ==)
DO_CMP_PPZZ_D(sve_cmpeq_ppzz_d, uint64_t, ==)

DO_CMP_PPZZ_B(sve_cmpne_ppzz_b, uint8_t,  !=)
DO_CMP_PPZZ_H(sve_cmpne_ppzz_h, uint16_t, !=)
DO_CMP_PPZZ_S(sve_cmpne_ppzz_s, uint32_t, !=)
DO_CMP_PPZZ_D(sve_cmpne_ppzz_d, uint64_t, !=)

DO_CMP_PPZZ_B(sve_cmpgt_ppzz_b, int8_t,  >)
DO_CMP_PPZZ_H(sve_cmpgt_ppzz_h, int16_t, >)
DO_CMP_PPZZ_S(sve_cmpgt_ppzz_s, int32_t, >)
DO_CMP_PPZZ_D(sve_cmpgt_ppzz_d, int64_t, >)

DO_CMP_PPZZ_B(sve_cmpge_ppzz_b, int8_t,  >=)
DO_CMP_PPZZ_H(sve_cmpge_ppzz_h, int16_t, >=)
DO_CMP_PPZZ_S(sve_cmpge_ppzz_s, int32_t, >=)
DO_CMP_PPZZ_D(sve_cmpge_ppzz_d, int64_t, >=)

DO_CMP_PPZZ_B(sve_cmphi_ppzz_b, uint8_t,  >)
DO_CMP_PPZZ_H(sve_cmphi_ppzz_h, uint16_t, >)
DO_CMP_PPZZ_S(sve_cmphi_ppzz_s, uint32_t, >)
DO_CMP_PPZZ_D(sve_cmphi_ppzz_d, uint64_t, >)

DO_CMP_PPZZ_B(sve_cmphs_ppzz_b, uint8_t,  >=)
DO_CMP_PPZZ_H(sve_cmphs_ppzz_h, uint16_t, >=)
DO_CMP_PPZZ_S(sve_cmphs_ppzz_s, uint32_t, >=)
DO_CMP_PPZZ_D(sve_cmphs_ppzz_d, uint64_t, >=)

#undef DO_CMP_PPZZ_B
#undef DO_CMP_PPZZ_H
#undef DO_CMP_PPZZ_S
#undef DO_CMP_PPZZ_D
#undef DO_CMP_PPZZ

/* Similar, but the second source is "wide".  */
#define DO_CMP_PPZW(NAME, TYPE, TYPEW, OP, H, MASK)                     \
uint32_t HELPER(NAME)(void *vd, void *vn, void *vm, void *vg, uint32_t desc) \
{                                                                            \
    intptr_t opr_sz = simd_oprsz(desc);                                      \
    uint32_t flags = PREDTEST_INIT;                                          \
    intptr_t i = opr_sz;                                                     \
    do {                                                                     \
        uint64_t out = 0, pg;                                                \
        do {                                                                 \
            TYPEW mm = *(TYPEW *)(vm + i - 8);                               \
            do {                                                             \
                i -= sizeof(TYPE), out <<= sizeof(TYPE);                     \
                TYPE nn = *(TYPE *)(vn + H(i));                              \
                out |= nn OP mm;                                             \
            } while (i & 7);                                                 \
        } while (i & 63);                                                    \
        pg = *(uint64_t *)(vg + (i >> 3)) & MASK;                            \
        out &= pg;                                                           \
        *(uint64_t *)(vd + (i >> 3)) = out;                                  \
        flags = iter_predtest_bwd(out, pg, flags);                           \
    } while (i > 0);                                                         \
    return flags;                                                            \
}

#define DO_CMP_PPZW_B(NAME, TYPE, TYPEW, OP) \
    DO_CMP_PPZW(NAME, TYPE, TYPEW, OP, H1,   0xffffffffffffffffull)
#define DO_CMP_PPZW_H(NAME, TYPE, TYPEW, OP) \
    DO_CMP_PPZW(NAME, TYPE, TYPEW, OP, H1_2, 0x5555555555555555ull)
#define DO_CMP_PPZW_S(NAME, TYPE, TYPEW, OP) \
    DO_CMP_PPZW(NAME, TYPE, TYPEW, OP, H1_4, 0x1111111111111111ull)

DO_CMP_PPZW_B(sve_cmpeq_ppzw_b, int8_t,  uint64_t, ==)
DO_CMP_PPZW_H(sve_cmpeq_ppzw_h, int16_t, uint64_t, ==)
DO_CMP_PPZW_S(sve_cmpeq_ppzw_s, int32_t, uint64_t, ==)

DO_CMP_PPZW_B(sve_cmpne_ppzw_b, int8_t,  uint64_t, !=)
DO_CMP_PPZW_H(sve_cmpne_ppzw_h, int16_t, uint64_t, !=)
DO_CMP_PPZW_S(sve_cmpne_ppzw_s, int32_t, uint64_t, !=)

DO_CMP_PPZW_B(sve_cmpgt_ppzw_b, int8_t,   int64_t, >)
DO_CMP_PPZW_H(sve_cmpgt_ppzw_h, int16_t,  int64_t, >)
DO_CMP_PPZW_S(sve_cmpgt_ppzw_s, int32_t,  int64_t, >)

DO_CMP_PPZW_B(sve_cmpge_ppzw_b, int8_t,   int64_t, >=)
DO_CMP_PPZW_H(sve_cmpge_ppzw_h, int16_t,  int64_t, >=)
DO_CMP_PPZW_S(sve_cmpge_ppzw_s, int32_t,  int64_t, >=)

DO_CMP_PPZW_B(sve_cmphi_ppzw_b, uint8_t,  uint64_t, >)
DO_CMP_PPZW_H(sve_cmphi_ppzw_h, uint16_t, uint64_t, >)
DO_CMP_PPZW_S(sve_cmphi_ppzw_s, uint32_t, uint64_t, >)

DO_CMP_PPZW_B(sve_cmphs_ppzw_b, uint8_t,  uint64_t, >=)
DO_CMP_PPZW_H(sve_cmphs_ppzw_h, uint16_t, uint64_t, >=)
DO_CMP_PPZW_S(sve_cmphs_ppzw_s, uint32_t, uint64_t, >=)

DO_CMP_PPZW_B(sve_cmplt_ppzw_b, int8_t,   int64_t, <)
DO_CMP_PPZW_H(sve_cmplt_ppzw_h, int16_t,  int64_t, <)
DO_CMP_PPZW_S(sve_cmplt_ppzw_s, int32_t,  int64_t, <)

DO_CMP_PPZW_B(sve_cmple_ppzw_b, int8_t,   int64_t, <=)
DO_CMP_PPZW_H(sve_cmple_ppzw_h, int16_t,  int64_t, <=)
DO_CMP_PPZW_S(sve_cmple_ppzw_s, int32_t,  int64_t, <=)

DO_CMP_PPZW_B(sve_cmplo_ppzw_b, uint8_t,  uint64_t, <)
DO_CMP_PPZW_H(sve_cmplo_ppzw_h, uint16_t, uint64_t, <)
DO_CMP_PPZW_S(sve_cmplo_ppzw_s, uint32_t, uint64_t, <)

DO_CMP_PPZW_B(sve_cmpls_ppzw_b, uint8_t,  uint64_t, <=)
DO_CMP_PPZW_H(sve_cmpls_ppzw_h, uint16_t, uint64_t, <=)
DO_CMP_PPZW_S(sve_cmpls_ppzw_s, uint32_t, uint64_t, <=)

#undef DO_CMP_PPZW_B
#undef DO_CMP_PPZW_H
#undef DO_CMP_PPZW_S
#undef DO_CMP_PPZW

/* Similar, but the second source is immediate.  */
#define DO_CMP_PPZI(NAME, TYPE, OP, H, MASK)                         \
uint32_t HELPER(NAME)(void *vd, void *vn, void *vg, uint32_t desc)   \
{                                                                    \
    intptr_t opr_sz = simd_oprsz(desc);                              \
    uint32_t flags = PREDTEST_INIT;                                  \
    TYPE mm = simd_data(desc);                                       \
    intptr_t i = opr_sz;                                             \
    do {                                                             \
        uint64_t out = 0, pg;                                        \
        do {                                                         \
            i -= sizeof(TYPE), out <<= sizeof(TYPE);                 \
            TYPE nn = *(TYPE *)(vn + H(i));                          \
            out |= nn OP mm;                                         \
        } while (i & 63);                                            \
        pg = *(uint64_t *)(vg + (i >> 3)) & MASK;                    \
        out &= pg;                                                   \
        *(uint64_t *)(vd + (i >> 3)) = out;                          \
        flags = iter_predtest_bwd(out, pg, flags);                   \
    } while (i > 0);                                                 \
    return flags;                                                    \
}

#define DO_CMP_PPZI_B(NAME, TYPE, OP) \
    DO_CMP_PPZI(NAME, TYPE, OP, H1,   0xffffffffffffffffull)
#define DO_CMP_PPZI_H(NAME, TYPE, OP) \
    DO_CMP_PPZI(NAME, TYPE, OP, H1_2, 0x5555555555555555ull)
#define DO_CMP_PPZI_S(NAME, TYPE, OP) \
    DO_CMP_PPZI(NAME, TYPE, OP, H1_4, 0x1111111111111111ull)
#define DO_CMP_PPZI_D(NAME, TYPE, OP) \
    DO_CMP_PPZI(NAME, TYPE, OP, H1_8, 0x0101010101010101ull)

DO_CMP_PPZI_B(sve_cmpeq_ppzi_b, uint8_t,  ==)
DO_CMP_PPZI_H(sve_cmpeq_ppzi_h, uint16_t, ==)
DO_CMP_PPZI_S(sve_cmpeq_ppzi_s, uint32_t, ==)
DO_CMP_PPZI_D(sve_cmpeq_ppzi_d, uint64_t, ==)

DO_CMP_PPZI_B(sve_cmpne_ppzi_b, uint8_t,  !=)
DO_CMP_PPZI_H(sve_cmpne_ppzi_h, uint16_t, !=)
DO_CMP_PPZI_S(sve_cmpne_ppzi_s, uint32_t, !=)
DO_CMP_PPZI_D(sve_cmpne_ppzi_d, uint64_t, !=)

DO_CMP_PPZI_B(sve_cmpgt_ppzi_b, int8_t,  >)
DO_CMP_PPZI_H(sve_cmpgt_ppzi_h, int16_t, >)
DO_CMP_PPZI_S(sve_cmpgt_ppzi_s, int32_t, >)
DO_CMP_PPZI_D(sve_cmpgt_ppzi_d, int64_t, >)

DO_CMP_PPZI_B(sve_cmpge_ppzi_b, int8_t,  >=)
DO_CMP_PPZI_H(sve_cmpge_ppzi_h, int16_t, >=)
DO_CMP_PPZI_S(sve_cmpge_ppzi_s, int32_t, >=)
DO_CMP_PPZI_D(sve_cmpge_ppzi_d, int64_t, >=)

DO_CMP_PPZI_B(sve_cmphi_ppzi_b, uint8_t,  >)
DO_CMP_PPZI_H(sve_cmphi_ppzi_h, uint16_t, >)
DO_CMP_PPZI_S(sve_cmphi_ppzi_s, uint32_t, >)
DO_CMP_PPZI_D(sve_cmphi_ppzi_d, uint64_t, >)

DO_CMP_PPZI_B(sve_cmphs_ppzi_b, uint8_t,  >=)
DO_CMP_PPZI_H(sve_cmphs_ppzi_h, uint16_t, >=)
DO_CMP_PPZI_S(sve_cmphs_ppzi_s, uint32_t, >=)
DO_CMP_PPZI_D(sve_cmphs_ppzi_d, uint64_t, >=)

DO_CMP_PPZI_B(sve_cmplt_ppzi_b, int8_t,  <)
DO_CMP_PPZI_H(sve_cmplt_ppzi_h, int16_t, <)
DO_CMP_PPZI_S(sve_cmplt_ppzi_s, int32_t, <)
DO_CMP_PPZI_D(sve_cmplt_ppzi_d, int64_t, <)

DO_CMP_PPZI_B(sve_cmple_ppzi_b, int8_t,  <=)
DO_CMP_PPZI_H(sve_cmple_ppzi_h, int16_t, <=)
DO_CMP_PPZI_S(sve_cmple_ppzi_s, int32_t, <=)
DO_CMP_PPZI_D(sve_cmple_ppzi_d, int64_t, <=)

DO_CMP_PPZI_B(sve_cmplo_ppzi_b, uint8_t,  <)
DO_CMP_PPZI_H(sve_cmplo_ppzi_h, uint16_t, <)
DO_CMP_PPZI_S(sve_cmplo_ppzi_s, uint32_t, <)
DO_CMP_PPZI_D(sve_cmplo_ppzi_d, uint64_t, <)

DO_CMP_PPZI_B(sve_cmpls_ppzi_b, uint8_t,  <=)
DO_CMP_PPZI_H(sve_cmpls_ppzi_h, uint16_t, <=)
DO_CMP_PPZI_S(sve_cmpls_ppzi_s, uint32_t, <=)
DO_CMP_PPZI_D(sve_cmpls_ppzi_d, uint64_t, <=)

#undef DO_CMP_PPZI_B
#undef DO_CMP_PPZI_H
#undef DO_CMP_PPZI_S
#undef DO_CMP_PPZI_D
#undef DO_CMP_PPZI

/* Similar to the ARM LastActive pseudocode function.  */
static bool last_active_pred(void *vd, void *vg, intptr_t oprsz)
{
    intptr_t i;

    for (i = QEMU_ALIGN_UP(oprsz, 8) - 8; i >= 0; i -= 8) {
        uint64_t pg = *(uint64_t *)(vg + i);
        if (pg) {
            return (pow2floor(pg) & *(uint64_t *)(vd + i)) != 0;
        }
    }
    return 0;
}

/* Compute a mask into RETB that is true for all G, up to and including
 * (if after) or excluding (if !after) the first G & N.
 * Return true if BRK found.
 */
static bool compute_brk(uint64_t *retb, uint64_t n, uint64_t g,
                        bool brk, bool after)
{
    uint64_t b;

    if (brk) {
        b = 0;
    } else if ((g & n) == 0) {
        /* For all G, no N are set; break not found.  */
        b = g;
    } else {
        /* Break somewhere in N.  Locate it.  */
        b = g & n;            /* guard true, pred true */
        b = b & -b;           /* first such */
        if (after) {
            b = b | (b - 1);  /* break after same */
        } else {
            b = b - 1;        /* break before same */
        }
        brk = true;
    }

    *retb = b;
    return brk;
}

/* Compute a zeroing BRK.  */
static void compute_brk_z(uint64_t *d, uint64_t *n, uint64_t *g,
                          intptr_t oprsz, bool after)
{
    bool brk = false;
    intptr_t i;

    for (i = 0; i < DIV_ROUND_UP(oprsz, 8); ++i) {
        uint64_t this_b, this_g = g[i];

        brk = compute_brk(&this_b, n[i], this_g, brk, after);
        d[i] = this_b & this_g;
    }
}

/* Likewise, but also compute flags.  */
static uint32_t compute_brks_z(uint64_t *d, uint64_t *n, uint64_t *g,
                               intptr_t oprsz, bool after)
{
    uint32_t flags = PREDTEST_INIT;
    bool brk = false;
    intptr_t i;

    for (i = 0; i < DIV_ROUND_UP(oprsz, 8); ++i) {
        uint64_t this_b, this_d, this_g = g[i];

        brk = compute_brk(&this_b, n[i], this_g, brk, after);
        d[i] = this_d = this_b & this_g;
        flags = iter_predtest_fwd(this_d, this_g, flags);
    }
    return flags;
}

/* Compute a merging BRK.  */
static void compute_brk_m(uint64_t *d, uint64_t *n, uint64_t *g,
                          intptr_t oprsz, bool after)
{
    bool brk = false;
    intptr_t i;

    for (i = 0; i < DIV_ROUND_UP(oprsz, 8); ++i) {
        uint64_t this_b, this_g = g[i];

        brk = compute_brk(&this_b, n[i], this_g, brk, after);
        d[i] = (this_b & this_g) | (d[i] & ~this_g);
    }
}

/* Likewise, but also compute flags.  */
static uint32_t compute_brks_m(uint64_t *d, uint64_t *n, uint64_t *g,
                               intptr_t oprsz, bool after)
{
    uint32_t flags = PREDTEST_INIT;
    bool brk = false;
    intptr_t i;

    for (i = 0; i < oprsz / 8; ++i) {
        uint64_t this_b, this_d = d[i], this_g = g[i];

        brk = compute_brk(&this_b, n[i], this_g, brk, after);
        d[i] = this_d = (this_b & this_g) | (this_d & ~this_g);
        flags = iter_predtest_fwd(this_d, this_g, flags);
    }
    return flags;
}

static uint32_t do_zero(ARMPredicateReg *d, intptr_t oprsz)
{
    /* It is quicker to zero the whole predicate than loop on OPRSZ.
     * The compiler should turn this into 4 64-bit integer stores.
     */
    memset(d, 0, sizeof(ARMPredicateReg));
    return PREDTEST_INIT;
}

void HELPER(sve_brkpa)(void *vd, void *vn, void *vm, void *vg,
                       uint32_t pred_desc)
{
    intptr_t oprsz = FIELD_EX32(pred_desc, PREDDESC, OPRSZ);
    if (last_active_pred(vn, vg, oprsz)) {
        compute_brk_z(vd, vm, vg, oprsz, true);
    } else {
        do_zero(vd, oprsz);
    }
}

uint32_t HELPER(sve_brkpas)(void *vd, void *vn, void *vm, void *vg,
                            uint32_t pred_desc)
{
    intptr_t oprsz = FIELD_EX32(pred_desc, PREDDESC, OPRSZ);
    if (last_active_pred(vn, vg, oprsz)) {
        return compute_brks_z(vd, vm, vg, oprsz, true);
    } else {
        return do_zero(vd, oprsz);
    }
}

void HELPER(sve_brkpb)(void *vd, void *vn, void *vm, void *vg,
                       uint32_t pred_desc)
{
    intptr_t oprsz = FIELD_EX32(pred_desc, PREDDESC, OPRSZ);
    if (last_active_pred(vn, vg, oprsz)) {
        compute_brk_z(vd, vm, vg, oprsz, false);
    } else {
        do_zero(vd, oprsz);
    }
}

uint32_t HELPER(sve_brkpbs)(void *vd, void *vn, void *vm, void *vg,
                            uint32_t pred_desc)
{
    intptr_t oprsz = FIELD_EX32(pred_desc, PREDDESC, OPRSZ);
    if (last_active_pred(vn, vg, oprsz)) {
        return compute_brks_z(vd, vm, vg, oprsz, false);
    } else {
        return do_zero(vd, oprsz);
    }
}

void HELPER(sve_brka_z)(void *vd, void *vn, void *vg, uint32_t pred_desc)
{
    intptr_t oprsz = FIELD_EX32(pred_desc, PREDDESC, OPRSZ);
    compute_brk_z(vd, vn, vg, oprsz, true);
}

uint32_t HELPER(sve_brkas_z)(void *vd, void *vn, void *vg, uint32_t pred_desc)
{
    intptr_t oprsz = FIELD_EX32(pred_desc, PREDDESC, OPRSZ);
    return compute_brks_z(vd, vn, vg, oprsz, true);
}

void HELPER(sve_brkb_z)(void *vd, void *vn, void *vg, uint32_t pred_desc)
{
    intptr_t oprsz = FIELD_EX32(pred_desc, PREDDESC, OPRSZ);
    compute_brk_z(vd, vn, vg, oprsz, false);
}

uint32_t HELPER(sve_brkbs_z)(void *vd, void *vn, void *vg, uint32_t pred_desc)
{
    intptr_t oprsz = FIELD_EX32(pred_desc, PREDDESC, OPRSZ);
    return compute_brks_z(vd, vn, vg, oprsz, false);
}

void HELPER(sve_brka_m)(void *vd, void *vn, void *vg, uint32_t pred_desc)
{
    intptr_t oprsz = FIELD_EX32(pred_desc, PREDDESC, OPRSZ);
    compute_brk_m(vd, vn, vg, oprsz, true);
}

uint32_t HELPER(sve_brkas_m)(void *vd, void *vn, void *vg, uint32_t pred_desc)
{
    intptr_t oprsz = FIELD_EX32(pred_desc, PREDDESC, OPRSZ);
    return compute_brks_m(vd, vn, vg, oprsz, true);
}

void HELPER(sve_brkb_m)(void *vd, void *vn, void *vg, uint32_t pred_desc)
{
    intptr_t oprsz = FIELD_EX32(pred_desc, PREDDESC, OPRSZ);
    compute_brk_m(vd, vn, vg, oprsz, false);
}

uint32_t HELPER(sve_brkbs_m)(void *vd, void *vn, void *vg, uint32_t pred_desc)
{
    intptr_t oprsz = FIELD_EX32(pred_desc, PREDDESC, OPRSZ);
    return compute_brks_m(vd, vn, vg, oprsz, false);
}

void HELPER(sve_brkn)(void *vd, void *vn, void *vg, uint32_t pred_desc)
{
    intptr_t oprsz = FIELD_EX32(pred_desc, PREDDESC, OPRSZ);
    if (!last_active_pred(vn, vg, oprsz)) {
        do_zero(vd, oprsz);
    }
}

/* As if PredTest(Ones(PL), D, esz).  */
static uint32_t predtest_ones(ARMPredicateReg *d, intptr_t oprsz,
                              uint64_t esz_mask)
{
    uint32_t flags = PREDTEST_INIT;
    intptr_t i;

    for (i = 0; i < oprsz / 8; i++) {
        flags = iter_predtest_fwd(d->p[i], esz_mask, flags);
    }
    if (oprsz & 7) {
        uint64_t mask = ~(-1ULL << (8 * (oprsz & 7)));
        flags = iter_predtest_fwd(d->p[i], esz_mask & mask, flags);
    }
    return flags;
}

uint32_t HELPER(sve_brkns)(void *vd, void *vn, void *vg, uint32_t pred_desc)
{
    intptr_t oprsz = FIELD_EX32(pred_desc, PREDDESC, OPRSZ);
    if (last_active_pred(vn, vg, oprsz)) {
        return predtest_ones(vd, oprsz, -1);
    } else {
        return do_zero(vd, oprsz);
    }
}

uint64_t HELPER(sve_cntp)(void *vn, void *vg, uint32_t pred_desc)
{
    intptr_t words = DIV_ROUND_UP(FIELD_EX32(pred_desc, PREDDESC, OPRSZ), 8);
    intptr_t esz = FIELD_EX32(pred_desc, PREDDESC, ESZ);
    uint64_t *n = vn, *g = vg, sum = 0, mask = pred_esz_masks[esz];
    intptr_t i;

    for (i = 0; i < words; ++i) {
        uint64_t t = n[i] & g[i] & mask;
        sum += ctpop64(t);
    }
    return sum;
}

uint32_t HELPER(sve_whilel)(void *vd, uint32_t count, uint32_t pred_desc)
{
    intptr_t oprsz = FIELD_EX32(pred_desc, PREDDESC, OPRSZ);
    intptr_t esz = FIELD_EX32(pred_desc, PREDDESC, ESZ);
    uint64_t esz_mask = pred_esz_masks[esz];
    ARMPredicateReg *d = vd;
    uint32_t flags;
    intptr_t i;

    /* Begin with a zero predicate register.  */
    flags = do_zero(d, oprsz);
    if (count == 0) {
        return flags;
    }

    /* Set all of the requested bits.  */
    for (i = 0; i < count / 64; ++i) {
        d->p[i] = esz_mask;
    }
    if (count & 63) {
        d->p[i] = MAKE_64BIT_MASK(0, count & 63) & esz_mask;
    }

    return predtest_ones(d, oprsz, esz_mask);
}

uint32_t HELPER(sve_whileg)(void *vd, uint32_t count, uint32_t pred_desc)
{
    intptr_t oprsz = FIELD_EX32(pred_desc, PREDDESC, OPRSZ);
    intptr_t esz = FIELD_EX32(pred_desc, PREDDESC, ESZ);
    uint64_t esz_mask = pred_esz_masks[esz];
    ARMPredicateReg *d = vd;
    intptr_t i, invcount, oprbits;
    uint64_t bits;

    if (count == 0) {
        return do_zero(d, oprsz);
    }

    oprbits = oprsz * 8;
    tcg_debug_assert(count <= oprbits);

    bits = esz_mask;
    if (oprbits & 63) {
        bits &= MAKE_64BIT_MASK(0, oprbits & 63);
    }

    invcount = oprbits - count;
    for (i = (oprsz - 1) / 8; i > invcount / 64; --i) {
        d->p[i] = bits;
        bits = esz_mask;
    }

    d->p[i] = bits & MAKE_64BIT_MASK(invcount & 63, 64);

    while (--i >= 0) {
        d->p[i] = 0;
    }

    return predtest_ones(d, oprsz, esz_mask);
}

/* Recursive reduction on a function;
 * C.f. the ARM ARM function ReducePredicated.
 *
 * While it would be possible to write this without the DATA temporary,
 * it is much simpler to process the predicate register this way.
 * The recursion is bounded to depth 7 (128 fp16 elements), so there's
 * little to gain with a more complex non-recursive form.
 */
#define DO_REDUCE(NAME, TYPE, H, FUNC, IDENT)                         \
static TYPE NAME##_reduce(TYPE *data, float_status *status, uintptr_t n) \
{                                                                     \
    if (n == 1) {                                                     \
        return *data;                                                 \
    } else {                                                          \
        uintptr_t half = n / 2;                                       \
        TYPE lo = NAME##_reduce(data, status, half);                  \
        TYPE hi = NAME##_reduce(data + half, status, half);           \
        return FUNC(lo, hi, status);                                  \
    }                                                                 \
}                                                                     \
uint64_t HELPER(NAME)(void *vn, void *vg, float_status *s, uint32_t desc) \
{                                                                     \
    uintptr_t i, oprsz = simd_oprsz(desc), maxsz = simd_data(desc);   \
    TYPE data[sizeof(ARMVectorReg) / sizeof(TYPE)];                   \
    for (i = 0; i < oprsz; ) {                                        \
        uint16_t pg = *(uint16_t *)(vg + H1_2(i >> 3));               \
        do {                                                          \
            TYPE nn = *(TYPE *)(vn + H(i));                           \
            *(TYPE *)((void *)data + i) = (pg & 1 ? nn : IDENT);      \
            i += sizeof(TYPE), pg >>= sizeof(TYPE);                   \
        } while (i & 15);                                             \
    }                                                                 \
    for (; i < maxsz; i += sizeof(TYPE)) {                            \
        *(TYPE *)((void *)data + i) = IDENT;                          \
    }                                                                 \
    return NAME##_reduce(data, s, maxsz / sizeof(TYPE));              \
}

DO_REDUCE(sve_faddv_h, float16, H1_2, float16_add, float16_zero)
DO_REDUCE(sve_faddv_s, float32, H1_4, float32_add, float32_zero)
DO_REDUCE(sve_faddv_d, float64, H1_8, float64_add, float64_zero)

/* Identity is floatN_default_nan, without the function call.  */
DO_REDUCE(sve_fminnmv_h, float16, H1_2, float16_minnum, 0x7E00)
DO_REDUCE(sve_fminnmv_s, float32, H1_4, float32_minnum, 0x7FC00000)
DO_REDUCE(sve_fminnmv_d, float64, H1_8, float64_minnum, 0x7FF8000000000000ULL)

DO_REDUCE(sve_fmaxnmv_h, float16, H1_2, float16_maxnum, 0x7E00)
DO_REDUCE(sve_fmaxnmv_s, float32, H1_4, float32_maxnum, 0x7FC00000)
DO_REDUCE(sve_fmaxnmv_d, float64, H1_8, float64_maxnum, 0x7FF8000000000000ULL)

DO_REDUCE(sve_fminv_h, float16, H1_2, float16_min, float16_infinity)
DO_REDUCE(sve_fminv_s, float32, H1_4, float32_min, float32_infinity)
DO_REDUCE(sve_fminv_d, float64, H1_8, float64_min, float64_infinity)

DO_REDUCE(sve_fmaxv_h, float16, H1_2, float16_max, float16_chs(float16_infinity))
DO_REDUCE(sve_fmaxv_s, float32, H1_4, float32_max, float32_chs(float32_infinity))
DO_REDUCE(sve_fmaxv_d, float64, H1_8, float64_max, float64_chs(float64_infinity))

DO_REDUCE(sve_ah_fminv_h, float16, H1_2, helper_vfp_ah_minh, float16_infinity)
DO_REDUCE(sve_ah_fminv_s, float32, H1_4, helper_vfp_ah_mins, float32_infinity)
DO_REDUCE(sve_ah_fminv_d, float64, H1_8, helper_vfp_ah_mind, float64_infinity)

DO_REDUCE(sve_ah_fmaxv_h, float16, H1_2, helper_vfp_ah_maxh,
          float16_chs(float16_infinity))
DO_REDUCE(sve_ah_fmaxv_s, float32, H1_4, helper_vfp_ah_maxs,
          float32_chs(float32_infinity))
DO_REDUCE(sve_ah_fmaxv_d, float64, H1_8, helper_vfp_ah_maxd,
          float64_chs(float64_infinity))

#undef DO_REDUCE

uint64_t HELPER(sve_fadda_h)(uint64_t nn, void *vm, void *vg,
                             float_status *status, uint32_t desc)
{
    intptr_t i = 0, opr_sz = simd_oprsz(desc);
    float16 result = nn;

    do {
        uint16_t pg = *(uint16_t *)(vg + H1_2(i >> 3));
        do {
            if (pg & 1) {
                float16 mm = *(float16 *)(vm + H1_2(i));
                result = float16_add(result, mm, status);
            }
            i += sizeof(float16), pg >>= sizeof(float16);
        } while (i & 15);
    } while (i < opr_sz);

    return result;
}

uint64_t HELPER(sve_fadda_s)(uint64_t nn, void *vm, void *vg,
                             float_status *status, uint32_t desc)
{
    intptr_t i = 0, opr_sz = simd_oprsz(desc);
    float32 result = nn;

    do {
        uint16_t pg = *(uint16_t *)(vg + H1_2(i >> 3));
        do {
            if (pg & 1) {
                float32 mm = *(float32 *)(vm + H1_2(i));
                result = float32_add(result, mm, status);
            }
            i += sizeof(float32), pg >>= sizeof(float32);
        } while (i & 15);
    } while (i < opr_sz);

    return result;
}

uint64_t HELPER(sve_fadda_d)(uint64_t nn, void *vm, void *vg,
                             float_status *status, uint32_t desc)
{
    intptr_t i = 0, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *m = vm;
    uint8_t *pg = vg;

    for (i = 0; i < opr_sz; i++) {
        if (pg[H1(i)] & 1) {
            nn = float64_add(nn, m[i], status);
        }
    }

    return nn;
}

/* Fully general three-operand expander, controlled by a predicate,
 * With the extra float_status parameter.
 */
#define DO_ZPZZ_FP(NAME, TYPE, H, OP)                           \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *vg,       \
                  float_status *status, uint32_t desc)          \
{                                                               \
    intptr_t i = simd_oprsz(desc);                              \
    uint64_t *g = vg;                                           \
    do {                                                        \
        uint64_t pg = g[(i - 1) >> 6];                          \
        do {                                                    \
            i -= sizeof(TYPE);                                  \
            if (likely((pg >> (i & 63)) & 1)) {                 \
                TYPE nn = *(TYPE *)(vn + H(i));                 \
                TYPE mm = *(TYPE *)(vm + H(i));                 \
                *(TYPE *)(vd + H(i)) = OP(nn, mm, status);      \
            }                                                   \
        } while (i & 63);                                       \
    } while (i != 0);                                           \
}

DO_ZPZZ_FP(sve_fadd_h, uint16_t, H1_2, float16_add)
DO_ZPZZ_FP(sve_fadd_s, uint32_t, H1_4, float32_add)
DO_ZPZZ_FP(sve_fadd_d, uint64_t, H1_8, float64_add)

DO_ZPZZ_FP(sve_fsub_h, uint16_t, H1_2, float16_sub)
DO_ZPZZ_FP(sve_fsub_s, uint32_t, H1_4, float32_sub)
DO_ZPZZ_FP(sve_fsub_d, uint64_t, H1_8, float64_sub)

DO_ZPZZ_FP(sve_fmul_h, uint16_t, H1_2, float16_mul)
DO_ZPZZ_FP(sve_fmul_s, uint32_t, H1_4, float32_mul)
DO_ZPZZ_FP(sve_fmul_d, uint64_t, H1_8, float64_mul)

DO_ZPZZ_FP(sve_fdiv_h, uint16_t, H1_2, float16_div)
DO_ZPZZ_FP(sve_fdiv_s, uint32_t, H1_4, float32_div)
DO_ZPZZ_FP(sve_fdiv_d, uint64_t, H1_8, float64_div)

DO_ZPZZ_FP(sve_fmin_h, uint16_t, H1_2, float16_min)
DO_ZPZZ_FP(sve_fmin_s, uint32_t, H1_4, float32_min)
DO_ZPZZ_FP(sve_fmin_d, uint64_t, H1_8, float64_min)

DO_ZPZZ_FP(sve_fmax_h, uint16_t, H1_2, float16_max)
DO_ZPZZ_FP(sve_fmax_s, uint32_t, H1_4, float32_max)
DO_ZPZZ_FP(sve_fmax_d, uint64_t, H1_8, float64_max)

DO_ZPZZ_FP(sve_ah_fmin_h, uint16_t, H1_2, helper_vfp_ah_minh)
DO_ZPZZ_FP(sve_ah_fmin_s, uint32_t, H1_4, helper_vfp_ah_mins)
DO_ZPZZ_FP(sve_ah_fmin_d, uint64_t, H1_8, helper_vfp_ah_mind)

DO_ZPZZ_FP(sve_ah_fmax_h, uint16_t, H1_2, helper_vfp_ah_maxh)
DO_ZPZZ_FP(sve_ah_fmax_s, uint32_t, H1_4, helper_vfp_ah_maxs)
DO_ZPZZ_FP(sve_ah_fmax_d, uint64_t, H1_8, helper_vfp_ah_maxd)

DO_ZPZZ_FP(sve_fminnum_h, uint16_t, H1_2, float16_minnum)
DO_ZPZZ_FP(sve_fminnum_s, uint32_t, H1_4, float32_minnum)
DO_ZPZZ_FP(sve_fminnum_d, uint64_t, H1_8, float64_minnum)

DO_ZPZZ_FP(sve_fmaxnum_h, uint16_t, H1_2, float16_maxnum)
DO_ZPZZ_FP(sve_fmaxnum_s, uint32_t, H1_4, float32_maxnum)
DO_ZPZZ_FP(sve_fmaxnum_d, uint64_t, H1_8, float64_maxnum)

static inline float16 abd_h(float16 a, float16 b, float_status *s)
{
    return float16_abs(float16_sub(a, b, s));
}

static inline float32 abd_s(float32 a, float32 b, float_status *s)
{
    return float32_abs(float32_sub(a, b, s));
}

static inline float64 abd_d(float64 a, float64 b, float_status *s)
{
    return float64_abs(float64_sub(a, b, s));
}

/* ABD when FPCR.AH = 1: avoid flipping sign bit of a NaN result */
static float16 ah_abd_h(float16 op1, float16 op2, float_status *stat)
{
    float16 r = float16_sub(op1, op2, stat);
    return float16_is_any_nan(r) ? r : float16_abs(r);
}

static float32 ah_abd_s(float32 op1, float32 op2, float_status *stat)
{
    float32 r = float32_sub(op1, op2, stat);
    return float32_is_any_nan(r) ? r : float32_abs(r);
}

static float64 ah_abd_d(float64 op1, float64 op2, float_status *stat)
{
    float64 r = float64_sub(op1, op2, stat);
    return float64_is_any_nan(r) ? r : float64_abs(r);
}

DO_ZPZZ_FP(sve_fabd_h, uint16_t, H1_2, abd_h)
DO_ZPZZ_FP(sve_fabd_s, uint32_t, H1_4, abd_s)
DO_ZPZZ_FP(sve_fabd_d, uint64_t, H1_8, abd_d)
DO_ZPZZ_FP(sve_ah_fabd_h, uint16_t, H1_2, ah_abd_h)
DO_ZPZZ_FP(sve_ah_fabd_s, uint32_t, H1_4, ah_abd_s)
DO_ZPZZ_FP(sve_ah_fabd_d, uint64_t, H1_8, ah_abd_d)

static inline float64 scalbn_d(float64 a, int64_t b, float_status *s)
{
    int b_int = MIN(MAX(b, INT_MIN), INT_MAX);
    return float64_scalbn(a, b_int, s);
}

DO_ZPZZ_FP(sve_fscalbn_h, int16_t, H1_2, float16_scalbn)
DO_ZPZZ_FP(sve_fscalbn_s, int32_t, H1_4, float32_scalbn)
DO_ZPZZ_FP(sve_fscalbn_d, int64_t, H1_8, scalbn_d)

DO_ZPZZ_FP(sve_fmulx_h, uint16_t, H1_2, helper_advsimd_mulxh)
DO_ZPZZ_FP(sve_fmulx_s, uint32_t, H1_4, helper_vfp_mulxs)
DO_ZPZZ_FP(sve_fmulx_d, uint64_t, H1_8, helper_vfp_mulxd)

#undef DO_ZPZZ_FP

/* Three-operand expander, with one scalar operand, controlled by
 * a predicate, with the extra float_status parameter.
 */
#define DO_ZPZS_FP(NAME, TYPE, H, OP) \
void HELPER(NAME)(void *vd, void *vn, void *vg, uint64_t scalar,  \
                  float_status *status, uint32_t desc)            \
{                                                                 \
    intptr_t i = simd_oprsz(desc);                                \
    uint64_t *g = vg;                                             \
    TYPE mm = scalar;                                             \
    do {                                                          \
        uint64_t pg = g[(i - 1) >> 6];                            \
        do {                                                      \
            i -= sizeof(TYPE);                                    \
            if (likely((pg >> (i & 63)) & 1)) {                   \
                TYPE nn = *(TYPE *)(vn + H(i));                   \
                *(TYPE *)(vd + H(i)) = OP(nn, mm, status);        \
            }                                                     \
        } while (i & 63);                                         \
    } while (i != 0);                                             \
}

DO_ZPZS_FP(sve_fadds_h, float16, H1_2, float16_add)
DO_ZPZS_FP(sve_fadds_s, float32, H1_4, float32_add)
DO_ZPZS_FP(sve_fadds_d, float64, H1_8, float64_add)

DO_ZPZS_FP(sve_fsubs_h, float16, H1_2, float16_sub)
DO_ZPZS_FP(sve_fsubs_s, float32, H1_4, float32_sub)
DO_ZPZS_FP(sve_fsubs_d, float64, H1_8, float64_sub)

DO_ZPZS_FP(sve_fmuls_h, float16, H1_2, float16_mul)
DO_ZPZS_FP(sve_fmuls_s, float32, H1_4, float32_mul)
DO_ZPZS_FP(sve_fmuls_d, float64, H1_8, float64_mul)

static inline float16 subr_h(float16 a, float16 b, float_status *s)
{
    return float16_sub(b, a, s);
}

static inline float32 subr_s(float32 a, float32 b, float_status *s)
{
    return float32_sub(b, a, s);
}

static inline float64 subr_d(float64 a, float64 b, float_status *s)
{
    return float64_sub(b, a, s);
}

DO_ZPZS_FP(sve_fsubrs_h, float16, H1_2, subr_h)
DO_ZPZS_FP(sve_fsubrs_s, float32, H1_4, subr_s)
DO_ZPZS_FP(sve_fsubrs_d, float64, H1_8, subr_d)

DO_ZPZS_FP(sve_fmaxnms_h, float16, H1_2, float16_maxnum)
DO_ZPZS_FP(sve_fmaxnms_s, float32, H1_4, float32_maxnum)
DO_ZPZS_FP(sve_fmaxnms_d, float64, H1_8, float64_maxnum)

DO_ZPZS_FP(sve_fminnms_h, float16, H1_2, float16_minnum)
DO_ZPZS_FP(sve_fminnms_s, float32, H1_4, float32_minnum)
DO_ZPZS_FP(sve_fminnms_d, float64, H1_8, float64_minnum)

DO_ZPZS_FP(sve_fmaxs_h, float16, H1_2, float16_max)
DO_ZPZS_FP(sve_fmaxs_s, float32, H1_4, float32_max)
DO_ZPZS_FP(sve_fmaxs_d, float64, H1_8, float64_max)

DO_ZPZS_FP(sve_fmins_h, float16, H1_2, float16_min)
DO_ZPZS_FP(sve_fmins_s, float32, H1_4, float32_min)
DO_ZPZS_FP(sve_fmins_d, float64, H1_8, float64_min)

DO_ZPZS_FP(sve_ah_fmaxs_h, float16, H1_2, helper_vfp_ah_maxh)
DO_ZPZS_FP(sve_ah_fmaxs_s, float32, H1_4, helper_vfp_ah_maxs)
DO_ZPZS_FP(sve_ah_fmaxs_d, float64, H1_8, helper_vfp_ah_maxd)

DO_ZPZS_FP(sve_ah_fmins_h, float16, H1_2, helper_vfp_ah_minh)
DO_ZPZS_FP(sve_ah_fmins_s, float32, H1_4, helper_vfp_ah_mins)
DO_ZPZS_FP(sve_ah_fmins_d, float64, H1_8, helper_vfp_ah_mind)

/* Fully general two-operand expander, controlled by a predicate,
 * With the extra float_status parameter.
 */
#define DO_ZPZ_FP(NAME, TYPE, H, OP)                                  \
void HELPER(NAME)(void *vd, void *vn, void *vg,                       \
                  float_status *status, uint32_t desc)                \
{                                                                     \
    intptr_t i = simd_oprsz(desc);                                    \
    uint64_t *g = vg;                                                 \
    do {                                                              \
        uint64_t pg = g[(i - 1) >> 6];                                \
        do {                                                          \
            i -= sizeof(TYPE);                                        \
            if (likely((pg >> (i & 63)) & 1)) {                       \
                TYPE nn = *(TYPE *)(vn + H(i));                       \
                *(TYPE *)(vd + H(i)) = OP(nn, status);                \
            }                                                         \
        } while (i & 63);                                             \
    } while (i != 0);                                                 \
}

/* SVE fp16 conversions always use IEEE mode.  Like AdvSIMD, they ignore
 * FZ16.  When converting from fp16, this affects flushing input denormals;
 * when converting to fp16, this affects flushing output denormals.
 */
static inline float32 sve_f16_to_f32(float16 f, float_status *fpst)
{
    bool save = get_flush_inputs_to_zero(fpst);
    float32 ret;

    set_flush_inputs_to_zero(false, fpst);
    ret = float16_to_float32(f, true, fpst);
    set_flush_inputs_to_zero(save, fpst);
    return ret;
}

static inline float64 sve_f16_to_f64(float16 f, float_status *fpst)
{
    bool save = get_flush_inputs_to_zero(fpst);
    float64 ret;

    set_flush_inputs_to_zero(false, fpst);
    ret = float16_to_float64(f, true, fpst);
    set_flush_inputs_to_zero(save, fpst);
    return ret;
}

static inline float16 sve_f32_to_f16(float32 f, float_status *fpst)
{
    bool save = get_flush_to_zero(fpst);
    float16 ret;

    set_flush_to_zero(false, fpst);
    ret = float32_to_float16(f, true, fpst);
    set_flush_to_zero(save, fpst);
    return ret;
}

static inline float16 sve_f64_to_f16(float64 f, float_status *fpst)
{
    bool save = get_flush_to_zero(fpst);
    float16 ret;

    set_flush_to_zero(false, fpst);
    ret = float64_to_float16(f, true, fpst);
    set_flush_to_zero(save, fpst);
    return ret;
}

static inline int16_t vfp_float16_to_int16_rtz(float16 f, float_status *s)
{
    if (float16_is_any_nan(f)) {
        float_raise(float_flag_invalid, s);
        return 0;
    }
    return float16_to_int16_round_to_zero(f, s);
}

static inline int64_t vfp_float16_to_int64_rtz(float16 f, float_status *s)
{
    if (float16_is_any_nan(f)) {
        float_raise(float_flag_invalid, s);
        return 0;
    }
    return float16_to_int64_round_to_zero(f, s);
}

static inline int64_t vfp_float32_to_int64_rtz(float32 f, float_status *s)
{
    if (float32_is_any_nan(f)) {
        float_raise(float_flag_invalid, s);
        return 0;
    }
    return float32_to_int64_round_to_zero(f, s);
}

static inline int64_t vfp_float64_to_int64_rtz(float64 f, float_status *s)
{
    if (float64_is_any_nan(f)) {
        float_raise(float_flag_invalid, s);
        return 0;
    }
    return float64_to_int64_round_to_zero(f, s);
}

static inline uint16_t vfp_float16_to_uint16_rtz(float16 f, float_status *s)
{
    if (float16_is_any_nan(f)) {
        float_raise(float_flag_invalid, s);
        return 0;
    }
    return float16_to_uint16_round_to_zero(f, s);
}

static inline uint64_t vfp_float16_to_uint64_rtz(float16 f, float_status *s)
{
    if (float16_is_any_nan(f)) {
        float_raise(float_flag_invalid, s);
        return 0;
    }
    return float16_to_uint64_round_to_zero(f, s);
}

static inline uint64_t vfp_float32_to_uint64_rtz(float32 f, float_status *s)
{
    if (float32_is_any_nan(f)) {
        float_raise(float_flag_invalid, s);
        return 0;
    }
    return float32_to_uint64_round_to_zero(f, s);
}

static inline uint64_t vfp_float64_to_uint64_rtz(float64 f, float_status *s)
{
    if (float64_is_any_nan(f)) {
        float_raise(float_flag_invalid, s);
        return 0;
    }
    return float64_to_uint64_round_to_zero(f, s);
}

DO_ZPZ_FP(sve_fcvt_sh, uint32_t, H1_4, sve_f32_to_f16)
DO_ZPZ_FP(sve_fcvt_hs, uint32_t, H1_4, sve_f16_to_f32)
DO_ZPZ_FP(sve_bfcvt,   uint32_t, H1_4, float32_to_bfloat16)
DO_ZPZ_FP(sve_fcvt_dh, uint64_t, H1_8, sve_f64_to_f16)
DO_ZPZ_FP(sve_fcvt_hd, uint64_t, H1_8, sve_f16_to_f64)
DO_ZPZ_FP(sve_fcvt_ds, uint64_t, H1_8, float64_to_float32)
DO_ZPZ_FP(sve_fcvt_sd, uint64_t, H1_8, float32_to_float64)

DO_ZPZ_FP(sve_fcvtzs_hh, uint16_t, H1_2, vfp_float16_to_int16_rtz)
DO_ZPZ_FP(sve_fcvtzs_hs, uint32_t, H1_4, helper_vfp_tosizh)
DO_ZPZ_FP(sve_fcvtzs_ss, uint32_t, H1_4, helper_vfp_tosizs)
DO_ZPZ_FP(sve_fcvtzs_hd, uint64_t, H1_8, vfp_float16_to_int64_rtz)
DO_ZPZ_FP(sve_fcvtzs_sd, uint64_t, H1_8, vfp_float32_to_int64_rtz)
DO_ZPZ_FP(sve_fcvtzs_ds, uint64_t, H1_8, helper_vfp_tosizd)
DO_ZPZ_FP(sve_fcvtzs_dd, uint64_t, H1_8, vfp_float64_to_int64_rtz)

DO_ZPZ_FP(sve_fcvtzu_hh, uint16_t, H1_2, vfp_float16_to_uint16_rtz)
DO_ZPZ_FP(sve_fcvtzu_hs, uint32_t, H1_4, helper_vfp_touizh)
DO_ZPZ_FP(sve_fcvtzu_ss, uint32_t, H1_4, helper_vfp_touizs)
DO_ZPZ_FP(sve_fcvtzu_hd, uint64_t, H1_8, vfp_float16_to_uint64_rtz)
DO_ZPZ_FP(sve_fcvtzu_sd, uint64_t, H1_8, vfp_float32_to_uint64_rtz)
DO_ZPZ_FP(sve_fcvtzu_ds, uint64_t, H1_8, helper_vfp_touizd)
DO_ZPZ_FP(sve_fcvtzu_dd, uint64_t, H1_8, vfp_float64_to_uint64_rtz)

DO_ZPZ_FP(sve_frint_h, uint16_t, H1_2, helper_advsimd_rinth)
DO_ZPZ_FP(sve_frint_s, uint32_t, H1_4, helper_rints)
DO_ZPZ_FP(sve_frint_d, uint64_t, H1_8, helper_rintd)

DO_ZPZ_FP(sve_frintx_h, uint16_t, H1_2, float16_round_to_int)
DO_ZPZ_FP(sve_frintx_s, uint32_t, H1_4, float32_round_to_int)
DO_ZPZ_FP(sve_frintx_d, uint64_t, H1_8, float64_round_to_int)

DO_ZPZ_FP(sve_frecpx_h, uint16_t, H1_2, helper_frecpx_f16)
DO_ZPZ_FP(sve_frecpx_s, uint32_t, H1_4, helper_frecpx_f32)
DO_ZPZ_FP(sve_frecpx_d, uint64_t, H1_8, helper_frecpx_f64)

DO_ZPZ_FP(sve_fsqrt_h, uint16_t, H1_2, float16_sqrt)
DO_ZPZ_FP(sve_fsqrt_s, uint32_t, H1_4, float32_sqrt)
DO_ZPZ_FP(sve_fsqrt_d, uint64_t, H1_8, float64_sqrt)

DO_ZPZ_FP(sve_scvt_hh, uint16_t, H1_2, int16_to_float16)
DO_ZPZ_FP(sve_scvt_sh, uint32_t, H1_4, int32_to_float16)
DO_ZPZ_FP(sve_scvt_ss, uint32_t, H1_4, int32_to_float32)
DO_ZPZ_FP(sve_scvt_sd, uint64_t, H1_8, int32_to_float64)
DO_ZPZ_FP(sve_scvt_dh, uint64_t, H1_8, int64_to_float16)
DO_ZPZ_FP(sve_scvt_ds, uint64_t, H1_8, int64_to_float32)
DO_ZPZ_FP(sve_scvt_dd, uint64_t, H1_8, int64_to_float64)

DO_ZPZ_FP(sve_ucvt_hh, uint16_t, H1_2, uint16_to_float16)
DO_ZPZ_FP(sve_ucvt_sh, uint32_t, H1_4, uint32_to_float16)
DO_ZPZ_FP(sve_ucvt_ss, uint32_t, H1_4, uint32_to_float32)
DO_ZPZ_FP(sve_ucvt_sd, uint64_t, H1_8, uint32_to_float64)
DO_ZPZ_FP(sve_ucvt_dh, uint64_t, H1_8, uint64_to_float16)
DO_ZPZ_FP(sve_ucvt_ds, uint64_t, H1_8, uint64_to_float32)
DO_ZPZ_FP(sve_ucvt_dd, uint64_t, H1_8, uint64_to_float64)

static int16_t do_float16_logb_as_int(float16 a, float_status *s)
{
    /* Extract frac to the top of the uint32_t. */
    uint32_t frac = (uint32_t)a << (16 + 6);
    int16_t exp = extract32(a, 10, 5);

    if (unlikely(exp == 0)) {
        if (frac != 0) {
            if (!get_flush_inputs_to_zero(s)) {
                /* denormal: bias - fractional_zeros */
                return -15 - clz32(frac);
            }
            /* flush to zero */
            float_raise(float_flag_input_denormal_flushed, s);
        }
    } else if (unlikely(exp == 0x1f)) {
        if (frac == 0) {
            return INT16_MAX; /* infinity */
        }
    } else {
        /* normal: exp - bias */
        return exp - 15;
    }
    /* nan or zero */
    float_raise(float_flag_invalid, s);
    return INT16_MIN;
}

static int32_t do_float32_logb_as_int(float32 a, float_status *s)
{
    /* Extract frac to the top of the uint32_t. */
    uint32_t frac = a << 9;
    int32_t exp = extract32(a, 23, 8);

    if (unlikely(exp == 0)) {
        if (frac != 0) {
            if (!get_flush_inputs_to_zero(s)) {
                /* denormal: bias - fractional_zeros */
                return -127 - clz32(frac);
            }
            /* flush to zero */
            float_raise(float_flag_input_denormal_flushed, s);
        }
    } else if (unlikely(exp == 0xff)) {
        if (frac == 0) {
            return INT32_MAX; /* infinity */
        }
    } else {
        /* normal: exp - bias */
        return exp - 127;
    }
    /* nan or zero */
    float_raise(float_flag_invalid, s);
    return INT32_MIN;
}

static int64_t do_float64_logb_as_int(float64 a, float_status *s)
{
    /* Extract frac to the top of the uint64_t. */
    uint64_t frac = a << 12;
    int64_t exp = extract64(a, 52, 11);

    if (unlikely(exp == 0)) {
        if (frac != 0) {
            if (!get_flush_inputs_to_zero(s)) {
                /* denormal: bias - fractional_zeros */
                return -1023 - clz64(frac);
            }
            /* flush to zero */
            float_raise(float_flag_input_denormal_flushed, s);
        }
    } else if (unlikely(exp == 0x7ff)) {
        if (frac == 0) {
            return INT64_MAX; /* infinity */
        }
    } else {
        /* normal: exp - bias */
        return exp - 1023;
    }
    /* nan or zero */
    float_raise(float_flag_invalid, s);
    return INT64_MIN;
}

DO_ZPZ_FP(flogb_h, float16, H1_2, do_float16_logb_as_int)
DO_ZPZ_FP(flogb_s, float32, H1_4, do_float32_logb_as_int)
DO_ZPZ_FP(flogb_d, float64, H1_8, do_float64_logb_as_int)

#undef DO_ZPZ_FP

static void do_fmla_zpzzz_h(void *vd, void *vn, void *vm, void *va, void *vg,
                            float_status *status, uint32_t desc,
                            uint16_t neg1, uint16_t neg3, int flags)
{
    intptr_t i = simd_oprsz(desc);
    uint64_t *g = vg;

    do {
        uint64_t pg = g[(i - 1) >> 6];
        do {
            i -= 2;
            if (likely((pg >> (i & 63)) & 1)) {
                float16 e1, e2, e3, r;

                e1 = *(uint16_t *)(vn + H1_2(i)) ^ neg1;
                e2 = *(uint16_t *)(vm + H1_2(i));
                e3 = *(uint16_t *)(va + H1_2(i)) ^ neg3;
                r = float16_muladd(e1, e2, e3, flags, status);
                *(uint16_t *)(vd + H1_2(i)) = r;
            }
        } while (i & 63);
    } while (i != 0);
}

void HELPER(sve_fmla_zpzzz_h)(void *vd, void *vn, void *vm, void *va,
                              void *vg, float_status *status, uint32_t desc)
{
    do_fmla_zpzzz_h(vd, vn, vm, va, vg, status, desc, 0, 0, 0);
}

void HELPER(sve_fmls_zpzzz_h)(void *vd, void *vn, void *vm, void *va,
                              void *vg, float_status *status, uint32_t desc)
{
    do_fmla_zpzzz_h(vd, vn, vm, va, vg, status, desc, 0x8000, 0, 0);
}

void HELPER(sve_fnmla_zpzzz_h)(void *vd, void *vn, void *vm, void *va,
                               void *vg, float_status *status, uint32_t desc)
{
    do_fmla_zpzzz_h(vd, vn, vm, va, vg, status, desc, 0x8000, 0x8000, 0);
}

void HELPER(sve_fnmls_zpzzz_h)(void *vd, void *vn, void *vm, void *va,
                               void *vg, float_status *status, uint32_t desc)
{
    do_fmla_zpzzz_h(vd, vn, vm, va, vg, status, desc, 0, 0x8000, 0);
}

void HELPER(sve_ah_fmls_zpzzz_h)(void *vd, void *vn, void *vm, void *va,
                              void *vg, float_status *status, uint32_t desc)
{
    do_fmla_zpzzz_h(vd, vn, vm, va, vg, status, desc, 0, 0,
                    float_muladd_negate_product);
}

void HELPER(sve_ah_fnmla_zpzzz_h)(void *vd, void *vn, void *vm, void *va,
                               void *vg, float_status *status, uint32_t desc)
{
    do_fmla_zpzzz_h(vd, vn, vm, va, vg, status, desc, 0, 0,
                    float_muladd_negate_product | float_muladd_negate_c);
}

void HELPER(sve_ah_fnmls_zpzzz_h)(void *vd, void *vn, void *vm, void *va,
                               void *vg, float_status *status, uint32_t desc)
{
    do_fmla_zpzzz_h(vd, vn, vm, va, vg, status, desc, 0, 0,
                    float_muladd_negate_c);
}

static void do_fmla_zpzzz_s(void *vd, void *vn, void *vm, void *va, void *vg,
                            float_status *status, uint32_t desc,
                            uint32_t neg1, uint32_t neg3, int flags)
{
    intptr_t i = simd_oprsz(desc);
    uint64_t *g = vg;

    do {
        uint64_t pg = g[(i - 1) >> 6];
        do {
            i -= 4;
            if (likely((pg >> (i & 63)) & 1)) {
                float32 e1, e2, e3, r;

                e1 = *(uint32_t *)(vn + H1_4(i)) ^ neg1;
                e2 = *(uint32_t *)(vm + H1_4(i));
                e3 = *(uint32_t *)(va + H1_4(i)) ^ neg3;
                r = float32_muladd(e1, e2, e3, flags, status);
                *(uint32_t *)(vd + H1_4(i)) = r;
            }
        } while (i & 63);
    } while (i != 0);
}

void HELPER(sve_fmla_zpzzz_s)(void *vd, void *vn, void *vm, void *va,
                              void *vg, float_status *status, uint32_t desc)
{
    do_fmla_zpzzz_s(vd, vn, vm, va, vg, status, desc, 0, 0, 0);
}

void HELPER(sve_fmls_zpzzz_s)(void *vd, void *vn, void *vm, void *va,
                              void *vg, float_status *status, uint32_t desc)
{
    do_fmla_zpzzz_s(vd, vn, vm, va, vg, status, desc, 0x80000000, 0, 0);
}

void HELPER(sve_fnmla_zpzzz_s)(void *vd, void *vn, void *vm, void *va,
                               void *vg, float_status *status, uint32_t desc)
{
    do_fmla_zpzzz_s(vd, vn, vm, va, vg, status, desc, 0x80000000, 0x80000000, 0);
}

void HELPER(sve_fnmls_zpzzz_s)(void *vd, void *vn, void *vm, void *va,
                               void *vg, float_status *status, uint32_t desc)
{
    do_fmla_zpzzz_s(vd, vn, vm, va, vg, status, desc, 0, 0x80000000, 0);
}

void HELPER(sve_ah_fmls_zpzzz_s)(void *vd, void *vn, void *vm, void *va,
                              void *vg, float_status *status, uint32_t desc)
{
    do_fmla_zpzzz_s(vd, vn, vm, va, vg, status, desc, 0, 0,
                    float_muladd_negate_product);
}

void HELPER(sve_ah_fnmla_zpzzz_s)(void *vd, void *vn, void *vm, void *va,
                               void *vg, float_status *status, uint32_t desc)
{
    do_fmla_zpzzz_s(vd, vn, vm, va, vg, status, desc, 0, 0,
                    float_muladd_negate_product | float_muladd_negate_c);
}

void HELPER(sve_ah_fnmls_zpzzz_s)(void *vd, void *vn, void *vm, void *va,
                               void *vg, float_status *status, uint32_t desc)
{
    do_fmla_zpzzz_s(vd, vn, vm, va, vg, status, desc, 0, 0,
                    float_muladd_negate_c);
}

static void do_fmla_zpzzz_d(void *vd, void *vn, void *vm, void *va, void *vg,
                            float_status *status, uint32_t desc,
                            uint64_t neg1, uint64_t neg3, int flags)
{
    intptr_t i = simd_oprsz(desc);
    uint64_t *g = vg;

    do {
        uint64_t pg = g[(i - 1) >> 6];
        do {
            i -= 8;
            if (likely((pg >> (i & 63)) & 1)) {
                float64 e1, e2, e3, r;

                e1 = *(uint64_t *)(vn + i) ^ neg1;
                e2 = *(uint64_t *)(vm + i);
                e3 = *(uint64_t *)(va + i) ^ neg3;
                r = float64_muladd(e1, e2, e3, flags, status);
                *(uint64_t *)(vd + i) = r;
            }
        } while (i & 63);
    } while (i != 0);
}

void HELPER(sve_fmla_zpzzz_d)(void *vd, void *vn, void *vm, void *va,
                              void *vg, float_status *status, uint32_t desc)
{
    do_fmla_zpzzz_d(vd, vn, vm, va, vg, status, desc, 0, 0, 0);
}

void HELPER(sve_fmls_zpzzz_d)(void *vd, void *vn, void *vm, void *va,
                              void *vg, float_status *status, uint32_t desc)
{
    do_fmla_zpzzz_d(vd, vn, vm, va, vg, status, desc, INT64_MIN, 0, 0);
}

void HELPER(sve_fnmla_zpzzz_d)(void *vd, void *vn, void *vm, void *va,
                               void *vg, float_status *status, uint32_t desc)
{
    do_fmla_zpzzz_d(vd, vn, vm, va, vg, status, desc, INT64_MIN, INT64_MIN, 0);
}

void HELPER(sve_fnmls_zpzzz_d)(void *vd, void *vn, void *vm, void *va,
                               void *vg, float_status *status, uint32_t desc)
{
    do_fmla_zpzzz_d(vd, vn, vm, va, vg, status, desc, 0, INT64_MIN, 0);
}

void HELPER(sve_ah_fmls_zpzzz_d)(void *vd, void *vn, void *vm, void *va,
                              void *vg, float_status *status, uint32_t desc)
{
    do_fmla_zpzzz_d(vd, vn, vm, va, vg, status, desc, 0, 0,
                    float_muladd_negate_product);
}

void HELPER(sve_ah_fnmla_zpzzz_d)(void *vd, void *vn, void *vm, void *va,
                               void *vg, float_status *status, uint32_t desc)
{
    do_fmla_zpzzz_d(vd, vn, vm, va, vg, status, desc, 0, 0,
                    float_muladd_negate_product | float_muladd_negate_c);
}

void HELPER(sve_ah_fnmls_zpzzz_d)(void *vd, void *vn, void *vm, void *va,
                               void *vg, float_status *status, uint32_t desc)
{
    do_fmla_zpzzz_d(vd, vn, vm, va, vg, status, desc, 0, 0,
                    float_muladd_negate_c);
}

/* Two operand floating-point comparison controlled by a predicate.
 * Unlike the integer version, we are not allowed to optimistically
 * compare operands, since the comparison may have side effects wrt
 * the FPSR.
 */
#define DO_FPCMP_PPZZ(NAME, TYPE, H, OP)                                \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *vg,               \
                  float_status *status, uint32_t desc)                  \
{                                                                       \
    intptr_t i = simd_oprsz(desc), j = (i - 1) >> 6;                    \
    uint64_t *d = vd, *g = vg;                                          \
    do {                                                                \
        uint64_t out = 0, pg = g[j];                                    \
        do {                                                            \
            i -= sizeof(TYPE), out <<= sizeof(TYPE);                    \
            if (likely((pg >> (i & 63)) & 1)) {                         \
                TYPE nn = *(TYPE *)(vn + H(i));                         \
                TYPE mm = *(TYPE *)(vm + H(i));                         \
                out |= OP(TYPE, nn, mm, status);                        \
            }                                                           \
        } while (i & 63);                                               \
        d[j--] = out;                                                   \
    } while (i > 0);                                                    \
}

#define DO_FPCMP_PPZZ_H(NAME, OP) \
    DO_FPCMP_PPZZ(NAME##_h, float16, H1_2, OP)
#define DO_FPCMP_PPZZ_S(NAME, OP) \
    DO_FPCMP_PPZZ(NAME##_s, float32, H1_4, OP)
#define DO_FPCMP_PPZZ_D(NAME, OP) \
    DO_FPCMP_PPZZ(NAME##_d, float64, H1_8, OP)

#define DO_FPCMP_PPZZ_ALL(NAME, OP) \
    DO_FPCMP_PPZZ_H(NAME, OP)   \
    DO_FPCMP_PPZZ_S(NAME, OP)   \
    DO_FPCMP_PPZZ_D(NAME, OP)

#define DO_FCMGE(TYPE, X, Y, ST)  TYPE##_compare(Y, X, ST) <= 0
#define DO_FCMGT(TYPE, X, Y, ST)  TYPE##_compare(Y, X, ST) < 0
#define DO_FCMLE(TYPE, X, Y, ST)  TYPE##_compare(X, Y, ST) <= 0
#define DO_FCMLT(TYPE, X, Y, ST)  TYPE##_compare(X, Y, ST) < 0
#define DO_FCMEQ(TYPE, X, Y, ST)  TYPE##_compare_quiet(X, Y, ST) == 0
#define DO_FCMNE(TYPE, X, Y, ST)  TYPE##_compare_quiet(X, Y, ST) != 0
#define DO_FCMUO(TYPE, X, Y, ST)  \
    TYPE##_compare_quiet(X, Y, ST) == float_relation_unordered
#define DO_FACGE(TYPE, X, Y, ST)  \
    TYPE##_compare(TYPE##_abs(Y), TYPE##_abs(X), ST) <= 0
#define DO_FACGT(TYPE, X, Y, ST)  \
    TYPE##_compare(TYPE##_abs(Y), TYPE##_abs(X), ST) < 0

DO_FPCMP_PPZZ_ALL(sve_fcmge, DO_FCMGE)
DO_FPCMP_PPZZ_ALL(sve_fcmgt, DO_FCMGT)
DO_FPCMP_PPZZ_ALL(sve_fcmeq, DO_FCMEQ)
DO_FPCMP_PPZZ_ALL(sve_fcmne, DO_FCMNE)
DO_FPCMP_PPZZ_ALL(sve_fcmuo, DO_FCMUO)
DO_FPCMP_PPZZ_ALL(sve_facge, DO_FACGE)
DO_FPCMP_PPZZ_ALL(sve_facgt, DO_FACGT)

#undef DO_FPCMP_PPZZ_ALL
#undef DO_FPCMP_PPZZ_D
#undef DO_FPCMP_PPZZ_S
#undef DO_FPCMP_PPZZ_H
#undef DO_FPCMP_PPZZ

/* One operand floating-point comparison against zero, controlled
 * by a predicate.
 */
#define DO_FPCMP_PPZ0(NAME, TYPE, H, OP)                   \
void HELPER(NAME)(void *vd, void *vn, void *vg,            \
                  float_status *status, uint32_t desc)     \
{                                                          \
    intptr_t i = simd_oprsz(desc), j = (i - 1) >> 6;       \
    uint64_t *d = vd, *g = vg;                             \
    do {                                                   \
        uint64_t out = 0, pg = g[j];                       \
        do {                                               \
            i -= sizeof(TYPE), out <<= sizeof(TYPE);       \
            if ((pg >> (i & 63)) & 1) {                    \
                TYPE nn = *(TYPE *)(vn + H(i));            \
                out |= OP(TYPE, nn, 0, status);            \
            }                                              \
        } while (i & 63);                                  \
        d[j--] = out;                                      \
    } while (i > 0);                                       \
}

#define DO_FPCMP_PPZ0_H(NAME, OP) \
    DO_FPCMP_PPZ0(NAME##_h, float16, H1_2, OP)
#define DO_FPCMP_PPZ0_S(NAME, OP) \
    DO_FPCMP_PPZ0(NAME##_s, float32, H1_4, OP)
#define DO_FPCMP_PPZ0_D(NAME, OP) \
    DO_FPCMP_PPZ0(NAME##_d, float64, H1_8, OP)

#define DO_FPCMP_PPZ0_ALL(NAME, OP) \
    DO_FPCMP_PPZ0_H(NAME, OP)   \
    DO_FPCMP_PPZ0_S(NAME, OP)   \
    DO_FPCMP_PPZ0_D(NAME, OP)

DO_FPCMP_PPZ0_ALL(sve_fcmge0, DO_FCMGE)
DO_FPCMP_PPZ0_ALL(sve_fcmgt0, DO_FCMGT)
DO_FPCMP_PPZ0_ALL(sve_fcmle0, DO_FCMLE)
DO_FPCMP_PPZ0_ALL(sve_fcmlt0, DO_FCMLT)
DO_FPCMP_PPZ0_ALL(sve_fcmeq0, DO_FCMEQ)
DO_FPCMP_PPZ0_ALL(sve_fcmne0, DO_FCMNE)

/* FP Trig Multiply-Add. */

void HELPER(sve_ftmad_h)(void *vd, void *vn, void *vm,
                         float_status *s, uint32_t desc)
{
    static const float16 coeff[16] = {
        0x3c00, 0xb155, 0x2030, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x3c00, 0xb800, 0x293a, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    };
    intptr_t i, opr_sz = simd_oprsz(desc) / sizeof(float16);
    intptr_t x = extract32(desc, SIMD_DATA_SHIFT, 3);
    bool fpcr_ah = extract32(desc, SIMD_DATA_SHIFT + 3, 1);
    float16 *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz; i++) {
        float16 mm = m[i];
        intptr_t xx = x;
        int flags = 0;

        if (float16_is_neg(mm)) {
            if (fpcr_ah) {
                flags = float_muladd_negate_product;
            } else {
                mm = float16_abs(mm);
            }
            xx += 8;
        }
        d[i] = float16_muladd(n[i], mm, coeff[xx], flags, s);
    }
}

void HELPER(sve_ftmad_s)(void *vd, void *vn, void *vm,
                         float_status *s, uint32_t desc)
{
    static const float32 coeff[16] = {
        0x3f800000, 0xbe2aaaab, 0x3c088886, 0xb95008b9,
        0x36369d6d, 0x00000000, 0x00000000, 0x00000000,
        0x3f800000, 0xbf000000, 0x3d2aaaa6, 0xbab60705,
        0x37cd37cc, 0x00000000, 0x00000000, 0x00000000,
    };
    intptr_t i, opr_sz = simd_oprsz(desc) / sizeof(float32);
    intptr_t x = extract32(desc, SIMD_DATA_SHIFT, 3);
    bool fpcr_ah = extract32(desc, SIMD_DATA_SHIFT + 3, 1);
    float32 *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz; i++) {
        float32 mm = m[i];
        intptr_t xx = x;
        int flags = 0;

        if (float32_is_neg(mm)) {
            if (fpcr_ah) {
                flags = float_muladd_negate_product;
            } else {
                mm = float32_abs(mm);
            }
            xx += 8;
        }
        d[i] = float32_muladd(n[i], mm, coeff[xx], flags, s);
    }
}

void HELPER(sve_ftmad_d)(void *vd, void *vn, void *vm,
                         float_status *s, uint32_t desc)
{
    static const float64 coeff[16] = {
        0x3ff0000000000000ull, 0xbfc5555555555543ull,
        0x3f8111111110f30cull, 0xbf2a01a019b92fc6ull,
        0x3ec71de351f3d22bull, 0xbe5ae5e2b60f7b91ull,
        0x3de5d8408868552full, 0x0000000000000000ull,
        0x3ff0000000000000ull, 0xbfe0000000000000ull,
        0x3fa5555555555536ull, 0xbf56c16c16c13a0bull,
        0x3efa01a019b1e8d8ull, 0xbe927e4f7282f468ull,
        0x3e21ee96d2641b13ull, 0xbda8f76380fbb401ull,
    };
    intptr_t i, opr_sz = simd_oprsz(desc) / sizeof(float64);
    intptr_t x = extract32(desc, SIMD_DATA_SHIFT, 3);
    bool fpcr_ah = extract32(desc, SIMD_DATA_SHIFT + 3, 1);
    float64 *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz; i++) {
        float64 mm = m[i];
        intptr_t xx = x;
        int flags = 0;

        if (float64_is_neg(mm)) {
            if (fpcr_ah) {
                flags = float_muladd_negate_product;
            } else {
                mm = float64_abs(mm);
            }
            xx += 8;
        }
        d[i] = float64_muladd(n[i], mm, coeff[xx], flags, s);
    }
}

/*
 * FP Complex Add
 */

void HELPER(sve_fcadd_h)(void *vd, void *vn, void *vm, void *vg,
                         float_status *s, uint32_t desc)
{
    intptr_t j, i = simd_oprsz(desc);
    uint64_t *g = vg;
    bool rot = extract32(desc, SIMD_DATA_SHIFT, 1);
    bool fpcr_ah = extract32(desc, SIMD_DATA_SHIFT + 1, 1);

    do {
        uint64_t pg = g[(i - 1) >> 6];
        do {
            float16 e0, e1, e2, e3;

            /* I holds the real index; J holds the imag index.  */
            j = i - sizeof(float16);
            i -= 2 * sizeof(float16);

            e0 = *(float16 *)(vn + H1_2(i));
            e1 = *(float16 *)(vm + H1_2(j));
            e2 = *(float16 *)(vn + H1_2(j));
            e3 = *(float16 *)(vm + H1_2(i));

            if (rot) {
                e3 = float16_maybe_ah_chs(e3, fpcr_ah);
            } else {
                e1 = float16_maybe_ah_chs(e1, fpcr_ah);
            }

            if (likely((pg >> (i & 63)) & 1)) {
                *(float16 *)(vd + H1_2(i)) = float16_add(e0, e1, s);
            }
            if (likely((pg >> (j & 63)) & 1)) {
                *(float16 *)(vd + H1_2(j)) = float16_add(e2, e3, s);
            }
        } while (i & 63);
    } while (i != 0);
}

void HELPER(sve_fcadd_s)(void *vd, void *vn, void *vm, void *vg,
                         float_status *s, uint32_t desc)
{
    intptr_t j, i = simd_oprsz(desc);
    uint64_t *g = vg;
    bool rot = extract32(desc, SIMD_DATA_SHIFT, 1);
    bool fpcr_ah = extract32(desc, SIMD_DATA_SHIFT + 1, 1);

    do {
        uint64_t pg = g[(i - 1) >> 6];
        do {
            float32 e0, e1, e2, e3;

            /* I holds the real index; J holds the imag index.  */
            j = i - sizeof(float32);
            i -= 2 * sizeof(float32);

            e0 = *(float32 *)(vn + H1_2(i));
            e1 = *(float32 *)(vm + H1_2(j));
            e2 = *(float32 *)(vn + H1_2(j));
            e3 = *(float32 *)(vm + H1_2(i));

            if (rot) {
                e3 = float32_maybe_ah_chs(e3, fpcr_ah);
            } else {
                e1 = float32_maybe_ah_chs(e1, fpcr_ah);
            }

            if (likely((pg >> (i & 63)) & 1)) {
                *(float32 *)(vd + H1_2(i)) = float32_add(e0, e1, s);
            }
            if (likely((pg >> (j & 63)) & 1)) {
                *(float32 *)(vd + H1_2(j)) = float32_add(e2, e3, s);
            }
        } while (i & 63);
    } while (i != 0);
}

void HELPER(sve_fcadd_d)(void *vd, void *vn, void *vm, void *vg,
                         float_status *s, uint32_t desc)
{
    intptr_t j, i = simd_oprsz(desc);
    uint64_t *g = vg;
    bool rot = extract32(desc, SIMD_DATA_SHIFT, 1);
    bool fpcr_ah = extract32(desc, SIMD_DATA_SHIFT + 1, 1);

    do {
        uint64_t pg = g[(i - 1) >> 6];
        do {
            float64 e0, e1, e2, e3;

            /* I holds the real index; J holds the imag index.  */
            j = i - sizeof(float64);
            i -= 2 * sizeof(float64);

            e0 = *(float64 *)(vn + H1_2(i));
            e1 = *(float64 *)(vm + H1_2(j));
            e2 = *(float64 *)(vn + H1_2(j));
            e3 = *(float64 *)(vm + H1_2(i));

            if (rot) {
                e3 = float64_maybe_ah_chs(e3, fpcr_ah);
            } else {
                e1 = float64_maybe_ah_chs(e1, fpcr_ah);
            }

            if (likely((pg >> (i & 63)) & 1)) {
                *(float64 *)(vd + H1_2(i)) = float64_add(e0, e1, s);
            }
            if (likely((pg >> (j & 63)) & 1)) {
                *(float64 *)(vd + H1_2(j)) = float64_add(e2, e3, s);
            }
        } while (i & 63);
    } while (i != 0);
}

/*
 * FP Complex Multiply
 */

void HELPER(sve_fcmla_zpzzz_h)(void *vd, void *vn, void *vm, void *va,
                               void *vg, float_status *status, uint32_t desc)
{
    intptr_t j, i = simd_oprsz(desc);
    bool flip = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint32_t fpcr_ah = extract32(desc, SIMD_DATA_SHIFT + 2, 1);
    uint32_t negf_imag = extract32(desc, SIMD_DATA_SHIFT + 1, 1);
    uint32_t negf_real = flip ^ negf_imag;
    float16 negx_imag, negx_real;
    uint64_t *g = vg;

    /* With AH=0, use negx; with AH=1 use negf. */
    negx_real = (negf_real & ~fpcr_ah) << 15;
    negx_imag = (negf_imag & ~fpcr_ah) << 15;
    negf_real = (negf_real & fpcr_ah ? float_muladd_negate_product : 0);
    negf_imag = (negf_imag & fpcr_ah ? float_muladd_negate_product : 0);

    do {
        uint64_t pg = g[(i - 1) >> 6];
        do {
            float16 e1, e2, e3, e4, nr, ni, mr, mi, d;

            /* I holds the real index; J holds the imag index.  */
            j = i - sizeof(float16);
            i -= 2 * sizeof(float16);

            nr = *(float16 *)(vn + H1_2(i));
            ni = *(float16 *)(vn + H1_2(j));
            mr = *(float16 *)(vm + H1_2(i));
            mi = *(float16 *)(vm + H1_2(j));

            e2 = (flip ? ni : nr);
            e1 = (flip ? mi : mr) ^ negx_real;
            e4 = e2;
            e3 = (flip ? mr : mi) ^ negx_imag;

            if (likely((pg >> (i & 63)) & 1)) {
                d = *(float16 *)(va + H1_2(i));
                d = float16_muladd(e2, e1, d, negf_real, status);
                *(float16 *)(vd + H1_2(i)) = d;
            }
            if (likely((pg >> (j & 63)) & 1)) {
                d = *(float16 *)(va + H1_2(j));
                d = float16_muladd(e4, e3, d, negf_imag, status);
                *(float16 *)(vd + H1_2(j)) = d;
            }
        } while (i & 63);
    } while (i != 0);
}

void HELPER(sve_fcmla_zpzzz_s)(void *vd, void *vn, void *vm, void *va,
                               void *vg, float_status *status, uint32_t desc)
{
    intptr_t j, i = simd_oprsz(desc);
    bool flip = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint32_t fpcr_ah = extract32(desc, SIMD_DATA_SHIFT + 2, 1);
    uint32_t negf_imag = extract32(desc, SIMD_DATA_SHIFT + 1, 1);
    uint32_t negf_real = flip ^ negf_imag;
    float32 negx_imag, negx_real;
    uint64_t *g = vg;

    /* With AH=0, use negx; with AH=1 use negf. */
    negx_real = (negf_real & ~fpcr_ah) << 31;
    negx_imag = (negf_imag & ~fpcr_ah) << 31;
    negf_real = (negf_real & fpcr_ah ? float_muladd_negate_product : 0);
    negf_imag = (negf_imag & fpcr_ah ? float_muladd_negate_product : 0);

    do {
        uint64_t pg = g[(i - 1) >> 6];
        do {
            float32 e1, e2, e3, e4, nr, ni, mr, mi, d;

            /* I holds the real index; J holds the imag index.  */
            j = i - sizeof(float32);
            i -= 2 * sizeof(float32);

            nr = *(float32 *)(vn + H1_2(i));
            ni = *(float32 *)(vn + H1_2(j));
            mr = *(float32 *)(vm + H1_2(i));
            mi = *(float32 *)(vm + H1_2(j));

            e2 = (flip ? ni : nr);
            e1 = (flip ? mi : mr) ^ negx_real;
            e4 = e2;
            e3 = (flip ? mr : mi) ^ negx_imag;

            if (likely((pg >> (i & 63)) & 1)) {
                d = *(float32 *)(va + H1_2(i));
                d = float32_muladd(e2, e1, d, negf_real, status);
                *(float32 *)(vd + H1_2(i)) = d;
            }
            if (likely((pg >> (j & 63)) & 1)) {
                d = *(float32 *)(va + H1_2(j));
                d = float32_muladd(e4, e3, d, negf_imag, status);
                *(float32 *)(vd + H1_2(j)) = d;
            }
        } while (i & 63);
    } while (i != 0);
}

void HELPER(sve_fcmla_zpzzz_d)(void *vd, void *vn, void *vm, void *va,
                               void *vg, float_status *status, uint32_t desc)
{
    intptr_t j, i = simd_oprsz(desc);
    bool flip = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint32_t fpcr_ah = extract32(desc, SIMD_DATA_SHIFT + 2, 1);
    uint32_t negf_imag = extract32(desc, SIMD_DATA_SHIFT + 1, 1);
    uint32_t negf_real = flip ^ negf_imag;
    float64 negx_imag, negx_real;
    uint64_t *g = vg;

    /* With AH=0, use negx; with AH=1 use negf. */
    negx_real = (uint64_t)(negf_real & ~fpcr_ah) << 63;
    negx_imag = (uint64_t)(negf_imag & ~fpcr_ah) << 63;
    negf_real = (negf_real & fpcr_ah ? float_muladd_negate_product : 0);
    negf_imag = (negf_imag & fpcr_ah ? float_muladd_negate_product : 0);

    do {
        uint64_t pg = g[(i - 1) >> 6];
        do {
            float64 e1, e2, e3, e4, nr, ni, mr, mi, d;

            /* I holds the real index; J holds the imag index.  */
            j = i - sizeof(float64);
            i -= 2 * sizeof(float64);

            nr = *(float64 *)(vn + H1_2(i));
            ni = *(float64 *)(vn + H1_2(j));
            mr = *(float64 *)(vm + H1_2(i));
            mi = *(float64 *)(vm + H1_2(j));

            e2 = (flip ? ni : nr);
            e1 = (flip ? mi : mr) ^ negx_real;
            e4 = e2;
            e3 = (flip ? mr : mi) ^ negx_imag;

            if (likely((pg >> (i & 63)) & 1)) {
                d = *(float64 *)(va + H1_2(i));
                d = float64_muladd(e2, e1, d, negf_real, status);
                *(float64 *)(vd + H1_2(i)) = d;
            }
            if (likely((pg >> (j & 63)) & 1)) {
                d = *(float64 *)(va + H1_2(j));
                d = float64_muladd(e4, e3, d, negf_imag, status);
                *(float64 *)(vd + H1_2(j)) = d;
            }
        } while (i & 63);
    } while (i != 0);
}

/*
 * Load contiguous data, protected by a governing predicate.
 */

/*
 * Skip through a sequence of inactive elements in the guarding predicate @vg,
 * beginning at @reg_off bounded by @reg_max.  Return the offset of the active
 * element >= @reg_off, or @reg_max if there were no active elements at all.
 */
static intptr_t find_next_active(uint64_t *vg, intptr_t reg_off,
                                 intptr_t reg_max, int esz)
{
    uint64_t pg_mask = pred_esz_masks[esz];
    uint64_t pg = (vg[reg_off >> 6] & pg_mask) >> (reg_off & 63);

    /* In normal usage, the first element is active.  */
    if (likely(pg & 1)) {
        return reg_off;
    }

    if (pg == 0) {
        reg_off &= -64;
        do {
            reg_off += 64;
            if (unlikely(reg_off >= reg_max)) {
                /* The entire predicate was false.  */
                return reg_max;
            }
            pg = vg[reg_off >> 6] & pg_mask;
        } while (pg == 0);
    }
    reg_off += ctz64(pg);

    /* We should never see an out of range predicate bit set.  */
    tcg_debug_assert(reg_off < reg_max);
    return reg_off;
}

/*
 * Resolve the guest virtual address to info->host and info->flags.
 * If @nofault, return false if the page is invalid, otherwise
 * exit via page fault exception.
 */

bool sve_probe_page(SVEHostPage *info, bool nofault, CPUARMState *env,
                    target_ulong addr, int mem_off, MMUAccessType access_type,
                    int mmu_idx, uintptr_t retaddr)
{
    int flags;

    addr += mem_off;

    /*
     * User-only currently always issues with TBI.  See the comment
     * above useronly_clean_ptr.  Usually we clean this top byte away
     * during translation, but we can't do that for e.g. vector + imm
     * addressing modes.
     *
     * We currently always enable TBI for user-only, and do not provide
     * a way to turn it off.  So clean the pointer unconditionally here,
     * rather than look it up here, or pass it down from above.
     */
    addr = useronly_clean_ptr(addr);

#ifdef CONFIG_USER_ONLY
    flags = probe_access_flags(env, addr, 0, access_type, mmu_idx, nofault,
                               &info->host, retaddr);
#else
    CPUTLBEntryFull *full;
    flags = probe_access_full(env, addr, 0, access_type, mmu_idx, nofault,
                              &info->host, &full, retaddr);
#endif
    info->flags = flags;

    if (flags & TLB_INVALID_MASK) {
        g_assert(nofault);
        return false;
    }

#ifdef CONFIG_USER_ONLY
    memset(&info->attrs, 0, sizeof(info->attrs));
    /* Require both ANON and MTE; see allocation_tag_mem(). */
    info->tagged = (flags & PAGE_ANON) && (flags & PAGE_MTE);
#else
    info->attrs = full->attrs;
    info->tagged = full->extra.arm.pte_attrs == 0xf0;
#endif

    /* Ensure that info->host[] is relative to addr, not addr + mem_off. */
    info->host -= mem_off;
    return true;
}

/*
 * Find first active element on each page, and a loose bound for the
 * final element on each page.  Identify any single element that spans
 * the page boundary.  Return true if there are any active elements.
 */
bool sve_cont_ldst_elements(SVEContLdSt *info, target_ulong addr, uint64_t *vg,
                            intptr_t reg_max, int esz, int msize)
{
    const int esize = 1 << esz;
    const uint64_t pg_mask = pred_esz_masks[esz];
    intptr_t reg_off_first = -1, reg_off_last = -1, reg_off_split;
    intptr_t mem_off_last, mem_off_split;
    intptr_t page_split, elt_split;
    intptr_t i;

    /* Set all of the element indices to -1, and the TLB data to 0. */
    memset(info, -1, offsetof(SVEContLdSt, page));
    memset(info->page, 0, sizeof(info->page));

    /* Gross scan over the entire predicate to find bounds. */
    i = 0;
    do {
        uint64_t pg = vg[i] & pg_mask;
        if (pg) {
            reg_off_last = i * 64 + 63 - clz64(pg);
            if (reg_off_first < 0) {
                reg_off_first = i * 64 + ctz64(pg);
            }
        }
    } while (++i * 64 < reg_max);

    if (unlikely(reg_off_first < 0)) {
        /* No active elements, no pages touched. */
        return false;
    }
    tcg_debug_assert(reg_off_last >= 0 && reg_off_last < reg_max);

    info->reg_off_first[0] = reg_off_first;
    info->mem_off_first[0] = (reg_off_first >> esz) * msize;
    mem_off_last = (reg_off_last >> esz) * msize;

    page_split = -(addr | TARGET_PAGE_MASK);
    if (likely(mem_off_last + msize <= page_split)) {
        /* The entire operation fits within a single page. */
        info->reg_off_last[0] = reg_off_last;
        return true;
    }

    info->page_split = page_split;
    elt_split = page_split / msize;
    reg_off_split = elt_split << esz;
    mem_off_split = elt_split * msize;

    /*
     * This is the last full element on the first page, but it is not
     * necessarily active.  If there is no full element, i.e. the first
     * active element is the one that's split, this value remains -1.
     * It is useful as iteration bounds.
     */
    if (elt_split != 0) {
        info->reg_off_last[0] = reg_off_split - esize;
    }

    /* Determine if an unaligned element spans the pages.  */
    if (page_split % msize != 0) {
        /* It is helpful to know if the split element is active. */
        if ((vg[reg_off_split >> 6] >> (reg_off_split & 63)) & 1) {
            info->reg_off_split = reg_off_split;
            info->mem_off_split = mem_off_split;

            if (reg_off_split == reg_off_last) {
                /* The page crossing element is last. */
                return true;
            }
        }
        reg_off_split += esize;
        mem_off_split += msize;
    }

    /*
     * We do want the first active element on the second page, because
     * this may affect the address reported in an exception.
     */
    reg_off_split = find_next_active(vg, reg_off_split, reg_max, esz);
    tcg_debug_assert(reg_off_split <= reg_off_last);
    info->reg_off_first[1] = reg_off_split;
    info->mem_off_first[1] = (reg_off_split >> esz) * msize;
    info->reg_off_last[1] = reg_off_last;
    return true;
}

/*
 * Resolve the guest virtual addresses to info->page[].
 * Control the generation of page faults with @fault.  Return false if
 * there is no work to do, which can only happen with @fault == FAULT_NO.
 */
bool sve_cont_ldst_pages(SVEContLdSt *info, SVEContFault fault,
                         CPUARMState *env, target_ulong addr,
                         MMUAccessType access_type, uintptr_t retaddr)
{
    int mmu_idx = arm_env_mmu_index(env);
    int mem_off = info->mem_off_first[0];
    bool nofault = fault == FAULT_NO;
    bool have_work = true;

    if (!sve_probe_page(&info->page[0], nofault, env, addr, mem_off,
                        access_type, mmu_idx, retaddr)) {
        /* No work to be done. */
        return false;
    }

    if (likely(info->page_split < 0)) {
        /* The entire operation was on the one page. */
        return true;
    }

    /*
     * If the second page is invalid, then we want the fault address to be
     * the first byte on that page which is accessed.
     */
    if (info->mem_off_split >= 0) {
        /*
         * There is an element split across the pages.  The fault address
         * should be the first byte of the second page.
         */
        mem_off = info->page_split;
        /*
         * If the split element is also the first active element
         * of the vector, then:  For first-fault we should continue
         * to generate faults for the second page.  For no-fault,
         * we have work only if the second page is valid.
         */
        if (info->mem_off_first[0] < info->mem_off_split) {
            nofault = FAULT_FIRST;
            have_work = false;
        }
    } else {
        /*
         * There is no element split across the pages.  The fault address
         * should be the first active element on the second page.
         */
        mem_off = info->mem_off_first[1];
        /*
         * There must have been one active element on the first page,
         * so we're out of first-fault territory.
         */
        nofault = fault != FAULT_ALL;
    }

    have_work |= sve_probe_page(&info->page[1], nofault, env, addr, mem_off,
                                access_type, mmu_idx, retaddr);
    return have_work;
}

#ifndef CONFIG_USER_ONLY
void sve_cont_ldst_watchpoints(SVEContLdSt *info, CPUARMState *env,
                               uint64_t *vg, target_ulong addr,
                               int esize, int msize, int wp_access,
                               uintptr_t retaddr)
{
    intptr_t mem_off, reg_off, reg_last;
    int flags0 = info->page[0].flags;
    int flags1 = info->page[1].flags;

    if (likely(!((flags0 | flags1) & TLB_WATCHPOINT))) {
        return;
    }

    /* Indicate that watchpoints are handled. */
    info->page[0].flags = flags0 & ~TLB_WATCHPOINT;
    info->page[1].flags = flags1 & ~TLB_WATCHPOINT;

    if (flags0 & TLB_WATCHPOINT) {
        mem_off = info->mem_off_first[0];
        reg_off = info->reg_off_first[0];
        reg_last = info->reg_off_last[0];

        while (reg_off <= reg_last) {
            uint64_t pg = vg[reg_off >> 6];
            do {
                if ((pg >> (reg_off & 63)) & 1) {
                    cpu_check_watchpoint(env_cpu(env), addr + mem_off,
                                         msize, info->page[0].attrs,
                                         wp_access, retaddr);
                }
                reg_off += esize;
                mem_off += msize;
            } while (reg_off <= reg_last && (reg_off & 63));
        }
    }

    mem_off = info->mem_off_split;
    if (mem_off >= 0) {
        cpu_check_watchpoint(env_cpu(env), addr + mem_off, msize,
                             info->page[0].attrs, wp_access, retaddr);
    }

    mem_off = info->mem_off_first[1];
    if ((flags1 & TLB_WATCHPOINT) && mem_off >= 0) {
        reg_off = info->reg_off_first[1];
        reg_last = info->reg_off_last[1];

        do {
            uint64_t pg = vg[reg_off >> 6];
            do {
                if ((pg >> (reg_off & 63)) & 1) {
                    cpu_check_watchpoint(env_cpu(env), addr + mem_off,
                                         msize, info->page[1].attrs,
                                         wp_access, retaddr);
                }
                reg_off += esize;
                mem_off += msize;
            } while (reg_off & 63);
        } while (reg_off <= reg_last);
    }
}
#endif

void sve_cont_ldst_mte_check(SVEContLdSt *info, CPUARMState *env,
                             uint64_t *vg, target_ulong addr, int esize,
                             int msize, uint32_t mtedesc, uintptr_t ra)
{
    intptr_t mem_off, reg_off, reg_last;

    /* Process the page only if MemAttr == Tagged. */
    if (info->page[0].tagged) {
        mem_off = info->mem_off_first[0];
        reg_off = info->reg_off_first[0];
        reg_last = info->reg_off_split;
        if (reg_last < 0) {
            reg_last = info->reg_off_last[0];
        }

        do {
            uint64_t pg = vg[reg_off >> 6];
            do {
                if ((pg >> (reg_off & 63)) & 1) {
                    mte_check(env, mtedesc, addr, ra);
                }
                reg_off += esize;
                mem_off += msize;
            } while (reg_off <= reg_last && (reg_off & 63));
        } while (reg_off <= reg_last);
    }

    mem_off = info->mem_off_first[1];
    if (mem_off >= 0 && info->page[1].tagged) {
        reg_off = info->reg_off_first[1];
        reg_last = info->reg_off_last[1];

        do {
            uint64_t pg = vg[reg_off >> 6];
            do {
                if ((pg >> (reg_off & 63)) & 1) {
                    mte_check(env, mtedesc, addr, ra);
                }
                reg_off += esize;
                mem_off += msize;
            } while (reg_off & 63);
        } while (reg_off <= reg_last);
    }
}

/*
 * Common helper for all contiguous 1,2,3,4-register predicated stores.
 */
static inline QEMU_ALWAYS_INLINE
void sve_ldN_r(CPUARMState *env, uint64_t *vg, const target_ulong addr,
               uint32_t desc, const uintptr_t retaddr,
               const int esz, const int msz, const int N, uint32_t mtedesc,
               sve_ldst1_host_fn *host_fn,
               sve_ldst1_tlb_fn *tlb_fn)
{
    const unsigned rd = simd_data(desc);
    const intptr_t reg_max = simd_oprsz(desc);
    intptr_t reg_off, reg_last, mem_off;
    SVEContLdSt info;
    void *host;
    int flags, i;

    /* Find the active elements.  */
    if (!sve_cont_ldst_elements(&info, addr, vg, reg_max, esz, N << msz)) {
        /* The entire predicate was false; no load occurs.  */
        for (i = 0; i < N; ++i) {
            memset(&env->vfp.zregs[(rd + i) & 31], 0, reg_max);
        }
        return;
    }

    /* Probe the page(s).  Exit with exception for any invalid page. */
    sve_cont_ldst_pages(&info, FAULT_ALL, env, addr, MMU_DATA_LOAD, retaddr);

    /* Handle watchpoints for all active elements. */
    sve_cont_ldst_watchpoints(&info, env, vg, addr, 1 << esz, N << msz,
                              BP_MEM_READ, retaddr);

    /*
     * Handle mte checks for all active elements.
     * Since TBI must be set for MTE, !mtedesc => !mte_active.
     */
    if (mtedesc) {
        sve_cont_ldst_mte_check(&info, env, vg, addr, 1 << esz, N << msz,
                                mtedesc, retaddr);
    }

    flags = info.page[0].flags | info.page[1].flags;
    if (unlikely(flags != 0)) {
        /*
         * At least one page includes MMIO.
         * Any bus operation can fail with cpu_transaction_failed,
         * which for ARM will raise SyncExternal.  Perform the load
         * into scratch memory to preserve register state until the end.
         */
        ARMVectorReg scratch[4] = { };

        mem_off = info.mem_off_first[0];
        reg_off = info.reg_off_first[0];
        reg_last = info.reg_off_last[1];
        if (reg_last < 0) {
            reg_last = info.reg_off_split;
            if (reg_last < 0) {
                reg_last = info.reg_off_last[0];
            }
        }

        do {
            uint64_t pg = vg[reg_off >> 6];
            do {
                if ((pg >> (reg_off & 63)) & 1) {
                    for (i = 0; i < N; ++i) {
                        tlb_fn(env, &scratch[i], reg_off,
                               addr + mem_off + (i << msz), retaddr);
                    }
                }
                reg_off += 1 << esz;
                mem_off += N << msz;
            } while (reg_off & 63);
        } while (reg_off <= reg_last);

        for (i = 0; i < N; ++i) {
            memcpy(&env->vfp.zregs[(rd + i) & 31], &scratch[i], reg_max);
        }
        return;
    }

    /* The entire operation is in RAM, on valid pages. */

    for (i = 0; i < N; ++i) {
        memset(&env->vfp.zregs[(rd + i) & 31], 0, reg_max);
    }

    mem_off = info.mem_off_first[0];
    reg_off = info.reg_off_first[0];
    reg_last = info.reg_off_last[0];
    host = info.page[0].host;

    set_helper_retaddr(retaddr);

    while (reg_off <= reg_last) {
        uint64_t pg = vg[reg_off >> 6];
        do {
            if ((pg >> (reg_off & 63)) & 1) {
                for (i = 0; i < N; ++i) {
                    host_fn(&env->vfp.zregs[(rd + i) & 31], reg_off,
                            host + mem_off + (i << msz));
                }
            }
            reg_off += 1 << esz;
            mem_off += N << msz;
        } while (reg_off <= reg_last && (reg_off & 63));
    }

    clear_helper_retaddr();

    /*
     * Use the slow path to manage the cross-page misalignment.
     * But we know this is RAM and cannot trap.
     */
    mem_off = info.mem_off_split;
    if (unlikely(mem_off >= 0)) {
        reg_off = info.reg_off_split;
        for (i = 0; i < N; ++i) {
            tlb_fn(env, &env->vfp.zregs[(rd + i) & 31], reg_off,
                   addr + mem_off + (i << msz), retaddr);
        }
    }

    mem_off = info.mem_off_first[1];
    if (unlikely(mem_off >= 0)) {
        reg_off = info.reg_off_first[1];
        reg_last = info.reg_off_last[1];
        host = info.page[1].host;

        set_helper_retaddr(retaddr);

        do {
            uint64_t pg = vg[reg_off >> 6];
            do {
                if ((pg >> (reg_off & 63)) & 1) {
                    for (i = 0; i < N; ++i) {
                        host_fn(&env->vfp.zregs[(rd + i) & 31], reg_off,
                                host + mem_off + (i << msz));
                    }
                }
                reg_off += 1 << esz;
                mem_off += N << msz;
            } while (reg_off & 63);
        } while (reg_off <= reg_last);

        clear_helper_retaddr();
    }
}

static inline QEMU_ALWAYS_INLINE
void sve_ldN_r_mte(CPUARMState *env, uint64_t *vg, target_ulong addr,
                   uint32_t desc, const uintptr_t ra,
                   const int esz, const int msz, const int N,
                   sve_ldst1_host_fn *host_fn,
                   sve_ldst1_tlb_fn *tlb_fn)
{
    uint32_t mtedesc = desc >> (SIMD_DATA_SHIFT + SVE_MTEDESC_SHIFT);
    int bit55 = extract64(addr, 55, 1);

    /* Remove mtedesc from the normal sve descriptor. */
    desc = extract32(desc, 0, SIMD_DATA_SHIFT + SVE_MTEDESC_SHIFT);

    /* Perform gross MTE suppression early. */
    if (!tbi_check(mtedesc, bit55) ||
        tcma_check(mtedesc, bit55, allocation_tag_from_addr(addr))) {
        mtedesc = 0;
    }

    sve_ldN_r(env, vg, addr, desc, ra, esz, msz, N, mtedesc, host_fn, tlb_fn);
}

#define DO_LD1_1(NAME, ESZ)                                             \
void HELPER(sve_##NAME##_r)(CPUARMState *env, void *vg,                 \
                            target_ulong addr, uint32_t desc)           \
{                                                                       \
    sve_ldN_r(env, vg, addr, desc, GETPC(), ESZ, MO_8, 1, 0,            \
              sve_##NAME##_host, sve_##NAME##_tlb);                     \
}                                                                       \
void HELPER(sve_##NAME##_r_mte)(CPUARMState *env, void *vg,             \
                                target_ulong addr, uint32_t desc)       \
{                                                                       \
    sve_ldN_r_mte(env, vg, addr, desc, GETPC(), ESZ, MO_8, 1,           \
                  sve_##NAME##_host, sve_##NAME##_tlb);                 \
}

#define DO_LD1_2(NAME, ESZ, MSZ)                                        \
void HELPER(sve_##NAME##_le_r)(CPUARMState *env, void *vg,              \
                               target_ulong addr, uint32_t desc)        \
{                                                                       \
    sve_ldN_r(env, vg, addr, desc, GETPC(), ESZ, MSZ, 1, 0,             \
              sve_##NAME##_le_host, sve_##NAME##_le_tlb);               \
}                                                                       \
void HELPER(sve_##NAME##_be_r)(CPUARMState *env, void *vg,              \
                               target_ulong addr, uint32_t desc)        \
{                                                                       \
    sve_ldN_r(env, vg, addr, desc, GETPC(), ESZ, MSZ, 1, 0,             \
              sve_##NAME##_be_host, sve_##NAME##_be_tlb);               \
}                                                                       \
void HELPER(sve_##NAME##_le_r_mte)(CPUARMState *env, void *vg,          \
                                   target_ulong addr, uint32_t desc)    \
{                                                                       \
    sve_ldN_r_mte(env, vg, addr, desc, GETPC(), ESZ, MSZ, 1,            \
                  sve_##NAME##_le_host, sve_##NAME##_le_tlb);           \
}                                                                       \
void HELPER(sve_##NAME##_be_r_mte)(CPUARMState *env, void *vg,          \
                                   target_ulong addr, uint32_t desc)    \
{                                                                       \
    sve_ldN_r_mte(env, vg, addr, desc, GETPC(), ESZ, MSZ, 1,            \
                  sve_##NAME##_be_host, sve_##NAME##_be_tlb);           \
}

DO_LD1_1(ld1bb,  MO_8)
DO_LD1_1(ld1bhu, MO_16)
DO_LD1_1(ld1bhs, MO_16)
DO_LD1_1(ld1bsu, MO_32)
DO_LD1_1(ld1bss, MO_32)
DO_LD1_1(ld1bdu, MO_64)
DO_LD1_1(ld1bds, MO_64)

DO_LD1_2(ld1hh,  MO_16, MO_16)
DO_LD1_2(ld1hsu, MO_32, MO_16)
DO_LD1_2(ld1hss, MO_32, MO_16)
DO_LD1_2(ld1hdu, MO_64, MO_16)
DO_LD1_2(ld1hds, MO_64, MO_16)

DO_LD1_2(ld1ss,  MO_32, MO_32)
DO_LD1_2(ld1sdu, MO_64, MO_32)
DO_LD1_2(ld1sds, MO_64, MO_32)

DO_LD1_2(ld1dd,  MO_64, MO_64)

#undef DO_LD1_1
#undef DO_LD1_2

#define DO_LDN_1(N)                                                     \
void HELPER(sve_ld##N##bb_r)(CPUARMState *env, void *vg,                \
                             target_ulong addr, uint32_t desc)          \
{                                                                       \
    sve_ldN_r(env, vg, addr, desc, GETPC(), MO_8, MO_8, N, 0,           \
              sve_ld1bb_host, sve_ld1bb_tlb);                           \
}                                                                       \
void HELPER(sve_ld##N##bb_r_mte)(CPUARMState *env, void *vg,            \
                                 target_ulong addr, uint32_t desc)      \
{                                                                       \
    sve_ldN_r_mte(env, vg, addr, desc, GETPC(), MO_8, MO_8, N,          \
                  sve_ld1bb_host, sve_ld1bb_tlb);                       \
}

#define DO_LDN_2(N, SUFF, ESZ)                                          \
void HELPER(sve_ld##N##SUFF##_le_r)(CPUARMState *env, void *vg,         \
                                    target_ulong addr, uint32_t desc)   \
{                                                                       \
    sve_ldN_r(env, vg, addr, desc, GETPC(), ESZ, ESZ, N, 0,             \
              sve_ld1##SUFF##_le_host, sve_ld1##SUFF##_le_tlb);         \
}                                                                       \
void HELPER(sve_ld##N##SUFF##_be_r)(CPUARMState *env, void *vg,         \
                                    target_ulong addr, uint32_t desc)   \
{                                                                       \
    sve_ldN_r(env, vg, addr, desc, GETPC(), ESZ, ESZ, N, 0,             \
              sve_ld1##SUFF##_be_host, sve_ld1##SUFF##_be_tlb);         \
}                                                                       \
void HELPER(sve_ld##N##SUFF##_le_r_mte)(CPUARMState *env, void *vg,     \
                                        target_ulong addr, uint32_t desc) \
{                                                                       \
    sve_ldN_r_mte(env, vg, addr, desc, GETPC(), ESZ, ESZ, N,            \
                  sve_ld1##SUFF##_le_host, sve_ld1##SUFF##_le_tlb);     \
}                                                                       \
void HELPER(sve_ld##N##SUFF##_be_r_mte)(CPUARMState *env, void *vg,     \
                                        target_ulong addr, uint32_t desc) \
{                                                                       \
    sve_ldN_r_mte(env, vg, addr, desc, GETPC(), ESZ, ESZ, N,            \
                  sve_ld1##SUFF##_be_host, sve_ld1##SUFF##_be_tlb);     \
}

DO_LDN_1(2)
DO_LDN_1(3)
DO_LDN_1(4)

DO_LDN_2(2, hh, MO_16)
DO_LDN_2(3, hh, MO_16)
DO_LDN_2(4, hh, MO_16)

DO_LDN_2(2, ss, MO_32)
DO_LDN_2(3, ss, MO_32)
DO_LDN_2(4, ss, MO_32)

DO_LDN_2(2, dd, MO_64)
DO_LDN_2(3, dd, MO_64)
DO_LDN_2(4, dd, MO_64)

#undef DO_LDN_1
#undef DO_LDN_2

/*
 * Load contiguous data, first-fault and no-fault.
 *
 * For user-only, we control the race between page_check_range and
 * another thread's munmap by using set/clear_helper_retaddr.  Any
 * SEGV that occurs between those markers is assumed to be because
 * the guest page vanished.  Keep that block as small as possible
 * so that unrelated QEMU bugs are not blamed on the guest.
 */

/* Fault on byte I.  All bits in FFR from I are cleared.  The vector
 * result from I is CONSTRAINED UNPREDICTABLE; we choose the MERGE
 * option, which leaves subsequent data unchanged.
 */
static void record_fault(CPUARMState *env, uintptr_t i, uintptr_t oprsz)
{
    uint64_t *ffr = env->vfp.pregs[FFR_PRED_NUM].p;

    if (i & 63) {
        ffr[i / 64] &= MAKE_64BIT_MASK(0, i & 63);
        i = ROUND_UP(i, 64);
    }
    for (; i < oprsz; i += 64) {
        ffr[i / 64] = 0;
    }
}

/*
 * Common helper for all contiguous no-fault and first-fault loads.
 */
static inline QEMU_ALWAYS_INLINE
void sve_ldnfff1_r(CPUARMState *env, void *vg, const target_ulong addr,
                   uint32_t desc, const uintptr_t retaddr, uint32_t mtedesc,
                   const int esz, const int msz, const SVEContFault fault,
                   sve_ldst1_host_fn *host_fn,
                   sve_ldst1_tlb_fn *tlb_fn)
{
    const unsigned rd = simd_data(desc);
    void *vd = &env->vfp.zregs[rd];
    const intptr_t reg_max = simd_oprsz(desc);
    intptr_t reg_off, mem_off, reg_last;
    SVEContLdSt info;
    int flags;
    void *host;

    /* Find the active elements.  */
    if (!sve_cont_ldst_elements(&info, addr, vg, reg_max, esz, 1 << msz)) {
        /* The entire predicate was false; no load occurs.  */
        memset(vd, 0, reg_max);
        return;
    }
    reg_off = info.reg_off_first[0];

    /* Probe the page(s). */
    if (!sve_cont_ldst_pages(&info, fault, env, addr, MMU_DATA_LOAD, retaddr)) {
        /* Fault on first element. */
        tcg_debug_assert(fault == FAULT_NO);
        memset(vd, 0, reg_max);
        goto do_fault;
    }

    mem_off = info.mem_off_first[0];
    flags = info.page[0].flags;

    /*
     * Disable MTE checking if the Tagged bit is not set.  Since TBI must
     * be set within MTEDESC for MTE, !mtedesc => !mte_active.
     */
    if (!info.page[0].tagged) {
        mtedesc = 0;
    }

    if (fault == FAULT_FIRST) {
        /* Trapping mte check for the first-fault element.  */
        if (mtedesc) {
            mte_check(env, mtedesc, addr + mem_off, retaddr);
        }

        /*
         * Special handling of the first active element,
         * if it crosses a page boundary or is MMIO.
         */
        bool is_split = mem_off == info.mem_off_split;
        if (unlikely(flags != 0) || unlikely(is_split)) {
            /*
             * Use the slow path for cross-page handling.
             * Might trap for MMIO or watchpoints.
             */
            tlb_fn(env, vd, reg_off, addr + mem_off, retaddr);

            /* After any fault, zero the other elements. */
            swap_memzero(vd, reg_off);
            reg_off += 1 << esz;
            mem_off += 1 << msz;
            swap_memzero(vd + reg_off, reg_max - reg_off);

            if (is_split) {
                goto second_page;
            }
        } else {
            memset(vd, 0, reg_max);
        }
    } else {
        memset(vd, 0, reg_max);
        if (unlikely(mem_off == info.mem_off_split)) {
            /* The first active element crosses a page boundary. */
            flags |= info.page[1].flags;
            if (unlikely(flags & TLB_MMIO)) {
                /* Some page is MMIO, see below. */
                goto do_fault;
            }
            if (unlikely(flags & TLB_WATCHPOINT) &&
                (cpu_watchpoint_address_matches
                 (env_cpu(env), addr + mem_off, 1 << msz)
                 & BP_MEM_READ)) {
                /* Watchpoint hit, see below. */
                goto do_fault;
            }
            if (mtedesc && !mte_probe(env, mtedesc, addr + mem_off)) {
                goto do_fault;
            }
            /*
             * Use the slow path for cross-page handling.
             * This is RAM, without a watchpoint, and will not trap.
             */
            tlb_fn(env, vd, reg_off, addr + mem_off, retaddr);
            goto second_page;
        }
    }

    /*
     * From this point on, all memory operations are MemSingleNF.
     *
     * Per the MemSingleNF pseudocode, a no-fault load from Device memory
     * must not actually hit the bus -- it returns (UNKNOWN, FAULT) instead.
     *
     * Unfortuately we do not have access to the memory attributes from the
     * PTE to tell Device memory from Normal memory.  So we make a mostly
     * correct check, and indicate (UNKNOWN, FAULT) for any MMIO.
     * This gives the right answer for the common cases of "Normal memory,
     * backed by host RAM" and "Device memory, backed by MMIO".
     * The architecture allows us to suppress an NF load and return
     * (UNKNOWN, FAULT) for any reason, so our behaviour for the corner
     * case of "Normal memory, backed by MMIO" is permitted.  The case we
     * get wrong is "Device memory, backed by host RAM", for which we
     * should return (UNKNOWN, FAULT) for but do not.
     *
     * Similarly, CPU_BP breakpoints would raise exceptions, and so
     * return (UNKNOWN, FAULT).  For simplicity, we consider gdb and
     * architectural breakpoints the same.
     */
    if (unlikely(flags & TLB_MMIO)) {
        goto do_fault;
    }

    reg_last = info.reg_off_last[0];
    host = info.page[0].host;

    set_helper_retaddr(retaddr);

    do {
        uint64_t pg = *(uint64_t *)(vg + (reg_off >> 3));
        do {
            if ((pg >> (reg_off & 63)) & 1) {
                if (unlikely(flags & TLB_WATCHPOINT) &&
                    (cpu_watchpoint_address_matches
                     (env_cpu(env), addr + mem_off, 1 << msz)
                     & BP_MEM_READ)) {
                    clear_helper_retaddr();
                    goto do_fault;
                }
                if (mtedesc && !mte_probe(env, mtedesc, addr + mem_off)) {
                    clear_helper_retaddr();
                    goto do_fault;
                }
                host_fn(vd, reg_off, host + mem_off);
            }
            reg_off += 1 << esz;
            mem_off += 1 << msz;
        } while (reg_off <= reg_last && (reg_off & 63));
    } while (reg_off <= reg_last);

    clear_helper_retaddr();

    /*
     * MemSingleNF is allowed to fail for any reason.  We have special
     * code above to handle the first element crossing a page boundary.
     * As an implementation choice, decline to handle a cross-page element
     * in any other position.
     */
    reg_off = info.reg_off_split;
    if (reg_off >= 0) {
        goto do_fault;
    }

 second_page:
    reg_off = info.reg_off_first[1];
    if (likely(reg_off < 0)) {
        /* No active elements on the second page.  All done. */
        return;
    }

    /*
     * MemSingleNF is allowed to fail for any reason.  As an implementation
     * choice, decline to handle elements on the second page.  This should
     * be low frequency as the guest walks through memory -- the next
     * iteration of the guest's loop should be aligned on the page boundary,
     * and then all following iterations will stay aligned.
     */

 do_fault:
    record_fault(env, reg_off, reg_max);
}

static inline QEMU_ALWAYS_INLINE
void sve_ldnfff1_r_mte(CPUARMState *env, void *vg, target_ulong addr,
                       uint32_t desc, const uintptr_t retaddr,
                       const int esz, const int msz, const SVEContFault fault,
                       sve_ldst1_host_fn *host_fn,
                       sve_ldst1_tlb_fn *tlb_fn)
{
    uint32_t mtedesc = desc >> (SIMD_DATA_SHIFT + SVE_MTEDESC_SHIFT);
    int bit55 = extract64(addr, 55, 1);

    /* Remove mtedesc from the normal sve descriptor. */
    desc = extract32(desc, 0, SIMD_DATA_SHIFT + SVE_MTEDESC_SHIFT);

    /* Perform gross MTE suppression early. */
    if (!tbi_check(mtedesc, bit55) ||
        tcma_check(mtedesc, bit55, allocation_tag_from_addr(addr))) {
        mtedesc = 0;
    }

    sve_ldnfff1_r(env, vg, addr, desc, retaddr, mtedesc,
                  esz, msz, fault, host_fn, tlb_fn);
}

#define DO_LDFF1_LDNF1_1(PART, ESZ)                                     \
void HELPER(sve_ldff1##PART##_r)(CPUARMState *env, void *vg,            \
                                 target_ulong addr, uint32_t desc)      \
{                                                                       \
    sve_ldnfff1_r(env, vg, addr, desc, GETPC(), 0, ESZ, MO_8, FAULT_FIRST, \
                  sve_ld1##PART##_host, sve_ld1##PART##_tlb);           \
}                                                                       \
void HELPER(sve_ldnf1##PART##_r)(CPUARMState *env, void *vg,            \
                                 target_ulong addr, uint32_t desc)      \
{                                                                       \
    sve_ldnfff1_r(env, vg, addr, desc, GETPC(), 0, ESZ, MO_8, FAULT_NO, \
                  sve_ld1##PART##_host, sve_ld1##PART##_tlb);           \
}                                                                       \
void HELPER(sve_ldff1##PART##_r_mte)(CPUARMState *env, void *vg,        \
                                     target_ulong addr, uint32_t desc)  \
{                                                                       \
    sve_ldnfff1_r_mte(env, vg, addr, desc, GETPC(), ESZ, MO_8, FAULT_FIRST, \
                      sve_ld1##PART##_host, sve_ld1##PART##_tlb);       \
}                                                                       \
void HELPER(sve_ldnf1##PART##_r_mte)(CPUARMState *env, void *vg,        \
                                     target_ulong addr, uint32_t desc)  \
{                                                                       \
    sve_ldnfff1_r_mte(env, vg, addr, desc, GETPC(), ESZ, MO_8, FAULT_NO, \
                  sve_ld1##PART##_host, sve_ld1##PART##_tlb);           \
}

#define DO_LDFF1_LDNF1_2(PART, ESZ, MSZ)                                \
void HELPER(sve_ldff1##PART##_le_r)(CPUARMState *env, void *vg,         \
                                    target_ulong addr, uint32_t desc)   \
{                                                                       \
    sve_ldnfff1_r(env, vg, addr, desc, GETPC(), 0, ESZ, MSZ, FAULT_FIRST, \
                  sve_ld1##PART##_le_host, sve_ld1##PART##_le_tlb);     \
}                                                                       \
void HELPER(sve_ldnf1##PART##_le_r)(CPUARMState *env, void *vg,         \
                                    target_ulong addr, uint32_t desc)   \
{                                                                       \
    sve_ldnfff1_r(env, vg, addr, desc, GETPC(), 0, ESZ, MSZ, FAULT_NO,  \
                  sve_ld1##PART##_le_host, sve_ld1##PART##_le_tlb);     \
}                                                                       \
void HELPER(sve_ldff1##PART##_be_r)(CPUARMState *env, void *vg,         \
                                    target_ulong addr, uint32_t desc)   \
{                                                                       \
    sve_ldnfff1_r(env, vg, addr, desc, GETPC(), 0, ESZ, MSZ, FAULT_FIRST, \
                  sve_ld1##PART##_be_host, sve_ld1##PART##_be_tlb);     \
}                                                                       \
void HELPER(sve_ldnf1##PART##_be_r)(CPUARMState *env, void *vg,         \
                                    target_ulong addr, uint32_t desc)   \
{                                                                       \
    sve_ldnfff1_r(env, vg, addr, desc, GETPC(), 0, ESZ, MSZ, FAULT_NO,  \
                  sve_ld1##PART##_be_host, sve_ld1##PART##_be_tlb);     \
}                                                                       \
void HELPER(sve_ldff1##PART##_le_r_mte)(CPUARMState *env, void *vg,     \
                                        target_ulong addr, uint32_t desc) \
{                                                                       \
    sve_ldnfff1_r_mte(env, vg, addr, desc, GETPC(), ESZ, MSZ, FAULT_FIRST, \
                      sve_ld1##PART##_le_host, sve_ld1##PART##_le_tlb); \
}                                                                       \
void HELPER(sve_ldnf1##PART##_le_r_mte)(CPUARMState *env, void *vg,     \
                                        target_ulong addr, uint32_t desc) \
{                                                                       \
    sve_ldnfff1_r_mte(env, vg, addr, desc, GETPC(), ESZ, MSZ, FAULT_NO, \
                      sve_ld1##PART##_le_host, sve_ld1##PART##_le_tlb); \
}                                                                       \
void HELPER(sve_ldff1##PART##_be_r_mte)(CPUARMState *env, void *vg,     \
                                        target_ulong addr, uint32_t desc) \
{                                                                       \
    sve_ldnfff1_r_mte(env, vg, addr, desc, GETPC(), ESZ, MSZ, FAULT_FIRST, \
                      sve_ld1##PART##_be_host, sve_ld1##PART##_be_tlb); \
}                                                                       \
void HELPER(sve_ldnf1##PART##_be_r_mte)(CPUARMState *env, void *vg,     \
                                        target_ulong addr, uint32_t desc) \
{                                                                       \
    sve_ldnfff1_r_mte(env, vg, addr, desc, GETPC(), ESZ, MSZ, FAULT_NO, \
                      sve_ld1##PART##_be_host, sve_ld1##PART##_be_tlb); \
}

DO_LDFF1_LDNF1_1(bb,  MO_8)
DO_LDFF1_LDNF1_1(bhu, MO_16)
DO_LDFF1_LDNF1_1(bhs, MO_16)
DO_LDFF1_LDNF1_1(bsu, MO_32)
DO_LDFF1_LDNF1_1(bss, MO_32)
DO_LDFF1_LDNF1_1(bdu, MO_64)
DO_LDFF1_LDNF1_1(bds, MO_64)

DO_LDFF1_LDNF1_2(hh,  MO_16, MO_16)
DO_LDFF1_LDNF1_2(hsu, MO_32, MO_16)
DO_LDFF1_LDNF1_2(hss, MO_32, MO_16)
DO_LDFF1_LDNF1_2(hdu, MO_64, MO_16)
DO_LDFF1_LDNF1_2(hds, MO_64, MO_16)

DO_LDFF1_LDNF1_2(ss,  MO_32, MO_32)
DO_LDFF1_LDNF1_2(sdu, MO_64, MO_32)
DO_LDFF1_LDNF1_2(sds, MO_64, MO_32)

DO_LDFF1_LDNF1_2(dd,  MO_64, MO_64)

#undef DO_LDFF1_LDNF1_1
#undef DO_LDFF1_LDNF1_2

/*
 * Common helper for all contiguous 1,2,3,4-register predicated stores.
 */

static inline QEMU_ALWAYS_INLINE
void sve_stN_r(CPUARMState *env, uint64_t *vg, target_ulong addr,
               uint32_t desc, const uintptr_t retaddr,
               const int esz, const int msz, const int N, uint32_t mtedesc,
               sve_ldst1_host_fn *host_fn,
               sve_ldst1_tlb_fn *tlb_fn)
{
    const unsigned rd = simd_data(desc);
    const intptr_t reg_max = simd_oprsz(desc);
    intptr_t reg_off, reg_last, mem_off;
    SVEContLdSt info;
    void *host;
    int i, flags;

    /* Find the active elements.  */
    if (!sve_cont_ldst_elements(&info, addr, vg, reg_max, esz, N << msz)) {
        /* The entire predicate was false; no store occurs.  */
        return;
    }

    /* Probe the page(s).  Exit with exception for any invalid page. */
    sve_cont_ldst_pages(&info, FAULT_ALL, env, addr, MMU_DATA_STORE, retaddr);

    /* Handle watchpoints for all active elements. */
    sve_cont_ldst_watchpoints(&info, env, vg, addr, 1 << esz, N << msz,
                              BP_MEM_WRITE, retaddr);

    /*
     * Handle mte checks for all active elements.
     * Since TBI must be set for MTE, !mtedesc => !mte_active.
     */
    if (mtedesc) {
        sve_cont_ldst_mte_check(&info, env, vg, addr, 1 << esz, N << msz,
                                mtedesc, retaddr);
    }

    flags = info.page[0].flags | info.page[1].flags;
    if (unlikely(flags != 0)) {
        /*
         * At least one page includes MMIO.
         * Any bus operation can fail with cpu_transaction_failed,
         * which for ARM will raise SyncExternal.  We cannot avoid
         * this fault and will leave with the store incomplete.
         */
        mem_off = info.mem_off_first[0];
        reg_off = info.reg_off_first[0];
        reg_last = info.reg_off_last[1];
        if (reg_last < 0) {
            reg_last = info.reg_off_split;
            if (reg_last < 0) {
                reg_last = info.reg_off_last[0];
            }
        }

        do {
            uint64_t pg = vg[reg_off >> 6];
            do {
                if ((pg >> (reg_off & 63)) & 1) {
                    for (i = 0; i < N; ++i) {
                        tlb_fn(env, &env->vfp.zregs[(rd + i) & 31], reg_off,
                               addr + mem_off + (i << msz), retaddr);
                    }
                }
                reg_off += 1 << esz;
                mem_off += N << msz;
            } while (reg_off & 63);
        } while (reg_off <= reg_last);
        return;
    }

    mem_off = info.mem_off_first[0];
    reg_off = info.reg_off_first[0];
    reg_last = info.reg_off_last[0];
    host = info.page[0].host;

    set_helper_retaddr(retaddr);

    while (reg_off <= reg_last) {
        uint64_t pg = vg[reg_off >> 6];
        do {
            if ((pg >> (reg_off & 63)) & 1) {
                for (i = 0; i < N; ++i) {
                    host_fn(&env->vfp.zregs[(rd + i) & 31], reg_off,
                            host + mem_off + (i << msz));
                }
            }
            reg_off += 1 << esz;
            mem_off += N << msz;
        } while (reg_off <= reg_last && (reg_off & 63));
    }

    clear_helper_retaddr();

    /*
     * Use the slow path to manage the cross-page misalignment.
     * But we know this is RAM and cannot trap.
     */
    mem_off = info.mem_off_split;
    if (unlikely(mem_off >= 0)) {
        reg_off = info.reg_off_split;
        for (i = 0; i < N; ++i) {
            tlb_fn(env, &env->vfp.zregs[(rd + i) & 31], reg_off,
                   addr + mem_off + (i << msz), retaddr);
        }
    }

    mem_off = info.mem_off_first[1];
    if (unlikely(mem_off >= 0)) {
        reg_off = info.reg_off_first[1];
        reg_last = info.reg_off_last[1];
        host = info.page[1].host;

        set_helper_retaddr(retaddr);

        do {
            uint64_t pg = vg[reg_off >> 6];
            do {
                if ((pg >> (reg_off & 63)) & 1) {
                    for (i = 0; i < N; ++i) {
                        host_fn(&env->vfp.zregs[(rd + i) & 31], reg_off,
                                host + mem_off + (i << msz));
                    }
                }
                reg_off += 1 << esz;
                mem_off += N << msz;
            } while (reg_off & 63);
        } while (reg_off <= reg_last);

        clear_helper_retaddr();
    }
}

static inline QEMU_ALWAYS_INLINE
void sve_stN_r_mte(CPUARMState *env, uint64_t *vg, target_ulong addr,
                   uint32_t desc, const uintptr_t ra,
                   const int esz, const int msz, const int N,
                   sve_ldst1_host_fn *host_fn,
                   sve_ldst1_tlb_fn *tlb_fn)
{
    uint32_t mtedesc = desc >> (SIMD_DATA_SHIFT + SVE_MTEDESC_SHIFT);
    int bit55 = extract64(addr, 55, 1);

    /* Remove mtedesc from the normal sve descriptor. */
    desc = extract32(desc, 0, SIMD_DATA_SHIFT + SVE_MTEDESC_SHIFT);

    /* Perform gross MTE suppression early. */
    if (!tbi_check(mtedesc, bit55) ||
        tcma_check(mtedesc, bit55, allocation_tag_from_addr(addr))) {
        mtedesc = 0;
    }

    sve_stN_r(env, vg, addr, desc, ra, esz, msz, N, mtedesc, host_fn, tlb_fn);
}

#define DO_STN_1(N, NAME, ESZ)                                          \
void HELPER(sve_st##N##NAME##_r)(CPUARMState *env, void *vg,            \
                                 target_ulong addr, uint32_t desc)      \
{                                                                       \
    sve_stN_r(env, vg, addr, desc, GETPC(), ESZ, MO_8, N, 0,            \
              sve_st1##NAME##_host, sve_st1##NAME##_tlb);               \
}                                                                       \
void HELPER(sve_st##N##NAME##_r_mte)(CPUARMState *env, void *vg,        \
                                     target_ulong addr, uint32_t desc)  \
{                                                                       \
    sve_stN_r_mte(env, vg, addr, desc, GETPC(), ESZ, MO_8, N,           \
                  sve_st1##NAME##_host, sve_st1##NAME##_tlb);           \
}

#define DO_STN_2(N, NAME, ESZ, MSZ)                                     \
void HELPER(sve_st##N##NAME##_le_r)(CPUARMState *env, void *vg,         \
                                    target_ulong addr, uint32_t desc)   \
{                                                                       \
    sve_stN_r(env, vg, addr, desc, GETPC(), ESZ, MSZ, N, 0,             \
              sve_st1##NAME##_le_host, sve_st1##NAME##_le_tlb);         \
}                                                                       \
void HELPER(sve_st##N##NAME##_be_r)(CPUARMState *env, void *vg,         \
                                    target_ulong addr, uint32_t desc)   \
{                                                                       \
    sve_stN_r(env, vg, addr, desc, GETPC(), ESZ, MSZ, N, 0,             \
              sve_st1##NAME##_be_host, sve_st1##NAME##_be_tlb);         \
}                                                                       \
void HELPER(sve_st##N##NAME##_le_r_mte)(CPUARMState *env, void *vg,     \
                                        target_ulong addr, uint32_t desc) \
{                                                                       \
    sve_stN_r_mte(env, vg, addr, desc, GETPC(), ESZ, MSZ, N,            \
                  sve_st1##NAME##_le_host, sve_st1##NAME##_le_tlb);     \
}                                                                       \
void HELPER(sve_st##N##NAME##_be_r_mte)(CPUARMState *env, void *vg,     \
                                        target_ulong addr, uint32_t desc) \
{                                                                       \
    sve_stN_r_mte(env, vg, addr, desc, GETPC(), ESZ, MSZ, N,            \
                  sve_st1##NAME##_be_host, sve_st1##NAME##_be_tlb);     \
}

DO_STN_1(1, bb, MO_8)
DO_STN_1(1, bh, MO_16)
DO_STN_1(1, bs, MO_32)
DO_STN_1(1, bd, MO_64)
DO_STN_1(2, bb, MO_8)
DO_STN_1(3, bb, MO_8)
DO_STN_1(4, bb, MO_8)

DO_STN_2(1, hh, MO_16, MO_16)
DO_STN_2(1, hs, MO_32, MO_16)
DO_STN_2(1, hd, MO_64, MO_16)
DO_STN_2(2, hh, MO_16, MO_16)
DO_STN_2(3, hh, MO_16, MO_16)
DO_STN_2(4, hh, MO_16, MO_16)

DO_STN_2(1, ss, MO_32, MO_32)
DO_STN_2(1, sd, MO_64, MO_32)
DO_STN_2(2, ss, MO_32, MO_32)
DO_STN_2(3, ss, MO_32, MO_32)
DO_STN_2(4, ss, MO_32, MO_32)

DO_STN_2(1, dd, MO_64, MO_64)
DO_STN_2(2, dd, MO_64, MO_64)
DO_STN_2(3, dd, MO_64, MO_64)
DO_STN_2(4, dd, MO_64, MO_64)

#undef DO_STN_1
#undef DO_STN_2

/*
 * Loads with a vector index.
 */

/*
 * Load the element at @reg + @reg_ofs, sign or zero-extend as needed.
 */
typedef target_ulong zreg_off_fn(void *reg, intptr_t reg_ofs);

static target_ulong off_zsu_s(void *reg, intptr_t reg_ofs)
{
    return *(uint32_t *)(reg + H1_4(reg_ofs));
}

static target_ulong off_zss_s(void *reg, intptr_t reg_ofs)
{
    return *(int32_t *)(reg + H1_4(reg_ofs));
}

static target_ulong off_zsu_d(void *reg, intptr_t reg_ofs)
{
    return (uint32_t)*(uint64_t *)(reg + reg_ofs);
}

static target_ulong off_zss_d(void *reg, intptr_t reg_ofs)
{
    return (int32_t)*(uint64_t *)(reg + reg_ofs);
}

static target_ulong off_zd_d(void *reg, intptr_t reg_ofs)
{
    return *(uint64_t *)(reg + reg_ofs);
}

static inline QEMU_ALWAYS_INLINE
void sve_ld1_z(CPUARMState *env, void *vd, uint64_t *vg, void *vm,
               target_ulong base, uint32_t desc, uintptr_t retaddr,
               uint32_t mtedesc, int esize, int msize,
               zreg_off_fn *off_fn,
               sve_ldst1_host_fn *host_fn,
               sve_ldst1_tlb_fn *tlb_fn)
{
    const int mmu_idx = arm_env_mmu_index(env);
    const intptr_t reg_max = simd_oprsz(desc);
    const int scale = simd_data(desc);
    ARMVectorReg scratch;
    intptr_t reg_off;
    SVEHostPage info, info2;

    memset(&scratch, 0, reg_max);
    reg_off = 0;
    do {
        uint64_t pg = vg[reg_off >> 6];
        do {
            if (likely(pg & 1)) {
                target_ulong addr = base + (off_fn(vm, reg_off) << scale);
                target_ulong in_page = -(addr | TARGET_PAGE_MASK);

                sve_probe_page(&info, false, env, addr, 0, MMU_DATA_LOAD,
                               mmu_idx, retaddr);

                if (likely(in_page >= msize)) {
                    if (unlikely(info.flags & TLB_WATCHPOINT)) {
                        cpu_check_watchpoint(env_cpu(env), addr, msize,
                                             info.attrs, BP_MEM_READ, retaddr);
                    }
                    if (mtedesc && info.tagged) {
                        mte_check(env, mtedesc, addr, retaddr);
                    }
                    if (unlikely(info.flags & TLB_MMIO)) {
                        tlb_fn(env, &scratch, reg_off, addr, retaddr);
                    } else {
                        set_helper_retaddr(retaddr);
                        host_fn(&scratch, reg_off, info.host);
                        clear_helper_retaddr();
                    }
                } else {
                    /* Element crosses the page boundary. */
                    sve_probe_page(&info2, false, env, addr + in_page, 0,
                                   MMU_DATA_LOAD, mmu_idx, retaddr);
                    if (unlikely((info.flags | info2.flags) & TLB_WATCHPOINT)) {
                        cpu_check_watchpoint(env_cpu(env), addr,
                                             msize, info.attrs,
                                             BP_MEM_READ, retaddr);
                    }
                    if (mtedesc && info.tagged) {
                        mte_check(env, mtedesc, addr, retaddr);
                    }
                    tlb_fn(env, &scratch, reg_off, addr, retaddr);
                }
            }
            reg_off += esize;
            pg >>= esize;
        } while (reg_off & 63);
    } while (reg_off < reg_max);

    /* Wait until all exceptions have been raised to write back.  */
    memcpy(vd, &scratch, reg_max);
}

static inline QEMU_ALWAYS_INLINE
void sve_ld1_z_mte(CPUARMState *env, void *vd, uint64_t *vg, void *vm,
                   target_ulong base, uint32_t desc, uintptr_t retaddr,
                   int esize, int msize, zreg_off_fn *off_fn,
                   sve_ldst1_host_fn *host_fn,
                   sve_ldst1_tlb_fn *tlb_fn)
{
    uint32_t mtedesc = desc >> (SIMD_DATA_SHIFT + SVE_MTEDESC_SHIFT);
    /* Remove mtedesc from the normal sve descriptor. */
    desc = extract32(desc, 0, SIMD_DATA_SHIFT + SVE_MTEDESC_SHIFT);

    /*
     * ??? TODO: For the 32-bit offset extractions, base + ofs cannot
     * offset base entirely over the address space hole to change the
     * pointer tag, or change the bit55 selector.  So we could here
     * examine TBI + TCMA like we do for sve_ldN_r_mte().
     */
    sve_ld1_z(env, vd, vg, vm, base, desc, retaddr, mtedesc,
              esize, msize, off_fn, host_fn, tlb_fn);
}

#define DO_LD1_ZPZ_S(MEM, OFS, MSZ) \
void HELPER(sve_ld##MEM##_##OFS)(CPUARMState *env, void *vd, void *vg,       \
                                 void *vm, target_ulong base, uint32_t desc) \
{                                                                            \
    sve_ld1_z(env, vd, vg, vm, base, desc, GETPC(), 0, 4, 1 << MSZ,          \
              off_##OFS##_s, sve_ld1##MEM##_host, sve_ld1##MEM##_tlb);       \
}                                                                            \
void HELPER(sve_ld##MEM##_##OFS##_mte)(CPUARMState *env, void *vd, void *vg, \
     void *vm, target_ulong base, uint32_t desc)                             \
{                                                                            \
    sve_ld1_z_mte(env, vd, vg, vm, base, desc, GETPC(), 4, 1 << MSZ,         \
                  off_##OFS##_s, sve_ld1##MEM##_host, sve_ld1##MEM##_tlb);   \
}

#define DO_LD1_ZPZ_D(MEM, OFS, MSZ) \
void HELPER(sve_ld##MEM##_##OFS)(CPUARMState *env, void *vd, void *vg,       \
                                 void *vm, target_ulong base, uint32_t desc) \
{                                                                            \
    sve_ld1_z(env, vd, vg, vm, base, desc, GETPC(), 0, 8, 1 << MSZ,          \
              off_##OFS##_d, sve_ld1##MEM##_host, sve_ld1##MEM##_tlb);       \
}                                                                            \
void HELPER(sve_ld##MEM##_##OFS##_mte)(CPUARMState *env, void *vd, void *vg, \
    void *vm, target_ulong base, uint32_t desc)                              \
{                                                                            \
    sve_ld1_z_mte(env, vd, vg, vm, base, desc, GETPC(), 8, 1 << MSZ,         \
                  off_##OFS##_d, sve_ld1##MEM##_host, sve_ld1##MEM##_tlb);   \
}

DO_LD1_ZPZ_S(bsu, zsu, MO_8)
DO_LD1_ZPZ_S(bsu, zss, MO_8)
DO_LD1_ZPZ_D(bdu, zsu, MO_8)
DO_LD1_ZPZ_D(bdu, zss, MO_8)
DO_LD1_ZPZ_D(bdu, zd, MO_8)

DO_LD1_ZPZ_S(bss, zsu, MO_8)
DO_LD1_ZPZ_S(bss, zss, MO_8)
DO_LD1_ZPZ_D(bds, zsu, MO_8)
DO_LD1_ZPZ_D(bds, zss, MO_8)
DO_LD1_ZPZ_D(bds, zd, MO_8)

DO_LD1_ZPZ_S(hsu_le, zsu, MO_16)
DO_LD1_ZPZ_S(hsu_le, zss, MO_16)
DO_LD1_ZPZ_D(hdu_le, zsu, MO_16)
DO_LD1_ZPZ_D(hdu_le, zss, MO_16)
DO_LD1_ZPZ_D(hdu_le, zd, MO_16)

DO_LD1_ZPZ_S(hsu_be, zsu, MO_16)
DO_LD1_ZPZ_S(hsu_be, zss, MO_16)
DO_LD1_ZPZ_D(hdu_be, zsu, MO_16)
DO_LD1_ZPZ_D(hdu_be, zss, MO_16)
DO_LD1_ZPZ_D(hdu_be, zd, MO_16)

DO_LD1_ZPZ_S(hss_le, zsu, MO_16)
DO_LD1_ZPZ_S(hss_le, zss, MO_16)
DO_LD1_ZPZ_D(hds_le, zsu, MO_16)
DO_LD1_ZPZ_D(hds_le, zss, MO_16)
DO_LD1_ZPZ_D(hds_le, zd, MO_16)

DO_LD1_ZPZ_S(hss_be, zsu, MO_16)
DO_LD1_ZPZ_S(hss_be, zss, MO_16)
DO_LD1_ZPZ_D(hds_be, zsu, MO_16)
DO_LD1_ZPZ_D(hds_be, zss, MO_16)
DO_LD1_ZPZ_D(hds_be, zd, MO_16)

DO_LD1_ZPZ_S(ss_le, zsu, MO_32)
DO_LD1_ZPZ_S(ss_le, zss, MO_32)
DO_LD1_ZPZ_D(sdu_le, zsu, MO_32)
DO_LD1_ZPZ_D(sdu_le, zss, MO_32)
DO_LD1_ZPZ_D(sdu_le, zd, MO_32)

DO_LD1_ZPZ_S(ss_be, zsu, MO_32)
DO_LD1_ZPZ_S(ss_be, zss, MO_32)
DO_LD1_ZPZ_D(sdu_be, zsu, MO_32)
DO_LD1_ZPZ_D(sdu_be, zss, MO_32)
DO_LD1_ZPZ_D(sdu_be, zd, MO_32)

DO_LD1_ZPZ_D(sds_le, zsu, MO_32)
DO_LD1_ZPZ_D(sds_le, zss, MO_32)
DO_LD1_ZPZ_D(sds_le, zd, MO_32)

DO_LD1_ZPZ_D(sds_be, zsu, MO_32)
DO_LD1_ZPZ_D(sds_be, zss, MO_32)
DO_LD1_ZPZ_D(sds_be, zd, MO_32)

DO_LD1_ZPZ_D(dd_le, zsu, MO_64)
DO_LD1_ZPZ_D(dd_le, zss, MO_64)
DO_LD1_ZPZ_D(dd_le, zd, MO_64)

DO_LD1_ZPZ_D(dd_be, zsu, MO_64)
DO_LD1_ZPZ_D(dd_be, zss, MO_64)
DO_LD1_ZPZ_D(dd_be, zd, MO_64)

#undef DO_LD1_ZPZ_S
#undef DO_LD1_ZPZ_D

/* First fault loads with a vector index.  */

/*
 * Common helpers for all gather first-faulting loads.
 */

static inline QEMU_ALWAYS_INLINE
void sve_ldff1_z(CPUARMState *env, void *vd, uint64_t *vg, void *vm,
                 target_ulong base, uint32_t desc, uintptr_t retaddr,
                 uint32_t mtedesc, const int esz, const int msz,
                 zreg_off_fn *off_fn,
                 sve_ldst1_host_fn *host_fn,
                 sve_ldst1_tlb_fn *tlb_fn)
{
    const int mmu_idx = arm_env_mmu_index(env);
    const intptr_t reg_max = simd_oprsz(desc);
    const int scale = simd_data(desc);
    const int esize = 1 << esz;
    const int msize = 1 << msz;
    intptr_t reg_off;
    SVEHostPage info;
    target_ulong addr, in_page;
    ARMVectorReg scratch;

    /* Skip to the first true predicate.  */
    reg_off = find_next_active(vg, 0, reg_max, esz);
    if (unlikely(reg_off >= reg_max)) {
        /* The entire predicate was false; no load occurs.  */
        memset(vd, 0, reg_max);
        return;
    }

    /* Protect against overlap between vd and vm. */
    if (unlikely(vd == vm)) {
        vm = memcpy(&scratch, vm, reg_max);
    }

    /*
     * Probe the first element, allowing faults.
     */
    addr = base + (off_fn(vm, reg_off) << scale);
    if (mtedesc) {
        mte_check(env, mtedesc, addr, retaddr);
    }
    tlb_fn(env, vd, reg_off, addr, retaddr);

    /* After any fault, zero the other elements. */
    swap_memzero(vd, reg_off);
    reg_off += esize;
    swap_memzero(vd + reg_off, reg_max - reg_off);

    /*
     * Probe the remaining elements, not allowing faults.
     */
    while (reg_off < reg_max) {
        uint64_t pg = vg[reg_off >> 6];
        do {
            if (likely((pg >> (reg_off & 63)) & 1)) {
                addr = base + (off_fn(vm, reg_off) << scale);
                in_page = -(addr | TARGET_PAGE_MASK);

                if (unlikely(in_page < msize)) {
                    /* Stop if the element crosses a page boundary. */
                    goto fault;
                }

                sve_probe_page(&info, true, env, addr, 0, MMU_DATA_LOAD,
                               mmu_idx, retaddr);
                if (unlikely(info.flags & (TLB_INVALID_MASK | TLB_MMIO))) {
                    goto fault;
                }
                if (unlikely(info.flags & TLB_WATCHPOINT) &&
                    (cpu_watchpoint_address_matches
                     (env_cpu(env), addr, msize) & BP_MEM_READ)) {
                    goto fault;
                }
                if (mtedesc && info.tagged && !mte_probe(env, mtedesc, addr)) {
                    goto fault;
                }

                set_helper_retaddr(retaddr);
                host_fn(vd, reg_off, info.host);
                clear_helper_retaddr();
            }
            reg_off += esize;
        } while (reg_off & 63);
    }
    return;

 fault:
    record_fault(env, reg_off, reg_max);
}

static inline QEMU_ALWAYS_INLINE
void sve_ldff1_z_mte(CPUARMState *env, void *vd, uint64_t *vg, void *vm,
                     target_ulong base, uint32_t desc, uintptr_t retaddr,
                     const int esz, const int msz,
                     zreg_off_fn *off_fn,
                     sve_ldst1_host_fn *host_fn,
                     sve_ldst1_tlb_fn *tlb_fn)
{
    uint32_t mtedesc = desc >> (SIMD_DATA_SHIFT + SVE_MTEDESC_SHIFT);
    /* Remove mtedesc from the normal sve descriptor. */
    desc = extract32(desc, 0, SIMD_DATA_SHIFT + SVE_MTEDESC_SHIFT);

    /*
     * ??? TODO: For the 32-bit offset extractions, base + ofs cannot
     * offset base entirely over the address space hole to change the
     * pointer tag, or change the bit55 selector.  So we could here
     * examine TBI + TCMA like we do for sve_ldN_r_mte().
     */
    sve_ldff1_z(env, vd, vg, vm, base, desc, retaddr, mtedesc,
                esz, msz, off_fn, host_fn, tlb_fn);
}

#define DO_LDFF1_ZPZ_S(MEM, OFS, MSZ)                                   \
void HELPER(sve_ldff##MEM##_##OFS)                                      \
    (CPUARMState *env, void *vd, void *vg,                              \
     void *vm, target_ulong base, uint32_t desc)                        \
{                                                                       \
    sve_ldff1_z(env, vd, vg, vm, base, desc, GETPC(), 0, MO_32, MSZ,    \
                off_##OFS##_s, sve_ld1##MEM##_host, sve_ld1##MEM##_tlb); \
}                                                                       \
void HELPER(sve_ldff##MEM##_##OFS##_mte)                                \
    (CPUARMState *env, void *vd, void *vg,                              \
     void *vm, target_ulong base, uint32_t desc)                        \
{                                                                       \
    sve_ldff1_z_mte(env, vd, vg, vm, base, desc, GETPC(), MO_32, MSZ,   \
                    off_##OFS##_s, sve_ld1##MEM##_host, sve_ld1##MEM##_tlb); \
}

#define DO_LDFF1_ZPZ_D(MEM, OFS, MSZ)                                   \
void HELPER(sve_ldff##MEM##_##OFS)                                      \
    (CPUARMState *env, void *vd, void *vg,                              \
     void *vm, target_ulong base, uint32_t desc)                        \
{                                                                       \
    sve_ldff1_z(env, vd, vg, vm, base, desc, GETPC(), 0, MO_64, MSZ,    \
                off_##OFS##_d, sve_ld1##MEM##_host, sve_ld1##MEM##_tlb); \
}                                                                       \
void HELPER(sve_ldff##MEM##_##OFS##_mte)                                \
    (CPUARMState *env, void *vd, void *vg,                              \
     void *vm, target_ulong base, uint32_t desc)                        \
{                                                                       \
    sve_ldff1_z_mte(env, vd, vg, vm, base, desc, GETPC(), MO_64, MSZ,   \
                    off_##OFS##_d, sve_ld1##MEM##_host, sve_ld1##MEM##_tlb); \
}

DO_LDFF1_ZPZ_S(bsu, zsu, MO_8)
DO_LDFF1_ZPZ_S(bsu, zss, MO_8)
DO_LDFF1_ZPZ_D(bdu, zsu, MO_8)
DO_LDFF1_ZPZ_D(bdu, zss, MO_8)
DO_LDFF1_ZPZ_D(bdu, zd, MO_8)

DO_LDFF1_ZPZ_S(bss, zsu, MO_8)
DO_LDFF1_ZPZ_S(bss, zss, MO_8)
DO_LDFF1_ZPZ_D(bds, zsu, MO_8)
DO_LDFF1_ZPZ_D(bds, zss, MO_8)
DO_LDFF1_ZPZ_D(bds, zd, MO_8)

DO_LDFF1_ZPZ_S(hsu_le, zsu, MO_16)
DO_LDFF1_ZPZ_S(hsu_le, zss, MO_16)
DO_LDFF1_ZPZ_D(hdu_le, zsu, MO_16)
DO_LDFF1_ZPZ_D(hdu_le, zss, MO_16)
DO_LDFF1_ZPZ_D(hdu_le, zd, MO_16)

DO_LDFF1_ZPZ_S(hsu_be, zsu, MO_16)
DO_LDFF1_ZPZ_S(hsu_be, zss, MO_16)
DO_LDFF1_ZPZ_D(hdu_be, zsu, MO_16)
DO_LDFF1_ZPZ_D(hdu_be, zss, MO_16)
DO_LDFF1_ZPZ_D(hdu_be, zd, MO_16)

DO_LDFF1_ZPZ_S(hss_le, zsu, MO_16)
DO_LDFF1_ZPZ_S(hss_le, zss, MO_16)
DO_LDFF1_ZPZ_D(hds_le, zsu, MO_16)
DO_LDFF1_ZPZ_D(hds_le, zss, MO_16)
DO_LDFF1_ZPZ_D(hds_le, zd, MO_16)

DO_LDFF1_ZPZ_S(hss_be, zsu, MO_16)
DO_LDFF1_ZPZ_S(hss_be, zss, MO_16)
DO_LDFF1_ZPZ_D(hds_be, zsu, MO_16)
DO_LDFF1_ZPZ_D(hds_be, zss, MO_16)
DO_LDFF1_ZPZ_D(hds_be, zd, MO_16)

DO_LDFF1_ZPZ_S(ss_le,  zsu, MO_32)
DO_LDFF1_ZPZ_S(ss_le,  zss, MO_32)
DO_LDFF1_ZPZ_D(sdu_le, zsu, MO_32)
DO_LDFF1_ZPZ_D(sdu_le, zss, MO_32)
DO_LDFF1_ZPZ_D(sdu_le, zd, MO_32)

DO_LDFF1_ZPZ_S(ss_be,  zsu, MO_32)
DO_LDFF1_ZPZ_S(ss_be,  zss, MO_32)
DO_LDFF1_ZPZ_D(sdu_be, zsu, MO_32)
DO_LDFF1_ZPZ_D(sdu_be, zss, MO_32)
DO_LDFF1_ZPZ_D(sdu_be, zd, MO_32)

DO_LDFF1_ZPZ_D(sds_le, zsu, MO_32)
DO_LDFF1_ZPZ_D(sds_le, zss, MO_32)
DO_LDFF1_ZPZ_D(sds_le, zd, MO_32)

DO_LDFF1_ZPZ_D(sds_be, zsu, MO_32)
DO_LDFF1_ZPZ_D(sds_be, zss, MO_32)
DO_LDFF1_ZPZ_D(sds_be, zd, MO_32)

DO_LDFF1_ZPZ_D(dd_le, zsu, MO_64)
DO_LDFF1_ZPZ_D(dd_le, zss, MO_64)
DO_LDFF1_ZPZ_D(dd_le, zd, MO_64)

DO_LDFF1_ZPZ_D(dd_be, zsu, MO_64)
DO_LDFF1_ZPZ_D(dd_be, zss, MO_64)
DO_LDFF1_ZPZ_D(dd_be, zd, MO_64)

/* Stores with a vector index.  */

static inline QEMU_ALWAYS_INLINE
void sve_st1_z(CPUARMState *env, void *vd, uint64_t *vg, void *vm,
               target_ulong base, uint32_t desc, uintptr_t retaddr,
               uint32_t mtedesc, int esize, int msize,
               zreg_off_fn *off_fn,
               sve_ldst1_host_fn *host_fn,
               sve_ldst1_tlb_fn *tlb_fn)
{
    const int mmu_idx = arm_env_mmu_index(env);
    const intptr_t reg_max = simd_oprsz(desc);
    const int scale = simd_data(desc);
    void *host[ARM_MAX_VQ * 4];
    intptr_t reg_off, i;
    SVEHostPage info, info2;

    /*
     * Probe all of the elements for host addresses and flags.
     */
    i = reg_off = 0;
    do {
        uint64_t pg = vg[reg_off >> 6];
        do {
            target_ulong addr = base + (off_fn(vm, reg_off) << scale);
            target_ulong in_page = -(addr | TARGET_PAGE_MASK);

            host[i] = NULL;
            if (likely((pg >> (reg_off & 63)) & 1)) {
                if (likely(in_page >= msize)) {
                    sve_probe_page(&info, false, env, addr, 0, MMU_DATA_STORE,
                                   mmu_idx, retaddr);
                    if (!(info.flags & TLB_MMIO)) {
                        host[i] = info.host;
                    }
                } else {
                    /*
                     * Element crosses the page boundary.
                     * Probe both pages, but do not record the host address,
                     * so that we use the slow path.
                     */
                    sve_probe_page(&info, false, env, addr, 0,
                                   MMU_DATA_STORE, mmu_idx, retaddr);
                    sve_probe_page(&info2, false, env, addr + in_page, 0,
                                   MMU_DATA_STORE, mmu_idx, retaddr);
                    info.flags |= info2.flags;
                }

                if (unlikely(info.flags & TLB_WATCHPOINT)) {
                    cpu_check_watchpoint(env_cpu(env), addr, msize,
                                         info.attrs, BP_MEM_WRITE, retaddr);
                }

                if (mtedesc && info.tagged) {
                    mte_check(env, mtedesc, addr, retaddr);
                }
            }
            i += 1;
            reg_off += esize;
        } while (reg_off & 63);
    } while (reg_off < reg_max);

    /*
     * Now that we have recognized all exceptions except SyncExternal
     * (from TLB_MMIO), which we cannot avoid, perform all of the stores.
     *
     * Note for the common case of an element in RAM, not crossing a page
     * boundary, we have stored the host address in host[].  This doubles
     * as a first-level check against the predicate, since only enabled
     * elements have non-null host addresses.
     */
    i = reg_off = 0;
    do {
        void *h = host[i];
        if (likely(h != NULL)) {
            set_helper_retaddr(retaddr);
            host_fn(vd, reg_off, h);
            clear_helper_retaddr();
        } else if ((vg[reg_off >> 6] >> (reg_off & 63)) & 1) {
            target_ulong addr = base + (off_fn(vm, reg_off) << scale);
            tlb_fn(env, vd, reg_off, addr, retaddr);
        }
        i += 1;
        reg_off += esize;
    } while (reg_off < reg_max);
}

static inline QEMU_ALWAYS_INLINE
void sve_st1_z_mte(CPUARMState *env, void *vd, uint64_t *vg, void *vm,
                   target_ulong base, uint32_t desc, uintptr_t retaddr,
                   int esize, int msize, zreg_off_fn *off_fn,
                   sve_ldst1_host_fn *host_fn,
                   sve_ldst1_tlb_fn *tlb_fn)
{
    uint32_t mtedesc = desc >> (SIMD_DATA_SHIFT + SVE_MTEDESC_SHIFT);
    /* Remove mtedesc from the normal sve descriptor. */
    desc = extract32(desc, 0, SIMD_DATA_SHIFT + SVE_MTEDESC_SHIFT);

    /*
     * ??? TODO: For the 32-bit offset extractions, base + ofs cannot
     * offset base entirely over the address space hole to change the
     * pointer tag, or change the bit55 selector.  So we could here
     * examine TBI + TCMA like we do for sve_ldN_r_mte().
     */
    sve_st1_z(env, vd, vg, vm, base, desc, retaddr, mtedesc,
              esize, msize, off_fn, host_fn, tlb_fn);
}

#define DO_ST1_ZPZ_S(MEM, OFS, MSZ)                                     \
void HELPER(sve_st##MEM##_##OFS)(CPUARMState *env, void *vd, void *vg,  \
                                 void *vm, target_ulong base, uint32_t desc) \
{                                                                       \
    sve_st1_z(env, vd, vg, vm, base, desc, GETPC(), 0, 4, 1 << MSZ,     \
              off_##OFS##_s, sve_st1##MEM##_host, sve_st1##MEM##_tlb);  \
}                                                                       \
void HELPER(sve_st##MEM##_##OFS##_mte)(CPUARMState *env, void *vd, void *vg, \
    void *vm, target_ulong base, uint32_t desc)                         \
{                                                                       \
    sve_st1_z_mte(env, vd, vg, vm, base, desc, GETPC(), 4, 1 << MSZ,    \
                  off_##OFS##_s, sve_st1##MEM##_host, sve_st1##MEM##_tlb); \
}

#define DO_ST1_ZPZ_D(MEM, OFS, MSZ)                                     \
void HELPER(sve_st##MEM##_##OFS)(CPUARMState *env, void *vd, void *vg,  \
                                 void *vm, target_ulong base, uint32_t desc) \
{                                                                       \
    sve_st1_z(env, vd, vg, vm, base, desc, GETPC(), 0, 8, 1 << MSZ,     \
              off_##OFS##_d, sve_st1##MEM##_host, sve_st1##MEM##_tlb);  \
}                                                                       \
void HELPER(sve_st##MEM##_##OFS##_mte)(CPUARMState *env, void *vd, void *vg, \
    void *vm, target_ulong base, uint32_t desc)                         \
{                                                                       \
    sve_st1_z_mte(env, vd, vg, vm, base, desc, GETPC(), 8, 1 << MSZ,    \
                  off_##OFS##_d, sve_st1##MEM##_host, sve_st1##MEM##_tlb); \
}

DO_ST1_ZPZ_S(bs, zsu, MO_8)
DO_ST1_ZPZ_S(hs_le, zsu, MO_16)
DO_ST1_ZPZ_S(hs_be, zsu, MO_16)
DO_ST1_ZPZ_S(ss_le, zsu, MO_32)
DO_ST1_ZPZ_S(ss_be, zsu, MO_32)

DO_ST1_ZPZ_S(bs, zss, MO_8)
DO_ST1_ZPZ_S(hs_le, zss, MO_16)
DO_ST1_ZPZ_S(hs_be, zss, MO_16)
DO_ST1_ZPZ_S(ss_le, zss, MO_32)
DO_ST1_ZPZ_S(ss_be, zss, MO_32)

DO_ST1_ZPZ_D(bd, zsu, MO_8)
DO_ST1_ZPZ_D(hd_le, zsu, MO_16)
DO_ST1_ZPZ_D(hd_be, zsu, MO_16)
DO_ST1_ZPZ_D(sd_le, zsu, MO_32)
DO_ST1_ZPZ_D(sd_be, zsu, MO_32)
DO_ST1_ZPZ_D(dd_le, zsu, MO_64)
DO_ST1_ZPZ_D(dd_be, zsu, MO_64)

DO_ST1_ZPZ_D(bd, zss, MO_8)
DO_ST1_ZPZ_D(hd_le, zss, MO_16)
DO_ST1_ZPZ_D(hd_be, zss, MO_16)
DO_ST1_ZPZ_D(sd_le, zss, MO_32)
DO_ST1_ZPZ_D(sd_be, zss, MO_32)
DO_ST1_ZPZ_D(dd_le, zss, MO_64)
DO_ST1_ZPZ_D(dd_be, zss, MO_64)

DO_ST1_ZPZ_D(bd, zd, MO_8)
DO_ST1_ZPZ_D(hd_le, zd, MO_16)
DO_ST1_ZPZ_D(hd_be, zd, MO_16)
DO_ST1_ZPZ_D(sd_le, zd, MO_32)
DO_ST1_ZPZ_D(sd_be, zd, MO_32)
DO_ST1_ZPZ_D(dd_le, zd, MO_64)
DO_ST1_ZPZ_D(dd_be, zd, MO_64)

#undef DO_ST1_ZPZ_S
#undef DO_ST1_ZPZ_D

void HELPER(sve2_eor3)(void *vd, void *vn, void *vm, void *vk, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd, *n = vn, *m = vm, *k = vk;

    for (i = 0; i < opr_sz; ++i) {
        d[i] = n[i] ^ m[i] ^ k[i];
    }
}

void HELPER(sve2_bcax)(void *vd, void *vn, void *vm, void *vk, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd, *n = vn, *m = vm, *k = vk;

    for (i = 0; i < opr_sz; ++i) {
        d[i] = n[i] ^ (m[i] & ~k[i]);
    }
}

void HELPER(sve2_bsl1n)(void *vd, void *vn, void *vm, void *vk, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd, *n = vn, *m = vm, *k = vk;

    for (i = 0; i < opr_sz; ++i) {
        d[i] = (~n[i] & k[i]) | (m[i] & ~k[i]);
    }
}

void HELPER(sve2_bsl2n)(void *vd, void *vn, void *vm, void *vk, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd, *n = vn, *m = vm, *k = vk;

    for (i = 0; i < opr_sz; ++i) {
        d[i] = (n[i] & k[i]) | (~m[i] & ~k[i]);
    }
}

void HELPER(sve2_nbsl)(void *vd, void *vn, void *vm, void *vk, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd, *n = vn, *m = vm, *k = vk;

    for (i = 0; i < opr_sz; ++i) {
        d[i] = ~((n[i] & k[i]) | (m[i] & ~k[i]));
    }
}

/*
 * Returns true if m0 or m1 contains the low uint8_t/uint16_t in n.
 * See hasless(v,1) from
 *   https://graphics.stanford.edu/~seander/bithacks.html#ZeroInWord
 */
static inline bool do_match2(uint64_t n, uint64_t m0, uint64_t m1, int esz)
{
    int bits = 8 << esz;
    uint64_t ones = dup_const(esz, 1);
    uint64_t signs = ones << (bits - 1);
    uint64_t cmp0, cmp1;

    cmp1 = dup_const(esz, n);
    cmp0 = cmp1 ^ m0;
    cmp1 = cmp1 ^ m1;
    cmp0 = (cmp0 - ones) & ~cmp0;
    cmp1 = (cmp1 - ones) & ~cmp1;
    return (cmp0 | cmp1) & signs;
}

static inline uint32_t do_match(void *vd, void *vn, void *vm, void *vg,
                                uint32_t desc, int esz, bool nmatch)
{
    uint16_t esz_mask = pred_esz_masks[esz];
    intptr_t opr_sz = simd_oprsz(desc);
    uint32_t flags = PREDTEST_INIT;
    intptr_t i, j, k;

    for (i = 0; i < opr_sz; i += 16) {
        uint64_t m0 = *(uint64_t *)(vm + i);
        uint64_t m1 = *(uint64_t *)(vm + i + 8);
        uint16_t pg = *(uint16_t *)(vg + H1_2(i >> 3)) & esz_mask;
        uint16_t out = 0;

        for (j = 0; j < 16; j += 8) {
            uint64_t n = *(uint64_t *)(vn + i + j);

            for (k = 0; k < 8; k += 1 << esz) {
                if (pg & (1 << (j + k))) {
                    bool o = do_match2(n >> (k * 8), m0, m1, esz);
                    out |= (o ^ nmatch) << (j + k);
                }
            }
        }
        *(uint16_t *)(vd + H1_2(i >> 3)) = out;
        flags = iter_predtest_fwd(out, pg, flags);
    }
    return flags;
}

#define DO_PPZZ_MATCH(NAME, ESZ, INV)                                         \
uint32_t HELPER(NAME)(void *vd, void *vn, void *vm, void *vg, uint32_t desc)  \
{                                                                             \
    return do_match(vd, vn, vm, vg, desc, ESZ, INV);                          \
}

DO_PPZZ_MATCH(sve2_match_ppzz_b, MO_8, false)
DO_PPZZ_MATCH(sve2_match_ppzz_h, MO_16, false)

DO_PPZZ_MATCH(sve2_nmatch_ppzz_b, MO_8, true)
DO_PPZZ_MATCH(sve2_nmatch_ppzz_h, MO_16, true)

#undef DO_PPZZ_MATCH

void HELPER(sve2_histcnt_s)(void *vd, void *vn, void *vm, void *vg,
                            uint32_t desc)
{
    ARMVectorReg scratch;
    intptr_t i, j;
    intptr_t opr_sz = simd_oprsz(desc);
    uint32_t *d = vd, *n = vn, *m = vm;
    uint8_t *pg = vg;

    if (d == n) {
        n = memcpy(&scratch, n, opr_sz);
        if (d == m) {
            m = n;
        }
    } else if (d == m) {
        m = memcpy(&scratch, m, opr_sz);
    }

    for (i = 0; i < opr_sz; i += 4) {
        uint64_t count = 0;
        uint8_t pred;

        pred = pg[H1(i >> 3)] >> (i & 7);
        if (pred & 1) {
            uint32_t nn = n[H4(i >> 2)];

            for (j = 0; j <= i; j += 4) {
                pred = pg[H1(j >> 3)] >> (j & 7);
                if ((pred & 1) && nn == m[H4(j >> 2)]) {
                    ++count;
                }
            }
        }
        d[H4(i >> 2)] = count;
    }
}

void HELPER(sve2_histcnt_d)(void *vd, void *vn, void *vm, void *vg,
                            uint32_t desc)
{
    ARMVectorReg scratch;
    intptr_t i, j;
    intptr_t opr_sz = simd_oprsz(desc);
    uint64_t *d = vd, *n = vn, *m = vm;
    uint8_t *pg = vg;

    if (d == n) {
        n = memcpy(&scratch, n, opr_sz);
        if (d == m) {
            m = n;
        }
    } else if (d == m) {
        m = memcpy(&scratch, m, opr_sz);
    }

    for (i = 0; i < opr_sz / 8; ++i) {
        uint64_t count = 0;
        if (pg[H1(i)] & 1) {
            uint64_t nn = n[i];
            for (j = 0; j <= i; ++j) {
                if ((pg[H1(j)] & 1) && nn == m[j]) {
                    ++count;
                }
            }
        }
        d[i] = count;
    }
}

/*
 * Returns the number of bytes in m0 and m1 that match n.
 * Unlike do_match2 we don't just need true/false, we need an exact count.
 * This requires two extra logical operations.
 */
static inline uint64_t do_histseg_cnt(uint8_t n, uint64_t m0, uint64_t m1)
{
    const uint64_t mask = dup_const(MO_8, 0x7f);
    uint64_t cmp0, cmp1;

    cmp1 = dup_const(MO_8, n);
    cmp0 = cmp1 ^ m0;
    cmp1 = cmp1 ^ m1;

    /*
     * 1: clear msb of each byte to avoid carry to next byte (& mask)
     * 2: carry in to msb if byte != 0 (+ mask)
     * 3: set msb if cmp has msb set (| cmp)
     * 4: set ~msb to ignore them (| mask)
     * We now have 0xff for byte != 0 or 0x7f for byte == 0.
     * 5: invert, resulting in 0x80 if and only if byte == 0.
     */
    cmp0 = ~(((cmp0 & mask) + mask) | cmp0 | mask);
    cmp1 = ~(((cmp1 & mask) + mask) | cmp1 | mask);

    /*
     * Combine the two compares in a way that the bits do
     * not overlap, and so preserves the count of set bits.
     * If the host has an efficient instruction for ctpop,
     * then ctpop(x) + ctpop(y) has the same number of
     * operations as ctpop(x | (y >> 1)).  If the host does
     * not have an efficient ctpop, then we only want to
     * use it once.
     */
    return ctpop64(cmp0 | (cmp1 >> 1));
}

void HELPER(sve2_histseg)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, j;
    intptr_t opr_sz = simd_oprsz(desc);

    for (i = 0; i < opr_sz; i += 16) {
        uint64_t n0 = *(uint64_t *)(vn + i);
        uint64_t m0 = *(uint64_t *)(vm + i);
        uint64_t n1 = *(uint64_t *)(vn + i + 8);
        uint64_t m1 = *(uint64_t *)(vm + i + 8);
        uint64_t out0 = 0;
        uint64_t out1 = 0;

        for (j = 0; j < 64; j += 8) {
            uint64_t cnt0 = do_histseg_cnt(n0 >> j, m0, m1);
            uint64_t cnt1 = do_histseg_cnt(n1 >> j, m0, m1);
            out0 |= cnt0 << j;
            out1 |= cnt1 << j;
        }

        *(uint64_t *)(vd + i) = out0;
        *(uint64_t *)(vd + i + 8) = out1;
    }
}

void HELPER(sve2_xar_b)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    int shr = simd_data(desc);
    int shl = 8 - shr;
    uint64_t mask = dup_const(MO_8, 0xff >> shr);
    uint64_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz; ++i) {
        uint64_t t = n[i] ^ m[i];
        d[i] = ((t >> shr) & mask) | ((t << shl) & ~mask);
    }
}

void HELPER(sve2_xar_h)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    int shr = simd_data(desc);
    int shl = 16 - shr;
    uint64_t mask = dup_const(MO_16, 0xffff >> shr);
    uint64_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz; ++i) {
        uint64_t t = n[i] ^ m[i];
        d[i] = ((t >> shr) & mask) | ((t << shl) & ~mask);
    }
}

void HELPER(sve2_xar_s)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 4;
    int shr = simd_data(desc);
    uint32_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz; ++i) {
        d[i] = ror32(n[i] ^ m[i], shr);
    }
}

void HELPER(fmmla_s)(void *vd, void *vn, void *vm, void *va,
                     float_status *status, uint32_t desc)
{
    intptr_t s, opr_sz = simd_oprsz(desc) / (sizeof(float32) * 4);

    for (s = 0; s < opr_sz; ++s) {
        float32 *n = vn + s * sizeof(float32) * 4;
        float32 *m = vm + s * sizeof(float32) * 4;
        float32 *a = va + s * sizeof(float32) * 4;
        float32 *d = vd + s * sizeof(float32) * 4;
        float32 n00 = n[H4(0)], n01 = n[H4(1)];
        float32 n10 = n[H4(2)], n11 = n[H4(3)];
        float32 m00 = m[H4(0)], m01 = m[H4(1)];
        float32 m10 = m[H4(2)], m11 = m[H4(3)];
        float32 p0, p1;

        /* i = 0, j = 0 */
        p0 = float32_mul(n00, m00, status);
        p1 = float32_mul(n01, m01, status);
        d[H4(0)] = float32_add(a[H4(0)], float32_add(p0, p1, status), status);

        /* i = 0, j = 1 */
        p0 = float32_mul(n00, m10, status);
        p1 = float32_mul(n01, m11, status);
        d[H4(1)] = float32_add(a[H4(1)], float32_add(p0, p1, status), status);

        /* i = 1, j = 0 */
        p0 = float32_mul(n10, m00, status);
        p1 = float32_mul(n11, m01, status);
        d[H4(2)] = float32_add(a[H4(2)], float32_add(p0, p1, status), status);

        /* i = 1, j = 1 */
        p0 = float32_mul(n10, m10, status);
        p1 = float32_mul(n11, m11, status);
        d[H4(3)] = float32_add(a[H4(3)], float32_add(p0, p1, status), status);
    }
}

void HELPER(fmmla_d)(void *vd, void *vn, void *vm, void *va,
                     float_status *status, uint32_t desc)
{
    intptr_t s, opr_sz = simd_oprsz(desc) / (sizeof(float64) * 4);

    for (s = 0; s < opr_sz; ++s) {
        float64 *n = vn + s * sizeof(float64) * 4;
        float64 *m = vm + s * sizeof(float64) * 4;
        float64 *a = va + s * sizeof(float64) * 4;
        float64 *d = vd + s * sizeof(float64) * 4;
        float64 n00 = n[0], n01 = n[1], n10 = n[2], n11 = n[3];
        float64 m00 = m[0], m01 = m[1], m10 = m[2], m11 = m[3];
        float64 p0, p1;

        /* i = 0, j = 0 */
        p0 = float64_mul(n00, m00, status);
        p1 = float64_mul(n01, m01, status);
        d[0] = float64_add(a[0], float64_add(p0, p1, status), status);

        /* i = 0, j = 1 */
        p0 = float64_mul(n00, m10, status);
        p1 = float64_mul(n01, m11, status);
        d[1] = float64_add(a[1], float64_add(p0, p1, status), status);

        /* i = 1, j = 0 */
        p0 = float64_mul(n10, m00, status);
        p1 = float64_mul(n11, m01, status);
        d[2] = float64_add(a[2], float64_add(p0, p1, status), status);

        /* i = 1, j = 1 */
        p0 = float64_mul(n10, m10, status);
        p1 = float64_mul(n11, m11, status);
        d[3] = float64_add(a[3], float64_add(p0, p1, status), status);
    }
}

#define DO_FCVTNT(NAME, TYPEW, TYPEN, HW, HN, OP)                             \
void HELPER(NAME)(void *vd, void *vn, void *vg,                               \
                  float_status *status, uint32_t desc)                        \
{                                                                             \
    intptr_t i = simd_oprsz(desc);                                            \
    uint64_t *g = vg;                                                         \
    do {                                                                      \
        uint64_t pg = g[(i - 1) >> 6];                                        \
        do {                                                                  \
            i -= sizeof(TYPEW);                                               \
            if (likely((pg >> (i & 63)) & 1)) {                               \
                TYPEW nn = *(TYPEW *)(vn + HW(i));                            \
                *(TYPEN *)(vd + HN(i + sizeof(TYPEN))) = OP(nn, status);      \
            }                                                                 \
        } while (i & 63);                                                     \
    } while (i != 0);                                                         \
}

DO_FCVTNT(sve_bfcvtnt,    uint32_t, uint16_t, H1_4, H1_2, float32_to_bfloat16)
DO_FCVTNT(sve2_fcvtnt_sh, uint32_t, uint16_t, H1_4, H1_2, sve_f32_to_f16)
DO_FCVTNT(sve2_fcvtnt_ds, uint64_t, uint32_t, H1_8, H1_4, float64_to_float32)

#define DO_FCVTLT(NAME, TYPEW, TYPEN, HW, HN, OP)                             \
void HELPER(NAME)(void *vd, void *vn, void *vg,                               \
                  float_status *status, uint32_t desc)                        \
{                                                                             \
    intptr_t i = simd_oprsz(desc);                                            \
    uint64_t *g = vg;                                                         \
    do {                                                                      \
        uint64_t pg = g[(i - 1) >> 6];                                        \
        do {                                                                  \
            i -= sizeof(TYPEW);                                               \
            if (likely((pg >> (i & 63)) & 1)) {                               \
                TYPEN nn = *(TYPEN *)(vn + HN(i + sizeof(TYPEN)));            \
                *(TYPEW *)(vd + HW(i)) = OP(nn, status);                      \
            }                                                                 \
        } while (i & 63);                                                     \
    } while (i != 0);                                                         \
}

DO_FCVTLT(sve2_fcvtlt_hs, uint32_t, uint16_t, H1_4, H1_2, sve_f16_to_f32)
DO_FCVTLT(sve2_fcvtlt_sd, uint64_t, uint32_t, H1_8, H1_4, float32_to_float64)

#undef DO_FCVTLT
#undef DO_FCVTNT
