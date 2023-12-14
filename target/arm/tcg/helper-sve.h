/*
 *  AArch64 SVE specific helper definitions
 *
 *  Copyright (c) 2018 Linaro, Ltd
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

DEF_HELPER_FLAGS_2(sve_predtest1, TCG_CALL_NO_WG, i32, i64, i64)
DEF_HELPER_FLAGS_3(sve_predtest, TCG_CALL_NO_WG, i32, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve_pfirst, TCG_CALL_NO_WG, i32, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_pnext, TCG_CALL_NO_WG, i32, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_and_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_and_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_and_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_and_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_eor_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_eor_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_eor_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_eor_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_orr_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_orr_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_orr_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_orr_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_bic_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_bic_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_bic_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_bic_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_add_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_add_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_add_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_add_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_sub_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_sub_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_sub_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_sub_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_smax_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_smax_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_smax_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_smax_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_umax_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_umax_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_umax_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_umax_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_smin_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_smin_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_smin_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_smin_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_umin_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_umin_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_umin_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_umin_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_sabd_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_sabd_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_sabd_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_sabd_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_uabd_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_uabd_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_uabd_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_uabd_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_mul_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_mul_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_mul_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_mul_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_smulh_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_smulh_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_smulh_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_smulh_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_umulh_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_umulh_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_umulh_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_umulh_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_sadalp_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sadalp_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sadalp_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_uadalp_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uadalp_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uadalp_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_srshl_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_srshl_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_srshl_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_srshl_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_urshl_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_urshl_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_urshl_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_urshl_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_sqshl_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqshl_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqshl_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqshl_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_uqshl_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uqshl_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uqshl_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uqshl_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_sqrshl_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqrshl_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqrshl_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqrshl_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_uqrshl_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uqrshl_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uqrshl_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uqrshl_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_shadd_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_shadd_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_shadd_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_shadd_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_uhadd_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uhadd_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uhadd_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uhadd_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_srhadd_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_srhadd_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_srhadd_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_srhadd_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_urhadd_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_urhadd_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_urhadd_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_urhadd_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_shsub_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_shsub_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_shsub_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_shsub_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_uhsub_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uhsub_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uhsub_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uhsub_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_sdiv_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_sdiv_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_udiv_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_udiv_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_asr_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_asr_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_asr_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_asr_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_lsr_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_lsr_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_lsr_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_lsr_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_lsl_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_lsl_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_lsl_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_lsl_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_sel_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_sel_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_sel_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_sel_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_sel_zpzz_q, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_addp_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_addp_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_addp_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_addp_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_smaxp_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_smaxp_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_smaxp_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_smaxp_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_umaxp_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_umaxp_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_umaxp_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_umaxp_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_sminp_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sminp_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sminp_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sminp_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_uminp_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uminp_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uminp_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uminp_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_sqadd_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqadd_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqadd_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqadd_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_uqadd_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uqadd_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uqadd_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uqadd_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_sqsub_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqsub_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqsub_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqsub_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_uqsub_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uqsub_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uqsub_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uqsub_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_suqadd_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_suqadd_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_suqadd_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_suqadd_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_usqadd_zpzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_usqadd_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_usqadd_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_usqadd_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_asr_zpzw_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_asr_zpzw_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_asr_zpzw_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_lsr_zpzw_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_lsr_zpzw_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_lsr_zpzw_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_lsl_zpzw_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_lsl_zpzw_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_lsl_zpzw_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve_orv_b, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_orv_h, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_orv_s, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_orv_d, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve_eorv_b, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_eorv_h, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_eorv_s, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_eorv_d, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve_andv_b, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_andv_h, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_andv_s, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_andv_d, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve_saddv_b, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_saddv_h, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_saddv_s, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve_uaddv_b, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_uaddv_h, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_uaddv_s, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_uaddv_d, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve_smaxv_b, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_smaxv_h, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_smaxv_s, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_smaxv_d, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve_umaxv_b, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_umaxv_h, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_umaxv_s, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_umaxv_d, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve_sminv_b, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_sminv_h, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_sminv_s, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_sminv_d, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve_uminv_b, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_uminv_h, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_uminv_s, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_uminv_d, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_movz_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_movz_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_movz_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_movz_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_asr_zpzi_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_asr_zpzi_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_asr_zpzi_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_asr_zpzi_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_lsr_zpzi_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_lsr_zpzi_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_lsr_zpzi_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_lsr_zpzi_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_lsl_zpzi_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_lsl_zpzi_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_lsl_zpzi_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_lsl_zpzi_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_asrd_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_asrd_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_asrd_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_asrd_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_cls_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cls_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cls_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cls_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_clz_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_clz_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_clz_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_clz_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_cnt_zpz_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cnt_zpz_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cnt_zpz_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cnt_zpz_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_cnot_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cnot_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cnot_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cnot_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_fabs_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_fabs_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_fabs_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_fneg_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_fneg_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_fneg_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_not_zpz_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_not_zpz_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_not_zpz_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_not_zpz_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_sxtb_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_sxtb_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_sxtb_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_uxtb_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_uxtb_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_uxtb_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_sxth_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_sxth_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_uxth_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_uxth_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_sxtw_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_uxtw_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_abs_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_abs_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_abs_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_abs_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_neg_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_neg_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_neg_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_neg_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve_mla_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_mla_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_mla_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_mla_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve_mls_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_mls_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_mls_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_mls_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_index_b, TCG_CALL_NO_RWG, void, ptr, i32, i32, i32)
DEF_HELPER_FLAGS_4(sve_index_h, TCG_CALL_NO_RWG, void, ptr, i32, i32, i32)
DEF_HELPER_FLAGS_4(sve_index_s, TCG_CALL_NO_RWG, void, ptr, i32, i32, i32)
DEF_HELPER_FLAGS_4(sve_index_d, TCG_CALL_NO_RWG, void, ptr, i64, i64, i32)

DEF_HELPER_FLAGS_4(sve_asr_zzw_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_asr_zzw_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_asr_zzw_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_lsr_zzw_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_lsr_zzw_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_lsr_zzw_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_lsl_zzw_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_lsl_zzw_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_lsl_zzw_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_adr_p32, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_adr_p64, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_adr_s32, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_adr_u32, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve_fexpa_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_fexpa_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_fexpa_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_ftssel_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_ftssel_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_ftssel_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_sqaddi_b, TCG_CALL_NO_RWG, void, ptr, ptr, s32, i32)
DEF_HELPER_FLAGS_4(sve_sqaddi_h, TCG_CALL_NO_RWG, void, ptr, ptr, s32, i32)
DEF_HELPER_FLAGS_4(sve_sqaddi_s, TCG_CALL_NO_RWG, void, ptr, ptr, s64, i32)
DEF_HELPER_FLAGS_4(sve_sqaddi_d, TCG_CALL_NO_RWG, void, ptr, ptr, s64, i32)

DEF_HELPER_FLAGS_4(sve_uqaddi_b, TCG_CALL_NO_RWG, void, ptr, ptr, s32, i32)
DEF_HELPER_FLAGS_4(sve_uqaddi_h, TCG_CALL_NO_RWG, void, ptr, ptr, s32, i32)
DEF_HELPER_FLAGS_4(sve_uqaddi_s, TCG_CALL_NO_RWG, void, ptr, ptr, s64, i32)
DEF_HELPER_FLAGS_4(sve_uqaddi_d, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(sve_uqsubi_d, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)

DEF_HELPER_FLAGS_5(sve_cpy_m_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_5(sve_cpy_m_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_5(sve_cpy_m_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_5(sve_cpy_m_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i64, i32)

DEF_HELPER_FLAGS_4(sve_cpy_z_b, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(sve_cpy_z_h, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(sve_cpy_z_s, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(sve_cpy_z_d, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)

DEF_HELPER_FLAGS_4(sve_ext, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_insr_b, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(sve_insr_h, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(sve_insr_s, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(sve_insr_d, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)

DEF_HELPER_FLAGS_3(sve_rev_b, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_rev_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_rev_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_rev_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_tbl_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_tbl_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_tbl_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_tbl_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_tbl_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_tbl_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_tbl_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_tbl_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_tbx_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_tbx_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_tbx_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_tbx_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve_sunpk_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_sunpk_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_sunpk_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve_uunpk_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_uunpk_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_uunpk_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_zip_p, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_uzp_p, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_trn_p, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_rev_p, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve_punpk_p, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_zip_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_zip_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_zip_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_zip_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_zip_q, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_uzp_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_uzp_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_uzp_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_uzp_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_uzp_q, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_trn_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_trn_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_trn_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_trn_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_trn_q, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_compact_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_compact_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_2(sve_last_active_element, TCG_CALL_NO_RWG, s32, ptr, i32)

DEF_HELPER_FLAGS_4(sve_revb_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_revb_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_revb_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_revh_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_revh_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_revw_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sme_revd_q, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_rbit_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_rbit_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_rbit_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_rbit_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_sqabs_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqabs_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqabs_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqabs_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_sqneg_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqneg_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqneg_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqneg_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_urecpe_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_ursqrte_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_splice, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_cmpeq_ppzz_b, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmpne_ppzz_b, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmpge_ppzz_b, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmpgt_ppzz_b, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmphi_ppzz_b, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmphs_ppzz_b, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_cmpeq_ppzz_h, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmpne_ppzz_h, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmpge_ppzz_h, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmpgt_ppzz_h, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmphi_ppzz_h, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmphs_ppzz_h, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_cmpeq_ppzz_s, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmpne_ppzz_s, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmpge_ppzz_s, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmpgt_ppzz_s, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmphi_ppzz_s, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmphs_ppzz_s, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_cmpeq_ppzz_d, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmpne_ppzz_d, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmpge_ppzz_d, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmpgt_ppzz_d, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmphi_ppzz_d, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmphs_ppzz_d, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_cmpeq_ppzw_b, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmpne_ppzw_b, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmpge_ppzw_b, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmpgt_ppzw_b, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmphi_ppzw_b, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmphs_ppzw_b, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmple_ppzw_b, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmplt_ppzw_b, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmplo_ppzw_b, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmpls_ppzw_b, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_cmpeq_ppzw_h, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmpne_ppzw_h, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmpge_ppzw_h, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmpgt_ppzw_h, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmphi_ppzw_h, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmphs_ppzw_h, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmple_ppzw_h, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmplt_ppzw_h, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmplo_ppzw_h, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmpls_ppzw_h, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_cmpeq_ppzw_s, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmpne_ppzw_s, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmpge_ppzw_s, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmpgt_ppzw_s, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmphi_ppzw_s, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmphs_ppzw_s, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmple_ppzw_s, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmplt_ppzw_s, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmplo_ppzw_s, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_cmpls_ppzw_s, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_cmpeq_ppzi_b, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmpne_ppzi_b, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmpgt_ppzi_b, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmpge_ppzi_b, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmplt_ppzi_b, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmple_ppzi_b, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmphs_ppzi_b, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmphi_ppzi_b, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmplo_ppzi_b, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmpls_ppzi_b, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_cmpeq_ppzi_h, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmpne_ppzi_h, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmpgt_ppzi_h, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmpge_ppzi_h, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmplt_ppzi_h, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmple_ppzi_h, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmphs_ppzi_h, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmphi_ppzi_h, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmplo_ppzi_h, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmpls_ppzi_h, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_cmpeq_ppzi_s, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmpne_ppzi_s, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmpgt_ppzi_s, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmpge_ppzi_s, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmplt_ppzi_s, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmple_ppzi_s, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmphs_ppzi_s, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmphi_ppzi_s, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmplo_ppzi_s, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmpls_ppzi_s, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_cmpeq_ppzi_d, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmpne_ppzi_d, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmpgt_ppzi_d, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmpge_ppzi_d, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmplt_ppzi_d, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmple_ppzi_d, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmphs_ppzi_d, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmphi_ppzi_d, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmplo_ppzi_d, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_cmpls_ppzi_d, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_and_pppp, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_bic_pppp, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_eor_pppp, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_sel_pppp, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_orr_pppp, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_orn_pppp, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_nor_pppp, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_nand_pppp, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_brkpa, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_brkpb, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_brkpas, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_brkpbs, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_brka_z, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_brkb_z, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_brka_m, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_brkb_m, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_brkas_z, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_brkbs_z, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_brkas_m, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_brkbs_m, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_brkn, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_brkns, TCG_CALL_NO_RWG, i32, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve_cntp, TCG_CALL_NO_RWG, i64, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve_whilel, TCG_CALL_NO_RWG, i32, ptr, i32, i32)
DEF_HELPER_FLAGS_3(sve_whileg, TCG_CALL_NO_RWG, i32, ptr, i32, i32)

DEF_HELPER_FLAGS_4(sve_subri_b, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(sve_subri_h, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(sve_subri_s, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(sve_subri_d, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)

DEF_HELPER_FLAGS_4(sve_smaxi_b, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(sve_smaxi_h, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(sve_smaxi_s, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(sve_smaxi_d, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)

DEF_HELPER_FLAGS_4(sve_smini_b, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(sve_smini_h, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(sve_smini_s, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(sve_smini_d, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)

DEF_HELPER_FLAGS_4(sve_umaxi_b, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(sve_umaxi_h, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(sve_umaxi_s, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(sve_umaxi_d, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)

DEF_HELPER_FLAGS_4(sve_umini_b, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(sve_umini_h, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(sve_umini_s, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)
DEF_HELPER_FLAGS_4(sve_umini_d, TCG_CALL_NO_RWG, void, ptr, ptr, i64, i32)

DEF_HELPER_FLAGS_5(gvec_recps_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_recps_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_recps_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(gvec_rsqrts_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_rsqrts_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(gvec_rsqrts_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_faddv_h, TCG_CALL_NO_RWG,
                   i64, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_faddv_s, TCG_CALL_NO_RWG,
                   i64, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_faddv_d, TCG_CALL_NO_RWG,
                   i64, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_fmaxnmv_h, TCG_CALL_NO_RWG,
                   i64, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_fmaxnmv_s, TCG_CALL_NO_RWG,
                   i64, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_fmaxnmv_d, TCG_CALL_NO_RWG,
                   i64, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_fminnmv_h, TCG_CALL_NO_RWG,
                   i64, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_fminnmv_s, TCG_CALL_NO_RWG,
                   i64, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_fminnmv_d, TCG_CALL_NO_RWG,
                   i64, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_fmaxv_h, TCG_CALL_NO_RWG,
                   i64, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_fmaxv_s, TCG_CALL_NO_RWG,
                   i64, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_fmaxv_d, TCG_CALL_NO_RWG,
                   i64, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_fminv_h, TCG_CALL_NO_RWG,
                   i64, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_fminv_s, TCG_CALL_NO_RWG,
                   i64, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve_fminv_d, TCG_CALL_NO_RWG,
                   i64, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_fadda_h, TCG_CALL_NO_RWG,
                   i64, i64, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fadda_s, TCG_CALL_NO_RWG,
                   i64, i64, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fadda_d, TCG_CALL_NO_RWG,
                   i64, i64, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_fcmge0_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcmge0_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcmge0_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_fcmgt0_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcmgt0_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcmgt0_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_fcmlt0_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcmlt0_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcmlt0_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_fcmle0_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcmle0_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcmle0_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_fcmeq0_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcmeq0_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcmeq0_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_fcmne0_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcmne0_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcmne0_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fadd_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fadd_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fadd_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fsub_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fsub_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fsub_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fmul_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fmul_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fmul_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fdiv_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fdiv_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fdiv_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fmin_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fmin_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fmin_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fmax_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fmax_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fmax_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fminnum_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fminnum_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fminnum_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fmaxnum_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fmaxnum_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fmaxnum_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fabd_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fabd_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fabd_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fscalbn_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fscalbn_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fscalbn_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fmulx_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fmulx_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fmulx_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fadds_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i64, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fadds_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i64, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fadds_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i64, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fsubs_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i64, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fsubs_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i64, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fsubs_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i64, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fmuls_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i64, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fmuls_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i64, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fmuls_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i64, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fsubrs_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i64, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fsubrs_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i64, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fsubrs_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i64, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fmaxnms_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i64, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fmaxnms_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i64, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fmaxnms_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i64, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fminnms_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i64, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fminnms_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i64, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fminnms_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i64, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fmaxs_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i64, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fmaxs_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i64, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fmaxs_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i64, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fmins_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i64, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fmins_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i64, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fmins_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i64, ptr, i32)

DEF_HELPER_FLAGS_5(sve_fcvt_sh, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcvt_dh, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcvt_hs, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcvt_ds, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcvt_hd, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcvt_sd, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_bfcvt, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_fcvtzs_hh, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcvtzs_hs, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcvtzs_ss, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcvtzs_ds, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcvtzs_hd, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcvtzs_sd, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcvtzs_dd, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_fcvtzu_hh, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcvtzu_hs, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcvtzu_ss, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcvtzu_ds, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcvtzu_hd, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcvtzu_sd, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fcvtzu_dd, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_frint_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_frint_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_frint_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_frintx_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_frintx_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_frintx_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_frecpx_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_frecpx_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_frecpx_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_fsqrt_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fsqrt_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_fsqrt_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_scvt_hh, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_scvt_sh, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_scvt_dh, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_scvt_ss, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_scvt_sd, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_scvt_ds, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_scvt_dd, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_ucvt_hh, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_ucvt_sh, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_ucvt_dh, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_ucvt_ss, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_ucvt_sd, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_ucvt_ds, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_ucvt_dd, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fcmge_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fcmge_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fcmge_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fcmgt_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fcmgt_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fcmgt_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fcmeq_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fcmeq_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fcmeq_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fcmne_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fcmne_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fcmne_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fcmuo_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fcmuo_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fcmuo_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve_facge_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_facge_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_facge_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve_facgt_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_facgt_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_facgt_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve_fcadd_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fcadd_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve_fcadd_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_7(sve_fmla_zpzzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_7(sve_fmla_zpzzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_7(sve_fmla_zpzzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_7(sve_fmls_zpzzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_7(sve_fmls_zpzzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_7(sve_fmls_zpzzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_7(sve_fnmla_zpzzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_7(sve_fnmla_zpzzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_7(sve_fnmla_zpzzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_7(sve_fnmls_zpzzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_7(sve_fnmls_zpzzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_7(sve_fnmls_zpzzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_7(sve_fcmla_zpzzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_7(sve_fcmla_zpzzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_7(sve_fcmla_zpzzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve_ftmad_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_ftmad_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_ftmad_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_saddl_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_saddl_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_saddl_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_ssubl_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_ssubl_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_ssubl_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_sabdl_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sabdl_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sabdl_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_uaddl_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_uaddl_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_uaddl_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_usubl_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_usubl_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_usubl_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_uabdl_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_uabdl_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_uabdl_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_saddw_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_saddw_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_saddw_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_ssubw_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_ssubw_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_ssubw_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_uaddw_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_uaddw_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_uaddw_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_usubw_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_usubw_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_usubw_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve_ld1bb_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld2bb_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld3bb_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld4bb_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ld1hh_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld2hh_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld3hh_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld4hh_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ld1hh_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld2hh_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld3hh_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld4hh_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ld1ss_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld2ss_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld3ss_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld4ss_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ld1ss_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld2ss_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld3ss_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld4ss_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ld1dd_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld2dd_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld3dd_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld4dd_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ld1dd_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld2dd_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld3dd_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld4dd_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ld1bhu_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1bsu_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1bdu_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1bhs_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1bss_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1bds_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ld1hsu_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1hdu_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1hss_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1hds_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ld1hsu_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1hdu_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1hss_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1hds_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ld1sdu_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1sds_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ld1sdu_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1sds_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ld1bb_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld2bb_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld3bb_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld4bb_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ld1hh_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld2hh_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld3hh_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld4hh_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ld1hh_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld2hh_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld3hh_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld4hh_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ld1ss_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld2ss_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld3ss_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld4ss_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ld1ss_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld2ss_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld3ss_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld4ss_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ld1dd_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld2dd_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld3dd_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld4dd_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ld1dd_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld2dd_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld3dd_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld4dd_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ld1bhu_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1bsu_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1bdu_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1bhs_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1bss_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1bds_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ld1hsu_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1hdu_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1hss_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1hds_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ld1hsu_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1hdu_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1hss_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1hds_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ld1sdu_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1sds_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ld1sdu_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ld1sds_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ldff1bb_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1bhu_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1bsu_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1bdu_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1bhs_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1bss_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1bds_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ldff1hh_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1hsu_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1hdu_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1hss_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1hds_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ldff1hh_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1hsu_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1hdu_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1hss_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1hds_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ldff1ss_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1sdu_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1sds_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ldff1ss_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1sdu_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1sds_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ldff1dd_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1dd_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ldff1bb_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1bhu_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1bsu_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1bdu_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1bhs_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1bss_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1bds_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ldff1hh_le_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1hsu_le_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1hdu_le_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1hss_le_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1hds_le_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ldff1hh_be_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1hsu_be_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1hdu_be_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1hss_be_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1hds_be_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ldff1ss_le_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1sdu_le_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1sds_le_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ldff1ss_be_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1sdu_be_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1sds_be_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ldff1dd_le_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldff1dd_be_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ldnf1bb_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1bhu_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1bsu_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1bdu_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1bhs_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1bss_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1bds_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ldnf1hh_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1hsu_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1hdu_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1hss_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1hds_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ldnf1hh_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1hsu_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1hdu_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1hss_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1hds_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ldnf1ss_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1sdu_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1sds_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ldnf1ss_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1sdu_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1sds_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ldnf1dd_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1dd_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ldnf1bb_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1bhu_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1bsu_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1bdu_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1bhs_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1bss_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1bds_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ldnf1hh_le_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1hsu_le_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1hdu_le_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1hss_le_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1hds_le_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ldnf1hh_be_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1hsu_be_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1hdu_be_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1hss_be_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1hds_be_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ldnf1ss_le_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1sdu_le_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1sds_le_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ldnf1ss_be_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1sdu_be_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1sds_be_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_ldnf1dd_le_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_ldnf1dd_be_r_mte, TCG_CALL_NO_WG,
                   void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_st1bb_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st2bb_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st3bb_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st4bb_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_st1hh_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st2hh_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st3hh_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st4hh_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_st1hh_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st2hh_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st3hh_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st4hh_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_st1ss_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st2ss_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st3ss_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st4ss_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_st1ss_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st2ss_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st3ss_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st4ss_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_st1dd_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st2dd_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st3dd_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st4dd_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_st1dd_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st2dd_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st3dd_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st4dd_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_st1bh_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st1bs_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st1bd_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_st1hs_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st1hd_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st1hs_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st1hd_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_st1sd_le_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st1sd_be_r, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_st1bb_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st2bb_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st3bb_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st4bb_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_st1hh_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st2hh_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st3hh_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st4hh_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_st1hh_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st2hh_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st3hh_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st4hh_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_st1ss_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st2ss_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st3ss_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st4ss_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_st1ss_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st2ss_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st3ss_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st4ss_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_st1dd_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st2dd_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st3dd_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st4dd_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_st1dd_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st2dd_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st3dd_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st4dd_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_st1bh_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st1bs_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st1bd_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_st1hs_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st1hd_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st1hs_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st1hd_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve_st1sd_le_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)
DEF_HELPER_FLAGS_4(sve_st1sd_be_r_mte, TCG_CALL_NO_WG, void, env, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_ldbsu_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhsu_le_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhsu_be_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldss_le_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldss_be_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldbss_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhss_le_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhss_be_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_ldbsu_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhsu_le_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhsu_be_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldss_le_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldss_be_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldbss_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhss_le_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhss_be_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_ldbdu_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhdu_le_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhdu_be_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldsdu_le_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldsdu_be_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_lddd_le_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_lddd_be_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldbds_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhds_le_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhds_be_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldsds_le_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldsds_be_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_ldbdu_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhdu_le_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhdu_be_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldsdu_le_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldsdu_be_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_lddd_le_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_lddd_be_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldbds_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhds_le_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhds_be_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldsds_le_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldsds_be_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_ldbdu_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhdu_le_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhdu_be_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldsdu_le_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldsdu_be_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_lddd_le_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_lddd_be_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldbds_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhds_le_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhds_be_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldsds_le_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldsds_be_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_ldbsu_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhsu_le_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhsu_be_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldss_le_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldss_be_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldbss_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhss_le_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhss_be_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_ldbsu_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhsu_le_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhsu_be_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldss_le_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldss_be_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldbss_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhss_le_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhss_be_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_ldbdu_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhdu_le_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhdu_be_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldsdu_le_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldsdu_be_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_lddd_le_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_lddd_be_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldbds_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhds_le_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhds_be_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldsds_le_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldsds_be_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_ldbdu_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhdu_le_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhdu_be_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldsdu_le_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldsdu_be_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_lddd_le_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_lddd_be_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldbds_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhds_le_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhds_be_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldsds_le_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldsds_be_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_ldbdu_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhdu_le_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhdu_be_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldsdu_le_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldsdu_be_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_lddd_le_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_lddd_be_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldbds_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhds_le_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldhds_be_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldsds_le_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldsds_be_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_ldffbsu_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhsu_le_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhsu_be_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffss_le_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffss_be_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffbss_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhss_le_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhss_be_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_ldffbsu_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhsu_le_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhsu_be_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffss_le_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffss_be_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffbss_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhss_le_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhss_be_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_ldffbdu_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhdu_le_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhdu_be_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffsdu_le_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffsdu_be_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffdd_le_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffdd_be_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffbds_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhds_le_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhds_be_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffsds_le_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffsds_be_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_ldffbdu_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhdu_le_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhdu_be_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffsdu_le_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffsdu_be_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffdd_le_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffdd_be_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffbds_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhds_le_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhds_be_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffsds_le_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffsds_be_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_ldffbdu_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhdu_le_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhdu_be_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffsdu_le_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffsdu_be_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffdd_le_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffdd_be_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffbds_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhds_le_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhds_be_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffsds_le_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffsds_be_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_ldffbsu_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhsu_le_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhsu_be_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffss_le_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffss_be_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffbss_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhss_le_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhss_be_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_ldffbsu_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhsu_le_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhsu_be_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffss_le_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffss_be_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffbss_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhss_le_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhss_be_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_ldffbdu_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhdu_le_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhdu_be_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffsdu_le_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffsdu_be_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffdd_le_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffdd_be_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffbds_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhds_le_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhds_be_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffsds_le_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffsds_be_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_ldffbdu_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhdu_le_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhdu_be_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffsdu_le_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffsdu_be_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffdd_le_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffdd_be_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffbds_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhds_le_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhds_be_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffsds_le_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffsds_be_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_ldffbdu_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhdu_le_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhdu_be_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffsdu_le_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffsdu_be_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffdd_le_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffdd_be_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffbds_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhds_le_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffhds_be_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffsds_le_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_ldffsds_be_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_stbs_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_sths_le_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_sths_be_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stss_le_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stss_be_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_stbs_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_sths_le_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_sths_be_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stss_le_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stss_be_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_stbd_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_sthd_le_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_sthd_be_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stsd_le_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stsd_be_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stdd_le_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stdd_be_zsu, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_stbd_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_sthd_le_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_sthd_be_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stsd_le_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stsd_be_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stdd_le_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stdd_be_zss, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_stbd_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_sthd_le_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_sthd_be_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stsd_le_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stsd_be_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stdd_le_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stdd_be_zd, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_stbs_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_sths_le_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_sths_be_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stss_le_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stss_be_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_stbs_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_sths_le_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_sths_be_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stss_le_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stss_be_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_stbd_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_sthd_le_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_sthd_be_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stsd_le_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stsd_be_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stdd_le_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stdd_be_zsu_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_stbd_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_sthd_le_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_sthd_be_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stsd_le_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stsd_be_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stdd_le_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stdd_be_zss_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_6(sve_stbd_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_sthd_le_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_sthd_be_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stsd_le_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stsd_be_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stdd_le_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_6(sve_stdd_be_zd_mte, TCG_CALL_NO_WG,
                   void, env, ptr, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_4(sve2_sqdmull_zzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqdmull_zzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqdmull_zzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_smull_zzz_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_smull_zzz_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_smull_zzz_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_umull_zzz_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_umull_zzz_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_umull_zzz_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_pmull_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_pmull_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve2_sshll_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sshll_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sshll_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve2_ushll_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_ushll_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_ushll_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_eoril_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_eoril_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_eoril_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_eoril_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_bext_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_bext_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_bext_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_bext_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_bdep_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_bdep_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_bdep_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_bdep_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_bgrp_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_bgrp_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_bgrp_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_bgrp_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_cadd_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_cadd_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_cadd_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_cadd_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_sqcadd_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqcadd_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqcadd_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqcadd_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_sabal_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sabal_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sabal_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_uabal_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uabal_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_uabal_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_adcl_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_adcl_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve2_sqxtnb_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sqxtnb_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sqxtnb_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve2_uqxtnb_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_uqxtnb_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_uqxtnb_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve2_sqxtunb_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sqxtunb_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sqxtunb_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve2_sqxtnt_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sqxtnt_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sqxtnt_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve2_uqxtnt_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_uqxtnt_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_uqxtnt_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve2_sqxtunt_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sqxtunt_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sqxtunt_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve2_shrnb_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_shrnb_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_shrnb_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve2_shrnt_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_shrnt_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_shrnt_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve2_rshrnb_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_rshrnb_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_rshrnb_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve2_rshrnt_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_rshrnt_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_rshrnt_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve2_sqshrunb_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sqshrunb_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sqshrunb_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve2_sqshrunt_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sqshrunt_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sqshrunt_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve2_sqrshrunb_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sqrshrunb_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sqrshrunb_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve2_sqrshrunt_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sqrshrunt_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sqrshrunt_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve2_sqshrnb_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sqshrnb_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sqshrnb_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve2_sqshrnt_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sqshrnt_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sqshrnt_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve2_sqrshrnb_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sqrshrnb_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sqrshrnb_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve2_sqrshrnt_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sqrshrnt_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_sqrshrnt_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve2_uqshrnb_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_uqshrnb_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_uqshrnb_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve2_uqshrnt_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_uqshrnt_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_uqshrnt_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve2_uqrshrnb_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_uqrshrnb_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_uqrshrnb_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_3(sve2_uqrshrnt_h, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_uqrshrnt_s, TCG_CALL_NO_RWG, void, ptr, ptr, i32)
DEF_HELPER_FLAGS_3(sve2_uqrshrnt_d, TCG_CALL_NO_RWG, void, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_addhnb_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_addhnb_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_addhnb_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_addhnt_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_addhnt_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_addhnt_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_raddhnb_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_raddhnb_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_raddhnb_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_raddhnt_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_raddhnt_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_raddhnt_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_subhnb_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_subhnb_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_subhnb_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_subhnt_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_subhnt_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_subhnt_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_rsubhnb_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_rsubhnb_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_rsubhnb_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_rsubhnt_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_rsubhnt_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_rsubhnt_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_match_ppzz_b, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_match_ppzz_h, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_nmatch_ppzz_b, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_nmatch_ppzz_h, TCG_CALL_NO_RWG,
                   i32, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_histcnt_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_histcnt_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_histseg, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_xar_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_xar_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_xar_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve2_faddp_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve2_faddp_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve2_faddp_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve2_fmaxnmp_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve2_fmaxnmp_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve2_fmaxnmp_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve2_fminnmp_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve2_fminnmp_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve2_fminnmp_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve2_fmaxp_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve2_fmaxp_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve2_fmaxp_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(sve2_fminp_zpzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve2_fminp_zpzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sve2_fminp_zpzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_eor3, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_bcax, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_bsl1n, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_bsl2n, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_nbsl, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_sqdmlal_zzzw_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqdmlal_zzzw_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqdmlal_zzzw_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_sqdmlsl_zzzw_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqdmlsl_zzzw_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqdmlsl_zzzw_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_smlal_zzzw_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_smlal_zzzw_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_smlal_zzzw_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_umlal_zzzw_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_umlal_zzzw_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_umlal_zzzw_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_smlsl_zzzw_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_smlsl_zzzw_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_smlsl_zzzw_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_umlsl_zzzw_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_umlsl_zzzw_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_umlsl_zzzw_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_cmla_zzzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_cmla_zzzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_cmla_zzzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_cmla_zzzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_sqrdcmlah_zzzz_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqrdcmlah_zzzz_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqrdcmlah_zzzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqrdcmlah_zzzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_6(fmmla_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(fmmla_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_sqrdmlah_idx_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqrdmlah_idx_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqrdmlah_idx_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_sqrdmlsh_idx_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqrdmlsh_idx_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqrdmlsh_idx_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_sqdmlal_idx_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqdmlal_idx_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqdmlsl_idx_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqdmlsl_idx_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_sqdmull_idx_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqdmull_idx_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_smlal_idx_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_smlal_idx_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_smlsl_idx_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_smlsl_idx_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_umlal_idx_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_umlal_idx_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_umlsl_idx_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_umlsl_idx_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_smull_idx_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_smull_idx_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_umull_idx_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_umull_idx_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_cmla_idx_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_cmla_idx_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqrdcmlah_idx_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_sqrdcmlah_idx_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_cdot_zzzz_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_cdot_zzzz_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_cdot_idx_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_cdot_idx_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_fcvtnt_sh, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_fcvtnt_ds, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve_bfcvtnt, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sve2_fcvtlt_hs, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sve2_fcvtlt_sd, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(flogb_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(flogb_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(flogb_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_sqshl_zpzi_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqshl_zpzi_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqshl_zpzi_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqshl_zpzi_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_uqshl_zpzi_b, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_uqshl_zpzi_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_uqshl_zpzi_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_uqshl_zpzi_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_srshr_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_srshr_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_srshr_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_srshr_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_urshr_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_urshr_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_urshr_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_urshr_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_4(sve2_sqshlu_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqshlu_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqshlu_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sve2_sqshlu_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
