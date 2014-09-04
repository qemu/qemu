/*
 * Copyright (C) 2010-2012 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef CONFIG_USER_ONLY
DEF_HELPER_4(cp0_set, void, env, i32, i32, i32)
DEF_HELPER_3(cp0_get, i32, env, i32, i32)
DEF_HELPER_1(cp1_putc, void, i32)
#endif

DEF_HELPER_1(clz, i32, i32)
DEF_HELPER_1(clo, i32, i32)

DEF_HELPER_2(exception, void, env, i32)

DEF_HELPER_3(asr_write, void, env, i32, i32)
DEF_HELPER_1(asr_read, i32, env)

DEF_HELPER_2(get_user_reg, i32, env, i32)
DEF_HELPER_3(set_user_reg, void, env, i32, i32)

DEF_HELPER_3(add_cc, i32, env, i32, i32)
DEF_HELPER_3(adc_cc, i32, env, i32, i32)
DEF_HELPER_3(sub_cc, i32, env, i32, i32)
DEF_HELPER_3(sbc_cc, i32, env, i32, i32)

DEF_HELPER_2(shl, i32, i32, i32)
DEF_HELPER_2(shr, i32, i32, i32)
DEF_HELPER_2(sar, i32, i32, i32)
DEF_HELPER_3(shl_cc, i32, env, i32, i32)
DEF_HELPER_3(shr_cc, i32, env, i32, i32)
DEF_HELPER_3(sar_cc, i32, env, i32, i32)
DEF_HELPER_3(ror_cc, i32, env, i32, i32)

DEF_HELPER_1(ucf64_get_fpscr, i32, env)
DEF_HELPER_2(ucf64_set_fpscr, void, env, i32)

DEF_HELPER_3(ucf64_adds, f32, f32, f32, env)
DEF_HELPER_3(ucf64_addd, f64, f64, f64, env)
DEF_HELPER_3(ucf64_subs, f32, f32, f32, env)
DEF_HELPER_3(ucf64_subd, f64, f64, f64, env)
DEF_HELPER_3(ucf64_muls, f32, f32, f32, env)
DEF_HELPER_3(ucf64_muld, f64, f64, f64, env)
DEF_HELPER_3(ucf64_divs, f32, f32, f32, env)
DEF_HELPER_3(ucf64_divd, f64, f64, f64, env)
DEF_HELPER_1(ucf64_negs, f32, f32)
DEF_HELPER_1(ucf64_negd, f64, f64)
DEF_HELPER_1(ucf64_abss, f32, f32)
DEF_HELPER_1(ucf64_absd, f64, f64)
DEF_HELPER_4(ucf64_cmps, void, f32, f32, i32, env)
DEF_HELPER_4(ucf64_cmpd, void, f64, f64, i32, env)

DEF_HELPER_2(ucf64_sf2df, f64, f32, env)
DEF_HELPER_2(ucf64_df2sf, f32, f64, env)

DEF_HELPER_2(ucf64_si2sf, f32, f32, env)
DEF_HELPER_2(ucf64_si2df, f64, f32, env)

DEF_HELPER_2(ucf64_sf2si, f32, f32, env)
DEF_HELPER_2(ucf64_df2si, f32, f64, env)
