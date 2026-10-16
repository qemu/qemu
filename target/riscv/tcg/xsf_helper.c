/*
 * RISC-V translation helpers for the SiFive vendor extensions (xsf*)
 *
 * Copyright (c) 2023 SiFive, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "internals.h"
#include "vector_internals.h"

#define QOP_SUU_B int32_t, uint8_t, uint8_t, int32_t, int32_t
#define QOP_SUS_B int32_t, uint8_t, int8_t, int32_t, int32_t
#define QOP_SSU_B int32_t, int8_t, uint8_t, int32_t, int32_t
#define QOP_SSS_B int32_t, int8_t, int8_t, int32_t, int32_t

/* SiFive Custom int8 Matrix-Multiply */
/*
 * vd may overlap vs2, we need to allocate an additional vd array
 * to save temporary results of vd and write them back at the end.
 */
#define GEN_VEXT_SF_INT8_MATMUL(NAME, TD, T1, T2, TX1, TX2,           \
                                HD, HS1, HS2, ROWS, COLS, TILE_SIZE)  \
void HELPER(NAME)(void *vd, void *vs1, void *vs2,                     \
                  CPURISCVState *env, uint32_t desc)                  \
{                                                                     \
    int it, il, in, im, ivd, ivs1, ivs2;                              \
    TD *vds;                                                          \
                                                                      \
    if (env->vl % TILE_SIZE) {                                        \
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC()); \
        return;                                                       \
    }                                                                 \
                                                                      \
    VSTART_CHECK_EARLY_EXIT(env, env->vl);                            \
                                                                      \
    vds = g_malloc0(sizeof(TD) *                                      \
                    ROWS * ROWS * (env->vl / TILE_SIZE));             \
                                                                      \
    for (it = 0; it < (env->vl / TILE_SIZE); it++) {                  \
        for (il = 0; il < ROWS; il++) {                               \
            for (in = 0; in < ROWS; in++) {                           \
                ivd = ROWS * ROWS * it + ROWS * il + in;              \
                vds[ivd] = *((TD *)vd + HD(ivd));                     \
                for (im = 0; im < COLS; im++) {                       \
                    ivs1 = il * COLS + im;                            \
                    ivs2 = TILE_SIZE * it + im * ROWS + in;           \
                    T1 s1 = *((T1 *)vs1 + HS1(ivs1));                 \
                    T2 s2 = *((T2 *)vs2 + HS2(ivs2));                 \
                    vds[ivd] += (TX1)s1 * (TX2)s2;                    \
                }                                                     \
            }                                                         \
        }                                                             \
    }                                                                 \
                                                                      \
    for (it = 0; it < (env->vl / TILE_SIZE); it++) {                  \
        for (il = 0; il < ROWS; il++) {                               \
            for (in = 0; in < ROWS; in++) {                           \
                ivd = ROWS * ROWS * it + ROWS * il + in;              \
                *((TD *)vd + HD(ivd)) = vds[ivd];                     \
            }                                                         \
        }                                                             \
    }                                                                 \
                                                                      \
    env->vstart = 0;                                                  \
    g_free(vds);                                                      \
}

RVVCALL(GEN_VEXT_SF_INT8_MATMUL, sf_vqmaccu_4x8x4, QOP_SUU_B, H4, H1, H1,
        4, 8, 32)
RVVCALL(GEN_VEXT_SF_INT8_MATMUL, sf_vqmacc_4x8x4, QOP_SSS_B, H4, H1, H1,
        4, 8, 32)
RVVCALL(GEN_VEXT_SF_INT8_MATMUL, sf_vqmaccus_4x8x4, QOP_SUS_B, H4, H1, H1,
        4, 8, 32)
RVVCALL(GEN_VEXT_SF_INT8_MATMUL, sf_vqmaccsu_4x8x4, QOP_SSU_B, H4, H1, H1,
        4, 8, 32)
RVVCALL(GEN_VEXT_SF_INT8_MATMUL, sf_vqmaccu_2x8x2, QOP_SUU_B, H4, H1, H1,
        2, 8, 16)
RVVCALL(GEN_VEXT_SF_INT8_MATMUL, sf_vqmacc_2x8x2, QOP_SSS_B, H4, H1, H1,
        2, 8, 16)
RVVCALL(GEN_VEXT_SF_INT8_MATMUL, sf_vqmaccus_2x8x2, QOP_SUS_B, H4, H1, H1,
        2, 8, 16)
RVVCALL(GEN_VEXT_SF_INT8_MATMUL, sf_vqmaccsu_2x8x2, QOP_SSU_B, H4, H1, H1,
        2, 8, 16)
