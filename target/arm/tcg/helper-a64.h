/*
 *  AArch64 specific helper definitions
 *
 *  Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
DEF_HELPER_FLAGS_1(rbit64, TCG_CALL_NO_RWG_SE, i64, i64)
DEF_HELPER_2(msr_i_spsel, void, env, i32)
DEF_HELPER_2(msr_i_daifset, void, env, i32)
DEF_HELPER_2(msr_i_daifclear, void, env, i32)
DEF_HELPER_1(msr_set_allint_el1, void, env)
DEF_HELPER_3(vfp_cmph_a64, i64, f16, f16, ptr)
DEF_HELPER_3(vfp_cmpeh_a64, i64, f16, f16, ptr)
DEF_HELPER_3(vfp_cmps_a64, i64, f32, f32, ptr)
DEF_HELPER_3(vfp_cmpes_a64, i64, f32, f32, ptr)
DEF_HELPER_3(vfp_cmpd_a64, i64, f64, f64, ptr)
DEF_HELPER_3(vfp_cmped_a64, i64, f64, f64, ptr)
DEF_HELPER_FLAGS_4(simd_tblx, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(vfp_mulxs, TCG_CALL_NO_RWG, f32, f32, f32, ptr)
DEF_HELPER_FLAGS_3(vfp_mulxd, TCG_CALL_NO_RWG, f64, f64, f64, ptr)
DEF_HELPER_FLAGS_3(neon_ceq_f64, TCG_CALL_NO_RWG, i64, i64, i64, ptr)
DEF_HELPER_FLAGS_3(neon_cge_f64, TCG_CALL_NO_RWG, i64, i64, i64, ptr)
DEF_HELPER_FLAGS_3(neon_cgt_f64, TCG_CALL_NO_RWG, i64, i64, i64, ptr)
DEF_HELPER_FLAGS_3(recpsf_f16, TCG_CALL_NO_RWG, f16, f16, f16, ptr)
DEF_HELPER_FLAGS_3(recpsf_f32, TCG_CALL_NO_RWG, f32, f32, f32, ptr)
DEF_HELPER_FLAGS_3(recpsf_f64, TCG_CALL_NO_RWG, f64, f64, f64, ptr)
DEF_HELPER_FLAGS_3(rsqrtsf_f16, TCG_CALL_NO_RWG, f16, f16, f16, ptr)
DEF_HELPER_FLAGS_3(rsqrtsf_f32, TCG_CALL_NO_RWG, f32, f32, f32, ptr)
DEF_HELPER_FLAGS_3(rsqrtsf_f64, TCG_CALL_NO_RWG, f64, f64, f64, ptr)
DEF_HELPER_FLAGS_1(neon_addlp_s8, TCG_CALL_NO_RWG_SE, i64, i64)
DEF_HELPER_FLAGS_1(neon_addlp_u8, TCG_CALL_NO_RWG_SE, i64, i64)
DEF_HELPER_FLAGS_1(neon_addlp_s16, TCG_CALL_NO_RWG_SE, i64, i64)
DEF_HELPER_FLAGS_1(neon_addlp_u16, TCG_CALL_NO_RWG_SE, i64, i64)
DEF_HELPER_FLAGS_2(frecpx_f64, TCG_CALL_NO_RWG, f64, f64, ptr)
DEF_HELPER_FLAGS_2(frecpx_f32, TCG_CALL_NO_RWG, f32, f32, ptr)
DEF_HELPER_FLAGS_2(frecpx_f16, TCG_CALL_NO_RWG, f16, f16, ptr)
DEF_HELPER_FLAGS_2(fcvtx_f64_to_f32, TCG_CALL_NO_RWG, f32, f64, env)
DEF_HELPER_FLAGS_3(crc32_64, TCG_CALL_NO_RWG_SE, i64, i64, i64, i32)
DEF_HELPER_FLAGS_3(crc32c_64, TCG_CALL_NO_RWG_SE, i64, i64, i64, i32)
DEF_HELPER_FLAGS_3(advsimd_maxh, TCG_CALL_NO_RWG, f16, f16, f16, ptr)
DEF_HELPER_FLAGS_3(advsimd_minh, TCG_CALL_NO_RWG, f16, f16, f16, ptr)
DEF_HELPER_FLAGS_3(advsimd_maxnumh, TCG_CALL_NO_RWG, f16, f16, f16, ptr)
DEF_HELPER_FLAGS_3(advsimd_minnumh, TCG_CALL_NO_RWG, f16, f16, f16, ptr)
DEF_HELPER_3(advsimd_addh, f16, f16, f16, ptr)
DEF_HELPER_3(advsimd_subh, f16, f16, f16, ptr)
DEF_HELPER_3(advsimd_mulh, f16, f16, f16, ptr)
DEF_HELPER_3(advsimd_divh, f16, f16, f16, ptr)
DEF_HELPER_3(advsimd_ceq_f16, i32, f16, f16, ptr)
DEF_HELPER_3(advsimd_cge_f16, i32, f16, f16, ptr)
DEF_HELPER_3(advsimd_cgt_f16, i32, f16, f16, ptr)
DEF_HELPER_3(advsimd_acge_f16, i32, f16, f16, ptr)
DEF_HELPER_3(advsimd_acgt_f16, i32, f16, f16, ptr)
DEF_HELPER_3(advsimd_mulxh, f16, f16, f16, ptr)
DEF_HELPER_4(advsimd_muladdh, f16, f16, f16, f16, ptr)
DEF_HELPER_3(advsimd_add2h, i32, i32, i32, ptr)
DEF_HELPER_3(advsimd_sub2h, i32, i32, i32, ptr)
DEF_HELPER_3(advsimd_mul2h, i32, i32, i32, ptr)
DEF_HELPER_3(advsimd_div2h, i32, i32, i32, ptr)
DEF_HELPER_3(advsimd_max2h, i32, i32, i32, ptr)
DEF_HELPER_3(advsimd_min2h, i32, i32, i32, ptr)
DEF_HELPER_3(advsimd_maxnum2h, i32, i32, i32, ptr)
DEF_HELPER_3(advsimd_minnum2h, i32, i32, i32, ptr)
DEF_HELPER_3(advsimd_mulx2h, i32, i32, i32, ptr)
DEF_HELPER_4(advsimd_muladd2h, i32, i32, i32, i32, ptr)
DEF_HELPER_2(advsimd_rinth_exact, f16, f16, ptr)
DEF_HELPER_2(advsimd_rinth, f16, f16, ptr)
DEF_HELPER_2(advsimd_f16tosinth, i32, f16, ptr)
DEF_HELPER_2(advsimd_f16touinth, i32, f16, ptr)
DEF_HELPER_2(sqrt_f16, f16, f16, ptr)

DEF_HELPER_2(exception_return, void, env, i64)
DEF_HELPER_FLAGS_2(dc_zva, TCG_CALL_NO_WG, void, env, i64)

DEF_HELPER_FLAGS_3(pacia, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(pacib, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(pacda, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(pacdb, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(pacga, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(autia, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(autia_combined, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(autib, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(autib_combined, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(autda, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(autda_combined, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(autdb, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(autdb_combined, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_2(xpaci, TCG_CALL_NO_RWG_SE, i64, env, i64)
DEF_HELPER_FLAGS_2(xpacd, TCG_CALL_NO_RWG_SE, i64, env, i64)

DEF_HELPER_FLAGS_3(mte_check, TCG_CALL_NO_WG, i64, env, i32, i64)
DEF_HELPER_FLAGS_3(mte_check_zva, TCG_CALL_NO_WG, i64, env, i32, i64)
DEF_HELPER_FLAGS_3(irg, TCG_CALL_NO_RWG, i64, env, i64, i64)
DEF_HELPER_FLAGS_4(addsubg, TCG_CALL_NO_RWG_SE, i64, env, i64, s32, i32)
DEF_HELPER_FLAGS_3(ldg, TCG_CALL_NO_WG, i64, env, i64, i64)
DEF_HELPER_FLAGS_3(stg, TCG_CALL_NO_WG, void, env, i64, i64)
DEF_HELPER_FLAGS_3(stg_parallel, TCG_CALL_NO_WG, void, env, i64, i64)
DEF_HELPER_FLAGS_2(stg_stub, TCG_CALL_NO_WG, void, env, i64)
DEF_HELPER_FLAGS_3(st2g, TCG_CALL_NO_WG, void, env, i64, i64)
DEF_HELPER_FLAGS_3(st2g_parallel, TCG_CALL_NO_WG, void, env, i64, i64)
DEF_HELPER_FLAGS_2(st2g_stub, TCG_CALL_NO_WG, void, env, i64)
DEF_HELPER_FLAGS_2(ldgm, TCG_CALL_NO_WG, i64, env, i64)
DEF_HELPER_FLAGS_3(stgm, TCG_CALL_NO_WG, void, env, i64, i64)
DEF_HELPER_FLAGS_3(stzgm_tags, TCG_CALL_NO_WG, void, env, i64, i64)

DEF_HELPER_FLAGS_4(unaligned_access, TCG_CALL_NO_WG,
                   noreturn, env, i64, i32, i32)

DEF_HELPER_3(setp, void, env, i32, i32)
DEF_HELPER_3(setm, void, env, i32, i32)
DEF_HELPER_3(sete, void, env, i32, i32)
DEF_HELPER_3(setgp, void, env, i32, i32)
DEF_HELPER_3(setgm, void, env, i32, i32)
DEF_HELPER_3(setge, void, env, i32, i32)

DEF_HELPER_4(cpyp, void, env, i32, i32, i32)
DEF_HELPER_4(cpym, void, env, i32, i32, i32)
DEF_HELPER_4(cpye, void, env, i32, i32, i32)
DEF_HELPER_4(cpyfp, void, env, i32, i32, i32)
DEF_HELPER_4(cpyfm, void, env, i32, i32, i32)
DEF_HELPER_4(cpyfe, void, env, i32, i32, i32)

DEF_HELPER_FLAGS_1(guarded_page_check, TCG_CALL_NO_WG, void, env)
DEF_HELPER_FLAGS_2(guarded_page_br, TCG_CALL_NO_RWG, void, env, tl)

DEF_HELPER_FLAGS_5(gvec_fdiv_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fdiv_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fdiv_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_fmulx_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fmulx_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fmulx_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_fmulx_idx_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fmulx_idx_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_fmulx_idx_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
