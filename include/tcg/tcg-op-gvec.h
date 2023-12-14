/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Target dependent generic vector operation expansion
 *
 * Copyright (c) 2018 Linaro
 */

#ifndef TCG_TCG_OP_GVEC_H
#define TCG_TCG_OP_GVEC_H

#include "tcg/tcg-op-gvec-common.h"

#ifndef TARGET_LONG_BITS
#error must include QEMU headers
#endif

#if TARGET_LONG_BITS == 64
#define tcg_gen_gvec_dup_tl  tcg_gen_gvec_dup_i64
#define tcg_gen_vec_add8_tl  tcg_gen_vec_add8_i64
#define tcg_gen_vec_sub8_tl  tcg_gen_vec_sub8_i64
#define tcg_gen_vec_add16_tl tcg_gen_vec_add16_i64
#define tcg_gen_vec_sub16_tl tcg_gen_vec_sub16_i64
#define tcg_gen_vec_add32_tl tcg_gen_vec_add32_i64
#define tcg_gen_vec_sub32_tl tcg_gen_vec_sub32_i64
#define tcg_gen_vec_shl8i_tl tcg_gen_vec_shl8i_i64
#define tcg_gen_vec_shr8i_tl tcg_gen_vec_shr8i_i64
#define tcg_gen_vec_sar8i_tl tcg_gen_vec_sar8i_i64
#define tcg_gen_vec_shl16i_tl tcg_gen_vec_shl16i_i64
#define tcg_gen_vec_shr16i_tl tcg_gen_vec_shr16i_i64
#define tcg_gen_vec_sar16i_tl tcg_gen_vec_sar16i_i64
#elif TARGET_LONG_BITS == 32
#define tcg_gen_gvec_dup_tl  tcg_gen_gvec_dup_i32
#define tcg_gen_vec_add8_tl  tcg_gen_vec_add8_i32
#define tcg_gen_vec_sub8_tl  tcg_gen_vec_sub8_i32
#define tcg_gen_vec_add16_tl tcg_gen_vec_add16_i32
#define tcg_gen_vec_sub16_tl tcg_gen_vec_sub16_i32
#define tcg_gen_vec_add32_tl tcg_gen_add_i32
#define tcg_gen_vec_sub32_tl tcg_gen_sub_i32
#define tcg_gen_vec_shl8i_tl tcg_gen_vec_shl8i_i32
#define tcg_gen_vec_shr8i_tl tcg_gen_vec_shr8i_i32
#define tcg_gen_vec_sar8i_tl tcg_gen_vec_sar8i_i32
#define tcg_gen_vec_shl16i_tl tcg_gen_vec_shl16i_i32
#define tcg_gen_vec_shr16i_tl tcg_gen_vec_shr16i_i32
#define tcg_gen_vec_sar16i_tl tcg_gen_vec_sar16i_i32
#else
# error
#endif

#endif
