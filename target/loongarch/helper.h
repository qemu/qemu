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

/* fcmp.cXXX.s */
DEF_HELPER_4(fcmp_c_s, i64, env, i64, i64, i32)
/* fcmp.sXXX.s */
DEF_HELPER_4(fcmp_s_s, i64, env, i64, i64, i32)
/* fcmp.cXXX.d */
DEF_HELPER_4(fcmp_c_d, i64, env, i64, i64, i32)
/* fcmp.sXXX.d */
DEF_HELPER_4(fcmp_s_d, i64, env, i64, i64, i32)

DEF_HELPER_2(fcvt_d_s, i64, env, i64)
DEF_HELPER_2(fcvt_s_d, i64, env, i64)
DEF_HELPER_2(ffint_d_w, i64, env, i64)
DEF_HELPER_2(ffint_d_l, i64, env, i64)
DEF_HELPER_2(ffint_s_w, i64, env, i64)
DEF_HELPER_2(ffint_s_l, i64, env, i64)
DEF_HELPER_2(ftintrm_l_s, i64, env, i64)
DEF_HELPER_2(ftintrm_l_d, i64, env, i64)
DEF_HELPER_2(ftintrm_w_s, i64, env, i64)
DEF_HELPER_2(ftintrm_w_d, i64, env, i64)
DEF_HELPER_2(ftintrp_l_s, i64, env, i64)
DEF_HELPER_2(ftintrp_l_d, i64, env, i64)
DEF_HELPER_2(ftintrp_w_s, i64, env, i64)
DEF_HELPER_2(ftintrp_w_d, i64, env, i64)
DEF_HELPER_2(ftintrz_l_s, i64, env, i64)
DEF_HELPER_2(ftintrz_l_d, i64, env, i64)
DEF_HELPER_2(ftintrz_w_s, i64, env, i64)
DEF_HELPER_2(ftintrz_w_d, i64, env, i64)
DEF_HELPER_2(ftintrne_l_s, i64, env, i64)
DEF_HELPER_2(ftintrne_l_d, i64, env, i64)
DEF_HELPER_2(ftintrne_w_s, i64, env, i64)
DEF_HELPER_2(ftintrne_w_d, i64, env, i64)
DEF_HELPER_2(ftint_l_s, i64, env, i64)
DEF_HELPER_2(ftint_l_d, i64, env, i64)
DEF_HELPER_2(ftint_w_s, i64, env, i64)
DEF_HELPER_2(ftint_w_d, i64, env, i64)
DEF_HELPER_2(frint_s, i64, env, i64)
DEF_HELPER_2(frint_d, i64, env, i64)

DEF_HELPER_FLAGS_1(set_rounding_mode, TCG_CALL_NO_RWG, void, env)

DEF_HELPER_1(rdtime_d, i64, env)

#ifndef CONFIG_USER_ONLY
/* CSRs helper */
DEF_HELPER_1(csrrd_pgd, i64, env)
DEF_HELPER_1(csrrd_cpuid, i64, env)
DEF_HELPER_1(csrrd_tval, i64, env)
DEF_HELPER_2(csrwr_estat, i64, env, tl)
DEF_HELPER_2(csrwr_asid, i64, env, tl)
DEF_HELPER_2(csrwr_tcfg, i64, env, tl)
DEF_HELPER_2(csrwr_ticlr, i64, env, tl)
DEF_HELPER_2(iocsrrd_b, i64, env, tl)
DEF_HELPER_2(iocsrrd_h, i64, env, tl)
DEF_HELPER_2(iocsrrd_w, i64, env, tl)
DEF_HELPER_2(iocsrrd_d, i64, env, tl)
DEF_HELPER_3(iocsrwr_b, void, env, tl, tl)
DEF_HELPER_3(iocsrwr_h, void, env, tl, tl)
DEF_HELPER_3(iocsrwr_w, void, env, tl, tl)
DEF_HELPER_3(iocsrwr_d, void, env, tl, tl)

/* TLB helper */
DEF_HELPER_1(tlbwr, void, env)
DEF_HELPER_1(tlbfill, void, env)
DEF_HELPER_1(tlbsrch, void, env)
DEF_HELPER_1(tlbrd, void, env)
DEF_HELPER_1(tlbclr, void, env)
DEF_HELPER_1(tlbflush, void, env)
DEF_HELPER_1(invtlb_all, void, env)
DEF_HELPER_2(invtlb_all_g, void, env, i32)
DEF_HELPER_2(invtlb_all_asid, void, env, tl)
DEF_HELPER_3(invtlb_page_asid, void, env, tl, tl)
DEF_HELPER_3(invtlb_page_asid_or_g, void, env, tl, tl)

DEF_HELPER_4(lddir, tl, env, tl, tl, i32)
DEF_HELPER_4(ldpte, void, env, tl, tl, i32)
DEF_HELPER_1(ertn, void, env)
DEF_HELPER_1(idle, void, env)
#endif
