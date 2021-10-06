/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
DEF_HELPER_1(debug_start_packet, void, env)
DEF_HELPER_FLAGS_3(debug_check_store_width, TCG_CALL_NO_WG, void, env, int, int)
DEF_HELPER_FLAGS_3(debug_commit_end, TCG_CALL_NO_WG, void, env, int, int)
DEF_HELPER_2(commit_store, void, env, int)
DEF_HELPER_FLAGS_4(fcircadd, TCG_CALL_NO_RWG_SE, s32, s32, s32, s32, s32)
DEF_HELPER_FLAGS_1(fbrev, TCG_CALL_NO_RWG_SE, i32, i32)
DEF_HELPER_3(sfrecipa, i64, env, f32, f32)
DEF_HELPER_2(sfinvsqrta, i64, env, f32)
DEF_HELPER_4(vacsh_val, s64, env, s64, s64, s64)
DEF_HELPER_FLAGS_4(vacsh_pred, TCG_CALL_NO_RWG_SE, s32, env, s64, s64, s64)

/* Floating point */
DEF_HELPER_2(conv_sf2df, f64, env, f32)
DEF_HELPER_2(conv_df2sf, f32, env, f64)
DEF_HELPER_2(conv_uw2sf, f32, env, s32)
DEF_HELPER_2(conv_uw2df, f64, env, s32)
DEF_HELPER_2(conv_w2sf, f32, env, s32)
DEF_HELPER_2(conv_w2df, f64, env, s32)
DEF_HELPER_2(conv_ud2sf, f32, env, s64)
DEF_HELPER_2(conv_ud2df, f64, env, s64)
DEF_HELPER_2(conv_d2sf, f32, env, s64)
DEF_HELPER_2(conv_d2df, f64, env, s64)
DEF_HELPER_2(conv_sf2uw, i32, env, f32)
DEF_HELPER_2(conv_sf2w, s32, env, f32)
DEF_HELPER_2(conv_sf2ud, i64, env, f32)
DEF_HELPER_2(conv_sf2d, s64, env, f32)
DEF_HELPER_2(conv_df2uw, i32, env, f64)
DEF_HELPER_2(conv_df2w, s32, env, f64)
DEF_HELPER_2(conv_df2ud, i64, env, f64)
DEF_HELPER_2(conv_df2d, s64, env, f64)
DEF_HELPER_2(conv_sf2uw_chop, i32, env, f32)
DEF_HELPER_2(conv_sf2w_chop, s32, env, f32)
DEF_HELPER_2(conv_sf2ud_chop, i64, env, f32)
DEF_HELPER_2(conv_sf2d_chop, s64, env, f32)
DEF_HELPER_2(conv_df2uw_chop, i32, env, f64)
DEF_HELPER_2(conv_df2w_chop, s32, env, f64)
DEF_HELPER_2(conv_df2ud_chop, i64, env, f64)
DEF_HELPER_2(conv_df2d_chop, s64, env, f64)
DEF_HELPER_3(sfadd, f32, env, f32, f32)
DEF_HELPER_3(sfsub, f32, env, f32, f32)
DEF_HELPER_3(sfcmpeq, s32, env, f32, f32)
DEF_HELPER_3(sfcmpgt, s32, env, f32, f32)
DEF_HELPER_3(sfcmpge, s32, env, f32, f32)
DEF_HELPER_3(sfcmpuo, s32, env, f32, f32)
DEF_HELPER_3(sfmax, f32, env, f32, f32)
DEF_HELPER_3(sfmin, f32, env, f32, f32)
DEF_HELPER_3(sfclass, s32, env, f32, s32)
DEF_HELPER_3(sffixupn, f32, env, f32, f32)
DEF_HELPER_3(sffixupd, f32, env, f32, f32)
DEF_HELPER_2(sffixupr, f32, env, f32)

DEF_HELPER_3(dfadd, f64, env, f64, f64)
DEF_HELPER_3(dfsub, f64, env, f64, f64)
DEF_HELPER_3(dfmax, f64, env, f64, f64)
DEF_HELPER_3(dfmin, f64, env, f64, f64)
DEF_HELPER_3(dfcmpeq, s32, env, f64, f64)
DEF_HELPER_3(dfcmpgt, s32, env, f64, f64)
DEF_HELPER_3(dfcmpge, s32, env, f64, f64)
DEF_HELPER_3(dfcmpuo, s32, env, f64, f64)
DEF_HELPER_3(dfclass, s32, env, f64, s32)

DEF_HELPER_3(sfmpy, f32, env, f32, f32)
DEF_HELPER_4(sffma, f32, env, f32, f32, f32)
DEF_HELPER_5(sffma_sc, f32, env, f32, f32, f32, f32)
DEF_HELPER_4(sffms, f32, env, f32, f32, f32)
DEF_HELPER_4(sffma_lib, f32, env, f32, f32, f32)
DEF_HELPER_4(sffms_lib, f32, env, f32, f32, f32)

DEF_HELPER_3(dfmpyfix, f64, env, f64, f64)
DEF_HELPER_4(dfmpyhh, f64, env, f64, f64, f64)

DEF_HELPER_2(probe_pkt_scalar_store_s0, void, env, int)
