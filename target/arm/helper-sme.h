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
