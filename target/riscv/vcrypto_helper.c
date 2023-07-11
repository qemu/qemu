/*
 * RISC-V Vector Crypto Extension Helpers for QEMU.
 *
 * Copyright (C) 2023 SiFive, Inc.
 * Written by Codethink Ltd and SiFive.
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

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"
#include "cpu.h"
#include "crypto/aes.h"
#include "crypto/aes-round.h"
#include "exec/memop.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "internals.h"
#include "vector_internals.h"

static uint64_t clmul64(uint64_t y, uint64_t x)
{
    uint64_t result = 0;
    for (int j = 63; j >= 0; j--) {
        if ((y >> j) & 1) {
            result ^= (x << j);
        }
    }
    return result;
}

static uint64_t clmulh64(uint64_t y, uint64_t x)
{
    uint64_t result = 0;
    for (int j = 63; j >= 1; j--) {
        if ((y >> j) & 1) {
            result ^= (x >> (64 - j));
        }
    }
    return result;
}

RVVCALL(OPIVV2, vclmul_vv, OP_UUU_D, H8, H8, H8, clmul64)
GEN_VEXT_VV(vclmul_vv, 8)
RVVCALL(OPIVX2, vclmul_vx, OP_UUU_D, H8, H8, clmul64)
GEN_VEXT_VX(vclmul_vx, 8)
RVVCALL(OPIVV2, vclmulh_vv, OP_UUU_D, H8, H8, H8, clmulh64)
GEN_VEXT_VV(vclmulh_vv, 8)
RVVCALL(OPIVX2, vclmulh_vx, OP_UUU_D, H8, H8, clmulh64)
GEN_VEXT_VX(vclmulh_vx, 8)

RVVCALL(OPIVV2, vror_vv_b, OP_UUU_B, H1, H1, H1, ror8)
RVVCALL(OPIVV2, vror_vv_h, OP_UUU_H, H2, H2, H2, ror16)
RVVCALL(OPIVV2, vror_vv_w, OP_UUU_W, H4, H4, H4, ror32)
RVVCALL(OPIVV2, vror_vv_d, OP_UUU_D, H8, H8, H8, ror64)
GEN_VEXT_VV(vror_vv_b, 1)
GEN_VEXT_VV(vror_vv_h, 2)
GEN_VEXT_VV(vror_vv_w, 4)
GEN_VEXT_VV(vror_vv_d, 8)

RVVCALL(OPIVX2, vror_vx_b, OP_UUU_B, H1, H1, ror8)
RVVCALL(OPIVX2, vror_vx_h, OP_UUU_H, H2, H2, ror16)
RVVCALL(OPIVX2, vror_vx_w, OP_UUU_W, H4, H4, ror32)
RVVCALL(OPIVX2, vror_vx_d, OP_UUU_D, H8, H8, ror64)
GEN_VEXT_VX(vror_vx_b, 1)
GEN_VEXT_VX(vror_vx_h, 2)
GEN_VEXT_VX(vror_vx_w, 4)
GEN_VEXT_VX(vror_vx_d, 8)

RVVCALL(OPIVV2, vrol_vv_b, OP_UUU_B, H1, H1, H1, rol8)
RVVCALL(OPIVV2, vrol_vv_h, OP_UUU_H, H2, H2, H2, rol16)
RVVCALL(OPIVV2, vrol_vv_w, OP_UUU_W, H4, H4, H4, rol32)
RVVCALL(OPIVV2, vrol_vv_d, OP_UUU_D, H8, H8, H8, rol64)
GEN_VEXT_VV(vrol_vv_b, 1)
GEN_VEXT_VV(vrol_vv_h, 2)
GEN_VEXT_VV(vrol_vv_w, 4)
GEN_VEXT_VV(vrol_vv_d, 8)

RVVCALL(OPIVX2, vrol_vx_b, OP_UUU_B, H1, H1, rol8)
RVVCALL(OPIVX2, vrol_vx_h, OP_UUU_H, H2, H2, rol16)
RVVCALL(OPIVX2, vrol_vx_w, OP_UUU_W, H4, H4, rol32)
RVVCALL(OPIVX2, vrol_vx_d, OP_UUU_D, H8, H8, rol64)
GEN_VEXT_VX(vrol_vx_b, 1)
GEN_VEXT_VX(vrol_vx_h, 2)
GEN_VEXT_VX(vrol_vx_w, 4)
GEN_VEXT_VX(vrol_vx_d, 8)

static uint64_t brev8(uint64_t val)
{
    val = ((val & 0x5555555555555555ull) << 1) |
          ((val & 0xAAAAAAAAAAAAAAAAull) >> 1);
    val = ((val & 0x3333333333333333ull) << 2) |
          ((val & 0xCCCCCCCCCCCCCCCCull) >> 2);
    val = ((val & 0x0F0F0F0F0F0F0F0Full) << 4) |
          ((val & 0xF0F0F0F0F0F0F0F0ull) >> 4);

    return val;
}

RVVCALL(OPIVV1, vbrev8_v_b, OP_UU_B, H1, H1, brev8)
RVVCALL(OPIVV1, vbrev8_v_h, OP_UU_H, H2, H2, brev8)
RVVCALL(OPIVV1, vbrev8_v_w, OP_UU_W, H4, H4, brev8)
RVVCALL(OPIVV1, vbrev8_v_d, OP_UU_D, H8, H8, brev8)
GEN_VEXT_V(vbrev8_v_b, 1)
GEN_VEXT_V(vbrev8_v_h, 2)
GEN_VEXT_V(vbrev8_v_w, 4)
GEN_VEXT_V(vbrev8_v_d, 8)

#define DO_IDENTITY(a) (a)
RVVCALL(OPIVV1, vrev8_v_b, OP_UU_B, H1, H1, DO_IDENTITY)
RVVCALL(OPIVV1, vrev8_v_h, OP_UU_H, H2, H2, bswap16)
RVVCALL(OPIVV1, vrev8_v_w, OP_UU_W, H4, H4, bswap32)
RVVCALL(OPIVV1, vrev8_v_d, OP_UU_D, H8, H8, bswap64)
GEN_VEXT_V(vrev8_v_b, 1)
GEN_VEXT_V(vrev8_v_h, 2)
GEN_VEXT_V(vrev8_v_w, 4)
GEN_VEXT_V(vrev8_v_d, 8)

#define DO_ANDN(a, b) ((a) & ~(b))
RVVCALL(OPIVV2, vandn_vv_b, OP_UUU_B, H1, H1, H1, DO_ANDN)
RVVCALL(OPIVV2, vandn_vv_h, OP_UUU_H, H2, H2, H2, DO_ANDN)
RVVCALL(OPIVV2, vandn_vv_w, OP_UUU_W, H4, H4, H4, DO_ANDN)
RVVCALL(OPIVV2, vandn_vv_d, OP_UUU_D, H8, H8, H8, DO_ANDN)
GEN_VEXT_VV(vandn_vv_b, 1)
GEN_VEXT_VV(vandn_vv_h, 2)
GEN_VEXT_VV(vandn_vv_w, 4)
GEN_VEXT_VV(vandn_vv_d, 8)

RVVCALL(OPIVX2, vandn_vx_b, OP_UUU_B, H1, H1, DO_ANDN)
RVVCALL(OPIVX2, vandn_vx_h, OP_UUU_H, H2, H2, DO_ANDN)
RVVCALL(OPIVX2, vandn_vx_w, OP_UUU_W, H4, H4, DO_ANDN)
RVVCALL(OPIVX2, vandn_vx_d, OP_UUU_D, H8, H8, DO_ANDN)
GEN_VEXT_VX(vandn_vx_b, 1)
GEN_VEXT_VX(vandn_vx_h, 2)
GEN_VEXT_VX(vandn_vx_w, 4)
GEN_VEXT_VX(vandn_vx_d, 8)

RVVCALL(OPIVV1, vbrev_v_b, OP_UU_B, H1, H1, revbit8)
RVVCALL(OPIVV1, vbrev_v_h, OP_UU_H, H2, H2, revbit16)
RVVCALL(OPIVV1, vbrev_v_w, OP_UU_W, H4, H4, revbit32)
RVVCALL(OPIVV1, vbrev_v_d, OP_UU_D, H8, H8, revbit64)
GEN_VEXT_V(vbrev_v_b, 1)
GEN_VEXT_V(vbrev_v_h, 2)
GEN_VEXT_V(vbrev_v_w, 4)
GEN_VEXT_V(vbrev_v_d, 8)

RVVCALL(OPIVV1, vclz_v_b, OP_UU_B, H1, H1, clz8)
RVVCALL(OPIVV1, vclz_v_h, OP_UU_H, H2, H2, clz16)
RVVCALL(OPIVV1, vclz_v_w, OP_UU_W, H4, H4, clz32)
RVVCALL(OPIVV1, vclz_v_d, OP_UU_D, H8, H8, clz64)
GEN_VEXT_V(vclz_v_b, 1)
GEN_VEXT_V(vclz_v_h, 2)
GEN_VEXT_V(vclz_v_w, 4)
GEN_VEXT_V(vclz_v_d, 8)

RVVCALL(OPIVV1, vctz_v_b, OP_UU_B, H1, H1, ctz8)
RVVCALL(OPIVV1, vctz_v_h, OP_UU_H, H2, H2, ctz16)
RVVCALL(OPIVV1, vctz_v_w, OP_UU_W, H4, H4, ctz32)
RVVCALL(OPIVV1, vctz_v_d, OP_UU_D, H8, H8, ctz64)
GEN_VEXT_V(vctz_v_b, 1)
GEN_VEXT_V(vctz_v_h, 2)
GEN_VEXT_V(vctz_v_w, 4)
GEN_VEXT_V(vctz_v_d, 8)

RVVCALL(OPIVV1, vcpop_v_b, OP_UU_B, H1, H1, ctpop8)
RVVCALL(OPIVV1, vcpop_v_h, OP_UU_H, H2, H2, ctpop16)
RVVCALL(OPIVV1, vcpop_v_w, OP_UU_W, H4, H4, ctpop32)
RVVCALL(OPIVV1, vcpop_v_d, OP_UU_D, H8, H8, ctpop64)
GEN_VEXT_V(vcpop_v_b, 1)
GEN_VEXT_V(vcpop_v_h, 2)
GEN_VEXT_V(vcpop_v_w, 4)
GEN_VEXT_V(vcpop_v_d, 8)

#define DO_SLL(N, M) (N << (M & (sizeof(N) * 8 - 1)))
RVVCALL(OPIVV2, vwsll_vv_b, WOP_UUU_B, H2, H1, H1, DO_SLL)
RVVCALL(OPIVV2, vwsll_vv_h, WOP_UUU_H, H4, H2, H2, DO_SLL)
RVVCALL(OPIVV2, vwsll_vv_w, WOP_UUU_W, H8, H4, H4, DO_SLL)
GEN_VEXT_VV(vwsll_vv_b, 2)
GEN_VEXT_VV(vwsll_vv_h, 4)
GEN_VEXT_VV(vwsll_vv_w, 8)

RVVCALL(OPIVX2, vwsll_vx_b, WOP_UUU_B, H2, H1, DO_SLL)
RVVCALL(OPIVX2, vwsll_vx_h, WOP_UUU_H, H4, H2, DO_SLL)
RVVCALL(OPIVX2, vwsll_vx_w, WOP_UUU_W, H8, H4, DO_SLL)
GEN_VEXT_VX(vwsll_vx_b, 2)
GEN_VEXT_VX(vwsll_vx_h, 4)
GEN_VEXT_VX(vwsll_vx_w, 8)

void HELPER(egs_check)(uint32_t egs, CPURISCVState *env)
{
    uint32_t vl = env->vl;
    uint32_t vstart = env->vstart;

    if (vl % egs != 0 || vstart % egs != 0) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }
}

static inline void xor_round_key(AESState *round_state, AESState *round_key)
{
    round_state->v = round_state->v ^ round_key->v;
}

#define GEN_ZVKNED_HELPER_VV(NAME, ...)                                   \
    void HELPER(NAME)(void *vd, void *vs2, CPURISCVState *env,            \
                      uint32_t desc)                                      \
    {                                                                     \
        uint32_t vl = env->vl;                                            \
        uint32_t total_elems = vext_get_total_elems(env, desc, 4);        \
        uint32_t vta = vext_vta(desc);                                    \
                                                                          \
        for (uint32_t i = env->vstart / 4; i < env->vl / 4; i++) {        \
            AESState round_key;                                           \
            round_key.d[0] = *((uint64_t *)vs2 + H8(i * 2 + 0));          \
            round_key.d[1] = *((uint64_t *)vs2 + H8(i * 2 + 1));          \
            AESState round_state;                                         \
            round_state.d[0] = *((uint64_t *)vd + H8(i * 2 + 0));         \
            round_state.d[1] = *((uint64_t *)vd + H8(i * 2 + 1));         \
            __VA_ARGS__;                                                  \
            *((uint64_t *)vd + H8(i * 2 + 0)) = round_state.d[0];         \
            *((uint64_t *)vd + H8(i * 2 + 1)) = round_state.d[1];         \
        }                                                                 \
        env->vstart = 0;                                                  \
        /* set tail elements to 1s */                                     \
        vext_set_elems_1s(vd, vta, vl * 4, total_elems * 4);              \
    }

#define GEN_ZVKNED_HELPER_VS(NAME, ...)                                   \
    void HELPER(NAME)(void *vd, void *vs2, CPURISCVState *env,            \
                      uint32_t desc)                                      \
    {                                                                     \
        uint32_t vl = env->vl;                                            \
        uint32_t total_elems = vext_get_total_elems(env, desc, 4);        \
        uint32_t vta = vext_vta(desc);                                    \
                                                                          \
        for (uint32_t i = env->vstart / 4; i < env->vl / 4; i++) {        \
            AESState round_key;                                           \
            round_key.d[0] = *((uint64_t *)vs2 + H8(0));                  \
            round_key.d[1] = *((uint64_t *)vs2 + H8(1));                  \
            AESState round_state;                                         \
            round_state.d[0] = *((uint64_t *)vd + H8(i * 2 + 0));         \
            round_state.d[1] = *((uint64_t *)vd + H8(i * 2 + 1));         \
            __VA_ARGS__;                                                  \
            *((uint64_t *)vd + H8(i * 2 + 0)) = round_state.d[0];         \
            *((uint64_t *)vd + H8(i * 2 + 1)) = round_state.d[1];         \
        }                                                                 \
        env->vstart = 0;                                                  \
        /* set tail elements to 1s */                                     \
        vext_set_elems_1s(vd, vta, vl * 4, total_elems * 4);              \
    }

GEN_ZVKNED_HELPER_VV(vaesef_vv, aesenc_SB_SR_AK(&round_state,
                                                &round_state,
                                                &round_key,
                                                false);)
GEN_ZVKNED_HELPER_VS(vaesef_vs, aesenc_SB_SR_AK(&round_state,
                                                &round_state,
                                                &round_key,
                                                false);)
GEN_ZVKNED_HELPER_VV(vaesdf_vv, aesdec_ISB_ISR_AK(&round_state,
                                                  &round_state,
                                                  &round_key,
                                                  false);)
GEN_ZVKNED_HELPER_VS(vaesdf_vs, aesdec_ISB_ISR_AK(&round_state,
                                                  &round_state,
                                                  &round_key,
                                                  false);)
GEN_ZVKNED_HELPER_VV(vaesem_vv, aesenc_SB_SR_MC_AK(&round_state,
                                                   &round_state,
                                                   &round_key,
                                                   false);)
GEN_ZVKNED_HELPER_VS(vaesem_vs, aesenc_SB_SR_MC_AK(&round_state,
                                                   &round_state,
                                                   &round_key,
                                                   false);)
GEN_ZVKNED_HELPER_VV(vaesdm_vv, aesdec_ISB_ISR_AK_IMC(&round_state,
                                                      &round_state,
                                                      &round_key,
                                                      false);)
GEN_ZVKNED_HELPER_VS(vaesdm_vs, aesdec_ISB_ISR_AK_IMC(&round_state,
                                                      &round_state,
                                                      &round_key,
                                                      false);)
GEN_ZVKNED_HELPER_VS(vaesz_vs, xor_round_key(&round_state, &round_key);)

void HELPER(vaeskf1_vi)(void *vd_vptr, void *vs2_vptr, uint32_t uimm,
                        CPURISCVState *env, uint32_t desc)
{
    uint32_t *vd = vd_vptr;
    uint32_t *vs2 = vs2_vptr;
    uint32_t vl = env->vl;
    uint32_t total_elems = vext_get_total_elems(env, desc, 4);
    uint32_t vta = vext_vta(desc);

    uimm &= 0b1111;
    if (uimm > 10 || uimm == 0) {
        uimm ^= 0b1000;
    }

    for (uint32_t i = env->vstart / 4; i < env->vl / 4; i++) {
        uint32_t rk[8], tmp;
        static const uint32_t rcon[] = {
            0x00000001, 0x00000002, 0x00000004, 0x00000008, 0x00000010,
            0x00000020, 0x00000040, 0x00000080, 0x0000001B, 0x00000036,
        };

        rk[0] = vs2[i * 4 + H4(0)];
        rk[1] = vs2[i * 4 + H4(1)];
        rk[2] = vs2[i * 4 + H4(2)];
        rk[3] = vs2[i * 4 + H4(3)];
        tmp = ror32(rk[3], 8);

        rk[4] = rk[0] ^ (((uint32_t)AES_sbox[(tmp >> 24) & 0xff] << 24) |
                         ((uint32_t)AES_sbox[(tmp >> 16) & 0xff] << 16) |
                         ((uint32_t)AES_sbox[(tmp >> 8) & 0xff] << 8) |
                         ((uint32_t)AES_sbox[(tmp >> 0) & 0xff] << 0))
                      ^ rcon[uimm - 1];
        rk[5] = rk[1] ^ rk[4];
        rk[6] = rk[2] ^ rk[5];
        rk[7] = rk[3] ^ rk[6];

        vd[i * 4 + H4(0)] = rk[4];
        vd[i * 4 + H4(1)] = rk[5];
        vd[i * 4 + H4(2)] = rk[6];
        vd[i * 4 + H4(3)] = rk[7];
    }
    env->vstart = 0;
    /* set tail elements to 1s */
    vext_set_elems_1s(vd, vta, vl * 4, total_elems * 4);
}

void HELPER(vaeskf2_vi)(void *vd_vptr, void *vs2_vptr, uint32_t uimm,
                        CPURISCVState *env, uint32_t desc)
{
    uint32_t *vd = vd_vptr;
    uint32_t *vs2 = vs2_vptr;
    uint32_t vl = env->vl;
    uint32_t total_elems = vext_get_total_elems(env, desc, 4);
    uint32_t vta = vext_vta(desc);

    uimm &= 0b1111;
    if (uimm > 14 || uimm < 2) {
        uimm ^= 0b1000;
    }

    for (uint32_t i = env->vstart / 4; i < env->vl / 4; i++) {
        uint32_t rk[12], tmp;
        static const uint32_t rcon[] = {
            0x00000001, 0x00000002, 0x00000004, 0x00000008, 0x00000010,
            0x00000020, 0x00000040, 0x00000080, 0x0000001B, 0x00000036,
        };

        rk[0] = vd[i * 4 + H4(0)];
        rk[1] = vd[i * 4 + H4(1)];
        rk[2] = vd[i * 4 + H4(2)];
        rk[3] = vd[i * 4 + H4(3)];
        rk[4] = vs2[i * 4 + H4(0)];
        rk[5] = vs2[i * 4 + H4(1)];
        rk[6] = vs2[i * 4 + H4(2)];
        rk[7] = vs2[i * 4 + H4(3)];

        if (uimm % 2 == 0) {
            tmp = ror32(rk[7], 8);
            rk[8] = rk[0] ^ (((uint32_t)AES_sbox[(tmp >> 24) & 0xff] << 24) |
                             ((uint32_t)AES_sbox[(tmp >> 16) & 0xff] << 16) |
                             ((uint32_t)AES_sbox[(tmp >> 8) & 0xff] << 8) |
                             ((uint32_t)AES_sbox[(tmp >> 0) & 0xff] << 0))
                          ^ rcon[(uimm - 1) / 2];
        } else {
            rk[8] = rk[0] ^ (((uint32_t)AES_sbox[(rk[7] >> 24) & 0xff] << 24) |
                             ((uint32_t)AES_sbox[(rk[7] >> 16) & 0xff] << 16) |
                             ((uint32_t)AES_sbox[(rk[7] >> 8) & 0xff] << 8) |
                             ((uint32_t)AES_sbox[(rk[7] >> 0) & 0xff] << 0));
        }
        rk[9] = rk[1] ^ rk[8];
        rk[10] = rk[2] ^ rk[9];
        rk[11] = rk[3] ^ rk[10];

        vd[i * 4 + H4(0)] = rk[8];
        vd[i * 4 + H4(1)] = rk[9];
        vd[i * 4 + H4(2)] = rk[10];
        vd[i * 4 + H4(3)] = rk[11];
    }
    env->vstart = 0;
    /* set tail elements to 1s */
    vext_set_elems_1s(vd, vta, vl * 4, total_elems * 4);
}
