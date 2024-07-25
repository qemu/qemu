/*
 * Copyright(c) 2022-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_GEN_TCG_SYS_H
#define HEXAGON_GEN_TCG_SYS_H

/* System mode instructions */
#define fGEN_TCG_Y2_swi(SHORTCODE) \
    gen_helper_swi(tcg_env, RsV)

#define fGEN_TCG_Y2_cswi(SHORTCODE) \
    gen_helper_cswi(tcg_env, RsV)

#define fGEN_TCG_Y2_ciad(SHORTCODE) \
    gen_helper_ciad(tcg_env, RsV)

#define fGEN_TCG_Y4_siad(SHORTCODE) \
    gen_helper_siad(tcg_env, RsV)

#define fGEN_TCG_Y2_iassignw(SHORTCODE) \
    gen_helper_iassignw(tcg_env, RsV)

#define fGEN_TCG_Y2_iassignr(SHORTCODE) \
    gen_helper_iassignr(RdV, tcg_env, RsV)

#define fGEN_TCG_Y2_getimask(SHORTCODE) \
    gen_helper_getimask(RdV, tcg_env, RsV)

#define fGEN_TCG_Y2_setimask(SHORTCODE) \
    gen_helper_setimask(tcg_env, PtV, RsV)

#define fGEN_TCG_Y2_setprio(SHORTCODE) \
    gen_helper_setprio(tcg_env, PtV, RsV)

#define fGEN_TCG_Y2_crswap0(SHORTCODE) \
    do { \
        TCGv tmp = tcg_temp_new(); \
        tcg_gen_mov_tl(tmp, RxV); \
        tcg_gen_mov_tl(RxV, hex_t_sreg[HEX_SREG_SGP0]); \
        tcg_gen_mov_tl(ctx->t_sreg_new_value[HEX_SREG_SGP0], tmp); \
    } while (0)

#define fGEN_TCG_Y4_crswap1(SHORTCODE) \
    do { \
        TCGv tmp = tcg_temp_new(); \
        tcg_gen_mov_tl(tmp, RxV); \
        tcg_gen_mov_tl(RxV, hex_t_sreg[HEX_SREG_SGP1]); \
        tcg_gen_mov_tl(ctx->t_sreg_new_value[HEX_SREG_SGP1], tmp); \
    } while (0)

#define fGEN_TCG_Y4_crswap10(SHORTCODE) \
    do { \
        g_assert_not_reached(); \
        TCGv_i64 tmp = tcg_temp_new_i64(); \
        tcg_gen_mov_i64(tmp, RxxV); \
        tcg_gen_concat_i32_i64(RxxV, \
                               hex_t_sreg[HEX_SREG_SGP0], \
                               hex_t_sreg[HEX_SREG_SGP1]); \
        tcg_gen_extrl_i64_i32(ctx->t_sreg_new_value[HEX_SREG_SGP0], tmp); \
        tcg_gen_extrh_i64_i32(ctx->t_sreg_new_value[HEX_SREG_SGP1], tmp); \
    } while (0)

#endif
