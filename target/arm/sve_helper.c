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
#include "fpu/softfloat.h"


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

/* Expand active predicate bits to bytes, for byte elements.
 *  for (i = 0; i < 256; ++i) {
 *      unsigned long m = 0;
 *      for (j = 0; j < 8; j++) {
 *          if ((i >> j) & 1) {
 *              m |= 0xfful << (j << 3);
 *          }
 *      }
 *      printf("0x%016lx,\n", m);
 *  }
 */
static inline uint64_t expand_pred_b(uint8_t byte)
{
    static const uint64_t word[256] = {
        0x0000000000000000, 0x00000000000000ff, 0x000000000000ff00,
        0x000000000000ffff, 0x0000000000ff0000, 0x0000000000ff00ff,
        0x0000000000ffff00, 0x0000000000ffffff, 0x00000000ff000000,
        0x00000000ff0000ff, 0x00000000ff00ff00, 0x00000000ff00ffff,
        0x00000000ffff0000, 0x00000000ffff00ff, 0x00000000ffffff00,
        0x00000000ffffffff, 0x000000ff00000000, 0x000000ff000000ff,
        0x000000ff0000ff00, 0x000000ff0000ffff, 0x000000ff00ff0000,
        0x000000ff00ff00ff, 0x000000ff00ffff00, 0x000000ff00ffffff,
        0x000000ffff000000, 0x000000ffff0000ff, 0x000000ffff00ff00,
        0x000000ffff00ffff, 0x000000ffffff0000, 0x000000ffffff00ff,
        0x000000ffffffff00, 0x000000ffffffffff, 0x0000ff0000000000,
        0x0000ff00000000ff, 0x0000ff000000ff00, 0x0000ff000000ffff,
        0x0000ff0000ff0000, 0x0000ff0000ff00ff, 0x0000ff0000ffff00,
        0x0000ff0000ffffff, 0x0000ff00ff000000, 0x0000ff00ff0000ff,
        0x0000ff00ff00ff00, 0x0000ff00ff00ffff, 0x0000ff00ffff0000,
        0x0000ff00ffff00ff, 0x0000ff00ffffff00, 0x0000ff00ffffffff,
        0x0000ffff00000000, 0x0000ffff000000ff, 0x0000ffff0000ff00,
        0x0000ffff0000ffff, 0x0000ffff00ff0000, 0x0000ffff00ff00ff,
        0x0000ffff00ffff00, 0x0000ffff00ffffff, 0x0000ffffff000000,
        0x0000ffffff0000ff, 0x0000ffffff00ff00, 0x0000ffffff00ffff,
        0x0000ffffffff0000, 0x0000ffffffff00ff, 0x0000ffffffffff00,
        0x0000ffffffffffff, 0x00ff000000000000, 0x00ff0000000000ff,
        0x00ff00000000ff00, 0x00ff00000000ffff, 0x00ff000000ff0000,
        0x00ff000000ff00ff, 0x00ff000000ffff00, 0x00ff000000ffffff,
        0x00ff0000ff000000, 0x00ff0000ff0000ff, 0x00ff0000ff00ff00,
        0x00ff0000ff00ffff, 0x00ff0000ffff0000, 0x00ff0000ffff00ff,
        0x00ff0000ffffff00, 0x00ff0000ffffffff, 0x00ff00ff00000000,
        0x00ff00ff000000ff, 0x00ff00ff0000ff00, 0x00ff00ff0000ffff,
        0x00ff00ff00ff0000, 0x00ff00ff00ff00ff, 0x00ff00ff00ffff00,
        0x00ff00ff00ffffff, 0x00ff00ffff000000, 0x00ff00ffff0000ff,
        0x00ff00ffff00ff00, 0x00ff00ffff00ffff, 0x00ff00ffffff0000,
        0x00ff00ffffff00ff, 0x00ff00ffffffff00, 0x00ff00ffffffffff,
        0x00ffff0000000000, 0x00ffff00000000ff, 0x00ffff000000ff00,
        0x00ffff000000ffff, 0x00ffff0000ff0000, 0x00ffff0000ff00ff,
        0x00ffff0000ffff00, 0x00ffff0000ffffff, 0x00ffff00ff000000,
        0x00ffff00ff0000ff, 0x00ffff00ff00ff00, 0x00ffff00ff00ffff,
        0x00ffff00ffff0000, 0x00ffff00ffff00ff, 0x00ffff00ffffff00,
        0x00ffff00ffffffff, 0x00ffffff00000000, 0x00ffffff000000ff,
        0x00ffffff0000ff00, 0x00ffffff0000ffff, 0x00ffffff00ff0000,
        0x00ffffff00ff00ff, 0x00ffffff00ffff00, 0x00ffffff00ffffff,
        0x00ffffffff000000, 0x00ffffffff0000ff, 0x00ffffffff00ff00,
        0x00ffffffff00ffff, 0x00ffffffffff0000, 0x00ffffffffff00ff,
        0x00ffffffffffff00, 0x00ffffffffffffff, 0xff00000000000000,
        0xff000000000000ff, 0xff0000000000ff00, 0xff0000000000ffff,
        0xff00000000ff0000, 0xff00000000ff00ff, 0xff00000000ffff00,
        0xff00000000ffffff, 0xff000000ff000000, 0xff000000ff0000ff,
        0xff000000ff00ff00, 0xff000000ff00ffff, 0xff000000ffff0000,
        0xff000000ffff00ff, 0xff000000ffffff00, 0xff000000ffffffff,
        0xff0000ff00000000, 0xff0000ff000000ff, 0xff0000ff0000ff00,
        0xff0000ff0000ffff, 0xff0000ff00ff0000, 0xff0000ff00ff00ff,
        0xff0000ff00ffff00, 0xff0000ff00ffffff, 0xff0000ffff000000,
        0xff0000ffff0000ff, 0xff0000ffff00ff00, 0xff0000ffff00ffff,
        0xff0000ffffff0000, 0xff0000ffffff00ff, 0xff0000ffffffff00,
        0xff0000ffffffffff, 0xff00ff0000000000, 0xff00ff00000000ff,
        0xff00ff000000ff00, 0xff00ff000000ffff, 0xff00ff0000ff0000,
        0xff00ff0000ff00ff, 0xff00ff0000ffff00, 0xff00ff0000ffffff,
        0xff00ff00ff000000, 0xff00ff00ff0000ff, 0xff00ff00ff00ff00,
        0xff00ff00ff00ffff, 0xff00ff00ffff0000, 0xff00ff00ffff00ff,
        0xff00ff00ffffff00, 0xff00ff00ffffffff, 0xff00ffff00000000,
        0xff00ffff000000ff, 0xff00ffff0000ff00, 0xff00ffff0000ffff,
        0xff00ffff00ff0000, 0xff00ffff00ff00ff, 0xff00ffff00ffff00,
        0xff00ffff00ffffff, 0xff00ffffff000000, 0xff00ffffff0000ff,
        0xff00ffffff00ff00, 0xff00ffffff00ffff, 0xff00ffffffff0000,
        0xff00ffffffff00ff, 0xff00ffffffffff00, 0xff00ffffffffffff,
        0xffff000000000000, 0xffff0000000000ff, 0xffff00000000ff00,
        0xffff00000000ffff, 0xffff000000ff0000, 0xffff000000ff00ff,
        0xffff000000ffff00, 0xffff000000ffffff, 0xffff0000ff000000,
        0xffff0000ff0000ff, 0xffff0000ff00ff00, 0xffff0000ff00ffff,
        0xffff0000ffff0000, 0xffff0000ffff00ff, 0xffff0000ffffff00,
        0xffff0000ffffffff, 0xffff00ff00000000, 0xffff00ff000000ff,
        0xffff00ff0000ff00, 0xffff00ff0000ffff, 0xffff00ff00ff0000,
        0xffff00ff00ff00ff, 0xffff00ff00ffff00, 0xffff00ff00ffffff,
        0xffff00ffff000000, 0xffff00ffff0000ff, 0xffff00ffff00ff00,
        0xffff00ffff00ffff, 0xffff00ffffff0000, 0xffff00ffffff00ff,
        0xffff00ffffffff00, 0xffff00ffffffffff, 0xffffff0000000000,
        0xffffff00000000ff, 0xffffff000000ff00, 0xffffff000000ffff,
        0xffffff0000ff0000, 0xffffff0000ff00ff, 0xffffff0000ffff00,
        0xffffff0000ffffff, 0xffffff00ff000000, 0xffffff00ff0000ff,
        0xffffff00ff00ff00, 0xffffff00ff00ffff, 0xffffff00ffff0000,
        0xffffff00ffff00ff, 0xffffff00ffffff00, 0xffffff00ffffffff,
        0xffffffff00000000, 0xffffffff000000ff, 0xffffffff0000ff00,
        0xffffffff0000ffff, 0xffffffff00ff0000, 0xffffffff00ff00ff,
        0xffffffff00ffff00, 0xffffffff00ffffff, 0xffffffffff000000,
        0xffffffffff0000ff, 0xffffffffff00ff00, 0xffffffffff00ffff,
        0xffffffffffff0000, 0xffffffffffff00ff, 0xffffffffffffff00,
        0xffffffffffffffff,
    };
    return word[byte];
}

/* Similarly for half-word elements.
 *  for (i = 0; i < 256; ++i) {
 *      unsigned long m = 0;
 *      if (i & 0xaa) {
 *          continue;
 *      }
 *      for (j = 0; j < 8; j += 2) {
 *          if ((i >> j) & 1) {
 *              m |= 0xfffful << (j << 3);
 *          }
 *      }
 *      printf("[0x%x] = 0x%016lx,\n", i, m);
 *  }
 */
static inline uint64_t expand_pred_h(uint8_t byte)
{
    static const uint64_t word[] = {
        [0x01] = 0x000000000000ffff, [0x04] = 0x00000000ffff0000,
        [0x05] = 0x00000000ffffffff, [0x10] = 0x0000ffff00000000,
        [0x11] = 0x0000ffff0000ffff, [0x14] = 0x0000ffffffff0000,
        [0x15] = 0x0000ffffffffffff, [0x40] = 0xffff000000000000,
        [0x41] = 0xffff00000000ffff, [0x44] = 0xffff0000ffff0000,
        [0x45] = 0xffff0000ffffffff, [0x50] = 0xffffffff00000000,
        [0x51] = 0xffffffff0000ffff, [0x54] = 0xffffffffffff0000,
        [0x55] = 0xffffffffffffffff,
    };
    return word[byte & 0x55];
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

#undef DO_ZPZZ
#undef DO_ZPZZ_D

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

#define DO_FNEG(N)    (N ^ ~((__typeof(N))-1 >> 1))

DO_ZPZ(sve_fneg_h, uint16_t, H1_2, DO_FNEG)
DO_ZPZ(sve_fneg_s, uint32_t, H1_4, DO_FNEG)
DO_ZPZ_D(sve_fneg_d, uint64_t, DO_FNEG)

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

/* Store zero into every active element of Zd.  We will use this for two
 * and three-operand predicated instructions for which logic dictates a
 * zero result.  In particular, logical shift by element size, which is
 * otherwise undefined on the host.
 *
 * For element sizes smaller than uint64_t, we use tables to expand
 * the N bits of the controlling predicate to a byte mask, and clear
 * those bytes.
 */
void HELPER(sve_clr_b)(void *vd, void *vg, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd;
    uint8_t *pg = vg;
    for (i = 0; i < opr_sz; i += 1) {
        d[i] &= ~expand_pred_b(pg[H1(i)]);
    }
}

void HELPER(sve_clr_h)(void *vd, void *vg, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd;
    uint8_t *pg = vg;
    for (i = 0; i < opr_sz; i += 1) {
        d[i] &= ~expand_pred_h(pg[H1(i)]);
    }
}

void HELPER(sve_clr_s)(void *vd, void *vg, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd;
    uint8_t *pg = vg;
    for (i = 0; i < opr_sz; i += 1) {
        d[i] &= ~expand_pred_s(pg[H1(i)]);
    }
}

void HELPER(sve_clr_d)(void *vd, void *vg, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd;
    uint8_t *pg = vg;
    for (i = 0; i < opr_sz; i += 1) {
        if (pg[H1(i)] & 1) {
            d[i] = 0;
        }
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

#undef DO_SHR
#undef DO_SHL
#undef DO_ASRD
#undef DO_ZPZI
#undef DO_ZPZI_D

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
    uint16_t *d = vd, *n = vn, *m = vm;
    for (i = 0; i < opr_sz; i += 1) {
        uint16_t nn = n[i];
        uint16_t mm = m[i];
        if (mm & 1) {
            nn = float16_one;
        }
        d[i] = nn ^ (mm & 2) << 14;
    }
}

void HELPER(sve_ftssel_s)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 4;
    uint32_t *d = vd, *n = vn, *m = vm;
    for (i = 0; i < opr_sz; i += 1) {
        uint32_t nn = n[i];
        uint32_t mm = m[i];
        if (mm & 1) {
            nn = float32_one;
        }
        d[i] = nn ^ (mm & 2) << 30;
    }
}

void HELPER(sve_ftssel_d)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    uint64_t *d = vd, *n = vn, *m = vm;
    for (i = 0; i < opr_sz; i += 1) {
        uint64_t nn = n[i];
        uint64_t mm = m[i];
        if (mm & 1) {
            nn = float64_one;
        }
        d[i] = nn ^ (mm & 2) << 62;
    }
}

/*
 * Signed saturating addition with scalar operand.
 */

void HELPER(sve_sqaddi_b)(void *d, void *a, int32_t b, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz; i += sizeof(int8_t)) {
        int r = *(int8_t *)(a + i) + b;
        if (r > INT8_MAX) {
            r = INT8_MAX;
        } else if (r < INT8_MIN) {
            r = INT8_MIN;
        }
        *(int8_t *)(d + i) = r;
    }
}

void HELPER(sve_sqaddi_h)(void *d, void *a, int32_t b, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz; i += sizeof(int16_t)) {
        int r = *(int16_t *)(a + i) + b;
        if (r > INT16_MAX) {
            r = INT16_MAX;
        } else if (r < INT16_MIN) {
            r = INT16_MIN;
        }
        *(int16_t *)(d + i) = r;
    }
}

void HELPER(sve_sqaddi_s)(void *d, void *a, int64_t b, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz; i += sizeof(int32_t)) {
        int64_t r = *(int32_t *)(a + i) + b;
        if (r > INT32_MAX) {
            r = INT32_MAX;
        } else if (r < INT32_MIN) {
            r = INT32_MIN;
        }
        *(int32_t *)(d + i) = r;
    }
}

void HELPER(sve_sqaddi_d)(void *d, void *a, int64_t b, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz; i += sizeof(int64_t)) {
        int64_t ai = *(int64_t *)(a + i);
        int64_t r = ai + b;
        if (((r ^ ai) & ~(ai ^ b)) < 0) {
            /* Signed overflow.  */
            r = (r < 0 ? INT64_MAX : INT64_MIN);
        }
        *(int64_t *)(d + i) = r;
    }
}

/*
 * Unsigned saturating addition with scalar operand.
 */

void HELPER(sve_uqaddi_b)(void *d, void *a, int32_t b, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz; i += sizeof(uint8_t)) {
        int r = *(uint8_t *)(a + i) + b;
        if (r > UINT8_MAX) {
            r = UINT8_MAX;
        } else if (r < 0) {
            r = 0;
        }
        *(uint8_t *)(d + i) = r;
    }
}

void HELPER(sve_uqaddi_h)(void *d, void *a, int32_t b, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz; i += sizeof(uint16_t)) {
        int r = *(uint16_t *)(a + i) + b;
        if (r > UINT16_MAX) {
            r = UINT16_MAX;
        } else if (r < 0) {
            r = 0;
        }
        *(uint16_t *)(d + i) = r;
    }
}

void HELPER(sve_uqaddi_s)(void *d, void *a, int64_t b, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz; i += sizeof(uint32_t)) {
        int64_t r = *(uint32_t *)(a + i) + b;
        if (r > UINT32_MAX) {
            r = UINT32_MAX;
        } else if (r < 0) {
            r = 0;
        }
        *(uint32_t *)(d + i) = r;
    }
}

void HELPER(sve_uqaddi_d)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        uint64_t r = *(uint64_t *)(a + i) + b;
        if (r < b) {
            r = UINT64_MAX;
        }
        *(uint64_t *)(d + i) = r;
    }
}

void HELPER(sve_uqsubi_d)(void *d, void *a, uint64_t b, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);

    for (i = 0; i < oprsz; i += sizeof(uint64_t)) {
        uint64_t ai = *(uint64_t *)(a + i);
        *(uint64_t *)(d + i) = (ai < b ? 0 : ai - b);
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

/* Big-endian hosts need to frob the byte indicies.  If the copy
 * happens to be 8-byte aligned, then no frobbing necessary.
 */
static void swap_memmove(void *vd, void *vs, size_t n)
{
    uintptr_t d = (uintptr_t)vd;
    uintptr_t s = (uintptr_t)vs;
    uintptr_t o = (d | s | n) & 7;
    size_t i;

#ifndef HOST_WORDS_BIGENDIAN
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
