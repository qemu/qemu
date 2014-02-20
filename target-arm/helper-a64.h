/*
 *  AArch64 specific helper definitions
 *
 *  Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
DEF_HELPER_FLAGS_2(udiv64, TCG_CALL_NO_RWG_SE, i64, i64, i64)
DEF_HELPER_FLAGS_2(sdiv64, TCG_CALL_NO_RWG_SE, s64, s64, s64)
DEF_HELPER_FLAGS_1(clz64, TCG_CALL_NO_RWG_SE, i64, i64)
DEF_HELPER_FLAGS_1(cls64, TCG_CALL_NO_RWG_SE, i64, i64)
DEF_HELPER_FLAGS_1(cls32, TCG_CALL_NO_RWG_SE, i32, i32)
DEF_HELPER_FLAGS_1(rbit64, TCG_CALL_NO_RWG_SE, i64, i64)
DEF_HELPER_3(vfp_cmps_a64, i64, f32, f32, ptr)
DEF_HELPER_3(vfp_cmpes_a64, i64, f32, f32, ptr)
DEF_HELPER_3(vfp_cmpd_a64, i64, f64, f64, ptr)
DEF_HELPER_3(vfp_cmped_a64, i64, f64, f64, ptr)
DEF_HELPER_FLAGS_5(simd_tbl, TCG_CALL_NO_RWG_SE, i64, env, i64, i64, i32, i32)
DEF_HELPER_FLAGS_3(vfp_mulxs, TCG_CALL_NO_RWG, f32, f32, f32, ptr)
DEF_HELPER_FLAGS_3(vfp_mulxd, TCG_CALL_NO_RWG, f64, f64, f64, ptr)
