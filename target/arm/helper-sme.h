/*
 *  AArch64 SME specific helper definitions
 *
 *  Copyright (c) 2022 Linaro, Ltd
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

DEF_HELPER_FLAGS_2(set_pstate_sm, TCG_CALL_NO_RWG, void, env, i32)
DEF_HELPER_FLAGS_2(set_pstate_za, TCG_CALL_NO_RWG, void, env, i32)

DEF_HELPER_FLAGS_3(sme_zero, TCG_CALL_NO_RWG, void, env, i32, i32)

/* Move to/from vertical array slices, i.e. columns, so 'c'.  */
DEF_HELPER_FLAGS_4(sme_mova_cz_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sme_mova_zc_b, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sme_mova_cz_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sme_mova_zc_h, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sme_mova_cz_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sme_mova_zc_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sme_mova_cz_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sme_mova_zc_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sme_mova_cz_q, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_4(sme_mova_zc_q, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_5(sme_ld1b_h, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1b_v, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1b_h_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1b_v_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_5(sme_ld1h_be_h, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1h_le_h, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1h_be_v, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1h_le_v, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1h_be_h_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1h_le_h_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1h_be_v_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1h_le_v_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_5(sme_ld1s_be_h, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1s_le_h, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1s_be_v, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1s_le_v, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1s_be_h_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1s_le_h_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1s_be_v_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1s_le_v_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_5(sme_ld1d_be_h, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1d_le_h, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1d_be_v, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1d_le_v, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1d_be_h_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1d_le_h_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1d_be_v_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1d_le_v_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_5(sme_ld1q_be_h, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1q_le_h, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1q_be_v, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1q_le_v, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1q_be_h_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1q_le_h_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1q_be_v_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_ld1q_le_v_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_5(sme_st1b_h, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1b_v, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1b_h_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1b_v_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_5(sme_st1h_be_h, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1h_le_h, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1h_be_v, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1h_le_v, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1h_be_h_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1h_le_h_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1h_be_v_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1h_le_v_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_5(sme_st1s_be_h, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1s_le_h, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1s_be_v, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1s_le_v, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1s_be_h_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1s_le_h_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1s_be_v_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1s_le_v_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_5(sme_st1d_be_h, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1d_le_h, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1d_be_v, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1d_le_v, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1d_be_h_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1d_le_h_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1d_be_v_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1d_le_v_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_5(sme_st1q_be_h, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1q_le_h, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1q_be_v, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1q_le_v, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1q_be_h_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1q_le_h_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1q_be_v_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)
DEF_HELPER_FLAGS_5(sme_st1q_le_v_mte, TCG_CALL_NO_WG, void, env, ptr, ptr, tl, i32)

DEF_HELPER_FLAGS_5(sme_addha_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sme_addva_s, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sme_addha_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_5(sme_addva_d, TCG_CALL_NO_RWG, void, ptr, ptr, ptr, ptr, i32)

DEF_HELPER_FLAGS_7(sme_fmopa_h, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_7(sme_fmopa_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_7(sme_fmopa_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sme_bfmopa, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sme_smopa_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sme_umopa_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sme_sumopa_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sme_usmopa_s, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sme_smopa_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sme_umopa_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sme_sumopa_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
DEF_HELPER_FLAGS_6(sme_usmopa_d, TCG_CALL_NO_RWG,
                   void, ptr, ptr, ptr, ptr, ptr, i32)
