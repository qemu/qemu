/*
 * RISC-V Vector Extension Internals
 *
 * Copyright (c) 2020 T-Head Semiconductor Co., Ltd. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TARGET_RISCV_VECTOR_INTERNALS_H
#define TARGET_RISCV_VECTOR_INTERNALS_H

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "cpu.h"
#include "tcg/tcg-gvec-desc.h"
#include "internals.h"

static inline uint32_t vext_nf(uint32_t desc)
{
    return FIELD_EX32(simd_data(desc), VDATA, NF);
}

/*
 * Note that vector data is stored in host-endian 64-bit chunks,
 * so addressing units smaller than that needs a host-endian fixup.
 */
#if HOST_BIG_ENDIAN
#define H1(x)   ((x) ^ 7)
#define H1_2(x) ((x) ^ 6)
#define H1_4(x) ((x) ^ 4)
#define H2(x)   ((x) ^ 3)
#define H4(x)   ((x) ^ 1)
#define H8(x)   ((x))
#else
#define H1(x)   (x)
#define H1_2(x) (x)
#define H1_4(x) (x)
#define H2(x)   (x)
#define H4(x)   (x)
#define H8(x)   (x)
#endif

/*
 * Encode LMUL to lmul as following:
 *     LMUL    vlmul    lmul
 *      1       000       0
 *      2       001       1
 *      4       010       2
 *      8       011       3
 *      -       100       -
 *     1/8      101      -3
 *     1/4      110      -2
 *     1/2      111      -1
 */
static inline int32_t vext_lmul(uint32_t desc)
{
    return sextract32(FIELD_EX32(simd_data(desc), VDATA, LMUL), 0, 3);
}

static inline uint32_t vext_vm(uint32_t desc)
{
    return FIELD_EX32(simd_data(desc), VDATA, VM);
}

static inline uint32_t vext_vma(uint32_t desc)
{
    return FIELD_EX32(simd_data(desc), VDATA, VMA);
}

static inline uint32_t vext_vta(uint32_t desc)
{
    return FIELD_EX32(simd_data(desc), VDATA, VTA);
}

static inline uint32_t vext_vta_all_1s(uint32_t desc)
{
    return FIELD_EX32(simd_data(desc), VDATA, VTA_ALL_1S);
}

/*
 * Earlier designs (pre-0.9) had a varying number of bits
 * per mask value (MLEN). In the 0.9 design, MLEN=1.
 * (Section 4.5)
 */
static inline int vext_elem_mask(void *v0, int index)
{
    int idx = index / 64;
    int pos = index  % 64;
    return (((uint64_t *)v0)[idx] >> pos) & 1;
}

/*
 * Get number of total elements, including prestart, body and tail elements.
 * Note that when LMUL < 1, the tail includes the elements past VLMAX that
 * are held in the same vector register.
 */
static inline uint32_t vext_get_total_elems(CPURISCVState *env, uint32_t desc,
                                            uint32_t esz)
{
    uint32_t vlenb = simd_maxsz(desc);
    uint32_t sew = 1 << FIELD_EX64(env->vtype, VTYPE, VSEW);
    int8_t emul = ctzl(esz) - ctzl(sew) + vext_lmul(desc) < 0 ? 0 :
                  ctzl(esz) - ctzl(sew) + vext_lmul(desc);
    return (vlenb << emul) / esz;
}

/* set agnostic elements to 1s */
void vext_set_elems_1s(void *base, uint32_t is_agnostic, uint32_t cnt,
                       uint32_t tot);

/* expand macro args before macro */
#define RVVCALL(macro, ...)  macro(__VA_ARGS__)

/* (TD, T2, TX2) */
#define OP_UU_B uint8_t, uint8_t, uint8_t
#define OP_UU_H uint16_t, uint16_t, uint16_t
#define OP_UU_W uint32_t, uint32_t, uint32_t
#define OP_UU_D uint64_t, uint64_t, uint64_t

/* (TD, T1, T2, TX1, TX2) */
#define OP_UUU_B uint8_t, uint8_t, uint8_t, uint8_t, uint8_t
#define OP_UUU_H uint16_t, uint16_t, uint16_t, uint16_t, uint16_t
#define OP_UUU_W uint32_t, uint32_t, uint32_t, uint32_t, uint32_t
#define OP_UUU_D uint64_t, uint64_t, uint64_t, uint64_t, uint64_t

#define OPIVV1(NAME, TD, T2, TX2, HD, HS2, OP)         \
static void do_##NAME(void *vd, void *vs2, int i)      \
{                                                      \
    TX2 s2 = *((T2 *)vs2 + HS2(i));                    \
    *((TD *)vd + HD(i)) = OP(s2);                      \
}

#define GEN_VEXT_V(NAME, ESZ)                          \
void HELPER(NAME)(void *vd, void *v0, void *vs2,       \
                  CPURISCVState *env, uint32_t desc)   \
{                                                      \
    uint32_t vm = vext_vm(desc);                       \
    uint32_t vl = env->vl;                             \
    uint32_t total_elems =                             \
        vext_get_total_elems(env, desc, ESZ);          \
    uint32_t vta = vext_vta(desc);                     \
    uint32_t vma = vext_vma(desc);                     \
    uint32_t i;                                        \
                                                       \
    for (i = env->vstart; i < vl; i++) {               \
        if (!vm && !vext_elem_mask(v0, i)) {           \
            /* set masked-off elements to 1s */        \
            vext_set_elems_1s(vd, vma, i * ESZ,        \
                              (i + 1) * ESZ);          \
            continue;                                  \
        }                                              \
        do_##NAME(vd, vs2, i);                         \
    }                                                  \
    env->vstart = 0;                                   \
    /* set tail elements to 1s */                      \
    vext_set_elems_1s(vd, vta, vl * ESZ,               \
                      total_elems * ESZ);              \
}

/* operation of two vector elements */
typedef void opivv2_fn(void *vd, void *vs1, void *vs2, int i);

#define OPIVV2(NAME, TD, T1, T2, TX1, TX2, HD, HS1, HS2, OP)    \
static void do_##NAME(void *vd, void *vs1, void *vs2, int i)    \
{                                                               \
    TX1 s1 = *((T1 *)vs1 + HS1(i));                             \
    TX2 s2 = *((T2 *)vs2 + HS2(i));                             \
    *((TD *)vd + HD(i)) = OP(s2, s1);                           \
}

void do_vext_vv(void *vd, void *v0, void *vs1, void *vs2,
                CPURISCVState *env, uint32_t desc,
                opivv2_fn *fn, uint32_t esz);

/* generate the helpers for OPIVV */
#define GEN_VEXT_VV(NAME, ESZ)                            \
void HELPER(NAME)(void *vd, void *v0, void *vs1,          \
                  void *vs2, CPURISCVState *env,          \
                  uint32_t desc)                          \
{                                                         \
    do_vext_vv(vd, v0, vs1, vs2, env, desc,               \
               do_##NAME, ESZ);                           \
}

typedef void opivx2_fn(void *vd, target_long s1, void *vs2, int i);

/*
 * (T1)s1 gives the real operator type.
 * (TX1)(T1)s1 expands the operator type of widen or narrow operations.
 */
#define OPIVX2(NAME, TD, T1, T2, TX1, TX2, HD, HS2, OP)             \
static void do_##NAME(void *vd, target_long s1, void *vs2, int i)   \
{                                                                   \
    TX2 s2 = *((T2 *)vs2 + HS2(i));                                 \
    *((TD *)vd + HD(i)) = OP(s2, (TX1)(T1)s1);                      \
}

void do_vext_vx(void *vd, void *v0, target_long s1, void *vs2,
                CPURISCVState *env, uint32_t desc,
                opivx2_fn fn, uint32_t esz);

/* generate the helpers for OPIVX */
#define GEN_VEXT_VX(NAME, ESZ)                            \
void HELPER(NAME)(void *vd, void *v0, target_ulong s1,    \
                  void *vs2, CPURISCVState *env,          \
                  uint32_t desc)                          \
{                                                         \
    do_vext_vx(vd, v0, s1, vs2, env, desc,                \
               do_##NAME, ESZ);                           \
}

/* Three of the widening shortening macros: */
/* (TD, T1, T2, TX1, TX2) */
#define WOP_UUU_B uint16_t, uint8_t, uint8_t, uint16_t, uint16_t
#define WOP_UUU_H uint32_t, uint16_t, uint16_t, uint32_t, uint32_t
#define WOP_UUU_W uint64_t, uint32_t, uint32_t, uint64_t, uint64_t

#endif /* TARGET_RISCV_VECTOR_INTERNALS_H */
