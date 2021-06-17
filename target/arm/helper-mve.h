/*
 *  M-profile MVE specific helper definitions
 *
 *  Copyright (c) 2021 Linaro, Ltd.
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
DEF_HELPER_FLAGS_3(mve_vldrb, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vldrh, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vldrw, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vstrb, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vstrh, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vstrw, TCG_CALL_NO_WG, void, env, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vldrb_sh, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vldrb_sw, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vldrb_uh, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vldrb_uw, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vldrh_sw, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vldrh_uw, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vstrb_h, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vstrb_w, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vstrh_w, TCG_CALL_NO_WG, void, env, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vdup, TCG_CALL_NO_WG, void, env, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vclsb, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vclsh, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vclsw, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vclzb, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vclzh, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vclzw, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vrev16b, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vrev32b, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vrev32h, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vrev64b, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vrev64h, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vrev64w, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vmvn, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vabsb, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vabsh, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vabsw, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vfabsh, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vfabss, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vnegb, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vnegh, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vnegw, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vfnegh, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vfnegs, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vand, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vbic, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vorr, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vorn, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_veor, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vaddb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vaddh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vaddw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vsubb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vsubh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vsubw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vmulb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmulh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmulw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vmulhsb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmulhsh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmulhsw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmulhub, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmulhuh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmulhuw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vrmulhsb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vrmulhsh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vrmulhsw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vrmulhub, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vrmulhuh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vrmulhuw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vmaxsb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmaxsh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmaxsw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmaxub, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmaxuh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmaxuw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vminsb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vminsh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vminsw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vminub, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vminuh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vminuw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vabdsb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vabdsh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vabdsw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vabdub, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vabduh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vabduw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vhaddsb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vhaddsh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vhaddsw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vhaddub, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vhadduh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vhadduw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vhsubsb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vhsubsh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vhsubsw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vhsubub, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vhsubuh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vhsubuw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vmullbsb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmullbsh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmullbsw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmullbub, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmullbuh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmullbuw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vmulltsb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmulltsh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmulltsw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmulltub, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmulltuh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmulltuw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vadd_scalarb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vadd_scalarh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vadd_scalarw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vsub_scalarb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vsub_scalarh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vsub_scalarw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vmul_scalarb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vmul_scalarh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vmul_scalarw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vmlaldavsh, TCG_CALL_NO_WG, i64, env, ptr, ptr, i64)
DEF_HELPER_FLAGS_4(mve_vmlaldavsw, TCG_CALL_NO_WG, i64, env, ptr, ptr, i64)
DEF_HELPER_FLAGS_4(mve_vmlaldavxsh, TCG_CALL_NO_WG, i64, env, ptr, ptr, i64)
DEF_HELPER_FLAGS_4(mve_vmlaldavxsw, TCG_CALL_NO_WG, i64, env, ptr, ptr, i64)

DEF_HELPER_FLAGS_4(mve_vmlaldavuh, TCG_CALL_NO_WG, i64, env, ptr, ptr, i64)
DEF_HELPER_FLAGS_4(mve_vmlaldavuw, TCG_CALL_NO_WG, i64, env, ptr, ptr, i64)

DEF_HELPER_FLAGS_4(mve_vmlsldavsh, TCG_CALL_NO_WG, i64, env, ptr, ptr, i64)
DEF_HELPER_FLAGS_4(mve_vmlsldavsw, TCG_CALL_NO_WG, i64, env, ptr, ptr, i64)
DEF_HELPER_FLAGS_4(mve_vmlsldavxsh, TCG_CALL_NO_WG, i64, env, ptr, ptr, i64)
DEF_HELPER_FLAGS_4(mve_vmlsldavxsw, TCG_CALL_NO_WG, i64, env, ptr, ptr, i64)

DEF_HELPER_FLAGS_4(mve_vrmlaldavhsw, TCG_CALL_NO_WG, i64, env, ptr, ptr, i64)
DEF_HELPER_FLAGS_4(mve_vrmlaldavhxsw, TCG_CALL_NO_WG, i64, env, ptr, ptr, i64)

DEF_HELPER_FLAGS_4(mve_vrmlaldavhuw, TCG_CALL_NO_WG, i64, env, ptr, ptr, i64)

DEF_HELPER_FLAGS_4(mve_vrmlsldavhsw, TCG_CALL_NO_WG, i64, env, ptr, ptr, i64)
DEF_HELPER_FLAGS_4(mve_vrmlsldavhxsw, TCG_CALL_NO_WG, i64, env, ptr, ptr, i64)
