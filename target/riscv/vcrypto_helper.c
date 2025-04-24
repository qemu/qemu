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
#include "crypto/sm4.h"
#include "exec/memop.h"
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
        VSTART_CHECK_EARLY_EXIT(env, vl);                                 \
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
        VSTART_CHECK_EARLY_EXIT(env, vl);                                 \
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

    VSTART_CHECK_EARLY_EXIT(env, vl);

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

    VSTART_CHECK_EARLY_EXIT(env, vl);

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

static inline uint32_t sig0_sha256(uint32_t x)
{
    return ror32(x, 7) ^ ror32(x, 18) ^ (x >> 3);
}

static inline uint32_t sig1_sha256(uint32_t x)
{
    return ror32(x, 17) ^ ror32(x, 19) ^ (x >> 10);
}

static inline uint64_t sig0_sha512(uint64_t x)
{
    return ror64(x, 1) ^ ror64(x, 8) ^ (x >> 7);
}

static inline uint64_t sig1_sha512(uint64_t x)
{
    return ror64(x, 19) ^ ror64(x, 61) ^ (x >> 6);
}

static inline void vsha2ms_e32(uint32_t *vd, uint32_t *vs1, uint32_t *vs2)
{
    uint32_t res[4];
    res[0] = sig1_sha256(vs1[H4(2)]) + vs2[H4(1)] + sig0_sha256(vd[H4(1)]) +
             vd[H4(0)];
    res[1] = sig1_sha256(vs1[H4(3)]) + vs2[H4(2)] + sig0_sha256(vd[H4(2)]) +
             vd[H4(1)];
    res[2] =
        sig1_sha256(res[0]) + vs2[H4(3)] + sig0_sha256(vd[H4(3)]) + vd[H4(2)];
    res[3] =
        sig1_sha256(res[1]) + vs1[H4(0)] + sig0_sha256(vs2[H4(0)]) + vd[H4(3)];
    vd[H4(3)] = res[3];
    vd[H4(2)] = res[2];
    vd[H4(1)] = res[1];
    vd[H4(0)] = res[0];
}

static inline void vsha2ms_e64(uint64_t *vd, uint64_t *vs1, uint64_t *vs2)
{
    uint64_t res[4];
    res[0] = sig1_sha512(vs1[2]) + vs2[1] + sig0_sha512(vd[1]) + vd[0];
    res[1] = sig1_sha512(vs1[3]) + vs2[2] + sig0_sha512(vd[2]) + vd[1];
    res[2] = sig1_sha512(res[0]) + vs2[3] + sig0_sha512(vd[3]) + vd[2];
    res[3] = sig1_sha512(res[1]) + vs1[0] + sig0_sha512(vs2[0]) + vd[3];
    vd[3] = res[3];
    vd[2] = res[2];
    vd[1] = res[1];
    vd[0] = res[0];
}

void HELPER(vsha2ms_vv)(void *vd, void *vs1, void *vs2, CPURISCVState *env,
                        uint32_t desc)
{
    uint32_t sew = FIELD_EX64(env->vtype, VTYPE, VSEW);
    uint32_t esz = sew == MO_32 ? 4 : 8;
    uint32_t total_elems;
    uint32_t vta = vext_vta(desc);

    VSTART_CHECK_EARLY_EXIT(env, env->vl);

    for (uint32_t i = env->vstart / 4; i < env->vl / 4; i++) {
        if (sew == MO_32) {
            vsha2ms_e32(((uint32_t *)vd) + i * 4, ((uint32_t *)vs1) + i * 4,
                        ((uint32_t *)vs2) + i * 4);
        } else {
            /* If not 32 then SEW should be 64 */
            vsha2ms_e64(((uint64_t *)vd) + i * 4, ((uint64_t *)vs1) + i * 4,
                        ((uint64_t *)vs2) + i * 4);
        }
    }
    /* set tail elements to 1s */
    total_elems = vext_get_total_elems(env, desc, esz);
    vext_set_elems_1s(vd, vta, env->vl * esz, total_elems * esz);
    env->vstart = 0;
}

static inline uint64_t sum0_64(uint64_t x)
{
    return ror64(x, 28) ^ ror64(x, 34) ^ ror64(x, 39);
}

static inline uint32_t sum0_32(uint32_t x)
{
    return ror32(x, 2) ^ ror32(x, 13) ^ ror32(x, 22);
}

static inline uint64_t sum1_64(uint64_t x)
{
    return ror64(x, 14) ^ ror64(x, 18) ^ ror64(x, 41);
}

static inline uint32_t sum1_32(uint32_t x)
{
    return ror32(x, 6) ^ ror32(x, 11) ^ ror32(x, 25);
}

#define ch(x, y, z) ((x & y) ^ ((~x) & z))

#define maj(x, y, z) ((x & y) ^ (x & z) ^ (y & z))

static void vsha2c_64(uint64_t *vs2, uint64_t *vd, uint64_t *vs1)
{
    uint64_t a = vs2[3], b = vs2[2], e = vs2[1], f = vs2[0];
    uint64_t c = vd[3], d = vd[2], g = vd[1], h = vd[0];
    uint64_t W0 = vs1[0], W1 = vs1[1];
    uint64_t T1 = h + sum1_64(e) + ch(e, f, g) + W0;
    uint64_t T2 = sum0_64(a) + maj(a, b, c);

    h = g;
    g = f;
    f = e;
    e = d + T1;
    d = c;
    c = b;
    b = a;
    a = T1 + T2;

    T1 = h + sum1_64(e) + ch(e, f, g) + W1;
    T2 = sum0_64(a) + maj(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + T1;
    d = c;
    c = b;
    b = a;
    a = T1 + T2;

    vd[0] = f;
    vd[1] = e;
    vd[2] = b;
    vd[3] = a;
}

static void vsha2c_32(uint32_t *vs2, uint32_t *vd, uint32_t *vs1)
{
    uint32_t a = vs2[H4(3)], b = vs2[H4(2)], e = vs2[H4(1)], f = vs2[H4(0)];
    uint32_t c = vd[H4(3)], d = vd[H4(2)], g = vd[H4(1)], h = vd[H4(0)];
    uint32_t W0 = vs1[H4(0)], W1 = vs1[H4(1)];
    uint32_t T1 = h + sum1_32(e) + ch(e, f, g) + W0;
    uint32_t T2 = sum0_32(a) + maj(a, b, c);

    h = g;
    g = f;
    f = e;
    e = d + T1;
    d = c;
    c = b;
    b = a;
    a = T1 + T2;

    T1 = h + sum1_32(e) + ch(e, f, g) + W1;
    T2 = sum0_32(a) + maj(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + T1;
    d = c;
    c = b;
    b = a;
    a = T1 + T2;

    vd[H4(0)] = f;
    vd[H4(1)] = e;
    vd[H4(2)] = b;
    vd[H4(3)] = a;
}

void HELPER(vsha2ch32_vv)(void *vd, void *vs1, void *vs2, CPURISCVState *env,
                          uint32_t desc)
{
    const uint32_t esz = 4;
    uint32_t total_elems;
    uint32_t vta = vext_vta(desc);

    VSTART_CHECK_EARLY_EXIT(env, env->vl);

    for (uint32_t i = env->vstart / 4; i < env->vl / 4; i++) {
        vsha2c_32(((uint32_t *)vs2) + 4 * i, ((uint32_t *)vd) + 4 * i,
                  ((uint32_t *)vs1) + 4 * i + 2);
    }

    /* set tail elements to 1s */
    total_elems = vext_get_total_elems(env, desc, esz);
    vext_set_elems_1s(vd, vta, env->vl * esz, total_elems * esz);
    env->vstart = 0;
}

void HELPER(vsha2ch64_vv)(void *vd, void *vs1, void *vs2, CPURISCVState *env,
                          uint32_t desc)
{
    const uint32_t esz = 8;
    uint32_t total_elems;
    uint32_t vta = vext_vta(desc);

    VSTART_CHECK_EARLY_EXIT(env, env->vl);

    for (uint32_t i = env->vstart / 4; i < env->vl / 4; i++) {
        vsha2c_64(((uint64_t *)vs2) + 4 * i, ((uint64_t *)vd) + 4 * i,
                  ((uint64_t *)vs1) + 4 * i + 2);
    }

    /* set tail elements to 1s */
    total_elems = vext_get_total_elems(env, desc, esz);
    vext_set_elems_1s(vd, vta, env->vl * esz, total_elems * esz);
    env->vstart = 0;
}

void HELPER(vsha2cl32_vv)(void *vd, void *vs1, void *vs2, CPURISCVState *env,
                          uint32_t desc)
{
    const uint32_t esz = 4;
    uint32_t total_elems;
    uint32_t vta = vext_vta(desc);

    VSTART_CHECK_EARLY_EXIT(env, env->vl);

    for (uint32_t i = env->vstart / 4; i < env->vl / 4; i++) {
        vsha2c_32(((uint32_t *)vs2) + 4 * i, ((uint32_t *)vd) + 4 * i,
                  (((uint32_t *)vs1) + 4 * i));
    }

    /* set tail elements to 1s */
    total_elems = vext_get_total_elems(env, desc, esz);
    vext_set_elems_1s(vd, vta, env->vl * esz, total_elems * esz);
    env->vstart = 0;
}

void HELPER(vsha2cl64_vv)(void *vd, void *vs1, void *vs2, CPURISCVState *env,
                          uint32_t desc)
{
    uint32_t esz = 8;
    uint32_t total_elems;
    uint32_t vta = vext_vta(desc);

    VSTART_CHECK_EARLY_EXIT(env, env->vl);

    for (uint32_t i = env->vstart / 4; i < env->vl / 4; i++) {
        vsha2c_64(((uint64_t *)vs2) + 4 * i, ((uint64_t *)vd) + 4 * i,
                  (((uint64_t *)vs1) + 4 * i));
    }

    /* set tail elements to 1s */
    total_elems = vext_get_total_elems(env, desc, esz);
    vext_set_elems_1s(vd, vta, env->vl * esz, total_elems * esz);
    env->vstart = 0;
}

static inline uint32_t p1(uint32_t x)
{
    return x ^ rol32(x, 15) ^ rol32(x, 23);
}

static inline uint32_t zvksh_w(uint32_t m16, uint32_t m9, uint32_t m3,
                               uint32_t m13, uint32_t m6)
{
    return p1(m16 ^ m9 ^ rol32(m3, 15)) ^ rol32(m13, 7) ^ m6;
}

void HELPER(vsm3me_vv)(void *vd_vptr, void *vs1_vptr, void *vs2_vptr,
                       CPURISCVState *env, uint32_t desc)
{
    uint32_t esz = memop_size(FIELD_EX64(env->vtype, VTYPE, VSEW));
    uint32_t total_elems = vext_get_total_elems(env, desc, esz);
    uint32_t vta = vext_vta(desc);
    uint32_t *vd = vd_vptr;
    uint32_t *vs1 = vs1_vptr;
    uint32_t *vs2 = vs2_vptr;

    VSTART_CHECK_EARLY_EXIT(env, env->vl);

    for (int i = env->vstart / 8; i < env->vl / 8; i++) {
        uint32_t w[24];
        for (int j = 0; j < 8; j++) {
            w[j] = bswap32(vs1[H4((i * 8) + j)]);
            w[j + 8] = bswap32(vs2[H4((i * 8) + j)]);
        }
        for (int j = 0; j < 8; j++) {
            w[j + 16] =
                zvksh_w(w[j], w[j + 7], w[j + 13], w[j + 3], w[j + 10]);
        }
        for (int j = 0; j < 8; j++) {
            vd[(i * 8) + j] = bswap32(w[H4(j + 16)]);
        }
    }
    vext_set_elems_1s(vd_vptr, vta, env->vl * esz, total_elems * esz);
    env->vstart = 0;
}

static inline uint32_t ff1(uint32_t x, uint32_t y, uint32_t z)
{
    return x ^ y ^ z;
}

static inline uint32_t ff2(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) | (x & z) | (y & z);
}

static inline uint32_t ff_j(uint32_t x, uint32_t y, uint32_t z, uint32_t j)
{
    return (j <= 15) ? ff1(x, y, z) : ff2(x, y, z);
}

static inline uint32_t gg1(uint32_t x, uint32_t y, uint32_t z)
{
    return x ^ y ^ z;
}

static inline uint32_t gg2(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) | (~x & z);
}

static inline uint32_t gg_j(uint32_t x, uint32_t y, uint32_t z, uint32_t j)
{
    return (j <= 15) ? gg1(x, y, z) : gg2(x, y, z);
}

static inline uint32_t t_j(uint32_t j)
{
    return (j <= 15) ? 0x79cc4519 : 0x7a879d8a;
}

static inline uint32_t p_0(uint32_t x)
{
    return x ^ rol32(x, 9) ^ rol32(x, 17);
}

static void sm3c(uint32_t *vd, uint32_t *vs1, uint32_t *vs2, uint32_t uimm)
{
    uint32_t x0, x1;
    uint32_t j;
    uint32_t ss1, ss2, tt1, tt2;
    x0 = vs2[0] ^ vs2[4];
    x1 = vs2[1] ^ vs2[5];
    j = 2 * uimm;
    ss1 = rol32(rol32(vs1[0], 12) + vs1[4] + rol32(t_j(j), j % 32), 7);
    ss2 = ss1 ^ rol32(vs1[0], 12);
    tt1 = ff_j(vs1[0], vs1[1], vs1[2], j) + vs1[3] + ss2 + x0;
    tt2 = gg_j(vs1[4], vs1[5], vs1[6], j) + vs1[7] + ss1 + vs2[0];
    vs1[3] = vs1[2];
    vd[3] = rol32(vs1[1], 9);
    vs1[1] = vs1[0];
    vd[1] = tt1;
    vs1[7] = vs1[6];
    vd[7] = rol32(vs1[5], 19);
    vs1[5] = vs1[4];
    vd[5] = p_0(tt2);
    j = 2 * uimm + 1;
    ss1 = rol32(rol32(vd[1], 12) + vd[5] + rol32(t_j(j), j % 32), 7);
    ss2 = ss1 ^ rol32(vd[1], 12);
    tt1 = ff_j(vd[1], vs1[1], vd[3], j) + vs1[3] + ss2 + x1;
    tt2 = gg_j(vd[5], vs1[5], vd[7], j) + vs1[7] + ss1 + vs2[1];
    vd[2] = rol32(vs1[1], 9);
    vd[0] = tt1;
    vd[6] = rol32(vs1[5], 19);
    vd[4] = p_0(tt2);
}

void HELPER(vsm3c_vi)(void *vd_vptr, void *vs2_vptr, uint32_t uimm,
                      CPURISCVState *env, uint32_t desc)
{
    uint32_t esz = memop_size(FIELD_EX64(env->vtype, VTYPE, VSEW));
    uint32_t total_elems = vext_get_total_elems(env, desc, esz);
    uint32_t vta = vext_vta(desc);
    uint32_t *vd = vd_vptr;
    uint32_t *vs2 = vs2_vptr;
    uint32_t v1[8], v2[8], v3[8];

    VSTART_CHECK_EARLY_EXIT(env, env->vl);

    for (int i = env->vstart / 8; i < env->vl / 8; i++) {
        for (int k = 0; k < 8; k++) {
            v2[k] = bswap32(vd[H4(i * 8 + k)]);
            v3[k] = bswap32(vs2[H4(i * 8 + k)]);
        }
        sm3c(v1, v2, v3, uimm);
        for (int k = 0; k < 8; k++) {
            vd[i * 8 + k] = bswap32(v1[H4(k)]);
        }
    }
    vext_set_elems_1s(vd_vptr, vta, env->vl * esz, total_elems * esz);
    env->vstart = 0;
}

void HELPER(vghsh_vv)(void *vd_vptr, void *vs1_vptr, void *vs2_vptr,
                      CPURISCVState *env, uint32_t desc)
{
    uint64_t *vd = vd_vptr;
    uint64_t *vs1 = vs1_vptr;
    uint64_t *vs2 = vs2_vptr;
    uint32_t vta = vext_vta(desc);
    uint32_t total_elems = vext_get_total_elems(env, desc, 4);

    VSTART_CHECK_EARLY_EXIT(env, env->vl);

    for (uint32_t i = env->vstart / 4; i < env->vl / 4; i++) {
        uint64_t Y[2] = {vd[i * 2 + 0], vd[i * 2 + 1]};
        uint64_t H[2] = {brev8(vs2[i * 2 + 0]), brev8(vs2[i * 2 + 1])};
        uint64_t X[2] = {vs1[i * 2 + 0], vs1[i * 2 + 1]};
        uint64_t Z[2] = {0, 0};

        uint64_t S[2] = {brev8(Y[0] ^ X[0]), brev8(Y[1] ^ X[1])};

        for (int j = 0; j < 128; j++) {
            if ((S[j / 64] >> (j % 64)) & 1) {
                Z[0] ^= H[0];
                Z[1] ^= H[1];
            }
            bool reduce = ((H[1] >> 63) & 1);
            H[1] = H[1] << 1 | H[0] >> 63;
            H[0] = H[0] << 1;
            if (reduce) {
                H[0] ^= 0x87;
            }
        }

        vd[i * 2 + 0] = brev8(Z[0]);
        vd[i * 2 + 1] = brev8(Z[1]);
    }
    /* set tail elements to 1s */
    vext_set_elems_1s(vd, vta, env->vl * 4, total_elems * 4);
    env->vstart = 0;
}

void HELPER(vgmul_vv)(void *vd_vptr, void *vs2_vptr, CPURISCVState *env,
                      uint32_t desc)
{
    uint64_t *vd = vd_vptr;
    uint64_t *vs2 = vs2_vptr;
    uint32_t vta = vext_vta(desc);
    uint32_t total_elems = vext_get_total_elems(env, desc, 4);

    VSTART_CHECK_EARLY_EXIT(env, env->vl);

    for (uint32_t i = env->vstart / 4; i < env->vl / 4; i++) {
        uint64_t Y[2] = {brev8(vd[i * 2 + 0]), brev8(vd[i * 2 + 1])};
        uint64_t H[2] = {brev8(vs2[i * 2 + 0]), brev8(vs2[i * 2 + 1])};
        uint64_t Z[2] = {0, 0};

        for (int j = 0; j < 128; j++) {
            if ((Y[j / 64] >> (j % 64)) & 1) {
                Z[0] ^= H[0];
                Z[1] ^= H[1];
            }
            bool reduce = ((H[1] >> 63) & 1);
            H[1] = H[1] << 1 | H[0] >> 63;
            H[0] = H[0] << 1;
            if (reduce) {
                H[0] ^= 0x87;
            }
        }

        vd[i * 2 + 0] = brev8(Z[0]);
        vd[i * 2 + 1] = brev8(Z[1]);
    }
    /* set tail elements to 1s */
    vext_set_elems_1s(vd, vta, env->vl * 4, total_elems * 4);
    env->vstart = 0;
}

void HELPER(vsm4k_vi)(void *vd, void *vs2, uint32_t uimm5, CPURISCVState *env,
                      uint32_t desc)
{
    const uint32_t egs = 4;
    uint32_t rnd = uimm5 & 0x7;
    uint32_t group_start = env->vstart / egs;
    uint32_t group_end = env->vl / egs;
    uint32_t esz = sizeof(uint32_t);
    uint32_t total_elems = vext_get_total_elems(env, desc, esz);

    VSTART_CHECK_EARLY_EXIT(env, env->vl);

    for (uint32_t i = group_start; i < group_end; ++i) {
        uint32_t vstart = i * egs;
        uint32_t vend = (i + 1) * egs;
        uint32_t rk[4] = {0};
        uint32_t tmp[8] = {0};

        for (uint32_t j = vstart; j < vend; ++j) {
            rk[j - vstart] = *((uint32_t *)vs2 + H4(j));
        }

        for (uint32_t j = 0; j < egs; ++j) {
            tmp[j] = rk[j];
        }

        for (uint32_t j = 0; j < egs; ++j) {
            uint32_t b, s;
            b = tmp[j + 1] ^ tmp[j + 2] ^ tmp[j + 3] ^ sm4_ck[rnd * 4 + j];

            s = sm4_subword(b);

            tmp[j + 4] = tmp[j] ^ (s ^ rol32(s, 13) ^ rol32(s, 23));
        }

        for (uint32_t j = vstart; j < vend; ++j) {
            *((uint32_t *)vd + H4(j)) = tmp[egs + (j - vstart)];
        }
    }

    env->vstart = 0;
    /* set tail elements to 1s */
    vext_set_elems_1s(vd, vext_vta(desc), env->vl * esz, total_elems * esz);
}

static void do_sm4_round(uint32_t *rk, uint32_t *buf)
{
    const uint32_t egs = 4;
    uint32_t s, b;

    for (uint32_t j = egs; j < egs * 2; ++j) {
        b = buf[j - 3] ^ buf[j - 2] ^ buf[j - 1] ^ rk[j - 4];

        s = sm4_subword(b);

        buf[j] = buf[j - 4] ^ (s ^ rol32(s, 2) ^ rol32(s, 10) ^ rol32(s, 18) ^
                               rol32(s, 24));
    }
}

void HELPER(vsm4r_vv)(void *vd, void *vs2, CPURISCVState *env, uint32_t desc)
{
    const uint32_t egs = 4;
    uint32_t group_start = env->vstart / egs;
    uint32_t group_end = env->vl / egs;
    uint32_t esz = sizeof(uint32_t);
    uint32_t total_elems = vext_get_total_elems(env, desc, esz);

    VSTART_CHECK_EARLY_EXIT(env, env->vl);

    for (uint32_t i = group_start; i < group_end; ++i) {
        uint32_t vstart = i * egs;
        uint32_t vend = (i + 1) * egs;
        uint32_t rk[4] = {0};
        uint32_t tmp[8] = {0};

        for (uint32_t j = vstart; j < vend; ++j) {
            rk[j - vstart] = *((uint32_t *)vs2 + H4(j));
        }

        for (uint32_t j = vstart; j < vend; ++j) {
            tmp[j - vstart] = *((uint32_t *)vd + H4(j));
        }

        do_sm4_round(rk, tmp);

        for (uint32_t j = vstart; j < vend; ++j) {
            *((uint32_t *)vd + H4(j)) = tmp[egs + (j - vstart)];
        }
    }

    env->vstart = 0;
    /* set tail elements to 1s */
    vext_set_elems_1s(vd, vext_vta(desc), env->vl * esz, total_elems * esz);
}

void HELPER(vsm4r_vs)(void *vd, void *vs2, CPURISCVState *env, uint32_t desc)
{
    const uint32_t egs = 4;
    uint32_t group_start = env->vstart / egs;
    uint32_t group_end = env->vl / egs;
    uint32_t esz = sizeof(uint32_t);
    uint32_t total_elems = vext_get_total_elems(env, desc, esz);

    VSTART_CHECK_EARLY_EXIT(env, env->vl);

    for (uint32_t i = group_start; i < group_end; ++i) {
        uint32_t vstart = i * egs;
        uint32_t vend = (i + 1) * egs;
        uint32_t rk[4] = {0};
        uint32_t tmp[8] = {0};

        for (uint32_t j = 0; j < egs; ++j) {
            rk[j] = *((uint32_t *)vs2 + H4(j));
        }

        for (uint32_t j = vstart; j < vend; ++j) {
            tmp[j - vstart] = *((uint32_t *)vd + H4(j));
        }

        do_sm4_round(rk, tmp);

        for (uint32_t j = vstart; j < vend; ++j) {
            *((uint32_t *)vd + H4(j)) = tmp[egs + (j - vstart)];
        }
    }

    env->vstart = 0;
    /* set tail elements to 1s */
    vext_set_elems_1s(vd, vext_vta(desc), env->vl * esz, total_elems * esz);
}
