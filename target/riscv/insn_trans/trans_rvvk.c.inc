/*
 * RISC-V translation routines for the vector crypto extension.
 *
 * Copyright (C) 2023 SiFive, Inc.
 * Written by Codethink Ltd and SiFive.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Zvbc
 */

#define GEN_VV_MASKED_TRANS(NAME, CHECK)                     \
    static bool trans_##NAME(DisasContext *s, arg_rmrr *a)   \
    {                                                        \
        if (CHECK(s, a)) {                                   \
            return opivv_trans(a->rd, a->rs1, a->rs2, a->vm, \
                               gen_helper_##NAME, s);        \
        }                                                    \
        return false;                                        \
    }

static bool vclmul_vv_check(DisasContext *s, arg_rmrr *a)
{
    return opivv_check(s, a) &&
           s->cfg_ptr->ext_zvbc == true &&
           s->sew == MO_64;
}

GEN_VV_MASKED_TRANS(vclmul_vv, vclmul_vv_check)
GEN_VV_MASKED_TRANS(vclmulh_vv, vclmul_vv_check)

#define GEN_VX_MASKED_TRANS(NAME, CHECK)                     \
    static bool trans_##NAME(DisasContext *s, arg_rmrr *a)   \
    {                                                        \
        if (CHECK(s, a)) {                                   \
            return opivx_trans(a->rd, a->rs1, a->rs2, a->vm, \
                               gen_helper_##NAME, s);        \
        }                                                    \
        return false;                                        \
    }

static bool vclmul_vx_check(DisasContext *s, arg_rmrr *a)
{
    return opivx_check(s, a) &&
           s->cfg_ptr->ext_zvbc == true &&
           s->sew == MO_64;
}

GEN_VX_MASKED_TRANS(vclmul_vx, vclmul_vx_check)
GEN_VX_MASKED_TRANS(vclmulh_vx, vclmul_vx_check)

/*
 * Zvbb
 */

#define GEN_OPIVI_GVEC_TRANS_CHECK(NAME, IMM_MODE, OPIVX, SUF, CHECK)   \
    static bool trans_##NAME(DisasContext *s, arg_rmrr *a)              \
    {                                                                   \
        if (CHECK(s, a)) {                                              \
            static gen_helper_opivx *const fns[4] = {                   \
                gen_helper_##OPIVX##_b,                                 \
                gen_helper_##OPIVX##_h,                                 \
                gen_helper_##OPIVX##_w,                                 \
                gen_helper_##OPIVX##_d,                                 \
            };                                                          \
            return do_opivi_gvec(s, a, tcg_gen_gvec_##SUF, fns[s->sew], \
                                 IMM_MODE);                             \
        }                                                               \
        return false;                                                   \
    }

#define GEN_OPIVV_GVEC_TRANS_CHECK(NAME, SUF, CHECK)                     \
    static bool trans_##NAME(DisasContext *s, arg_rmrr *a)               \
    {                                                                    \
        if (CHECK(s, a)) {                                               \
            static gen_helper_gvec_4_ptr *const fns[4] = {               \
                gen_helper_##NAME##_b,                                   \
                gen_helper_##NAME##_h,                                   \
                gen_helper_##NAME##_w,                                   \
                gen_helper_##NAME##_d,                                   \
            };                                                           \
            return do_opivv_gvec(s, a, tcg_gen_gvec_##SUF, fns[s->sew]); \
        }                                                                \
        return false;                                                    \
    }

#define GEN_OPIVX_GVEC_SHIFT_TRANS_CHECK(NAME, SUF, CHECK)       \
    static bool trans_##NAME(DisasContext *s, arg_rmrr *a)       \
    {                                                            \
        if (CHECK(s, a)) {                                       \
            static gen_helper_opivx *const fns[4] = {            \
                gen_helper_##NAME##_b,                           \
                gen_helper_##NAME##_h,                           \
                gen_helper_##NAME##_w,                           \
                gen_helper_##NAME##_d,                           \
            };                                                   \
            return do_opivx_gvec_shift(s, a, tcg_gen_gvec_##SUF, \
                                       fns[s->sew]);             \
        }                                                        \
        return false;                                            \
    }

static bool zvbb_vv_check(DisasContext *s, arg_rmrr *a)
{
    return opivv_check(s, a) && s->cfg_ptr->ext_zvbb == true;
}

static bool zvbb_vx_check(DisasContext *s, arg_rmrr *a)
{
    return opivx_check(s, a) && s->cfg_ptr->ext_zvbb == true;
}

/* vrol.v[vx] */
GEN_OPIVV_GVEC_TRANS_CHECK(vrol_vv, rotlv, zvbb_vv_check)
GEN_OPIVX_GVEC_SHIFT_TRANS_CHECK(vrol_vx, rotls, zvbb_vx_check)

/* vror.v[vxi] */
GEN_OPIVV_GVEC_TRANS_CHECK(vror_vv, rotrv, zvbb_vv_check)
GEN_OPIVX_GVEC_SHIFT_TRANS_CHECK(vror_vx, rotrs, zvbb_vx_check)
GEN_OPIVI_GVEC_TRANS_CHECK(vror_vi, IMM_TRUNC_SEW, vror_vx, rotri, zvbb_vx_check)

#define GEN_OPIVX_GVEC_TRANS_CHECK(NAME, SUF, CHECK)                     \
    static bool trans_##NAME(DisasContext *s, arg_rmrr *a)               \
    {                                                                    \
        if (CHECK(s, a)) {                                               \
            static gen_helper_opivx *const fns[4] = {                    \
                gen_helper_##NAME##_b,                                   \
                gen_helper_##NAME##_h,                                   \
                gen_helper_##NAME##_w,                                   \
                gen_helper_##NAME##_d,                                   \
            };                                                           \
            return do_opivx_gvec(s, a, tcg_gen_gvec_##SUF, fns[s->sew]); \
        }                                                                \
        return false;                                                    \
    }

/* vandn.v[vx] */
GEN_OPIVV_GVEC_TRANS_CHECK(vandn_vv, andc, zvbb_vv_check)
GEN_OPIVX_GVEC_TRANS_CHECK(vandn_vx, andcs, zvbb_vx_check)

#define GEN_OPIV_TRANS(NAME, CHECK)                                        \
    static bool trans_##NAME(DisasContext *s, arg_rmr *a)                  \
    {                                                                      \
        if (CHECK(s, a)) {                                                 \
            uint32_t data = 0;                                             \
            static gen_helper_gvec_3_ptr *const fns[4] = {                 \
                gen_helper_##NAME##_b,                                     \
                gen_helper_##NAME##_h,                                     \
                gen_helper_##NAME##_w,                                     \
                gen_helper_##NAME##_d,                                     \
            };                                                             \
            TCGLabel *over = gen_new_label();                              \
            tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);     \
                                                                           \
            data = FIELD_DP32(data, VDATA, VM, a->vm);                     \
            data = FIELD_DP32(data, VDATA, LMUL, s->lmul);                 \
            data = FIELD_DP32(data, VDATA, VTA, s->vta);                   \
            data = FIELD_DP32(data, VDATA, VTA_ALL_1S, s->cfg_vta_all_1s); \
            data = FIELD_DP32(data, VDATA, VMA, s->vma);                   \
            tcg_gen_gvec_3_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, 0),         \
                               vreg_ofs(s, a->rs2), cpu_env,               \
                               s->cfg_ptr->vlen / 8, s->cfg_ptr->vlen / 8, \
                               data, fns[s->sew]);                         \
            mark_vs_dirty(s);                                              \
            gen_set_label(over);                                           \
            return true;                                                   \
        }                                                                  \
        return false;                                                      \
    }

static bool zvbb_opiv_check(DisasContext *s, arg_rmr *a)
{
    return s->cfg_ptr->ext_zvbb == true &&
           require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_ss(s, a->rd, a->rs2, a->vm);
}

GEN_OPIV_TRANS(vbrev8_v, zvbb_opiv_check)
GEN_OPIV_TRANS(vrev8_v, zvbb_opiv_check)
GEN_OPIV_TRANS(vbrev_v, zvbb_opiv_check)
GEN_OPIV_TRANS(vclz_v, zvbb_opiv_check)
GEN_OPIV_TRANS(vctz_v, zvbb_opiv_check)
GEN_OPIV_TRANS(vcpop_v, zvbb_opiv_check)

static bool vwsll_vv_check(DisasContext *s, arg_rmrr *a)
{
    return s->cfg_ptr->ext_zvbb && opivv_widen_check(s, a);
}

static bool vwsll_vx_check(DisasContext *s, arg_rmrr *a)
{
    return s->cfg_ptr->ext_zvbb && opivx_widen_check(s, a);
}

/* OPIVI without GVEC IR */
#define GEN_OPIVI_WIDEN_TRANS(NAME, IMM_MODE, OPIVX, CHECK)                  \
    static bool trans_##NAME(DisasContext *s, arg_rmrr *a)                   \
    {                                                                        \
        if (CHECK(s, a)) {                                                   \
            static gen_helper_opivx *const fns[3] = {                        \
                gen_helper_##OPIVX##_b,                                      \
                gen_helper_##OPIVX##_h,                                      \
                gen_helper_##OPIVX##_w,                                      \
            };                                                               \
            return opivi_trans(a->rd, a->rs1, a->rs2, a->vm, fns[s->sew], s, \
                               IMM_MODE);                                    \
        }                                                                    \
        return false;                                                        \
    }

GEN_OPIVV_WIDEN_TRANS(vwsll_vv, vwsll_vv_check)
GEN_OPIVX_WIDEN_TRANS(vwsll_vx, vwsll_vx_check)
GEN_OPIVI_WIDEN_TRANS(vwsll_vi, IMM_ZX, vwsll_vx, vwsll_vx_check)
