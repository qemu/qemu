/*
 *  Copyright(c) 2019-2020 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#include "helper_overrides.h"

DEF_HELPER_2(raise_exception, noreturn, env, i32)
DEF_HELPER_1(debug_start_packet, void, env)
DEF_HELPER_3(debug_check_store_width, void, env, int, int)
DEF_HELPER_1(commit_hvx_stores, void, env)
DEF_HELPER_3(debug_commit_end, void, env, int, int)
DEF_HELPER_3(sfrecipa_val, s32, env, s32, s32)
DEF_HELPER_3(sfrecipa_pred, s32, env, s32, s32)
DEF_HELPER_2(sfinvsqrta_val, s32, env, s32)
DEF_HELPER_2(sfinvsqrta_pred, s32, env, s32)
DEF_HELPER_4(vacsh_val, s64, env, s64, s64, s64)
DEF_HELPER_4(vacsh_pred, s32, env, s64, s64, s64)
DEF_HELPER_3(merge_inflight_store1s, s32, env, s32, s32)
DEF_HELPER_3(merge_inflight_store1u, s32, env, s32, s32)
DEF_HELPER_3(merge_inflight_store2s, s32, env, s32, s32)
DEF_HELPER_3(merge_inflight_store2u, s32, env, s32, s32)
DEF_HELPER_3(merge_inflight_store4s, s32, env, s32, s32)
DEF_HELPER_3(merge_inflight_store4u, s32, env, s32, s32)
DEF_HELPER_3(merge_inflight_store8u, s64, env, s32, s64)

#define DEF_QEMU(TAG, SHORTCODE, HELPER, GENFN, HELPFN) HELPER
#include "qemu_def_generated.h"
#undef DEF_QEMU

DEF_HELPER_2(debug_value, void, env, s32)
DEF_HELPER_2(debug_value_i64, void, env, s64)
