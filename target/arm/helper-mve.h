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

DEF_HELPER_FLAGS_4(mve_vqdmulhb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqdmulhh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqdmulhw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vqrdmulhb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqrdmulhh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqrdmulhw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vqaddsb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqaddsh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqaddsw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vqaddub, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqadduh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqadduw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vqsubsb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqsubsh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqsubsw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vqsubub, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqsubuh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqsubuw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vshlsb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vshlsh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vshlsw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vshlub, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vshluh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vshluw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vrshlsb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vrshlsh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vrshlsw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vrshlub, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vrshluh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vrshluw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vqshlsb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqshlsh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqshlsw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vqshlub, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqshluh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqshluw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vqrshlsb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqrshlsh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqrshlsw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vqrshlub, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqrshluh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqrshluw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vqdmladhb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqdmladhh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqdmladhw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vqdmladhxb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqdmladhxh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqdmladhxw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vqrdmladhb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqrdmladhh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqrdmladhw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vqrdmladhxb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqrdmladhxh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqrdmladhxw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vqdmlsdhb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqdmlsdhh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqdmlsdhw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vqdmlsdhxb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqdmlsdhxh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqdmlsdhxw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vqrdmlsdhb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqrdmlsdhh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqrdmlsdhw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vqrdmlsdhxb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqrdmlsdhxh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqrdmlsdhxw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vqdmullbh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqdmullbw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqdmullth, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vqdmulltw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vrhaddsb, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vrhaddsh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vrhaddsw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vrhaddub, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vrhadduh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vrhadduw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vadc, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vadci, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vsbc, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vsbci, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vcadd90b, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vcadd90h, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vcadd90w, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vcadd270b, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vcadd270h, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vcadd270w, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vhcadd90b, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vhcadd90h, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vhcadd90w, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vhcadd270b, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vhcadd270h, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vhcadd270w, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vadd_scalarb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vadd_scalarh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vadd_scalarw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vsub_scalarb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vsub_scalarh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vsub_scalarw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vmul_scalarb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vmul_scalarh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vmul_scalarw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vhadds_scalarb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vhadds_scalarh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vhadds_scalarw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vhaddu_scalarb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vhaddu_scalarh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vhaddu_scalarw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vhsubs_scalarb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vhsubs_scalarh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vhsubs_scalarw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vhsubu_scalarb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vhsubu_scalarh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vhsubu_scalarw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vqadds_scalarb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqadds_scalarh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqadds_scalarw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vqaddu_scalarb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqaddu_scalarh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqaddu_scalarw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vqsubs_scalarb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqsubs_scalarh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqsubs_scalarw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vqsubu_scalarb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqsubu_scalarh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqsubu_scalarw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vqdmulh_scalarb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqdmulh_scalarh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqdmulh_scalarw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vqrdmulh_scalarb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqrdmulh_scalarh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqrdmulh_scalarw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vbrsrb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vbrsrh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vbrsrw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vqdmullb_scalarh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqdmullb_scalarw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqdmullt_scalarh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqdmullt_scalarw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

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

DEF_HELPER_FLAGS_3(mve_vaddvsb, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vaddvub, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vaddvsh, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vaddvuh, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vaddvsw, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vaddvuw, TCG_CALL_NO_WG, i32, env, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vaddlv_s, TCG_CALL_NO_WG, i64, env, ptr, i64)
DEF_HELPER_FLAGS_3(mve_vaddlv_u, TCG_CALL_NO_WG, i64, env, ptr, i64)

DEF_HELPER_FLAGS_3(mve_vmovi, TCG_CALL_NO_WG, void, env, ptr, i64)
DEF_HELPER_FLAGS_3(mve_vandi, TCG_CALL_NO_WG, void, env, ptr, i64)
DEF_HELPER_FLAGS_3(mve_vorri, TCG_CALL_NO_WG, void, env, ptr, i64)

DEF_HELPER_FLAGS_4(mve_vshli_sb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vshli_sh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vshli_sw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vshli_ub, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vshli_uh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vshli_uw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vqshli_sb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqshli_sh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqshli_sw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vqshli_ub, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqshli_uh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqshli_uw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vqshlui_sb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqshlui_sh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqshlui_sw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vrshli_sb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vrshli_sh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vrshli_sw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vrshli_ub, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vrshli_uh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vrshli_uw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vshllbsb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vshllbsh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vshllbub, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vshllbuh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vshlltsb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vshlltsh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vshlltub, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vshlltuh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vsrib, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vsrih, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vsriw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vslib, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vslih, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vsliw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vshrnbb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vshrnbh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vshrntb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vshrnth, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vrshrnbb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vrshrnbh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vrshrntb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vrshrnth, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vqshrnb_sb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqshrnb_sh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqshrnt_sb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqshrnt_sh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vqshrnb_ub, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqshrnb_uh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqshrnt_ub, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqshrnt_uh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vqshrunbb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqshrunbh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqshruntb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqshrunth, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vqrshrnb_sb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqrshrnb_sh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqrshrnt_sb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqrshrnt_sh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vqrshrnb_ub, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqrshrnb_uh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqrshrnt_ub, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqrshrnt_uh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vqrshrunbb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqrshrunbh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqrshruntb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqrshrunth, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vshlc, TCG_CALL_NO_WG, i32, env, ptr, i32, i32)

DEF_HELPER_FLAGS_3(mve_sshrl, TCG_CALL_NO_RWG, i64, env, i64, i32)
DEF_HELPER_FLAGS_3(mve_ushll, TCG_CALL_NO_RWG, i64, env, i64, i32)
DEF_HELPER_FLAGS_3(mve_sqshll, TCG_CALL_NO_RWG, i64, env, i64, i32)
DEF_HELPER_FLAGS_3(mve_uqshll, TCG_CALL_NO_RWG, i64, env, i64, i32)
DEF_HELPER_FLAGS_3(mve_sqrshrl, TCG_CALL_NO_RWG, i64, env, i64, i32)
DEF_HELPER_FLAGS_3(mve_uqrshll, TCG_CALL_NO_RWG, i64, env, i64, i32)
DEF_HELPER_FLAGS_3(mve_sqrshrl48, TCG_CALL_NO_RWG, i64, env, i64, i32)
DEF_HELPER_FLAGS_3(mve_uqrshll48, TCG_CALL_NO_RWG, i64, env, i64, i32)

DEF_HELPER_FLAGS_3(mve_uqshl, TCG_CALL_NO_RWG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_sqshl, TCG_CALL_NO_RWG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_uqrshl, TCG_CALL_NO_RWG, i32, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_sqrshr, TCG_CALL_NO_RWG, i32, env, i32, i32)
