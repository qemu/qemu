/*
 *  Copyright(c) 2019-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "internal.h"
#include "helper_protos_generated.h.inc"

DEF_HELPER_FLAGS_2(raise_exception, TCG_CALL_NO_RETURN, noreturn, env, i32)
DEF_HELPER_2(commit_store, void, env, int)
DEF_HELPER_3(gather_store, void, env, i32, int)
DEF_HELPER_1(commit_hvx_stores, void, env)
DEF_HELPER_FLAGS_4(fcircadd, TCG_CALL_NO_RWG_SE, s32, s32, s32, s32, s32)
DEF_HELPER_FLAGS_1(fbrev, TCG_CALL_NO_RWG_SE, i32, i32)
DEF_HELPER_4(sfrecipa, i64, env, f32, f32, i32)
DEF_HELPER_3(sfinvsqrta, i64, env, f32, i32)
DEF_HELPER_5(vacsh_val, s64, env, s64, s64, s64, i32)
DEF_HELPER_FLAGS_4(vacsh_pred, TCG_CALL_NO_RWG_SE, s32, env, s64, s64, s64)
DEF_HELPER_FLAGS_2(cabacdecbin_val, TCG_CALL_NO_RWG_SE, s64, s64, s64)
DEF_HELPER_FLAGS_2(cabacdecbin_pred, TCG_CALL_NO_RWG_SE, s32, s64, s64)

/* Floating point */
DEF_HELPER_3(conv_sf2df, f64, env, f32, i32)
DEF_HELPER_3(conv_df2sf, f32, env, f64, i32)
DEF_HELPER_3(conv_uw2sf, f32, env, s32, i32)
DEF_HELPER_3(conv_uw2df, f64, env, s32, i32)
DEF_HELPER_3(conv_w2sf, f32, env, s32, i32)
DEF_HELPER_3(conv_w2df, f64, env, s32, i32)
DEF_HELPER_3(conv_ud2sf, f32, env, s64, i32)
DEF_HELPER_3(conv_ud2df, f64, env, s64, i32)
DEF_HELPER_3(conv_d2sf, f32, env, s64, i32)
DEF_HELPER_3(conv_d2df, f64, env, s64, i32)
DEF_HELPER_3(conv_sf2uw, i32, env, f32, i32)
DEF_HELPER_3(conv_sf2w, s32, env, f32, i32)
DEF_HELPER_3(conv_sf2ud, i64, env, f32, i32)
DEF_HELPER_3(conv_sf2d, s64, env, f32, i32)
DEF_HELPER_3(conv_df2uw, i32, env, f64, i32)
DEF_HELPER_3(conv_df2w, s32, env, f64, i32)
DEF_HELPER_3(conv_df2ud, i64, env, f64, i32)
DEF_HELPER_3(conv_df2d, s64, env, f64, i32)
DEF_HELPER_3(conv_sf2uw_chop, i32, env, f32, i32)
DEF_HELPER_3(conv_sf2w_chop, s32, env, f32, i32)
DEF_HELPER_3(conv_sf2ud_chop, i64, env, f32, i32)
DEF_HELPER_3(conv_sf2d_chop, s64, env, f32, i32)
DEF_HELPER_3(conv_df2uw_chop, i32, env, f64, i32)
DEF_HELPER_3(conv_df2w_chop, s32, env, f64, i32)
DEF_HELPER_3(conv_df2ud_chop, i64, env, f64, i32)
DEF_HELPER_3(conv_df2d_chop, s64, env, f64, i32)
DEF_HELPER_4(sfadd, f32, env, f32, f32, i32)
DEF_HELPER_4(sfsub, f32, env, f32, f32, i32)
DEF_HELPER_4(sfcmpeq, s32, env, f32, f32, i32)
DEF_HELPER_4(sfcmpgt, s32, env, f32, f32, i32)
DEF_HELPER_4(sfcmpge, s32, env, f32, f32, i32)
DEF_HELPER_4(sfcmpuo, s32, env, f32, f32, i32)
DEF_HELPER_4(sfmax, f32, env, f32, f32, i32)
DEF_HELPER_4(sfmin, f32, env, f32, f32, i32)
DEF_HELPER_4(sfclass, s32, env, f32, s32, i32)
DEF_HELPER_4(sffixupn, f32, env, f32, f32, i32)
DEF_HELPER_4(sffixupd, f32, env, f32, f32, i32)
DEF_HELPER_3(sffixupr, f32, env, f32, i32)

DEF_HELPER_4(dfadd, f64, env, f64, f64, i32)
DEF_HELPER_4(dfsub, f64, env, f64, f64, i32)
DEF_HELPER_4(dfmax, f64, env, f64, f64, i32)
DEF_HELPER_4(dfmin, f64, env, f64, f64, i32)
DEF_HELPER_4(dfcmpeq, s32, env, f64, f64, i32)
DEF_HELPER_4(dfcmpgt, s32, env, f64, f64, i32)
DEF_HELPER_4(dfcmpge, s32, env, f64, f64, i32)
DEF_HELPER_4(dfcmpuo, s32, env, f64, f64, i32)
DEF_HELPER_4(dfclass, s32, env, f64, s32, i32)

DEF_HELPER_4(sfmpy, f32, env, f32, f32, i32)
DEF_HELPER_5(sffma, f32, env, f32, f32, f32, i32)
DEF_HELPER_6(sffma_sc, f32, env, f32, f32, f32, f32, i32)
DEF_HELPER_5(sffms, f32, env, f32, f32, f32, i32)
DEF_HELPER_5(sffma_lib, f32, env, f32, f32, f32, i32)
DEF_HELPER_5(sffms_lib, f32, env, f32, f32, f32, i32)

DEF_HELPER_4(dfmpyfix, f64, env, f64, f64, i32)
DEF_HELPER_5(dfmpyhh, f64, env, f64, f64, f64, i32)

/* Histogram instructions */
DEF_HELPER_1(vhist, void, env)
DEF_HELPER_1(vhistq, void, env)
DEF_HELPER_1(vwhist256, void, env)
DEF_HELPER_1(vwhist256q, void, env)
DEF_HELPER_1(vwhist256_sat, void, env)
DEF_HELPER_1(vwhist256q_sat, void, env)
DEF_HELPER_1(vwhist128, void, env)
DEF_HELPER_1(vwhist128q, void, env)
DEF_HELPER_2(vwhist128m, void, env, s32)
DEF_HELPER_2(vwhist128qm, void, env, s32)

DEF_HELPER_4(probe_noshuf_load, void, env, i32, int, int)
DEF_HELPER_2(probe_pkt_scalar_store_s0, void, env, int)
DEF_HELPER_2(probe_hvx_stores, void, env, int)
DEF_HELPER_2(probe_pkt_scalar_hvx_stores, void, env, int)
