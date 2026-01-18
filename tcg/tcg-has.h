/* SPDX-License-Identifier: MIT */
/*
 * Define target-specific opcode support
 * Copyright (c) 2024 Linaro, Ltd.
 */

#ifndef TCG_HAS_H
#define TCG_HAS_H

#include "tcg-target-has.h"

#if !defined(TCG_TARGET_HAS_v64) \
    && !defined(TCG_TARGET_HAS_v128) \
    && !defined(TCG_TARGET_HAS_v256)
#define TCG_TARGET_MAYBE_vec            0
#define TCG_TARGET_HAS_abs_vec          0
#define TCG_TARGET_HAS_neg_vec          0
#define TCG_TARGET_HAS_not_vec          0
#define TCG_TARGET_HAS_andc_vec         0
#define TCG_TARGET_HAS_orc_vec          0
#define TCG_TARGET_HAS_nand_vec         0
#define TCG_TARGET_HAS_nor_vec          0
#define TCG_TARGET_HAS_eqv_vec          0
#define TCG_TARGET_HAS_roti_vec         0
#define TCG_TARGET_HAS_rots_vec         0
#define TCG_TARGET_HAS_rotv_vec         0
#define TCG_TARGET_HAS_shi_vec          0
#define TCG_TARGET_HAS_shs_vec          0
#define TCG_TARGET_HAS_shv_vec          0
#define TCG_TARGET_HAS_mul_vec          0
#define TCG_TARGET_HAS_sat_vec          0
#define TCG_TARGET_HAS_minmax_vec       0
#define TCG_TARGET_HAS_bitsel_vec       0
#define TCG_TARGET_HAS_cmpsel_vec       0
#define TCG_TARGET_HAS_tst_vec          0
#else
#define TCG_TARGET_MAYBE_vec            1
#endif
#ifndef TCG_TARGET_HAS_v64
#define TCG_TARGET_HAS_v64              0
#endif
#ifndef TCG_TARGET_HAS_v128
#define TCG_TARGET_HAS_v128             0
#endif
#ifndef TCG_TARGET_HAS_v256
#define TCG_TARGET_HAS_v256             0
#endif

#endif
