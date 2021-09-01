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

DEF_HELPER_FLAGS_4(mve_vldrb_sg_sh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vldrb_sg_sw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vldrh_sg_sw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vldrb_sg_ub, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vldrb_sg_uh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vldrb_sg_uw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vldrh_sg_uh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vldrh_sg_uw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vldrw_sg_uw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vldrd_sg_ud, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vstrb_sg_ub, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vstrb_sg_uh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vstrb_sg_uw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vstrh_sg_uh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vstrh_sg_uw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vstrw_sg_uw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vstrd_sg_ud, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vldrh_sg_os_sw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vldrh_sg_os_uh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vldrh_sg_os_uw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vldrw_sg_os_uw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vldrd_sg_os_ud, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vstrh_sg_os_uh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vstrh_sg_os_uw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vstrw_sg_os_uw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vstrd_sg_os_ud, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vldrw_sg_wb_uw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vldrd_sg_wb_ud, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vstrw_sg_wb_uw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vstrd_sg_wb_ud, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vld20b, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_vld20h, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_vld20w, TCG_CALL_NO_WG, void, env, i32, i32)

DEF_HELPER_FLAGS_3(mve_vld21b, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_vld21h, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_vld21w, TCG_CALL_NO_WG, void, env, i32, i32)

DEF_HELPER_FLAGS_3(mve_vld40b, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_vld40h, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_vld40w, TCG_CALL_NO_WG, void, env, i32, i32)

DEF_HELPER_FLAGS_3(mve_vld41b, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_vld41h, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_vld41w, TCG_CALL_NO_WG, void, env, i32, i32)

DEF_HELPER_FLAGS_3(mve_vld42b, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_vld42h, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_vld42w, TCG_CALL_NO_WG, void, env, i32, i32)

DEF_HELPER_FLAGS_3(mve_vld43b, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_vld43h, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_vld43w, TCG_CALL_NO_WG, void, env, i32, i32)

DEF_HELPER_FLAGS_3(mve_vst20b, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_vst20h, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_vst20w, TCG_CALL_NO_WG, void, env, i32, i32)

DEF_HELPER_FLAGS_3(mve_vst21b, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_vst21h, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_vst21w, TCG_CALL_NO_WG, void, env, i32, i32)

DEF_HELPER_FLAGS_3(mve_vst40b, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_vst40h, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_vst40w, TCG_CALL_NO_WG, void, env, i32, i32)

DEF_HELPER_FLAGS_3(mve_vst41b, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_vst41h, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_vst41w, TCG_CALL_NO_WG, void, env, i32, i32)

DEF_HELPER_FLAGS_3(mve_vst42b, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_vst42h, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_vst42w, TCG_CALL_NO_WG, void, env, i32, i32)

DEF_HELPER_FLAGS_3(mve_vst43b, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_vst43h, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_FLAGS_3(mve_vst43w, TCG_CALL_NO_WG, void, env, i32, i32)

DEF_HELPER_FLAGS_3(mve_vdup, TCG_CALL_NO_WG, void, env, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vidupb, TCG_CALL_NO_WG, i32, env, ptr, i32, i32)
DEF_HELPER_FLAGS_4(mve_viduph, TCG_CALL_NO_WG, i32, env, ptr, i32, i32)
DEF_HELPER_FLAGS_4(mve_vidupw, TCG_CALL_NO_WG, i32, env, ptr, i32, i32)

DEF_HELPER_FLAGS_5(mve_viwdupb, TCG_CALL_NO_WG, i32, env, ptr, i32, i32, i32)
DEF_HELPER_FLAGS_5(mve_viwduph, TCG_CALL_NO_WG, i32, env, ptr, i32, i32, i32)
DEF_HELPER_FLAGS_5(mve_viwdupw, TCG_CALL_NO_WG, i32, env, ptr, i32, i32, i32)

DEF_HELPER_FLAGS_5(mve_vdwdupb, TCG_CALL_NO_WG, i32, env, ptr, i32, i32, i32)
DEF_HELPER_FLAGS_5(mve_vdwduph, TCG_CALL_NO_WG, i32, env, ptr, i32, i32, i32)
DEF_HELPER_FLAGS_5(mve_vdwdupw, TCG_CALL_NO_WG, i32, env, ptr, i32, i32, i32)

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

DEF_HELPER_FLAGS_3(mve_vqabsb, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vqabsh, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vqabsw, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vqnegb, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vqnegh, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vqnegw, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vmaxab, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vmaxah, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vmaxaw, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vminab, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vminah, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vminaw, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vcvt_rm_sh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vcvt_rm_uh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vcvt_rm_ss, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vcvt_rm_us, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vcvtb_sh, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vcvtt_sh, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vcvtb_hs, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vcvtt_hs, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vmovnbb, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vmovnbh, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vmovntb, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vmovnth, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vqmovunbb, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vqmovunbh, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vqmovuntb, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vqmovunth, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vqmovnbsb, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vqmovnbsh, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vqmovntsb, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vqmovntsh, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vqmovnbub, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vqmovnbuh, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vqmovntub, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vqmovntuh, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vand, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vbic, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vorr, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vorn, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_veor, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vpsel, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_1(mve_vpnot, TCG_CALL_NO_WG, void, env)

DEF_HELPER_FLAGS_2(mve_vctp, TCG_CALL_NO_WG, void, env, i32)

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

DEF_HELPER_FLAGS_4(mve_vmullpbh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmullpth, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmullpbw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmullptw, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

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

DEF_HELPER_FLAGS_4(mve_vfaddh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vfadds, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vfsubh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vfsubs, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vfmulh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vfmuls, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vfabdh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vfabds, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vmaxnmh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmaxnms, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vminnmh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vminnms, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vmaxnmah, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vmaxnmas, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vminnmah, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vminnmas, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vfcadd90h, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vfcadd90s, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vfcadd270h, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vfcadd270s, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vfmah, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vfmas, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vfmsh, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vfmss, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vcmul0h, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vcmul0s, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vcmul90h, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vcmul90s, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vcmul180h, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vcmul180s, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vcmul270h, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vcmul270s, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

DEF_HELPER_FLAGS_4(mve_vcmla0h, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vcmla0s, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vcmla90h, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vcmla90s, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vcmla180h, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vcmla180s, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vcmla270h, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)
DEF_HELPER_FLAGS_4(mve_vcmla270s, TCG_CALL_NO_WG, void, env, ptr, ptr, ptr)

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

DEF_HELPER_FLAGS_4(mve_vmlab, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vmlah, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vmlaw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vmlasb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vmlash, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vmlasw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vqdmlahb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqdmlahh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqdmlahw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vqrdmlahb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqrdmlahh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqrdmlahw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vqdmlashb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqdmlashh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqdmlashw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vqrdmlashb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqrdmlashh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqrdmlashw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

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

DEF_HELPER_FLAGS_4(mve_vmladavsb, TCG_CALL_NO_WG, i32, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vmladavsh, TCG_CALL_NO_WG, i32, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vmladavsw, TCG_CALL_NO_WG, i32, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vmladavub, TCG_CALL_NO_WG, i32, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vmladavuh, TCG_CALL_NO_WG, i32, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vmladavuw, TCG_CALL_NO_WG, i32, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vmlsdavb, TCG_CALL_NO_WG, i32, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vmlsdavh, TCG_CALL_NO_WG, i32, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vmlsdavw, TCG_CALL_NO_WG, i32, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vmladavsxb, TCG_CALL_NO_WG, i32, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vmladavsxh, TCG_CALL_NO_WG, i32, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vmladavsxw, TCG_CALL_NO_WG, i32, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vmlsdavxb, TCG_CALL_NO_WG, i32, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vmlsdavxh, TCG_CALL_NO_WG, i32, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vmlsdavxw, TCG_CALL_NO_WG, i32, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vaddvsb, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vaddvub, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vaddvsh, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vaddvuh, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vaddvsw, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vaddvuw, TCG_CALL_NO_WG, i32, env, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vmaxvsb, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vmaxvsh, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vmaxvsw, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vmaxvub, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vmaxvuh, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vmaxvuw, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vmaxavb, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vmaxavh, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vmaxavw, TCG_CALL_NO_WG, i32, env, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vminvsb, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vminvsh, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vminvsw, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vminvub, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vminvuh, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vminvuw, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vminavb, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vminavh, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vminavw, TCG_CALL_NO_WG, i32, env, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vmaxnmvh, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vmaxnmvs, TCG_CALL_NO_WG, i32, env, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vminnmvh, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vminnmvs, TCG_CALL_NO_WG, i32, env, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vmaxnmavh, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vmaxnmavs, TCG_CALL_NO_WG, i32, env, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vminnmavh, TCG_CALL_NO_WG, i32, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vminnmavs, TCG_CALL_NO_WG, i32, env, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vaddlv_s, TCG_CALL_NO_WG, i64, env, ptr, i64)
DEF_HELPER_FLAGS_3(mve_vaddlv_u, TCG_CALL_NO_WG, i64, env, ptr, i64)

DEF_HELPER_FLAGS_4(mve_vabavsb, TCG_CALL_NO_WG, i32, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vabavsh, TCG_CALL_NO_WG, i32, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vabavsw, TCG_CALL_NO_WG, i32, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vabavub, TCG_CALL_NO_WG, i32, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vabavuh, TCG_CALL_NO_WG, i32, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vabavuw, TCG_CALL_NO_WG, i32, env, ptr, ptr, i32)

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

DEF_HELPER_FLAGS_4(mve_vqrshli_sb, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqrshli_sh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqrshli_sw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vqrshli_ub, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqrshli_uh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vqrshli_uw, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

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

DEF_HELPER_FLAGS_3(mve_vcmpeqb, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vcmpeqh, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vcmpeqw, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vcmpneb, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vcmpneh, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vcmpnew, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vcmpcsb, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vcmpcsh, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vcmpcsw, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vcmphib, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vcmphih, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vcmphiw, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vcmpgeb, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vcmpgeh, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vcmpgew, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vcmpltb, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vcmplth, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vcmpltw, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vcmpgtb, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vcmpgth, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vcmpgtw, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vcmpleb, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vcmpleh, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vcmplew, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vcmpeq_scalarb, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vcmpeq_scalarh, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vcmpeq_scalarw, TCG_CALL_NO_WG, void, env, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vcmpne_scalarb, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vcmpne_scalarh, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vcmpne_scalarw, TCG_CALL_NO_WG, void, env, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vcmpcs_scalarb, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vcmpcs_scalarh, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vcmpcs_scalarw, TCG_CALL_NO_WG, void, env, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vcmphi_scalarb, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vcmphi_scalarh, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vcmphi_scalarw, TCG_CALL_NO_WG, void, env, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vcmpge_scalarb, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vcmpge_scalarh, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vcmpge_scalarw, TCG_CALL_NO_WG, void, env, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vcmplt_scalarb, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vcmplt_scalarh, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vcmplt_scalarw, TCG_CALL_NO_WG, void, env, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vcmpgt_scalarb, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vcmpgt_scalarh, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vcmpgt_scalarw, TCG_CALL_NO_WG, void, env, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vcmple_scalarb, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vcmple_scalarh, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vcmple_scalarw, TCG_CALL_NO_WG, void, env, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vfcmpeqh, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vfcmpeqs, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vfcmpneh, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vfcmpnes, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vfcmpgeh, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vfcmpges, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vfcmplth, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vfcmplts, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vfcmpgth, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vfcmpgts, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vfcmpleh, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vfcmples, TCG_CALL_NO_WG, void, env, ptr, ptr)

DEF_HELPER_FLAGS_3(mve_vfcmpeq_scalarh, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vfcmpeq_scalars, TCG_CALL_NO_WG, void, env, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vfcmpne_scalarh, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vfcmpne_scalars, TCG_CALL_NO_WG, void, env, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vfcmpge_scalarh, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vfcmpge_scalars, TCG_CALL_NO_WG, void, env, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vfcmplt_scalarh, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vfcmplt_scalars, TCG_CALL_NO_WG, void, env, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vfcmpgt_scalarh, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vfcmpgt_scalars, TCG_CALL_NO_WG, void, env, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vfcmple_scalarh, TCG_CALL_NO_WG, void, env, ptr, i32)
DEF_HELPER_FLAGS_3(mve_vfcmple_scalars, TCG_CALL_NO_WG, void, env, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vfadd_scalarh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vfadd_scalars, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vfsub_scalarh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vfsub_scalars, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vfmul_scalarh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vfmul_scalars, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vfma_scalarh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vfma_scalars, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vfmas_scalarh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vfmas_scalars, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vcvt_sh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vcvt_uh, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vcvt_hs, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vcvt_hu, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vcvt_sf, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vcvt_uf, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vcvt_fs, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vcvt_fu, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(mve_vrint_rm_h, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(mve_vrint_rm_s, TCG_CALL_NO_WG, void, env, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(mve_vrintx_h, TCG_CALL_NO_WG, void, env, ptr, ptr)
DEF_HELPER_FLAGS_3(mve_vrintx_s, TCG_CALL_NO_WG, void, env, ptr, ptr)
