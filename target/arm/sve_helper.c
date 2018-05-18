/*
 * ARM SVE Operations
 *
 * Copyright (c) 2018 Linaro, Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"
#include "tcg/tcg-gvec-desc.h"


/* Note that vector data is stored in host-endian 64-bit chunks,
   so addressing units smaller than that needs a host-endian fixup.  */
#ifdef HOST_WORDS_BIGENDIAN
#define H1(x)   ((x) ^ 7)
#define H1_2(x) ((x) ^ 6)
#define H1_4(x) ((x) ^ 4)
#define H2(x)   ((x) ^ 3)
#define H4(x)   ((x) ^ 1)
#else
#define H1(x)   (x)
#define H1_2(x) (x)
#define H1_4(x) (x)
#define H2(x)   (x)
#define H4(x)   (x)
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
#define DO_DIV(N, M)  (M ? N / M : 0)

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

DO_ZPZZ(sve_sdiv_zpzz_s, int32_t, H1_4, DO_DIV)
DO_ZPZZ_D(sve_sdiv_zpzz_d, int64_t, DO_DIV)

DO_ZPZZ(sve_udiv_zpzz_s, uint32_t, H1_4, DO_DIV)
DO_ZPZZ_D(sve_udiv_zpzz_d, uint64_t, DO_DIV)

#undef DO_ZPZZ
#undef DO_ZPZZ_D
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

uint32_t HELPER(sve_pfirst)(void *vd, void *vg, uint32_t words)
{
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
    intptr_t words = extract32(pred_desc, 0, SIMD_OPRSZ_BITS);
    intptr_t esz = extract32(pred_desc, SIMD_DATA_SHIFT, 2);
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
