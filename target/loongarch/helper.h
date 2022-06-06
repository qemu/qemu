/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

DEF_HELPER_2(raise_exception, noreturn, env, i32)

DEF_HELPER_FLAGS_1(bitrev_w, TCG_CALL_NO_RWG_SE, tl, tl)
DEF_HELPER_FLAGS_1(bitrev_d, TCG_CALL_NO_RWG_SE, tl, tl)
DEF_HELPER_FLAGS_1(bitswap, TCG_CALL_NO_RWG_SE, tl, tl)

DEF_HELPER_FLAGS_3(asrtle_d, TCG_CALL_NO_WG, void, env, tl, tl)
DEF_HELPER_FLAGS_3(asrtgt_d, TCG_CALL_NO_WG, void, env, tl, tl)

DEF_HELPER_FLAGS_3(crc32, TCG_CALL_NO_RWG_SE, tl, tl, tl, tl)
DEF_HELPER_FLAGS_3(crc32c, TCG_CALL_NO_RWG_SE, tl, tl, tl, tl)
DEF_HELPER_FLAGS_2(cpucfg, TCG_CALL_NO_RWG_SE, tl, env, tl)

/* Floating-point helper */
DEF_HELPER_FLAGS_3(fadd_s, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(fadd_d, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(fsub_s, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(fsub_d, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(fmul_s, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(fmul_d, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(fdiv_s, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(fdiv_d, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(fmax_s, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(fmax_d, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(fmin_s, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(fmin_d, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(fmaxa_s, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(fmaxa_d, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(fmina_s, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(fmina_d, TCG_CALL_NO_WG, i64, env, i64, i64)

DEF_HELPER_FLAGS_5(fmuladd_s, TCG_CALL_NO_WG, i64, env, i64, i64, i64, i32)
DEF_HELPER_FLAGS_5(fmuladd_d, TCG_CALL_NO_WG, i64, env, i64, i64, i64, i32)

DEF_HELPER_FLAGS_3(fscaleb_s, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(fscaleb_d, TCG_CALL_NO_WG, i64, env, i64, i64)

DEF_HELPER_FLAGS_2(flogb_s, TCG_CALL_NO_WG, i64, env, i64)
DEF_HELPER_FLAGS_2(flogb_d, TCG_CALL_NO_WG, i64, env, i64)

DEF_HELPER_FLAGS_2(fsqrt_s, TCG_CALL_NO_WG, i64, env, i64)
DEF_HELPER_FLAGS_2(fsqrt_d, TCG_CALL_NO_WG, i64, env, i64)
DEF_HELPER_FLAGS_2(frsqrt_s, TCG_CALL_NO_WG, i64, env, i64)
DEF_HELPER_FLAGS_2(frsqrt_d, TCG_CALL_NO_WG, i64, env, i64)
DEF_HELPER_FLAGS_2(frecip_s, TCG_CALL_NO_WG, i64, env, i64)
DEF_HELPER_FLAGS_2(frecip_d, TCG_CALL_NO_WG, i64, env, i64)

DEF_HELPER_FLAGS_2(fclass_s, TCG_CALL_NO_RWG_SE, i64, env, i64)
DEF_HELPER_FLAGS_2(fclass_d, TCG_CALL_NO_RWG_SE, i64, env, i64)
