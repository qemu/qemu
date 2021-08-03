/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#ifndef HEXAGON_GEN_TCG_HVX_H
#define HEXAGON_GEN_TCG_HVX_H

/*
 * Histogram instructions
 *
 * Note that these instructions operate directly on the vector registers
 * and therefore happen after commit.
 *
 * The generate_<tag> function is called twice
 *     The first time is during the normal TCG generation
 *         ctx->pre_commit is true
 *         In the masked cases, we save the mask to the qtmp temporary
 *         Otherwise, there is nothing to do
 *     The second call is at the end of gen_commit_packet
 *         ctx->pre_commit is false
 *         Generate the call to the helper
 */

static inline void assert_vhist_tmp(DisasContext *ctx)
{
    /* vhist instructions require exactly one .tmp to be defined */
    g_assert(ctx->tmp_vregs_idx == 1);
}

#define fGEN_TCG_V6_vhist(SHORTCODE) \
    if (!ctx->pre_commit) { \
        assert_vhist_tmp(ctx); \
        gen_helper_vhist(cpu_env); \
    }
#define fGEN_TCG_V6_vhistq(SHORTCODE) \
    do { \
        if (ctx->pre_commit) { \
            intptr_t dstoff = offsetof(CPUHexagonState, qtmp); \
            tcg_gen_gvec_mov(MO_64, dstoff, QvV_off, \
                             sizeof(MMVector), sizeof(MMVector)); \
        } else { \
            assert_vhist_tmp(ctx); \
            gen_helper_vhistq(cpu_env); \
        } \
    } while (0)
#define fGEN_TCG_V6_vwhist256(SHORTCODE) \
    if (!ctx->pre_commit) { \
        assert_vhist_tmp(ctx); \
        gen_helper_vwhist256(cpu_env); \
    }
#define fGEN_TCG_V6_vwhist256q(SHORTCODE) \
    do { \
        if (ctx->pre_commit) { \
            intptr_t dstoff = offsetof(CPUHexagonState, qtmp); \
            tcg_gen_gvec_mov(MO_64, dstoff, QvV_off, \
                             sizeof(MMVector), sizeof(MMVector)); \
        } else { \
            assert_vhist_tmp(ctx); \
            gen_helper_vwhist256q(cpu_env); \
        } \
    } while (0)
#define fGEN_TCG_V6_vwhist256_sat(SHORTCODE) \
    if (!ctx->pre_commit) { \
        assert_vhist_tmp(ctx); \
        gen_helper_vwhist256_sat(cpu_env); \
    }
#define fGEN_TCG_V6_vwhist256q_sat(SHORTCODE) \
    do { \
        if (ctx->pre_commit) { \
            intptr_t dstoff = offsetof(CPUHexagonState, qtmp); \
            tcg_gen_gvec_mov(MO_64, dstoff, QvV_off, \
                             sizeof(MMVector), sizeof(MMVector)); \
        } else { \
            assert_vhist_tmp(ctx); \
            gen_helper_vwhist256q_sat(cpu_env); \
        } \
    } while (0)
#define fGEN_TCG_V6_vwhist128(SHORTCODE) \
    if (!ctx->pre_commit) { \
        assert_vhist_tmp(ctx); \
        gen_helper_vwhist128(cpu_env); \
    }
#define fGEN_TCG_V6_vwhist128q(SHORTCODE) \
    do { \
        if (ctx->pre_commit) { \
            intptr_t dstoff = offsetof(CPUHexagonState, qtmp); \
            tcg_gen_gvec_mov(MO_64, dstoff, QvV_off, \
                             sizeof(MMVector), sizeof(MMVector)); \
        } else { \
            assert_vhist_tmp(ctx); \
            gen_helper_vwhist128q(cpu_env); \
        } \
    } while (0)
#define fGEN_TCG_V6_vwhist128m(SHORTCODE) \
    if (!ctx->pre_commit) { \
        TCGv tcgv_uiV = tcg_constant_tl(uiV); \
        assert_vhist_tmp(ctx); \
        gen_helper_vwhist128m(cpu_env, tcgv_uiV); \
    }
#define fGEN_TCG_V6_vwhist128qm(SHORTCODE) \
    do { \
        if (ctx->pre_commit) { \
            intptr_t dstoff = offsetof(CPUHexagonState, qtmp); \
            tcg_gen_gvec_mov(MO_64, dstoff, QvV_off, \
                             sizeof(MMVector), sizeof(MMVector)); \
        } else { \
            TCGv tcgv_uiV = tcg_constant_tl(uiV); \
            assert_vhist_tmp(ctx); \
            gen_helper_vwhist128qm(cpu_env, tcgv_uiV); \
        } \
    } while (0)


#define fGEN_TCG_V6_vassign(SHORTCODE) \
    tcg_gen_gvec_mov(MO_64, VdV_off, VuV_off, \
                     sizeof(MMVector), sizeof(MMVector))

/* Vector conditional move */
#define fGEN_TCG_VEC_CMOV(PRED) \
    do { \
        TCGv lsb = tcg_temp_new(); \
        TCGLabel *false_label = gen_new_label(); \
        TCGLabel *end_label = gen_new_label(); \
        tcg_gen_andi_tl(lsb, PsV, 1); \
        tcg_gen_brcondi_tl(TCG_COND_NE, lsb, PRED, false_label); \
        tcg_temp_free(lsb); \
        tcg_gen_gvec_mov(MO_64, VdV_off, VuV_off, \
                         sizeof(MMVector), sizeof(MMVector)); \
        tcg_gen_br(end_label); \
        gen_set_label(false_label); \
        tcg_gen_ori_tl(hex_slot_cancelled, hex_slot_cancelled, \
                       1 << insn->slot); \
        gen_set_label(end_label); \
    } while (0)


/* Vector conditional move (true) */
#define fGEN_TCG_V6_vcmov(SHORTCODE) \
    fGEN_TCG_VEC_CMOV(1)

/* Vector conditional move (false) */
#define fGEN_TCG_V6_vncmov(SHORTCODE) \
    fGEN_TCG_VEC_CMOV(0)

/* Vector add - various forms */
#define fGEN_TCG_V6_vaddb(SHORTCODE) \
    tcg_gen_gvec_add(MO_8, VdV_off, VuV_off, VvV_off, \
                     sizeof(MMVector), sizeof(MMVector))

#define fGEN_TCG_V6_vaddh(SHORTCYDE) \
    tcg_gen_gvec_add(MO_16, VdV_off, VuV_off, VvV_off, \
                     sizeof(MMVector), sizeof(MMVector))

#define fGEN_TCG_V6_vaddw(SHORTCODE) \
    tcg_gen_gvec_add(MO_32, VdV_off, VuV_off, VvV_off, \
                     sizeof(MMVector), sizeof(MMVector))

#define fGEN_TCG_V6_vaddb_dv(SHORTCODE) \
    tcg_gen_gvec_add(MO_8, VddV_off, VuuV_off, VvvV_off, \
                     sizeof(MMVector) * 2, sizeof(MMVector) * 2)

#define fGEN_TCG_V6_vaddh_dv(SHORTCYDE) \
    tcg_gen_gvec_add(MO_16, VddV_off, VuuV_off, VvvV_off, \
                     sizeof(MMVector) * 2, sizeof(MMVector) * 2)

#define fGEN_TCG_V6_vaddw_dv(SHORTCODE) \
    tcg_gen_gvec_add(MO_32, VddV_off, VuuV_off, VvvV_off, \
                     sizeof(MMVector) * 2, sizeof(MMVector) * 2)

/* Vector sub - various forms */
#define fGEN_TCG_V6_vsubb(SHORTCODE) \
    tcg_gen_gvec_sub(MO_8, VdV_off, VuV_off, VvV_off, \
                     sizeof(MMVector), sizeof(MMVector))

#define fGEN_TCG_V6_vsubh(SHORTCODE) \
    tcg_gen_gvec_sub(MO_16, VdV_off, VuV_off, VvV_off, \
                     sizeof(MMVector), sizeof(MMVector))

#define fGEN_TCG_V6_vsubw(SHORTCODE) \
    tcg_gen_gvec_sub(MO_32, VdV_off, VuV_off, VvV_off, \
                     sizeof(MMVector), sizeof(MMVector))

#define fGEN_TCG_V6_vsubb_dv(SHORTCODE) \
    tcg_gen_gvec_sub(MO_8, VddV_off, VuuV_off, VvvV_off, \
                     sizeof(MMVector) * 2, sizeof(MMVector) * 2)

#define fGEN_TCG_V6_vsubh_dv(SHORTCODE) \
    tcg_gen_gvec_sub(MO_16, VddV_off, VuuV_off, VvvV_off, \
                     sizeof(MMVector) * 2, sizeof(MMVector) * 2)

#define fGEN_TCG_V6_vsubw_dv(SHORTCODE) \
    tcg_gen_gvec_sub(MO_32, VddV_off, VuuV_off, VvvV_off, \
                     sizeof(MMVector) * 2, sizeof(MMVector) * 2)

/* Vector shift right - various forms */
#define fGEN_TCG_V6_vasrh(SHORTCODE) \
    do { \
        TCGv shift = tcg_temp_new(); \
        tcg_gen_andi_tl(shift, RtV, 15); \
        tcg_gen_gvec_sars(MO_16, VdV_off, VuV_off, shift, \
                          sizeof(MMVector), sizeof(MMVector)); \
        tcg_temp_free(shift); \
    } while (0)

#define fGEN_TCG_V6_vasrh_acc(SHORTCODE) \
    do { \
        intptr_t tmpoff = offsetof(CPUHexagonState, vtmp); \
        TCGv shift = tcg_temp_new(); \
        tcg_gen_andi_tl(shift, RtV, 15); \
        tcg_gen_gvec_sars(MO_16, tmpoff, VuV_off, shift, \
                          sizeof(MMVector), sizeof(MMVector)); \
        tcg_gen_gvec_add(MO_16, VxV_off, VxV_off, tmpoff, \
                         sizeof(MMVector), sizeof(MMVector)); \
        tcg_temp_free(shift); \
    } while (0)

#define fGEN_TCG_V6_vasrw(SHORTCODE) \
    do { \
        TCGv shift = tcg_temp_new(); \
        tcg_gen_andi_tl(shift, RtV, 31); \
        tcg_gen_gvec_sars(MO_32, VdV_off, VuV_off, shift, \
                          sizeof(MMVector), sizeof(MMVector)); \
        tcg_temp_free(shift); \
    } while (0)

#define fGEN_TCG_V6_vasrw_acc(SHORTCODE) \
    do { \
        intptr_t tmpoff = offsetof(CPUHexagonState, vtmp); \
        TCGv shift = tcg_temp_new(); \
        tcg_gen_andi_tl(shift, RtV, 31); \
        tcg_gen_gvec_sars(MO_32, tmpoff, VuV_off, shift, \
                          sizeof(MMVector), sizeof(MMVector)); \
        tcg_gen_gvec_add(MO_32, VxV_off, VxV_off, tmpoff, \
                          sizeof(MMVector), sizeof(MMVector)); \
        tcg_temp_free(shift); \
    } while (0)

#define fGEN_TCG_V6_vlsrb(SHORTCODE) \
    do { \
        TCGv shift = tcg_temp_new(); \
        tcg_gen_andi_tl(shift, RtV, 7); \
        tcg_gen_gvec_shrs(MO_8, VdV_off, VuV_off, shift, \
                          sizeof(MMVector), sizeof(MMVector)); \
        tcg_temp_free(shift); \
    } while (0)

#define fGEN_TCG_V6_vlsrh(SHORTCODE) \
    do { \
        TCGv shift = tcg_temp_new(); \
        tcg_gen_andi_tl(shift, RtV, 15); \
        tcg_gen_gvec_shrs(MO_16, VdV_off, VuV_off, shift, \
                          sizeof(MMVector), sizeof(MMVector)); \
        tcg_temp_free(shift); \
    } while (0)

#define fGEN_TCG_V6_vlsrw(SHORTCODE) \
    do { \
        TCGv shift = tcg_temp_new(); \
        tcg_gen_andi_tl(shift, RtV, 31); \
        tcg_gen_gvec_shrs(MO_32, VdV_off, VuV_off, shift, \
                          sizeof(MMVector), sizeof(MMVector)); \
        tcg_temp_free(shift); \
    } while (0)

/* Vector shift left - various forms */
#define fGEN_TCG_V6_vaslb(SHORTCODE) \
    do { \
        TCGv shift = tcg_temp_new(); \
        tcg_gen_andi_tl(shift, RtV, 7); \
        tcg_gen_gvec_shls(MO_8, VdV_off, VuV_off, shift, \
                          sizeof(MMVector), sizeof(MMVector)); \
        tcg_temp_free(shift); \
    } while (0)

#define fGEN_TCG_V6_vaslh(SHORTCODE) \
    do { \
        TCGv shift = tcg_temp_new(); \
        tcg_gen_andi_tl(shift, RtV, 15); \
        tcg_gen_gvec_shls(MO_16, VdV_off, VuV_off, shift, \
                          sizeof(MMVector), sizeof(MMVector)); \
        tcg_temp_free(shift); \
    } while (0)

#define fGEN_TCG_V6_vaslh_acc(SHORTCODE) \
    do { \
        intptr_t tmpoff = offsetof(CPUHexagonState, vtmp); \
        TCGv shift = tcg_temp_new(); \
        tcg_gen_andi_tl(shift, RtV, 15); \
        tcg_gen_gvec_shls(MO_16, tmpoff, VuV_off, shift, \
                          sizeof(MMVector), sizeof(MMVector)); \
        tcg_gen_gvec_add(MO_16, VxV_off, VxV_off, tmpoff, \
                         sizeof(MMVector), sizeof(MMVector)); \
        tcg_temp_free(shift); \
    } while (0)

#define fGEN_TCG_V6_vaslw(SHORTCODE) \
    do { \
        TCGv shift = tcg_temp_new(); \
        tcg_gen_andi_tl(shift, RtV, 31); \
        tcg_gen_gvec_shls(MO_32, VdV_off, VuV_off, shift, \
                          sizeof(MMVector), sizeof(MMVector)); \
        tcg_temp_free(shift); \
    } while (0)

#define fGEN_TCG_V6_vaslw_acc(SHORTCODE) \
    do { \
        intptr_t tmpoff = offsetof(CPUHexagonState, vtmp); \
        TCGv shift = tcg_temp_new(); \
        tcg_gen_andi_tl(shift, RtV, 31); \
        tcg_gen_gvec_shls(MO_32, tmpoff, VuV_off, shift, \
                          sizeof(MMVector), sizeof(MMVector)); \
        tcg_gen_gvec_add(MO_32, VxV_off, VxV_off, tmpoff, \
                         sizeof(MMVector), sizeof(MMVector)); \
        tcg_temp_free(shift); \
    } while (0)

#endif
