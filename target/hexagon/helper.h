/*
 *  Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

DEF_HELPER_2(raise_exception, noreturn, env, i32)
DEF_HELPER_1(debug_start_packet, void, env)
DEF_HELPER_2(new_value, s32, env, int)
DEF_HELPER_3(debug_check_store_width, void, env, int, int)
DEF_HELPER_1(commit_hvx_stores, void, env)
DEF_HELPER_3(debug_commit_end, void, env, int, int)
DEF_HELPER_3(sfrecipa_val, s32, env, s32, s32)
DEF_HELPER_3(sfrecipa_pred, s32, env, s32, s32)
DEF_HELPER_2(sfinvsqrta_val, s32, env, s32)
DEF_HELPER_2(sfinvsqrta_pred, s32, env, s32)

#define DEF_QEMU(TAG, SHORTCODE, HELPER, GENFN, HELPFN) HELPER
#include "qemu.odef"
#undef DEF_QEMU

DEF_HELPER_2(debug_value, void, env, s32)
DEF_HELPER_2(debug_value_i64, void, env, s64)
