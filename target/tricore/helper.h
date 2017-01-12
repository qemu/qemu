/*
 *  Copyright (c) 2012-2014 Bastian Koppelmann C-Lab/University Paderborn
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

/* Arithmetic */
DEF_HELPER_3(add_ssov, i32, env, i32, i32)
DEF_HELPER_3(add64_ssov, i64, env, i64, i64)
DEF_HELPER_3(add_suov, i32, env, i32, i32)
DEF_HELPER_3(add_h_ssov, i32, env, i32, i32)
DEF_HELPER_3(add_h_suov, i32, env, i32, i32)
DEF_HELPER_4(addr_h_ssov, i32, env, i64, i32, i32)
DEF_HELPER_4(addsur_h_ssov, i32, env, i64, i32, i32)
DEF_HELPER_3(sub_ssov, i32, env, i32, i32)
DEF_HELPER_3(sub64_ssov, i64, env, i64, i64)
DEF_HELPER_3(sub_suov, i32, env, i32, i32)
DEF_HELPER_3(sub_h_ssov, i32, env, i32, i32)
DEF_HELPER_3(sub_h_suov, i32, env, i32, i32)
DEF_HELPER_4(subr_h_ssov, i32, env, i64, i32, i32)
DEF_HELPER_4(subadr_h_ssov, i32, env, i64, i32, i32)
DEF_HELPER_3(mul_ssov, i32, env, i32, i32)
DEF_HELPER_3(mul_suov, i32, env, i32, i32)
DEF_HELPER_3(sha_ssov, i32, env, i32, i32)
DEF_HELPER_3(absdif_ssov, i32, env, i32, i32)
DEF_HELPER_4(madd32_ssov, i32, env, i32, i32, i32)
DEF_HELPER_4(madd32_suov, i32, env, i32, i32, i32)
DEF_HELPER_4(madd64_ssov, i64, env, i32, i64, i32)
DEF_HELPER_5(madd64_q_ssov, i64, env, i64, i32, i32, i32)
DEF_HELPER_3(madd32_q_add_ssov, i32, env, i64, i64)
DEF_HELPER_5(maddr_q_ssov, i32, env, i32, i32, i32, i32)
DEF_HELPER_4(madd64_suov, i64, env, i32, i64, i32)
DEF_HELPER_4(msub32_ssov, i32, env, i32, i32, i32)
DEF_HELPER_4(msub32_suov, i32, env, i32, i32, i32)
DEF_HELPER_4(msub64_ssov, i64, env, i32, i64, i32)
DEF_HELPER_5(msub64_q_ssov, i64, env, i64, i32, i32, i32)
DEF_HELPER_3(msub32_q_sub_ssov, i32, env, i64, i64)
DEF_HELPER_5(msubr_q_ssov, i32, env, i32, i32, i32, i32)
DEF_HELPER_4(msub64_suov, i64, env, i32, i64, i32)
DEF_HELPER_3(absdif_h_ssov, i32, env, i32, i32)
DEF_HELPER_2(abs_ssov, i32, env, i32)
DEF_HELPER_2(abs_h_ssov, i32, env, i32)
/* hword/byte arithmetic */
DEF_HELPER_2(abs_b, i32, env, i32)
DEF_HELPER_2(abs_h, i32, env, i32)
DEF_HELPER_3(absdif_b, i32, env, i32, i32)
DEF_HELPER_3(absdif_h, i32, env, i32, i32)
DEF_HELPER_4(addr_h, i32, env, i64, i32, i32)
DEF_HELPER_4(addsur_h, i32, env, i64, i32, i32)
DEF_HELPER_5(maddr_q, i32, env, i32, i32, i32, i32)
DEF_HELPER_3(add_b, i32, env, i32, i32)
DEF_HELPER_3(add_h, i32, env, i32, i32)
DEF_HELPER_3(sub_b, i32, env, i32, i32)
DEF_HELPER_3(sub_h, i32, env, i32, i32)
DEF_HELPER_4(subr_h, i32, env, i64, i32, i32)
DEF_HELPER_4(subadr_h, i32, env, i64, i32, i32)
DEF_HELPER_5(msubr_q, i32, env, i32, i32, i32, i32)
DEF_HELPER_FLAGS_2(eq_b, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_FLAGS_2(eq_h, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_FLAGS_2(eqany_b, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_FLAGS_2(eqany_h, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_FLAGS_2(lt_b, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_FLAGS_2(lt_bu, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_FLAGS_2(lt_h, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_FLAGS_2(lt_hu, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_FLAGS_2(max_b, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_FLAGS_2(max_bu, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_FLAGS_2(max_h, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_FLAGS_2(max_hu, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_FLAGS_2(ixmax, TCG_CALL_NO_RWG_SE, i64, i64, i32)
DEF_HELPER_FLAGS_2(ixmax_u, TCG_CALL_NO_RWG_SE, i64, i64, i32)
DEF_HELPER_FLAGS_2(min_b, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_FLAGS_2(min_bu, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_FLAGS_2(min_h, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_FLAGS_2(min_hu, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_FLAGS_2(ixmin, TCG_CALL_NO_RWG_SE, i64, i64, i32)
DEF_HELPER_FLAGS_2(ixmin_u, TCG_CALL_NO_RWG_SE, i64, i64, i32)
/* count leading ... */
DEF_HELPER_FLAGS_1(clo_h, TCG_CALL_NO_RWG_SE, i32, i32)
DEF_HELPER_FLAGS_1(clz_h, TCG_CALL_NO_RWG_SE, i32, i32)
DEF_HELPER_FLAGS_1(cls_h, TCG_CALL_NO_RWG_SE, i32, i32)
/* sh */
DEF_HELPER_FLAGS_2(sh, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_FLAGS_2(sh_h, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_3(sha, i32, env, i32, i32)
DEF_HELPER_2(sha_h, i32, i32, i32)
/* merge/split/parity */
DEF_HELPER_FLAGS_2(bmerge, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_FLAGS_1(bsplit, TCG_CALL_NO_RWG_SE, i64, i32)
DEF_HELPER_FLAGS_1(parity, TCG_CALL_NO_RWG_SE, i32, i32)
/* float */
DEF_HELPER_FLAGS_4(pack, TCG_CALL_NO_RWG_SE, i32, i32, i32, i32, i32)
DEF_HELPER_1(unpack, i64, i32)
DEF_HELPER_3(fadd, i32, env, i32, i32)
DEF_HELPER_3(fsub, i32, env, i32, i32)
DEF_HELPER_3(fmul, i32, env, i32, i32)
DEF_HELPER_3(fdiv, i32, env, i32, i32)
DEF_HELPER_4(fmadd, i32, env, i32, i32, i32)
DEF_HELPER_4(fmsub, i32, env, i32, i32, i32)
DEF_HELPER_3(fcmp, i32, env, i32, i32)
DEF_HELPER_2(ftoi, i32, env, i32)
DEF_HELPER_2(itof, i32, env, i32)
DEF_HELPER_2(ftouz, i32, env, i32)
DEF_HELPER_2(updfl, void, env, i32)
/* dvinit */
DEF_HELPER_3(dvinit_b_13, i64, env, i32, i32)
DEF_HELPER_3(dvinit_b_131, i64, env, i32, i32)
DEF_HELPER_3(dvinit_h_13, i64, env, i32, i32)
DEF_HELPER_3(dvinit_h_131, i64, env, i32, i32)
DEF_HELPER_FLAGS_2(dvadj, TCG_CALL_NO_RWG_SE, i64, i64, i32)
DEF_HELPER_FLAGS_2(dvstep, TCG_CALL_NO_RWG_SE, i64, i64, i32)
DEF_HELPER_FLAGS_2(dvstep_u, TCG_CALL_NO_RWG_SE, i64, i64, i32)
DEF_HELPER_3(divide, i64, env, i32, i32)
DEF_HELPER_3(divide_u, i64, env, i32, i32)
/* mulh */
DEF_HELPER_FLAGS_5(mul_h, TCG_CALL_NO_RWG_SE, i64, i32, i32, i32, i32, i32)
DEF_HELPER_FLAGS_5(mulm_h, TCG_CALL_NO_RWG_SE, i64, i32, i32, i32, i32, i32)
DEF_HELPER_FLAGS_5(mulr_h, TCG_CALL_NO_RWG_SE, i32, i32, i32, i32, i32, i32)
/* crc32 */
DEF_HELPER_FLAGS_2(crc32, TCG_CALL_NO_RWG_SE, i32, i32, i32)
/* CSA */
DEF_HELPER_2(call, void, env, i32)
DEF_HELPER_1(ret, void, env)
DEF_HELPER_2(bisr, void, env, i32)
DEF_HELPER_1(rfe, void, env)
DEF_HELPER_1(rfm, void, env)
DEF_HELPER_2(ldlcx, void, env, i32)
DEF_HELPER_2(lducx, void, env, i32)
DEF_HELPER_2(stlcx, void, env, i32)
DEF_HELPER_2(stucx, void, env, i32)
DEF_HELPER_1(svlcx, void, env)
DEF_HELPER_1(svucx, void, env)
DEF_HELPER_1(rslcx, void, env)
/* Address mode helper */
DEF_HELPER_1(br_update, i32, i32)
DEF_HELPER_2(circ_update, i32, i32, i32)
/* PSW cache helper */
DEF_HELPER_2(psw_write, void, env, i32)
DEF_HELPER_1(psw_read, i32, env)
/* Exceptions */
DEF_HELPER_3(raise_exception_sync, noreturn, env, i32, i32)
