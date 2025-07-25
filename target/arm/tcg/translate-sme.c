/*
 * AArch64 SME translation
 *
 * Copyright (c) 2022 Linaro, Ltd
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

#include "qemu/osdep.h"
#include "translate.h"
#include "translate-a64.h"

/*
 * Include the generated decoder.
 */

#include "decode-sme.c.inc"

static bool sme2_zt0_enabled_check(DisasContext *s)
{
    if (!sme_za_enabled_check(s)) {
        return false;
    }
    if (s->zt0_excp_el) {
        gen_exception_insn_el(s, 0, EXCP_UDEF,
                              syn_smetrap(SME_ET_InaccessibleZT0, false),
                              s->zt0_excp_el);
        return false;
    }
    return true;
}

/* Resolve tile.size[rs+imm] to a host pointer. */
static TCGv_ptr get_tile_rowcol(DisasContext *s, int esz, int rs,
                                int tile, int imm, int div_len,
                                int vec_mod, bool vertical)
{
    int pos, len, offset;
    TCGv_i32 tmp;
    TCGv_ptr addr;

    /* Compute the final index, which is Rs+imm. */
    tmp = tcg_temp_new_i32();
    tcg_gen_trunc_tl_i32(tmp, cpu_reg(s, rs));
    /*
     * Round the vector index down to a multiple of vec_mod if necessary.
     * We do this before adding the offset, to handle cases like
     * MOVA (tile to vector, 2 registers) where we want to call this
     * several times in a loop with an increasing offset. We rely on
     * the instruction encodings always forcing the initial offset in
     * [rs + offset] to be a multiple of vec_mod. The pseudocode usually
     * does the round-down after adding the offset rather than before,
     * but MOVA is an exception.
     */
    if (vec_mod > 1) {
        tcg_gen_andc_i32(tmp, tmp, tcg_constant_i32(vec_mod - 1));
    }
    tcg_gen_addi_i32(tmp, tmp, imm);

    /* Prepare a power-of-two modulo via extraction of @len bits. */
    len = ctz32(streaming_vec_reg_size(s) / div_len) - esz;

    if (!len) {
        /*
         * SVL is 128 and the element size is 128. There is exactly
         * one 128x128 tile in the ZA storage, and so we calculate
         * (Rs + imm) MOD 1, which is always 0. We need to special case
         * this because TCG doesn't allow deposit ops with len 0.
         */
        tcg_gen_movi_i32(tmp, 0);
    } else if (vertical) {
        /*
         * Compute the byte offset of the index within the tile:
         *     (index % (svl / size)) * size
         *   = (index % (svl >> esz)) << esz
         * Perform the power-of-two modulo via extraction of the low @len bits.
         * Perform the multiply by shifting left by @pos bits.
         * Perform these operations simultaneously via deposit into zero.
         */
        pos = esz;
        tcg_gen_deposit_z_i32(tmp, tmp, pos, len);

        /*
         * For big-endian, adjust the indexed column byte offset within
         * the uint64_t host words that make up env->zarray[].
         */
        if (HOST_BIG_ENDIAN && esz < MO_64) {
            tcg_gen_xori_i32(tmp, tmp, 8 - (1 << esz));
        }
    } else {
        /*
         * Compute the byte offset of the index within the tile:
         *     (index % (svl / size)) * (size * sizeof(row))
         *   = (index % (svl >> esz)) << (esz + log2(sizeof(row)))
         */
        pos = esz + ctz32(sizeof(ARMVectorReg));
        tcg_gen_deposit_z_i32(tmp, tmp, pos, len);

        /* Row slices are always aligned and need no endian adjustment. */
    }

    /* The tile byte offset within env->zarray is the row. */
    offset = tile * sizeof(ARMVectorReg);

    /* Include the byte offset of zarray to make this relative to env. */
    offset += offsetof(CPUARMState, za_state.za);
    tcg_gen_addi_i32(tmp, tmp, offset);

    /* Add the byte offset to env to produce the final pointer. */
    addr = tcg_temp_new_ptr();
    tcg_gen_ext_i32_ptr(addr, tmp);
    tcg_gen_add_ptr(addr, addr, tcg_env);

    return addr;
}

/* Resolve ZArray[rs+imm] to a host pointer. */
static TCGv_ptr get_zarray(DisasContext *s, int rs, int imm,
                           int div_len, int vec_mod)
{
    /* ZA[n] equates to ZA0H.B[n]. */
    return get_tile_rowcol(s, MO_8, rs, 0, imm, div_len, vec_mod, false);
}

/*
 * Resolve tile.size[0] to a host pointer.
 * Used by e.g. outer product insns where we require the entire tile.
 */
static TCGv_ptr get_tile(DisasContext *s, int esz, int tile)
{
    TCGv_ptr addr = tcg_temp_new_ptr();
    int offset;

    offset = tile * sizeof(ARMVectorReg) + offsetof(CPUARMState, za_state.za);

    tcg_gen_addi_ptr(addr, tcg_env, offset);
    return addr;
}

static bool trans_ZERO(DisasContext *s, arg_ZERO *a)
{
    if (!dc_isar_feature(aa64_sme, s)) {
        return false;
    }
    if (sme_za_enabled_check(s)) {
        gen_helper_sme_zero(tcg_env, tcg_constant_i32(a->imm),
                            tcg_constant_i32(streaming_vec_reg_size(s)));
    }
    return true;
}

static bool trans_ZERO_zt0(DisasContext *s, arg_ZERO_zt0 *a)
{
    if (!dc_isar_feature(aa64_sme2, s)) {
        return false;
    }
    if (sme_enabled_check(s) && sme2_zt0_enabled_check(s)) {
        tcg_gen_gvec_dup_imm(MO_64, offsetof(CPUARMState, za_state.zt0),
                             sizeof_field(CPUARMState, za_state.zt0),
                             sizeof_field(CPUARMState, za_state.zt0), 0);
    }
    return true;
}

static bool trans_ZERO_za(DisasContext *s, arg_ZERO_za *a)
{
    if (!dc_isar_feature(aa64_sme2p1, s)) {
        return false;
    }
    if (sme_smza_enabled_check(s)) {
        int svl = streaming_vec_reg_size(s);
        int vstride = svl / a->ngrp;
        TCGv_ptr t_za = get_zarray(s, a->rv, a->off, a->ngrp, a->nvec);

        for (int r = 0; r < a->ngrp; ++r) {
            for (int i = 0; i < a->nvec; ++i) {
                int o_za = (r * vstride + i) * sizeof(ARMVectorReg);
                tcg_gen_gvec_dup_imm_var(MO_64, t_za, o_za, svl, svl, 0);
            }
        }
    }
    return true;
}

static bool do_mova_tile(DisasContext *s, arg_mova_p *a, bool to_vec)
{
    static gen_helper_gvec_4 * const h_fns[5] = {
        gen_helper_sve_sel_zpzz_b, gen_helper_sve_sel_zpzz_h,
        gen_helper_sve_sel_zpzz_s, gen_helper_sve_sel_zpzz_d,
        gen_helper_sve_sel_zpzz_q
    };
    static gen_helper_gvec_3 * const cz_fns[5] = {
        gen_helper_sme_mova_cz_b, gen_helper_sme_mova_cz_h,
        gen_helper_sme_mova_cz_s, gen_helper_sme_mova_cz_d,
        gen_helper_sme_mova_cz_q,
    };
    static gen_helper_gvec_3 * const zc_fns[5] = {
        gen_helper_sme_mova_zc_b, gen_helper_sme_mova_zc_h,
        gen_helper_sme_mova_zc_s, gen_helper_sme_mova_zc_d,
        gen_helper_sme_mova_zc_q,
    };

    TCGv_ptr t_za, t_zr, t_pg;
    TCGv_i32 t_desc;
    int svl;

    if (!sme_smza_enabled_check(s)) {
        return true;
    }

    t_za = get_tile_rowcol(s, a->esz, a->rs, a->za, a->off, 1, 0, a->v);
    t_zr = vec_full_reg_ptr(s, a->zr);
    t_pg = pred_full_reg_ptr(s, a->pg);

    svl = streaming_vec_reg_size(s);
    t_desc = tcg_constant_i32(simd_desc(svl, svl, 0));

    if (a->v) {
        /* Vertical slice -- use sme mova helpers. */
        if (to_vec) {
            zc_fns[a->esz](t_zr, t_za, t_pg, t_desc);
        } else {
            cz_fns[a->esz](t_za, t_zr, t_pg, t_desc);
        }
    } else {
        /* Horizontal slice -- reuse sve sel helpers. */
        if (to_vec) {
            h_fns[a->esz](t_zr, t_za, t_zr, t_pg, t_desc);
        } else {
            h_fns[a->esz](t_za, t_zr, t_za, t_pg, t_desc);
        }
    }
    return true;
}

TRANS_FEAT(MOVA_tz, aa64_sme, do_mova_tile, a, false)
TRANS_FEAT(MOVA_zt, aa64_sme, do_mova_tile, a, true)

static bool do_mova_tile_n(DisasContext *s, arg_mova_t *a, int n,
                           bool to_vec, bool zero)
{
    static gen_helper_gvec_2 * const cz_fns[] = {
        gen_helper_sme2_mova_cz_b, gen_helper_sme2_mova_cz_h,
        gen_helper_sme2_mova_cz_s, gen_helper_sme2_mova_cz_d,
    };
    static gen_helper_gvec_2 * const zc_fns[] = {
        gen_helper_sme2_mova_zc_b, gen_helper_sme2_mova_zc_h,
        gen_helper_sme2_mova_zc_s, gen_helper_sme2_mova_zc_d,
    };
    static gen_helper_gvec_2 * const zc_z_fns[] = {
        gen_helper_sme2p1_movaz_zc_b, gen_helper_sme2p1_movaz_zc_h,
        gen_helper_sme2p1_movaz_zc_s, gen_helper_sme2p1_movaz_zc_d,
        gen_helper_sme2p1_movaz_zc_q,
    };
    TCGv_ptr t_za;
    int svl, bytes_per_op = n << a->esz;

    /*
     * The MaxImplementedSVL check happens in the decode pseudocode,
     * before the SM+ZA enabled check in the operation pseudocode.
     * This will (currently) only fail for NREG=4, ESZ=MO_64.
     */
    if (s->max_svl < bytes_per_op) {
        unallocated_encoding(s);
        return true;
    }

    assert(a->esz <= MO_64 + zero);

    if (!sme_smza_enabled_check(s)) {
        return true;
    }

    svl = streaming_vec_reg_size(s);

    /*
     * The CurrentVL check happens in the operation pseudocode,
     * after the SM+ZA enabled check.
     */
    if (svl < bytes_per_op) {
        unallocated_encoding(s);
        return true;
    }

    if (a->v) {
        TCGv_i32 t_desc = tcg_constant_i32(simd_desc(svl, svl, 0));

        for (int i = 0; i < n; ++i) {
            TCGv_ptr t_zr = vec_full_reg_ptr(s, a->zr * n + i);
            t_za = get_tile_rowcol(s, a->esz, a->rs, a->za,
                                   a->off * n + i, 1, n, a->v);
            if (zero) {
                zc_z_fns[a->esz](t_zr, t_za, t_desc);
            } else if (to_vec) {
                zc_fns[a->esz](t_zr, t_za, t_desc);
            } else {
                cz_fns[a->esz](t_za, t_zr, t_desc);
            }
        }
    } else {
        for (int i = 0; i < n; ++i) {
            int o_zr = vec_full_reg_offset(s, a->zr * n + i);
            t_za = get_tile_rowcol(s, a->esz, a->rs, a->za,
                                   a->off * n + i, 1, n, a->v);
            if (to_vec) {
                tcg_gen_gvec_mov_var(MO_8, tcg_env, o_zr, t_za, 0, svl, svl);
                if (zero) {
                    tcg_gen_gvec_dup_imm_var(MO_8, t_za, 0, svl, svl, 0);
                }
            } else {
                tcg_gen_gvec_mov_var(MO_8, t_za, 0, tcg_env, o_zr, svl, svl);
            }
        }
    }
    return true;
}

TRANS_FEAT(MOVA_tz2, aa64_sme2, do_mova_tile_n, a, 2, false, false)
TRANS_FEAT(MOVA_tz4, aa64_sme2, do_mova_tile_n, a, 4, false, false)
TRANS_FEAT(MOVA_zt2, aa64_sme2, do_mova_tile_n, a, 2, true, false)
TRANS_FEAT(MOVA_zt4, aa64_sme2, do_mova_tile_n, a, 4, true, false)

TRANS_FEAT(MOVAZ_zt, aa64_sme2p1, do_mova_tile_n, a, 1, true, true)
TRANS_FEAT(MOVAZ_zt2, aa64_sme2p1, do_mova_tile_n, a, 2, true, true)
TRANS_FEAT(MOVAZ_zt4, aa64_sme2p1, do_mova_tile_n, a, 4, true, true)

static bool do_mova_array_n(DisasContext *s, arg_mova_a *a, int n,
                            bool to_vec, bool zero)
{
    TCGv_ptr t_za;
    int svl;

    if (!sme_smza_enabled_check(s)) {
        return true;
    }

    svl = streaming_vec_reg_size(s);
    t_za = get_zarray(s, a->rv, a->off, n, 0);

    for (int i = 0; i < n; ++i) {
        int o_za = (svl / n * sizeof(ARMVectorReg)) * i;
        int o_zr = vec_full_reg_offset(s, a->zr * n + i);

        if (to_vec) {
            tcg_gen_gvec_mov_var(MO_8, tcg_env, o_zr, t_za, o_za, svl, svl);
            if (zero) {
                tcg_gen_gvec_dup_imm_var(MO_8, t_za, o_za, svl, svl, 0);
            }
        } else {
            tcg_gen_gvec_mov_var(MO_8, t_za, o_za, tcg_env, o_zr, svl, svl);
        }
    }
    return true;
}

TRANS_FEAT(MOVA_az2, aa64_sme2, do_mova_array_n, a, 2, false, false)
TRANS_FEAT(MOVA_az4, aa64_sme2, do_mova_array_n, a, 4, false, false)
TRANS_FEAT(MOVA_za2, aa64_sme2, do_mova_array_n, a, 2, true, false)
TRANS_FEAT(MOVA_za4, aa64_sme2, do_mova_array_n, a, 4, true, false)

TRANS_FEAT(MOVAZ_za2, aa64_sme2p1, do_mova_array_n, a, 2, true, true)
TRANS_FEAT(MOVAZ_za4, aa64_sme2p1, do_mova_array_n, a, 4, true, true)

static bool do_movt(DisasContext *s, arg_MOVT_rzt *a,
                    void (*func)(TCGv_i64, TCGv_ptr, tcg_target_long))
{
    if (sme2_zt0_enabled_check(s)) {
        func(cpu_reg(s, a->rt), tcg_env,
             offsetof(CPUARMState, za_state.zt0) + a->off * 8);
    }
    return true;
}

TRANS_FEAT(MOVT_rzt, aa64_sme2, do_movt, a, tcg_gen_ld_i64)
TRANS_FEAT(MOVT_ztr, aa64_sme2, do_movt, a, tcg_gen_st_i64)

static bool trans_LDST1(DisasContext *s, arg_LDST1 *a)
{
    typedef void GenLdSt1(TCGv_env, TCGv_ptr, TCGv_ptr, TCGv, TCGv_i64);

    /*
     * Indexed by [esz][be][v][mte][st], which is (except for load/store)
     * also the order in which the elements appear in the function names,
     * and so how we must concatenate the pieces.
     */

#define FN_LS(F)     { gen_helper_sme_ld1##F, gen_helper_sme_st1##F }
#define FN_MTE(F)    { FN_LS(F), FN_LS(F##_mte) }
#define FN_HV(F)     { FN_MTE(F##_h), FN_MTE(F##_v) }
#define FN_END(L, B) { FN_HV(L), FN_HV(B) }

    static GenLdSt1 * const fns[5][2][2][2][2] = {
        FN_END(b, b),
        FN_END(h_le, h_be),
        FN_END(s_le, s_be),
        FN_END(d_le, d_be),
        FN_END(q_le, q_be),
    };

#undef FN_LS
#undef FN_MTE
#undef FN_HV
#undef FN_END

    TCGv_ptr t_za, t_pg;
    TCGv_i64 addr;
    uint64_t desc;
    bool be = s->be_data == MO_BE;
    bool mte = s->mte_active[0];

    if (!dc_isar_feature(aa64_sme, s)) {
        return false;
    }
    if (!sme_smza_enabled_check(s)) {
        return true;
    }

    t_za = get_tile_rowcol(s, a->esz, a->rs, a->za, a->off, 1, 0, a->v);
    t_pg = pred_full_reg_ptr(s, a->pg);
    addr = tcg_temp_new_i64();

    tcg_gen_shli_i64(addr, cpu_reg(s, a->rm), a->esz);
    tcg_gen_add_i64(addr, addr, cpu_reg_sp(s, a->rn));

    if (!mte) {
        addr = clean_data_tbi(s, addr);
    }

    desc = make_svemte_desc(s, streaming_vec_reg_size(s), 1, a->esz, a->st, 0);

    fns[a->esz][be][a->v][mte][a->st](tcg_env, t_za, t_pg, addr,
                                      tcg_constant_i64(desc));
    return true;
}

typedef void GenLdStR(DisasContext *, TCGv_ptr, int, int, int, int, MemOp);

static bool do_ldst_r(DisasContext *s, arg_ldstr *a, GenLdStR *fn)
{
    if (sme_za_enabled_check(s)) {
        int svl = streaming_vec_reg_size(s);
        int imm = a->imm;
        TCGv_ptr base = get_zarray(s, a->rv, imm, 1, 0);

        fn(s, base, 0, svl, a->rn, imm * svl,
           s->align_mem ? MO_ALIGN_16 : MO_UNALN);
    }
    return true;
}

TRANS_FEAT(LDR, aa64_sme, do_ldst_r, a, gen_sve_ldr)
TRANS_FEAT(STR, aa64_sme, do_ldst_r, a, gen_sve_str)

static bool do_ldst_zt0(DisasContext *s, arg_ldstzt0 *a, GenLdStR *fn)
{
    if (sme2_zt0_enabled_check(s)) {
        fn(s, tcg_env, offsetof(CPUARMState, za_state.zt0),
           sizeof_field(CPUARMState, za_state.zt0), a->rn, 0,
           s->align_mem ? MO_ALIGN_16 : MO_UNALN);
    }
    return true;
}

TRANS_FEAT(LDR_zt0, aa64_sme2, do_ldst_zt0, a, gen_sve_ldr)
TRANS_FEAT(STR_zt0, aa64_sme2, do_ldst_zt0, a, gen_sve_str)

static bool do_adda(DisasContext *s, arg_adda *a, MemOp esz,
                    gen_helper_gvec_4 *fn)
{
    int svl = streaming_vec_reg_size(s);
    uint32_t desc = simd_desc(svl, svl, 0);
    TCGv_ptr za, zn, pn, pm;

    if (!sme_smza_enabled_check(s)) {
        return true;
    }

    za = get_tile(s, esz, a->zad);
    zn = vec_full_reg_ptr(s, a->zn);
    pn = pred_full_reg_ptr(s, a->pn);
    pm = pred_full_reg_ptr(s, a->pm);

    fn(za, zn, pn, pm, tcg_constant_i32(desc));
    return true;
}

TRANS_FEAT(ADDHA_s, aa64_sme, do_adda, a, MO_32, gen_helper_sme_addha_s)
TRANS_FEAT(ADDVA_s, aa64_sme, do_adda, a, MO_32, gen_helper_sme_addva_s)
TRANS_FEAT(ADDHA_d, aa64_sme_i16i64, do_adda, a, MO_64, gen_helper_sme_addha_d)
TRANS_FEAT(ADDVA_d, aa64_sme_i16i64, do_adda, a, MO_64, gen_helper_sme_addva_d)

static bool do_outprod(DisasContext *s, arg_op *a, MemOp esz,
                       gen_helper_gvec_5 *fn)
{
    int svl = streaming_vec_reg_size(s);
    uint32_t desc = simd_desc(svl, svl, a->sub);
    TCGv_ptr za, zn, zm, pn, pm;

    if (!sme_smza_enabled_check(s)) {
        return true;
    }

    za = get_tile(s, esz, a->zad);
    zn = vec_full_reg_ptr(s, a->zn);
    zm = vec_full_reg_ptr(s, a->zm);
    pn = pred_full_reg_ptr(s, a->pn);
    pm = pred_full_reg_ptr(s, a->pm);

    fn(za, zn, zm, pn, pm, tcg_constant_i32(desc));
    return true;
}

static bool do_outprod_fpst(DisasContext *s, arg_op *a, MemOp esz,
                            ARMFPStatusFlavour e_fpst,
                            gen_helper_gvec_5_ptr *fn)
{
    int svl = streaming_vec_reg_size(s);
    uint32_t desc = simd_desc(svl, svl, 0);
    TCGv_ptr za, zn, zm, pn, pm, fpst;

    if (!sme_smza_enabled_check(s)) {
        return true;
    }

    za = get_tile(s, esz, a->zad);
    zn = vec_full_reg_ptr(s, a->zn);
    zm = vec_full_reg_ptr(s, a->zm);
    pn = pred_full_reg_ptr(s, a->pn);
    pm = pred_full_reg_ptr(s, a->pm);
    fpst = fpstatus_ptr(e_fpst);

    fn(za, zn, zm, pn, pm, fpst, tcg_constant_i32(desc));
    return true;
}

static bool do_outprod_env(DisasContext *s, arg_op *a, MemOp esz,
                           gen_helper_gvec_5_ptr *fn)
{
    int svl = streaming_vec_reg_size(s);
    uint32_t desc = simd_desc(svl, svl, 0);
    TCGv_ptr za, zn, zm, pn, pm;

    if (!sme_smza_enabled_check(s)) {
        return true;
    }

    za = get_tile(s, esz, a->zad);
    zn = vec_full_reg_ptr(s, a->zn);
    zm = vec_full_reg_ptr(s, a->zm);
    pn = pred_full_reg_ptr(s, a->pn);
    pm = pred_full_reg_ptr(s, a->pm);

    fn(za, zn, zm, pn, pm, tcg_env, tcg_constant_i32(desc));
    return true;
}

TRANS_FEAT(FMOPA_w_h, aa64_sme, do_outprod_env, a, MO_32,
           !a->sub ? gen_helper_sme_fmopa_w_h
           : !s->fpcr_ah ? gen_helper_sme_fmops_w_h
           : gen_helper_sme_ah_fmops_w_h)
TRANS_FEAT(FMOPA_h, aa64_sme_f16f16, do_outprod_fpst, a, MO_16, FPST_ZA_F16,
           !a->sub ? gen_helper_sme_fmopa_h
           : !s->fpcr_ah ? gen_helper_sme_fmops_h
           : gen_helper_sme_ah_fmops_h)
TRANS_FEAT(FMOPA_s, aa64_sme, do_outprod_fpst, a, MO_32, FPST_ZA,
           !a->sub ? gen_helper_sme_fmopa_s
           : !s->fpcr_ah ? gen_helper_sme_fmops_s
           : gen_helper_sme_ah_fmops_s)
TRANS_FEAT(FMOPA_d, aa64_sme_f64f64, do_outprod_fpst, a, MO_64, FPST_ZA,
           !a->sub ? gen_helper_sme_fmopa_d
           : !s->fpcr_ah ? gen_helper_sme_fmops_d
           : gen_helper_sme_ah_fmops_d)

TRANS_FEAT(BFMOPA, aa64_sme_b16b16, do_outprod_fpst, a, MO_16, FPST_ZA,
           !a->sub ? gen_helper_sme_bfmopa
           : !s->fpcr_ah ? gen_helper_sme_bfmops
           : gen_helper_sme_ah_bfmops)

TRANS_FEAT(BFMOPA_w, aa64_sme, do_outprod_env, a, MO_32,
           !a->sub ? gen_helper_sme_bfmopa_w
           : !s->fpcr_ah ? gen_helper_sme_bfmops_w
           : gen_helper_sme_ah_bfmops_w)

TRANS_FEAT(SMOPA_s, aa64_sme, do_outprod, a, MO_32, gen_helper_sme_smopa_s)
TRANS_FEAT(UMOPA_s, aa64_sme, do_outprod, a, MO_32, gen_helper_sme_umopa_s)
TRANS_FEAT(SUMOPA_s, aa64_sme, do_outprod, a, MO_32, gen_helper_sme_sumopa_s)
TRANS_FEAT(USMOPA_s, aa64_sme, do_outprod, a, MO_32, gen_helper_sme_usmopa_s)

TRANS_FEAT(SMOPA_d, aa64_sme_i16i64, do_outprod, a, MO_64, gen_helper_sme_smopa_d)
TRANS_FEAT(UMOPA_d, aa64_sme_i16i64, do_outprod, a, MO_64, gen_helper_sme_umopa_d)
TRANS_FEAT(SUMOPA_d, aa64_sme_i16i64, do_outprod, a, MO_64, gen_helper_sme_sumopa_d)
TRANS_FEAT(USMOPA_d, aa64_sme_i16i64, do_outprod, a, MO_64, gen_helper_sme_usmopa_d)

TRANS_FEAT(BMOPA, aa64_sme2, do_outprod, a, MO_32, gen_helper_sme2_bmopa_s)
TRANS_FEAT(SMOPA2_s, aa64_sme2, do_outprod, a, MO_32, gen_helper_sme2_smopa2_s)
TRANS_FEAT(UMOPA2_s, aa64_sme2, do_outprod, a, MO_32, gen_helper_sme2_umopa2_s)

static bool do_z2z_n1(DisasContext *s, arg_z2z_en *a, GVecGen3Fn *fn)
{
    int esz, dn, vsz, mofs, n;
    bool overlap = false;

    if (!sme_sm_enabled_check(s)) {
        return true;
    }

    esz = a->esz;
    n = a->n;
    dn = a->zdn;
    mofs = vec_full_reg_offset(s, a->zm);
    vsz = streaming_vec_reg_size(s);

    for (int i = 0; i < n; i++) {
        int dofs = vec_full_reg_offset(s, dn + i);
        if (dofs == mofs) {
            overlap = true;
        } else {
            fn(esz, dofs, dofs, mofs, vsz, vsz);
        }
    }
    if (overlap) {
        fn(esz, mofs, mofs, mofs, vsz, vsz);
    }
    return true;
}

static void gen_sme2_srshl(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                           uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static gen_helper_gvec_3 * const fns[] = {
        gen_helper_gvec_srshl_b, gen_helper_sme2_srshl_h,
        gen_helper_sme2_srshl_s, gen_helper_sme2_srshl_d,
    };
    tcg_debug_assert(vece <= MO_64);
    tcg_gen_gvec_3_ool(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, 0, fns[vece]);
}

static void gen_sme2_urshl(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                           uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static gen_helper_gvec_3 * const fns[] = {
        gen_helper_gvec_urshl_b, gen_helper_sme2_urshl_h,
        gen_helper_sme2_urshl_s, gen_helper_sme2_urshl_d,
    };
    tcg_debug_assert(vece <= MO_64);
    tcg_gen_gvec_3_ool(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, 0, fns[vece]);
}

TRANS_FEAT(ADD_n1, aa64_sme2, do_z2z_n1, a, tcg_gen_gvec_add)
TRANS_FEAT(SMAX_n1, aa64_sme2, do_z2z_n1, a, tcg_gen_gvec_smax)
TRANS_FEAT(SMIN_n1, aa64_sme2, do_z2z_n1, a, tcg_gen_gvec_smin)
TRANS_FEAT(UMAX_n1, aa64_sme2, do_z2z_n1, a, tcg_gen_gvec_umax)
TRANS_FEAT(UMIN_n1, aa64_sme2, do_z2z_n1, a, tcg_gen_gvec_umin)
TRANS_FEAT(SRSHL_n1, aa64_sme2, do_z2z_n1, a, gen_sme2_srshl)
TRANS_FEAT(URSHL_n1, aa64_sme2, do_z2z_n1, a, gen_sme2_urshl)
TRANS_FEAT(SQDMULH_n1, aa64_sme2, do_z2z_n1, a, gen_gvec_sve2_sqdmulh)

static bool do_z2z_nn(DisasContext *s, arg_z2z_en *a, GVecGen3Fn *fn)
{
    int esz, dn, dm, vsz, n;

    if (!sme_sm_enabled_check(s)) {
        return true;
    }

    esz = a->esz;
    n = a->n;
    dn = a->zdn;
    dm = a->zm;
    vsz = streaming_vec_reg_size(s);

    for (int i = 0; i < n; i++) {
        int dofs = vec_full_reg_offset(s, dn + i);
        int mofs = vec_full_reg_offset(s, dm + i);

        fn(esz, dofs, dofs, mofs, vsz, vsz);
    }
    return true;
}

TRANS_FEAT(SMAX_nn, aa64_sme2, do_z2z_nn, a, tcg_gen_gvec_smax)
TRANS_FEAT(SMIN_nn, aa64_sme2, do_z2z_nn, a, tcg_gen_gvec_smin)
TRANS_FEAT(UMAX_nn, aa64_sme2, do_z2z_nn, a, tcg_gen_gvec_umax)
TRANS_FEAT(UMIN_nn, aa64_sme2, do_z2z_nn, a, tcg_gen_gvec_umin)
TRANS_FEAT(SRSHL_nn, aa64_sme2, do_z2z_nn, a, gen_sme2_srshl)
TRANS_FEAT(URSHL_nn, aa64_sme2, do_z2z_nn, a, gen_sme2_urshl)
TRANS_FEAT(SQDMULH_nn, aa64_sme2, do_z2z_nn, a, gen_gvec_sve2_sqdmulh)

static bool do_z2z_n1_fpst(DisasContext *s, arg_z2z_en *a,
                           gen_helper_gvec_3_ptr * const fns[4])
{
    int esz = a->esz, n, dn, vsz, mofs;
    bool overlap = false;
    gen_helper_gvec_3_ptr *fn;
    TCGv_ptr fpst;

    /* These insns use MO_8 to encode BFloat16. */
    if (esz == MO_8 && !dc_isar_feature(aa64_sme_b16b16, s)) {
        return false;
    }
    if (!sme_sm_enabled_check(s)) {
        return true;
    }

    fpst = fpstatus_ptr(esz == MO_16 ? FPST_A64_F16 : FPST_A64);
    fn = fns[esz];
    n = a->n;
    dn = a->zdn;
    mofs = vec_full_reg_offset(s, a->zm);
    vsz = streaming_vec_reg_size(s);

    for (int i = 0; i < n; i++) {
        int dofs = vec_full_reg_offset(s, dn + i);
        if (dofs == mofs) {
            overlap = true;
        } else {
            tcg_gen_gvec_3_ptr(dofs, dofs, mofs, fpst, vsz, vsz, 0, fn);
        }
    }
    if (overlap) {
        tcg_gen_gvec_3_ptr(mofs, mofs, mofs, fpst, vsz, vsz, 0, fn);
    }
    return true;
}

static bool do_z2z_nn_fpst(DisasContext *s, arg_z2z_en *a,
                           gen_helper_gvec_3_ptr * const fns[4])
{
    int esz = a->esz, n, dn, dm, vsz;
    gen_helper_gvec_3_ptr *fn;
    TCGv_ptr fpst;

    if (esz == MO_8 && !dc_isar_feature(aa64_sme_b16b16, s)) {
        return false;
    }
    if (!sme_sm_enabled_check(s)) {
        return true;
    }

    fpst = fpstatus_ptr(esz == MO_16 ? FPST_A64_F16 : FPST_A64);
    fn = fns[esz];
    n = a->n;
    dn = a->zdn;
    dm = a->zm;
    vsz = streaming_vec_reg_size(s);

    for (int i = 0; i < n; i++) {
        int dofs = vec_full_reg_offset(s, dn + i);
        int mofs = vec_full_reg_offset(s, dm + i);

        tcg_gen_gvec_3_ptr(dofs, dofs, mofs, fpst, vsz, vsz, 0, fn);
    }
    return true;
}

static gen_helper_gvec_3_ptr * const f_vector_fmax[2][4] = {
    { gen_helper_gvec_fmax_b16,
      gen_helper_gvec_fmax_h,
      gen_helper_gvec_fmax_s,
      gen_helper_gvec_fmax_d },
    { gen_helper_gvec_ah_fmax_b16,
      gen_helper_gvec_ah_fmax_h,
      gen_helper_gvec_ah_fmax_s,
      gen_helper_gvec_ah_fmax_d },
};
TRANS_FEAT(FMAX_n1, aa64_sme2, do_z2z_n1_fpst, a, f_vector_fmax[s->fpcr_ah])
TRANS_FEAT(FMAX_nn, aa64_sme2, do_z2z_nn_fpst, a, f_vector_fmax[s->fpcr_ah])

static gen_helper_gvec_3_ptr * const f_vector_fmin[2][4] = {
    { gen_helper_gvec_fmin_b16,
      gen_helper_gvec_fmin_h,
      gen_helper_gvec_fmin_s,
      gen_helper_gvec_fmin_d },
    { gen_helper_gvec_ah_fmin_b16,
      gen_helper_gvec_ah_fmin_h,
      gen_helper_gvec_ah_fmin_s,
      gen_helper_gvec_ah_fmin_d },
};
TRANS_FEAT(FMIN_n1, aa64_sme2, do_z2z_n1_fpst, a, f_vector_fmin[s->fpcr_ah])
TRANS_FEAT(FMIN_nn, aa64_sme2, do_z2z_nn_fpst, a, f_vector_fmin[s->fpcr_ah])

static gen_helper_gvec_3_ptr * const f_vector_fmaxnm[4] = {
    gen_helper_gvec_fmaxnum_b16,
    gen_helper_gvec_fmaxnum_h,
    gen_helper_gvec_fmaxnum_s,
    gen_helper_gvec_fmaxnum_d,
};
TRANS_FEAT(FMAXNM_n1, aa64_sme2, do_z2z_n1_fpst, a, f_vector_fmaxnm)
TRANS_FEAT(FMAXNM_nn, aa64_sme2, do_z2z_nn_fpst, a, f_vector_fmaxnm)

static gen_helper_gvec_3_ptr * const f_vector_fminnm[4] = {
    gen_helper_gvec_fminnum_b16,
    gen_helper_gvec_fminnum_h,
    gen_helper_gvec_fminnum_s,
    gen_helper_gvec_fminnum_d,
};
TRANS_FEAT(FMINNM_n1, aa64_sme2, do_z2z_n1_fpst, a, f_vector_fminnm)
TRANS_FEAT(FMINNM_nn, aa64_sme2, do_z2z_nn_fpst, a, f_vector_fminnm)

/* Add/Sub vector Z[m] to each Z[n*N] with result in ZA[d*N]. */
static bool do_azz_n1(DisasContext *s, arg_azz_n *a, int esz,
                      GVecGen3FnVar *fn)
{
    TCGv_ptr t_za;
    int svl, n, o_zm;

    if (!sme_smza_enabled_check(s)) {
        return true;
    }

    n = a->n;
    t_za = get_zarray(s, a->rv, a->off, n, 0);
    o_zm = vec_full_reg_offset(s, a->zm);
    svl = streaming_vec_reg_size(s);

    for (int i = 0; i < n; ++i) {
        int o_za = (svl / n * sizeof(ARMVectorReg)) * i;
        int o_zn = vec_full_reg_offset(s, (a->zn + i) % 32);

        fn(esz, t_za, o_za, tcg_env, o_zn, tcg_env, o_zm, svl, svl);
    }
    return true;
}

TRANS_FEAT(ADD_azz_n1_s, aa64_sme2, do_azz_n1, a, MO_32, tcg_gen_gvec_add_var)
TRANS_FEAT(SUB_azz_n1_s, aa64_sme2, do_azz_n1, a, MO_32, tcg_gen_gvec_sub_var)
TRANS_FEAT(ADD_azz_n1_d, aa64_sme2_i16i64, do_azz_n1, a, MO_64, tcg_gen_gvec_add_var)
TRANS_FEAT(SUB_azz_n1_d, aa64_sme2_i16i64, do_azz_n1, a, MO_64, tcg_gen_gvec_sub_var)

/* Add/Sub each vector Z[m*N] to each Z[n*N] with result in ZA[d*N]. */
static bool do_azz_nn(DisasContext *s, arg_azz_n *a, int esz,
                      GVecGen3FnVar *fn)
{
    TCGv_ptr t_za;
    int svl, n;

    if (!sme_smza_enabled_check(s)) {
        return true;
    }

    n = a->n;
    t_za = get_zarray(s, a->rv, a->off, n, 1);
    svl = streaming_vec_reg_size(s);

    for (int i = 0; i < n; ++i) {
        int o_za = (svl / n * sizeof(ARMVectorReg)) * i;
        int o_zn = vec_full_reg_offset(s, a->zn + i);
        int o_zm = vec_full_reg_offset(s, a->zm + i);

        fn(esz, t_za, o_za, tcg_env, o_zn, tcg_env, o_zm, svl, svl);
    }
    return true;
}

TRANS_FEAT(ADD_azz_nn_s, aa64_sme2, do_azz_nn, a, MO_32, tcg_gen_gvec_add_var)
TRANS_FEAT(SUB_azz_nn_s, aa64_sme2, do_azz_nn, a, MO_32, tcg_gen_gvec_sub_var)
TRANS_FEAT(ADD_azz_nn_d, aa64_sme2_i16i64, do_azz_nn, a, MO_64, tcg_gen_gvec_add_var)
TRANS_FEAT(SUB_azz_nn_d, aa64_sme2_i16i64, do_azz_nn, a, MO_64, tcg_gen_gvec_sub_var)

/* Add/Sub each ZA[d*N] += Z[m*N] */
static bool do_aaz(DisasContext *s, arg_az_n *a, int esz, GVecGen3FnVar *fn)
{
    TCGv_ptr t_za;
    int svl, n;

    if (!sme_smza_enabled_check(s)) {
        return true;
    }

    n = a->n;
    t_za = get_zarray(s, a->rv, a->off, n, 0);
    svl = streaming_vec_reg_size(s);

    for (int i = 0; i < n; ++i) {
        int o_za = (svl / n * sizeof(ARMVectorReg)) * i;
        int o_zm = vec_full_reg_offset(s, a->zm + i);

        fn(esz, t_za, o_za, t_za, o_za, tcg_env, o_zm, svl, svl);
    }
    return true;
}

TRANS_FEAT(ADD_aaz_s, aa64_sme2, do_aaz, a, MO_32, tcg_gen_gvec_add_var)
TRANS_FEAT(SUB_aaz_s, aa64_sme2, do_aaz, a, MO_32, tcg_gen_gvec_sub_var)
TRANS_FEAT(ADD_aaz_d, aa64_sme2_i16i64, do_aaz, a, MO_64, tcg_gen_gvec_add_var)
TRANS_FEAT(SUB_aaz_d, aa64_sme2_i16i64, do_aaz, a, MO_64, tcg_gen_gvec_sub_var)

/*
 * Expand array multi-vector single (n1), array multi-vector (nn),
 * and array multi-vector indexed (nx), for floating-point accumulate.
 *   multi: true for nn, false for n1.
 *   fpst: >= 0 to set ptr argument for FPST_*, < 0 for ENV.
 *   data: stuff for simd_data, including any index.
 */
#define FPST_ENV  -1

static bool do_azz_fp(DisasContext *s, int nreg, int nsel,
                      int rv, int off, int zn, int zm,
                      int data, int shsel, bool multi, int fpst,
                      gen_helper_gvec_3_ptr *fn)
{
    if (sme_smza_enabled_check(s)) {
        int svl = streaming_vec_reg_size(s);
        int vstride = svl / nreg;
        TCGv_ptr t_za = get_zarray(s, rv, off, nreg, nsel);
        TCGv_ptr t, ptr;

        if (fpst >= 0) {
            ptr = fpstatus_ptr(fpst);
        } else {
            ptr = tcg_env;
        }
        t = tcg_temp_new_ptr();

        for (int r = 0; r < nreg; ++r) {
            TCGv_ptr t_zn = vec_full_reg_ptr(s, zn);
            TCGv_ptr t_zm = vec_full_reg_ptr(s, zm);

            for (int i = 0; i < nsel; ++i) {
                int o_za = (r * vstride + i) * sizeof(ARMVectorReg);
                int desc = simd_desc(svl, svl, data | (i << shsel));

                tcg_gen_addi_ptr(t, t_za, o_za);
                fn(t, t_zn, t_zm, ptr, tcg_constant_i32(desc));
            }

            /*
             * For multiple-and-single vectors, Zn may wrap.
             * For multiple vectors, both Zn and Zm are aligned.
             */
            zn = (zn + 1) % 32;
            zm += multi;
        }
    }
    return true;
}

static bool do_azz_acc_fp(DisasContext *s, int nreg, int nsel,
                          int rv, int off, int zn, int zm,
                          int data, int shsel, bool multi, int fpst,
                          gen_helper_gvec_4_ptr *fn)
{
    if (sme_smza_enabled_check(s)) {
        int svl = streaming_vec_reg_size(s);
        int vstride = svl / nreg;
        TCGv_ptr t_za = get_zarray(s, rv, off, nreg, nsel);
        TCGv_ptr t, ptr;

        if (fpst >= 0) {
            ptr = fpstatus_ptr(fpst);
        } else {
            ptr = tcg_env;
        }
        t = tcg_temp_new_ptr();

        for (int r = 0; r < nreg; ++r) {
            TCGv_ptr t_zn = vec_full_reg_ptr(s, zn);
            TCGv_ptr t_zm = vec_full_reg_ptr(s, zm);

            for (int i = 0; i < nsel; ++i) {
                int o_za = (r * vstride + i) * sizeof(ARMVectorReg);
                int desc = simd_desc(svl, svl, data | (i << shsel));

                tcg_gen_addi_ptr(t, t_za, o_za);
                fn(t, t_zn, t_zm, t, ptr, tcg_constant_i32(desc));
            }

            /*
             * For multiple-and-single vectors, Zn may wrap.
             * For multiple vectors, both Zn and Zm are aligned.
             */
            zn = (zn + 1) % 32;
            zm += multi;
        }
    }
    return true;
}

static bool do_fmlal(DisasContext *s, arg_azz_n *a, bool sub, bool multi)
{
    return do_azz_acc_fp(s, a->n, 2, a->rv, a->off, a->zn, a->zm,
                         (1 << 2) | sub, 1,
                         multi, FPST_ENV, gen_helper_sve2_fmlal_zzzw_s);
}

TRANS_FEAT(FMLAL_n1, aa64_sme2, do_fmlal, a, false, false)
TRANS_FEAT(FMLSL_n1, aa64_sme2, do_fmlal, a, true, false)
TRANS_FEAT(FMLAL_nn, aa64_sme2, do_fmlal, a, false, true)
TRANS_FEAT(FMLSL_nn, aa64_sme2, do_fmlal, a, true, true)

static bool do_fmlal_nx(DisasContext *s, arg_azx_n *a, bool sub)
{
    return do_azz_acc_fp(s, a->n, 2, a->rv, a->off, a->zn, a->zm,
                         (a->idx << 3) | (1 << 2) | sub, 1,
                         false, FPST_ENV, gen_helper_sve2_fmlal_zzxw_s);
}

TRANS_FEAT(FMLAL_nx, aa64_sme2, do_fmlal_nx, a, false)
TRANS_FEAT(FMLSL_nx, aa64_sme2, do_fmlal_nx, a, true)

static bool do_bfmlal(DisasContext *s, arg_azz_n *a, bool sub, bool multi)
{
    return do_azz_acc_fp(s, a->n, 2, a->rv, a->off, a->zn, a->zm,
                         0, 0, multi, FPST_ZA,
                         (!sub ? gen_helper_gvec_bfmlal
                          : s->fpcr_ah ? gen_helper_gvec_ah_bfmlsl
                          : gen_helper_gvec_bfmlsl));
}

TRANS_FEAT(BFMLAL_n1, aa64_sme2, do_bfmlal, a, false, false)
TRANS_FEAT(BFMLSL_n1, aa64_sme2, do_bfmlal, a, true, false)
TRANS_FEAT(BFMLAL_nn, aa64_sme2, do_bfmlal, a, false, true)
TRANS_FEAT(BFMLSL_nn, aa64_sme2, do_bfmlal, a, true, true)

static bool do_bfmlal_nx(DisasContext *s, arg_azx_n *a, bool sub)
{
    return do_azz_acc_fp(s, a->n, 2, a->rv, a->off, a->zn, a->zm,
                         a->idx << 1, 0, false, FPST_ZA,
                         !sub ? gen_helper_gvec_bfmlal_idx
                         : s->fpcr_ah ? gen_helper_gvec_ah_bfmlsl_idx
                         : gen_helper_gvec_bfmlsl_idx);
}

TRANS_FEAT(BFMLAL_nx, aa64_sme2, do_bfmlal_nx, a, false)
TRANS_FEAT(BFMLSL_nx, aa64_sme2, do_bfmlal_nx, a, true)

static bool do_fdot(DisasContext *s, arg_azz_n *a, bool multi)
{
    return do_azz_acc_fp(s, a->n, 1, a->rv, a->off, a->zn, a->zm, 1, 0,
                         multi, FPST_ENV, gen_helper_sme2_fdot_h);
}

TRANS_FEAT(FDOT_n1, aa64_sme2, do_fdot, a, false)
TRANS_FEAT(FDOT_nn, aa64_sme2, do_fdot, a, true)

static bool do_fdot_nx(DisasContext *s, arg_azx_n *a)
{
    return do_azz_acc_fp(s, a->n, 1, a->rv, a->off, a->zn, a->zm,
                         a->idx | (1 << 2), 0, false, FPST_ENV,
                         gen_helper_sme2_fdot_idx_h);
}

TRANS_FEAT(FDOT_nx, aa64_sme2, do_fdot_nx, a)

static bool do_bfdot(DisasContext *s, arg_azz_n *a, bool multi)
{
    return do_azz_acc_fp(s, a->n, 1, a->rv, a->off, a->zn, a->zm, 0, 0,
                         multi, FPST_ENV, gen_helper_gvec_bfdot);
}

TRANS_FEAT(BFDOT_n1, aa64_sme2, do_bfdot, a, false)
TRANS_FEAT(BFDOT_nn, aa64_sme2, do_bfdot, a, true)

static bool do_bfdot_nx(DisasContext *s, arg_azx_n *a)
{
    return do_azz_acc_fp(s, a->n, 1, a->rv, a->off, a->zn, a->zm, a->idx, 0,
                         false, FPST_ENV, gen_helper_gvec_bfdot_idx);
}

TRANS_FEAT(BFDOT_nx, aa64_sme2, do_bfdot_nx, a)

static bool do_vdot(DisasContext *s, arg_azx_n *a, gen_helper_gvec_4_ptr *fn)
{
    if (sme_smza_enabled_check(s)) {
        int svl = streaming_vec_reg_size(s);
        int vstride = svl / 2;
        TCGv_ptr t_za = get_zarray(s, a->rv, a->off, 2, 1);
        TCGv_ptr t_zn = vec_full_reg_ptr(s, a->zn);
        TCGv_ptr t_zm = vec_full_reg_ptr(s, a->zm);
        TCGv_ptr t = tcg_temp_new_ptr();

        for (int i = 0; i < 2; ++i) {
            int o_za = i * vstride * sizeof(ARMVectorReg);
            int desc = simd_desc(svl, svl, a->idx | (i << 2));

            tcg_gen_addi_ptr(t, t_za, o_za);
            fn(t, t_zn, t_zm, t, tcg_env, tcg_constant_i32(desc));
        }
    }
    return true;
}

TRANS_FEAT(FVDOT, aa64_sme, do_vdot, a, gen_helper_sme2_fvdot_idx_h)
TRANS_FEAT(BFVDOT, aa64_sme, do_vdot, a, gen_helper_sme2_bfvdot_idx)

static bool do_fmla(DisasContext *s, arg_azz_n *a, bool multi,
                    ARMFPStatusFlavour fpst, gen_helper_gvec_3_ptr *fn)
{
    return do_azz_fp(s, a->n, 1, a->rv, a->off, a->zn, a->zm,
                     0, 0, multi, fpst, fn);
}

TRANS_FEAT(FMLA_n1_h, aa64_sme_f16f16, do_fmla, a, false, FPST_ZA_F16,
           gen_helper_gvec_vfma_h)
TRANS_FEAT(FMLS_n1_h, aa64_sme_f16f16, do_fmla, a, false, FPST_ZA_F16,
           s->fpcr_ah ? gen_helper_gvec_ah_vfms_h : gen_helper_gvec_vfms_h)
TRANS_FEAT(FMLA_nn_h, aa64_sme_f16f16, do_fmla, a, true, FPST_ZA_F16,
           gen_helper_gvec_vfma_h)
TRANS_FEAT(FMLS_nn_h, aa64_sme_f16f16, do_fmla, a, true, FPST_ZA_F16,
           s->fpcr_ah ? gen_helper_gvec_ah_vfms_h : gen_helper_gvec_vfms_h)

TRANS_FEAT(FMLA_n1_s, aa64_sme2, do_fmla, a, false, FPST_ZA,
           gen_helper_gvec_vfma_s)
TRANS_FEAT(FMLS_n1_s, aa64_sme2, do_fmla, a, false, FPST_ZA,
           s->fpcr_ah ? gen_helper_gvec_ah_vfms_s : gen_helper_gvec_vfms_s)
TRANS_FEAT(FMLA_nn_s, aa64_sme2, do_fmla, a, true, FPST_ZA,
           gen_helper_gvec_vfma_s)
TRANS_FEAT(FMLS_nn_s, aa64_sme2, do_fmla, a, true, FPST_ZA,
           s->fpcr_ah ? gen_helper_gvec_ah_vfms_s : gen_helper_gvec_vfms_s)

TRANS_FEAT(FMLA_n1_d, aa64_sme2_f64f64, do_fmla, a, false, FPST_ZA,
           gen_helper_gvec_vfma_d)
TRANS_FEAT(FMLS_n1_d, aa64_sme2_f64f64, do_fmla, a, false, FPST_ZA,
           s->fpcr_ah ? gen_helper_gvec_ah_vfms_d : gen_helper_gvec_vfms_d)
TRANS_FEAT(FMLA_nn_d, aa64_sme2_f64f64, do_fmla, a, true, FPST_ZA,
           gen_helper_gvec_vfma_d)
TRANS_FEAT(FMLS_nn_d, aa64_sme2_f64f64, do_fmla, a, true, FPST_ZA,
           s->fpcr_ah ? gen_helper_gvec_ah_vfms_d : gen_helper_gvec_vfms_d)

TRANS_FEAT(BFMLA_n1, aa64_sme_b16b16, do_fmla, a, false, FPST_ZA,
           gen_helper_gvec_bfmla)
TRANS_FEAT(BFMLS_n1, aa64_sme_b16b16, do_fmla, a, false, FPST_ZA,
           s->fpcr_ah ? gen_helper_gvec_ah_bfmls : gen_helper_gvec_bfmls)
TRANS_FEAT(BFMLA_nn, aa64_sme_b16b16, do_fmla, a, true, FPST_ZA,
           gen_helper_gvec_bfmla)
TRANS_FEAT(BFMLS_nn, aa64_sme_b16b16, do_fmla, a, true, FPST_ZA,
           s->fpcr_ah ? gen_helper_gvec_ah_bfmls : gen_helper_gvec_bfmls)

static bool do_fmla_nx(DisasContext *s, arg_azx_n *a,
                       ARMFPStatusFlavour fpst, gen_helper_gvec_4_ptr *fn)
{
    return do_azz_acc_fp(s, a->n, 1, a->rv, a->off, a->zn, a->zm,
                         a->idx, 0, false, fpst, fn);
}

TRANS_FEAT(FMLA_nx_h, aa64_sme_f16f16, do_fmla_nx, a, FPST_ZA_F16,
           gen_helper_gvec_fmla_idx_h)
TRANS_FEAT(FMLS_nx_h, aa64_sme_f16f16, do_fmla_nx, a, FPST_ZA_F16,
           s->fpcr_ah ? gen_helper_gvec_ah_fmls_idx_h : gen_helper_gvec_fmls_idx_h)
TRANS_FEAT(FMLA_nx_s, aa64_sme2, do_fmla_nx, a, FPST_ZA,
           gen_helper_gvec_fmla_idx_s)
TRANS_FEAT(FMLS_nx_s, aa64_sme2, do_fmla_nx, a, FPST_ZA,
           s->fpcr_ah ? gen_helper_gvec_ah_fmls_idx_s : gen_helper_gvec_fmls_idx_s)
TRANS_FEAT(FMLA_nx_d, aa64_sme2_f64f64, do_fmla_nx, a, FPST_ZA,
           gen_helper_gvec_fmla_idx_d)
TRANS_FEAT(FMLS_nx_d, aa64_sme2_f64f64, do_fmla_nx, a, FPST_ZA,
           s->fpcr_ah ? gen_helper_gvec_ah_fmls_idx_d : gen_helper_gvec_fmls_idx_d)

TRANS_FEAT(BFMLA_nx, aa64_sme_b16b16, do_fmla_nx, a, FPST_ZA,
           gen_helper_gvec_bfmla_idx)
TRANS_FEAT(BFMLS_nx, aa64_sme_b16b16, do_fmla_nx, a, FPST_ZA,
           s->fpcr_ah ? gen_helper_gvec_ah_bfmls_idx : gen_helper_gvec_bfmls_idx)

static bool do_faddsub(DisasContext *s, arg_az_n *a, ARMFPStatusFlavour fpst,
                       gen_helper_gvec_3_ptr *fn)
{
    if (sme_smza_enabled_check(s)) {
        int svl = streaming_vec_reg_size(s);
        int n = a->n;
        int zm = a->zm;
        int vstride = svl / n;
        TCGv_ptr t_za = get_zarray(s, a->rv, a->off, n, 0);
        TCGv_ptr ptr = fpstatus_ptr(fpst);
        TCGv_ptr t = tcg_temp_new_ptr();

        for (int r = 0; r < n; ++r) {
            TCGv_ptr t_zm = vec_full_reg_ptr(s, zm + r);
            int o_za = r * vstride * sizeof(ARMVectorReg);
            int desc = simd_desc(svl, svl, 0);

            tcg_gen_addi_ptr(t, t_za, o_za);
            fn(t, t, t_zm, ptr, tcg_constant_i32(desc));
        }
    }
    return true;
}

TRANS_FEAT(FADD_nn_h, aa64_sme_f16f16, do_faddsub, a,
           FPST_ZA_F16, gen_helper_gvec_fadd_h)
TRANS_FEAT(FSUB_nn_h, aa64_sme_f16f16, do_faddsub, a,
           FPST_ZA_F16, gen_helper_gvec_fsub_h)

TRANS_FEAT(FADD_nn_s, aa64_sme2, do_faddsub, a,
           FPST_ZA, gen_helper_gvec_fadd_s)
TRANS_FEAT(FSUB_nn_s, aa64_sme2, do_faddsub, a,
           FPST_ZA, gen_helper_gvec_fsub_s)

TRANS_FEAT(FADD_nn_d, aa64_sme2_f64f64, do_faddsub, a,
           FPST_ZA, gen_helper_gvec_fadd_d)
TRANS_FEAT(FSUB_nn_d, aa64_sme2_f64f64, do_faddsub, a,
           FPST_ZA, gen_helper_gvec_fsub_d)

TRANS_FEAT(BFADD_nn, aa64_sme_b16b16, do_faddsub, a,
           FPST_ZA, gen_helper_gvec_bfadd)
TRANS_FEAT(BFSUB_nn, aa64_sme_b16b16, do_faddsub, a,
           FPST_ZA, gen_helper_gvec_bfsub)

/*
 * Expand array multi-vector single (n1), array multi-vector (nn),
 * and array multi-vector indexed (nx), for integer accumulate.
 *   multi: true for nn, false for n1.
 *   data: stuff for simd_data, including any index.
 */
static bool do_azz_acc(DisasContext *s, int nreg, int nsel,
                       int rv, int off, int zn, int zm,
                       int data, int shsel, bool multi,
                       gen_helper_gvec_4 *fn)
{
    if (sme_smza_enabled_check(s)) {
        int svl = streaming_vec_reg_size(s);
        int vstride = svl / nreg;
        TCGv_ptr t_za = get_zarray(s, rv, off, nreg, nsel);
        TCGv_ptr t = tcg_temp_new_ptr();

        for (int r = 0; r < nreg; ++r) {
            TCGv_ptr t_zn = vec_full_reg_ptr(s, zn);
            TCGv_ptr t_zm = vec_full_reg_ptr(s, zm);

            for (int i = 0; i < nsel; ++i) {
                int o_za = (r * vstride + i) * sizeof(ARMVectorReg);
                int desc = simd_desc(svl, svl, data | (i << shsel));

                tcg_gen_addi_ptr(t, t_za, o_za);
                fn(t, t_zn, t_zm, t, tcg_constant_i32(desc));
            }

            /*
             * For multiple-and-single vectors, Zn may wrap.
             * For multiple vectors, both Zn and Zm are aligned.
             */
            zn = (zn + 1) % 32;
            zm += multi;
        }
    }
    return true;
}

static bool do_dot(DisasContext *s, arg_azz_n *a, bool multi,
                   gen_helper_gvec_4 *fn)
{
    return do_azz_acc(s, a->n, 1, a->rv, a->off, a->zn, a->zm,
                      0, 0, multi, fn);
}

static void gen_helper_gvec_sudot_4b(TCGv_ptr d, TCGv_ptr n, TCGv_ptr m,
                                     TCGv_ptr a, TCGv_i32 desc)
{
    gen_helper_gvec_usdot_4b(d, m, n, a, desc);
}

TRANS_FEAT(USDOT_n1, aa64_sme2, do_dot, a, false, gen_helper_gvec_usdot_4b)
TRANS_FEAT(SUDOT_n1, aa64_sme2, do_dot, a, false, gen_helper_gvec_sudot_4b)
TRANS_FEAT(SDOT_n1_2h, aa64_sme2, do_dot, a, false, gen_helper_gvec_sdot_2h)
TRANS_FEAT(UDOT_n1_2h, aa64_sme2, do_dot, a, false, gen_helper_gvec_udot_2h)
TRANS_FEAT(SDOT_n1_4b, aa64_sme2, do_dot, a, false, gen_helper_gvec_sdot_4b)
TRANS_FEAT(UDOT_n1_4b, aa64_sme2, do_dot, a, false, gen_helper_gvec_udot_4b)
TRANS_FEAT(SDOT_n1_4h, aa64_sme2_i16i64, do_dot, a, false, gen_helper_gvec_sdot_4h)
TRANS_FEAT(UDOT_n1_4h, aa64_sme2_i16i64, do_dot, a, false, gen_helper_gvec_udot_4h)

TRANS_FEAT(USDOT_nn, aa64_sme2, do_dot, a, true, gen_helper_gvec_usdot_4b)
TRANS_FEAT(SDOT_nn_2h, aa64_sme2, do_dot, a, true, gen_helper_gvec_sdot_2h)
TRANS_FEAT(UDOT_nn_2h, aa64_sme2, do_dot, a, true, gen_helper_gvec_udot_2h)
TRANS_FEAT(SDOT_nn_4b, aa64_sme2, do_dot, a, true, gen_helper_gvec_sdot_4b)
TRANS_FEAT(UDOT_nn_4b, aa64_sme2, do_dot, a, true, gen_helper_gvec_udot_4b)
TRANS_FEAT(SDOT_nn_4h, aa64_sme2_i16i64, do_dot, a, true, gen_helper_gvec_sdot_4h)
TRANS_FEAT(UDOT_nn_4h, aa64_sme2_i16i64, do_dot, a, true, gen_helper_gvec_udot_4h)

static bool do_dot_nx(DisasContext *s, arg_azx_n *a, gen_helper_gvec_4 *fn)
{
    return do_azz_acc(s, a->n, 1, a->rv, a->off, a->zn, a->zm,
                      a->idx, 0, false, fn);
}

TRANS_FEAT(USDOT_nx, aa64_sme2, do_dot_nx, a, gen_helper_gvec_usdot_idx_4b)
TRANS_FEAT(SUDOT_nx, aa64_sme2, do_dot_nx, a, gen_helper_gvec_sudot_idx_4b)
TRANS_FEAT(SDOT_nx_2h, aa64_sme2, do_dot_nx, a, gen_helper_gvec_sdot_idx_2h)
TRANS_FEAT(UDOT_nx_2h, aa64_sme2, do_dot_nx, a, gen_helper_gvec_udot_idx_2h)
TRANS_FEAT(SDOT_nx_4b, aa64_sme2, do_dot_nx, a, gen_helper_gvec_sdot_idx_4b)
TRANS_FEAT(UDOT_nx_4b, aa64_sme2, do_dot_nx, a, gen_helper_gvec_udot_idx_4b)
TRANS_FEAT(SDOT_nx_4h, aa64_sme2_i16i64, do_dot_nx, a, gen_helper_gvec_sdot_idx_4h)
TRANS_FEAT(UDOT_nx_4h, aa64_sme2_i16i64, do_dot_nx, a, gen_helper_gvec_udot_idx_4h)

static bool do_vdot_nx(DisasContext *s, arg_azx_n *a, gen_helper_gvec_3 *fn)
{
    if (sme_smza_enabled_check(s)) {
        int svl = streaming_vec_reg_size(s);
        fn(get_zarray(s, a->rv, a->off, a->n, 0),
           vec_full_reg_ptr(s, a->zn),
           vec_full_reg_ptr(s, a->zm),
           tcg_constant_i32(simd_desc(svl, svl, a->idx)));
    }
    return true;
}

TRANS_FEAT(SVDOT_nx_2h, aa64_sme2, do_vdot_nx, a, gen_helper_sme2_svdot_idx_2h)
TRANS_FEAT(SVDOT_nx_4b, aa64_sme2, do_vdot_nx, a, gen_helper_sme2_svdot_idx_4b)
TRANS_FEAT(SVDOT_nx_4h, aa64_sme2, do_vdot_nx, a, gen_helper_sme2_svdot_idx_4h)

TRANS_FEAT(UVDOT_nx_2h, aa64_sme2, do_vdot_nx, a, gen_helper_sme2_uvdot_idx_2h)
TRANS_FEAT(UVDOT_nx_4b, aa64_sme2, do_vdot_nx, a, gen_helper_sme2_uvdot_idx_4b)
TRANS_FEAT(UVDOT_nx_4h, aa64_sme2, do_vdot_nx, a, gen_helper_sme2_uvdot_idx_4h)

TRANS_FEAT(SUVDOT_nx_4b, aa64_sme2, do_vdot_nx, a, gen_helper_sme2_suvdot_idx_4b)
TRANS_FEAT(USVDOT_nx_4b, aa64_sme2, do_vdot_nx, a, gen_helper_sme2_usvdot_idx_4b)

static bool do_smlal(DisasContext *s, arg_azz_n *a, bool multi,
                     gen_helper_gvec_4 *fn)
{
    return do_azz_acc(s, a->n, 2, a->rv, a->off, a->zn, a->zm,
                      0, 0, multi, fn);
}

TRANS_FEAT(SMLAL_n1, aa64_sme2, do_smlal, a, false, gen_helper_sve2_smlal_zzzw_s)
TRANS_FEAT(SMLSL_n1, aa64_sme2, do_smlal, a, false, gen_helper_sve2_smlsl_zzzw_s)
TRANS_FEAT(UMLAL_n1, aa64_sme2, do_smlal, a, false, gen_helper_sve2_umlal_zzzw_s)
TRANS_FEAT(UMLSL_n1, aa64_sme2, do_smlal, a, false, gen_helper_sve2_umlsl_zzzw_s)

TRANS_FEAT(SMLAL_nn, aa64_sme2, do_smlal, a, true, gen_helper_sve2_smlal_zzzw_s)
TRANS_FEAT(SMLSL_nn, aa64_sme2, do_smlal, a, true, gen_helper_sve2_smlsl_zzzw_s)
TRANS_FEAT(UMLAL_nn, aa64_sme2, do_smlal, a, true, gen_helper_sve2_umlal_zzzw_s)
TRANS_FEAT(UMLSL_nn, aa64_sme2, do_smlal, a, true, gen_helper_sve2_umlsl_zzzw_s)

static bool do_smlal_nx(DisasContext *s, arg_azx_n *a,
                         gen_helper_gvec_4 *fn)
{
    return do_azz_acc(s, a->n, 2, a->rv, a->off, a->zn, a->zm,
                      a->idx << 1, 0, false, fn);
}

TRANS_FEAT(SMLAL_nx, aa64_sme2, do_smlal_nx, a, gen_helper_sve2_smlal_idx_s)
TRANS_FEAT(SMLSL_nx, aa64_sme2, do_smlal_nx, a, gen_helper_sve2_smlsl_idx_s)
TRANS_FEAT(UMLAL_nx, aa64_sme2, do_smlal_nx, a, gen_helper_sve2_umlal_idx_s)
TRANS_FEAT(UMLSL_nx, aa64_sme2, do_smlal_nx, a, gen_helper_sve2_umlsl_idx_s)

static bool do_smlall(DisasContext *s, arg_azz_n *a, bool multi,
                     gen_helper_gvec_4 *fn)
{
    return do_azz_acc(s, a->n, 4, a->rv, a->off, a->zn, a->zm,
                      0, 0, multi, fn);
}

static void gen_helper_sme2_sumlall_s(TCGv_ptr d, TCGv_ptr n, TCGv_ptr m,
                                      TCGv_ptr a, TCGv_i32 desc)
{
    gen_helper_sme2_usmlall_s(d, m, n, a, desc);
}

TRANS_FEAT(SMLALL_n1_s, aa64_sme2, do_smlall, a, false, gen_helper_sme2_smlall_s)
TRANS_FEAT(SMLSLL_n1_s, aa64_sme2, do_smlall, a, false, gen_helper_sme2_smlsll_s)
TRANS_FEAT(UMLALL_n1_s, aa64_sme2, do_smlall, a, false, gen_helper_sme2_umlall_s)
TRANS_FEAT(UMLSLL_n1_s, aa64_sme2, do_smlall, a, false, gen_helper_sme2_umlsll_s)
TRANS_FEAT(USMLALL_n1_s, aa64_sme2, do_smlall, a, false, gen_helper_sme2_usmlall_s)
TRANS_FEAT(SUMLALL_n1_s, aa64_sme2, do_smlall, a, false, gen_helper_sme2_sumlall_s)

TRANS_FEAT(SMLALL_n1_d, aa64_sme2_i16i64, do_smlall, a, false, gen_helper_sme2_smlall_d)
TRANS_FEAT(SMLSLL_n1_d, aa64_sme2_i16i64, do_smlall, a, false, gen_helper_sme2_smlsll_d)
TRANS_FEAT(UMLALL_n1_d, aa64_sme2_i16i64, do_smlall, a, false, gen_helper_sme2_umlall_d)
TRANS_FEAT(UMLSLL_n1_d, aa64_sme2_i16i64, do_smlall, a, false, gen_helper_sme2_umlsll_d)

TRANS_FEAT(SMLALL_nn_s, aa64_sme2, do_smlall, a, true, gen_helper_sme2_smlall_s)
TRANS_FEAT(SMLSLL_nn_s, aa64_sme2, do_smlall, a, true, gen_helper_sme2_smlsll_s)
TRANS_FEAT(UMLALL_nn_s, aa64_sme2, do_smlall, a, true, gen_helper_sme2_umlall_s)
TRANS_FEAT(UMLSLL_nn_s, aa64_sme2, do_smlall, a, true, gen_helper_sme2_umlsll_s)
TRANS_FEAT(USMLALL_nn_s, aa64_sme2, do_smlall, a, true, gen_helper_sme2_usmlall_s)

TRANS_FEAT(SMLALL_nn_d, aa64_sme2_i16i64, do_smlall, a, true, gen_helper_sme2_smlall_d)
TRANS_FEAT(SMLSLL_nn_d, aa64_sme2_i16i64, do_smlall, a, true, gen_helper_sme2_smlsll_d)
TRANS_FEAT(UMLALL_nn_d, aa64_sme2_i16i64, do_smlall, a, true, gen_helper_sme2_umlall_d)
TRANS_FEAT(UMLSLL_nn_d, aa64_sme2_i16i64, do_smlall, a, true, gen_helper_sme2_umlsll_d)

static bool do_smlall_nx(DisasContext *s, arg_azx_n *a,
                        gen_helper_gvec_4 *fn)
{
    return do_azz_acc(s, a->n, 4, a->rv, a->off, a->zn, a->zm,
                      a->idx << 2, 0, false, fn);
}

TRANS_FEAT(SMLALL_nx_s, aa64_sme2, do_smlall_nx, a, gen_helper_sme2_smlall_idx_s)
TRANS_FEAT(SMLSLL_nx_s, aa64_sme2, do_smlall_nx, a, gen_helper_sme2_smlsll_idx_s)
TRANS_FEAT(UMLALL_nx_s, aa64_sme2, do_smlall_nx, a, gen_helper_sme2_umlall_idx_s)
TRANS_FEAT(UMLSLL_nx_s, aa64_sme2, do_smlall_nx, a, gen_helper_sme2_umlsll_idx_s)
TRANS_FEAT(USMLALL_nx_s, aa64_sme2, do_smlall_nx, a, gen_helper_sme2_usmlall_idx_s)
TRANS_FEAT(SUMLALL_nx_s, aa64_sme2, do_smlall_nx, a, gen_helper_sme2_sumlall_idx_s)

TRANS_FEAT(SMLALL_nx_d, aa64_sme2_i16i64, do_smlall_nx, a, gen_helper_sme2_smlall_idx_d)
TRANS_FEAT(SMLSLL_nx_d, aa64_sme2_i16i64, do_smlall_nx, a, gen_helper_sme2_smlsll_idx_d)
TRANS_FEAT(UMLALL_nx_d, aa64_sme2_i16i64, do_smlall_nx, a, gen_helper_sme2_umlall_idx_d)
TRANS_FEAT(UMLSLL_nx_d, aa64_sme2_i16i64, do_smlall_nx, a, gen_helper_sme2_umlsll_idx_d)

static bool do_zz_fpst(DisasContext *s, arg_zz_n *a, int data,
                       ARMFPStatusFlavour type, gen_helper_gvec_2_ptr *fn)
{
    if (sme_sm_enabled_check(s)) {
        int svl = streaming_vec_reg_size(s);
        TCGv_ptr fpst = fpstatus_ptr(type);

        for (int i = 0, n = a->n; i < n; ++i) {
            tcg_gen_gvec_2_ptr(vec_full_reg_offset(s, a->zd + i),
                               vec_full_reg_offset(s, a->zn + i),
                               fpst, svl, svl, data, fn);
        }
    }
    return true;
}

TRANS_FEAT(BFCVT, aa64_sme2, do_zz_fpst, a, 0,
           FPST_A64, gen_helper_sme2_bfcvt)
TRANS_FEAT(BFCVTN, aa64_sme2, do_zz_fpst, a, 0,
           FPST_A64, gen_helper_sme2_bfcvtn)
TRANS_FEAT(FCVT_n, aa64_sme2, do_zz_fpst, a, 0,
           FPST_A64, gen_helper_sme2_fcvt_n)
TRANS_FEAT(FCVTN, aa64_sme2, do_zz_fpst, a, 0,
           FPST_A64, gen_helper_sme2_fcvtn)

TRANS_FEAT(FCVT_w, aa64_sme_f16f16, do_zz_fpst, a, 0,
           FPST_A64_F16, gen_helper_sme2_fcvt_w)
TRANS_FEAT(FCVTL, aa64_sme_f16f16, do_zz_fpst, a, 0,
           FPST_A64_F16, gen_helper_sme2_fcvtl)

TRANS_FEAT(FCVTZS, aa64_sme2, do_zz_fpst, a, 0,
           FPST_A64, gen_helper_gvec_vcvt_rz_fs)
TRANS_FEAT(FCVTZU, aa64_sme2, do_zz_fpst, a, 0,
           FPST_A64, gen_helper_gvec_vcvt_rz_fu)

TRANS_FEAT(SCVTF, aa64_sme2, do_zz_fpst, a, 0,
           FPST_A64, gen_helper_sme2_scvtf)
TRANS_FEAT(UCVTF, aa64_sme2, do_zz_fpst, a, 0,
           FPST_A64, gen_helper_sme2_ucvtf)

TRANS_FEAT(FRINTN, aa64_sme2, do_zz_fpst, a, float_round_nearest_even,
           FPST_A64, gen_helper_gvec_vrint_rm_s)
TRANS_FEAT(FRINTP, aa64_sme2, do_zz_fpst, a, float_round_up,
           FPST_A64, gen_helper_gvec_vrint_rm_s)
TRANS_FEAT(FRINTM, aa64_sme2, do_zz_fpst, a, float_round_down,
           FPST_A64, gen_helper_gvec_vrint_rm_s)
TRANS_FEAT(FRINTA, aa64_sme2, do_zz_fpst, a, float_round_ties_away,
           FPST_A64, gen_helper_gvec_vrint_rm_s)

static bool do_zz(DisasContext *s, arg_zz_n *a, int data,
                  gen_helper_gvec_2 *fn)
{
    if (sme_sm_enabled_check(s)) {
        int svl = streaming_vec_reg_size(s);

        for (int i = 0, n = a->n; i < n; ++i) {
            tcg_gen_gvec_2_ool(vec_full_reg_offset(s, a->zd + i),
                               vec_full_reg_offset(s, a->zn + i),
                               svl, svl, data, fn);
        }
    }
    return true;
}

TRANS_FEAT(SQCVT_sh, aa64_sme2, do_zz, a, 0, gen_helper_sme2_sqcvt_sh)
TRANS_FEAT(UQCVT_sh, aa64_sme2, do_zz, a, 0, gen_helper_sme2_uqcvt_sh)
TRANS_FEAT(SQCVTU_sh, aa64_sme2, do_zz, a, 0, gen_helper_sme2_sqcvtu_sh)

TRANS_FEAT(SQCVT_sb, aa64_sme2, do_zz, a, 0, gen_helper_sme2_sqcvt_sb)
TRANS_FEAT(UQCVT_sb, aa64_sme2, do_zz, a, 0, gen_helper_sme2_uqcvt_sb)
TRANS_FEAT(SQCVTU_sb, aa64_sme2, do_zz, a, 0, gen_helper_sme2_sqcvtu_sb)

TRANS_FEAT(SQCVT_dh, aa64_sme2, do_zz, a, 0, gen_helper_sme2_sqcvt_dh)
TRANS_FEAT(UQCVT_dh, aa64_sme2, do_zz, a, 0, gen_helper_sme2_uqcvt_dh)
TRANS_FEAT(SQCVTU_dh, aa64_sme2, do_zz, a, 0, gen_helper_sme2_sqcvtu_dh)

TRANS_FEAT(SQCVTN_sb, aa64_sme2, do_zz, a, 0, gen_helper_sme2_sqcvtn_sb)
TRANS_FEAT(UQCVTN_sb, aa64_sme2, do_zz, a, 0, gen_helper_sme2_uqcvtn_sb)
TRANS_FEAT(SQCVTUN_sb, aa64_sme2, do_zz, a, 0, gen_helper_sme2_sqcvtun_sb)

TRANS_FEAT(SQCVTN_dh, aa64_sme2, do_zz, a, 0, gen_helper_sme2_sqcvtn_dh)
TRANS_FEAT(UQCVTN_dh, aa64_sme2, do_zz, a, 0, gen_helper_sme2_uqcvtn_dh)
TRANS_FEAT(SQCVTUN_dh, aa64_sme2, do_zz, a, 0, gen_helper_sme2_sqcvtun_dh)

TRANS_FEAT(SUNPK_2bh, aa64_sme2, do_zz, a, 0, gen_helper_sme2_sunpk2_bh)
TRANS_FEAT(SUNPK_2hs, aa64_sme2, do_zz, a, 0, gen_helper_sme2_sunpk2_hs)
TRANS_FEAT(SUNPK_2sd, aa64_sme2, do_zz, a, 0, gen_helper_sme2_sunpk2_sd)

TRANS_FEAT(SUNPK_4bh, aa64_sme2, do_zz, a, 0, gen_helper_sme2_sunpk4_bh)
TRANS_FEAT(SUNPK_4hs, aa64_sme2, do_zz, a, 0, gen_helper_sme2_sunpk4_hs)
TRANS_FEAT(SUNPK_4sd, aa64_sme2, do_zz, a, 0, gen_helper_sme2_sunpk4_sd)

TRANS_FEAT(UUNPK_2bh, aa64_sme2, do_zz, a, 0, gen_helper_sme2_uunpk2_bh)
TRANS_FEAT(UUNPK_2hs, aa64_sme2, do_zz, a, 0, gen_helper_sme2_uunpk2_hs)
TRANS_FEAT(UUNPK_2sd, aa64_sme2, do_zz, a, 0, gen_helper_sme2_uunpk2_sd)

TRANS_FEAT(UUNPK_4bh, aa64_sme2, do_zz, a, 0, gen_helper_sme2_uunpk4_bh)
TRANS_FEAT(UUNPK_4hs, aa64_sme2, do_zz, a, 0, gen_helper_sme2_uunpk4_hs)
TRANS_FEAT(UUNPK_4sd, aa64_sme2, do_zz, a, 0, gen_helper_sme2_uunpk4_sd)

static bool do_zipuzp_4(DisasContext *s, arg_zz_e *a,
                        gen_helper_gvec_2 * const fn[5])
{
    int bytes_per_op = 4 << a->esz;

    /* Both MO_64 and MO_128 can fail the size test. */
    if (s->max_svl < bytes_per_op) {
        unallocated_encoding(s);
    } else if (sme_sm_enabled_check(s)) {
        int svl = streaming_vec_reg_size(s);
        if (svl < bytes_per_op) {
            unallocated_encoding(s);
        } else {
            tcg_gen_gvec_2_ool(vec_full_reg_offset(s, a->zd),
                               vec_full_reg_offset(s, a->zn),
                               svl, svl, 0, fn[a->esz]);
        }
    }
    return true;
}

static gen_helper_gvec_2 * const zip4_fns[] = {
    gen_helper_sme2_zip4_b,
    gen_helper_sme2_zip4_h,
    gen_helper_sme2_zip4_s,
    gen_helper_sme2_zip4_d,
    gen_helper_sme2_zip4_q,
};
TRANS_FEAT(ZIP_4, aa64_sme2, do_zipuzp_4, a, zip4_fns)

static gen_helper_gvec_2 * const uzp4_fns[] = {
    gen_helper_sme2_uzp4_b,
    gen_helper_sme2_uzp4_h,
    gen_helper_sme2_uzp4_s,
    gen_helper_sme2_uzp4_d,
    gen_helper_sme2_uzp4_q,
};
TRANS_FEAT(UZP_4, aa64_sme2, do_zipuzp_4, a, uzp4_fns)

static bool do_zz_rshr(DisasContext *s, arg_rshr *a, gen_helper_gvec_2 *fn)
{
    if (sve_access_check(s)) {
        int vl = vec_full_reg_size(s);
        tcg_gen_gvec_2_ool(vec_full_reg_offset(s, a->zd),
                           vec_full_reg_offset(s, a->zn),
                           vl, vl, a->shift, fn);
    }
    return true;
}

TRANS_FEAT(SQRSHR_sh, aa64_sme2, do_zz_rshr, a, gen_helper_sme2_sqrshr_sh)
TRANS_FEAT(UQRSHR_sh, aa64_sme2, do_zz_rshr, a, gen_helper_sme2_uqrshr_sh)
TRANS_FEAT(SQRSHRU_sh, aa64_sme2, do_zz_rshr, a, gen_helper_sme2_sqrshru_sh)

TRANS_FEAT(SQRSHR_sb, aa64_sme2, do_zz_rshr, a, gen_helper_sme2_sqrshr_sb)
TRANS_FEAT(SQRSHR_dh, aa64_sme2, do_zz_rshr, a, gen_helper_sme2_sqrshr_dh)
TRANS_FEAT(UQRSHR_sb, aa64_sme2, do_zz_rshr, a, gen_helper_sme2_uqrshr_sb)
TRANS_FEAT(UQRSHR_dh, aa64_sme2, do_zz_rshr, a, gen_helper_sme2_uqrshr_dh)
TRANS_FEAT(SQRSHRU_sb, aa64_sme2, do_zz_rshr, a, gen_helper_sme2_sqrshru_sb)
TRANS_FEAT(SQRSHRU_dh, aa64_sme2, do_zz_rshr, a, gen_helper_sme2_sqrshru_dh)

TRANS_FEAT(SQRSHRN_sh, aa64_sme2_or_sve2p1, do_zz_rshr, a, gen_helper_sme2_sqrshrn_sh)
TRANS_FEAT(UQRSHRN_sh, aa64_sme2_or_sve2p1, do_zz_rshr, a, gen_helper_sme2_uqrshrn_sh)
TRANS_FEAT(SQRSHRUN_sh, aa64_sme2_or_sve2p1, do_zz_rshr, a, gen_helper_sme2_sqrshrun_sh)

TRANS_FEAT(SQRSHRN_sb, aa64_sme2, do_zz_rshr, a, gen_helper_sme2_sqrshrn_sb)
TRANS_FEAT(SQRSHRN_dh, aa64_sme2, do_zz_rshr, a, gen_helper_sme2_sqrshrn_dh)
TRANS_FEAT(UQRSHRN_sb, aa64_sme2, do_zz_rshr, a, gen_helper_sme2_uqrshrn_sb)
TRANS_FEAT(UQRSHRN_dh, aa64_sme2, do_zz_rshr, a, gen_helper_sme2_uqrshrn_dh)
TRANS_FEAT(SQRSHRUN_sb, aa64_sme2, do_zz_rshr, a, gen_helper_sme2_sqrshrun_sb)
TRANS_FEAT(SQRSHRUN_dh, aa64_sme2, do_zz_rshr, a, gen_helper_sme2_sqrshrun_dh)

static bool do_zipuzp_2(DisasContext *s, arg_zzz_e *a,
                        gen_helper_gvec_3 * const fn[5])
{
    int bytes_per_op = 2 << a->esz;

    /* MO_128 can fail the size test. */
    if (s->max_svl < bytes_per_op) {
        unallocated_encoding(s);
    } else if (sme_sm_enabled_check(s)) {
        int svl = streaming_vec_reg_size(s);
        if (svl < bytes_per_op) {
            unallocated_encoding(s);
        } else {
            tcg_gen_gvec_3_ool(vec_full_reg_offset(s, a->zd),
                               vec_full_reg_offset(s, a->zn),
                               vec_full_reg_offset(s, a->zm),
                               svl, svl, 0, fn[a->esz]);
        }
    }
    return true;
}

static gen_helper_gvec_3 * const zip2_fns[] = {
    gen_helper_sme2_zip2_b,
    gen_helper_sme2_zip2_h,
    gen_helper_sme2_zip2_s,
    gen_helper_sme2_zip2_d,
    gen_helper_sme2_zip2_q,
};
TRANS_FEAT(ZIP_2, aa64_sme2, do_zipuzp_2, a, zip2_fns)

static gen_helper_gvec_3 * const uzp2_fns[] = {
    gen_helper_sme2_uzp2_b,
    gen_helper_sme2_uzp2_h,
    gen_helper_sme2_uzp2_s,
    gen_helper_sme2_uzp2_d,
    gen_helper_sme2_uzp2_q,
};
TRANS_FEAT(UZP_2, aa64_sme2, do_zipuzp_2, a, uzp2_fns)

static bool trans_FCLAMP(DisasContext *s, arg_zzz_en *a)
{
    static gen_helper_gvec_3_ptr * const fn[] = {
        gen_helper_sme2_bfclamp,
        gen_helper_sme2_fclamp_h,
        gen_helper_sme2_fclamp_s,
        gen_helper_sme2_fclamp_d,
    };
    TCGv_ptr fpst;
    int vl;

    if (!dc_isar_feature(aa64_sme2, s)) {
        return false;
    }
    /* This insn uses MO_8 to encode BFloat16. */
    if (a->esz == MO_8 && !dc_isar_feature(aa64_sme_b16b16, s)) {
        return false;
    }
    if (!sme_sm_enabled_check(s)) {
        return true;
    }

    fpst = fpstatus_ptr(a->esz == MO_16 ? FPST_A64_F16 : FPST_A64);
    vl = vec_full_reg_size(s);

    tcg_gen_gvec_3_ptr(vec_full_reg_offset(s, a->zd),
                       vec_full_reg_offset(s, a->zn),
                       vec_full_reg_offset(s, a->zm),
                       fpst, vl, vl, a->n, fn[a->esz]);
    return true;
}

static bool do_clamp(DisasContext *s, arg_zzz_en *a,
                     gen_helper_gvec_3 * const fn[4])
{
    int vl;

    if (!dc_isar_feature(aa64_sme2, s)) {
        return false;
    }
    if (!sme_sm_enabled_check(s)) {
        return true;
    }

    /*
     * Clamp is just a min+max, easily supported by most host
     * vector operations -- we already have such an expansion in
     * translate-sve.c for a single output.
     * TODO: Add support in gvec for multiple simultaneous output,
     * and/or copy to temporary upon overlap.
     */
    vl = vec_full_reg_size(s);
    tcg_gen_gvec_3_ool(vec_full_reg_offset(s, a->zd),
                       vec_full_reg_offset(s, a->zn),
                       vec_full_reg_offset(s, a->zm),
                       vl, vl, a->n, fn[a->esz]);
    return true;
}

static gen_helper_gvec_3 * const sclamp_fns[] = {
    gen_helper_sme2_sclamp_b,
    gen_helper_sme2_sclamp_h,
    gen_helper_sme2_sclamp_s,
    gen_helper_sme2_sclamp_d,
};
TRANS(SCLAMP, do_clamp, a, sclamp_fns)

static gen_helper_gvec_3 * const uclamp_fns[] = {
    gen_helper_sme2_uclamp_b,
    gen_helper_sme2_uclamp_h,
    gen_helper_sme2_uclamp_s,
    gen_helper_sme2_uclamp_d,
};
TRANS(UCLAMP, do_clamp, a, uclamp_fns)

static bool trans_SEL(DisasContext *s, arg_SEL *a)
{
    typedef void sme_sel_fn(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_i32, TCGv_i32);
    static sme_sel_fn * const fns[4] = {
        gen_helper_sme2_sel_b, gen_helper_sme2_sel_h,
        gen_helper_sme2_sel_s, gen_helper_sme2_sel_d
    };

    if (!dc_isar_feature(aa64_sme2, s)) {
        return false;
    }
    if (sme_sm_enabled_check(s)) {
        int svl = streaming_vec_reg_size(s);
        uint32_t desc = simd_desc(svl, svl, a->n);
        TCGv_ptr t_d = tcg_temp_new_ptr();
        TCGv_ptr t_n = tcg_temp_new_ptr();
        TCGv_ptr t_m = tcg_temp_new_ptr();
        TCGv_i32 png = tcg_temp_new_i32();

        tcg_gen_addi_ptr(t_d, tcg_env, vec_full_reg_offset(s, a->zd));
        tcg_gen_addi_ptr(t_n, tcg_env, vec_full_reg_offset(s, a->zn));
        tcg_gen_addi_ptr(t_m, tcg_env, vec_full_reg_offset(s, a->zm));

        tcg_gen_ld16u_i32(png, tcg_env, pred_full_reg_offset(s, a->pg)
                          ^ (HOST_BIG_ENDIAN ? 6 : 0));

        fns[a->esz](t_d, t_n, t_m, png, tcg_constant_i32(desc));
    }
    return true;
}

static bool do_lut(DisasContext *s, arg_lut *a,
                   gen_helper_gvec_2_ptr *fn, bool strided)
{
    if (sme_sm_enabled_check(s) && sme2_zt0_enabled_check(s)) {
        int svl = streaming_vec_reg_size(s);
        tcg_gen_gvec_2_ptr(vec_full_reg_offset(s, a->zd),
                           vec_full_reg_offset(s, a->zn),
                           tcg_env, svl, svl, strided | (a->idx << 1), fn);
    }
    return true;
}

TRANS_FEAT(LUTI2_c_1b, aa64_sme2, do_lut, a, gen_helper_sme2_luti2_1b, false)
TRANS_FEAT(LUTI2_c_1h, aa64_sme2, do_lut, a, gen_helper_sme2_luti2_1h, false)
TRANS_FEAT(LUTI2_c_1s, aa64_sme2, do_lut, a, gen_helper_sme2_luti2_1s, false)

TRANS_FEAT(LUTI2_c_2b, aa64_sme2, do_lut, a, gen_helper_sme2_luti2_2b, false)
TRANS_FEAT(LUTI2_c_2h, aa64_sme2, do_lut, a, gen_helper_sme2_luti2_2h, false)
TRANS_FEAT(LUTI2_c_2s, aa64_sme2, do_lut, a, gen_helper_sme2_luti2_2s, false)

TRANS_FEAT(LUTI2_c_4b, aa64_sme2, do_lut, a, gen_helper_sme2_luti2_4b, false)
TRANS_FEAT(LUTI2_c_4h, aa64_sme2, do_lut, a, gen_helper_sme2_luti2_4h, false)
TRANS_FEAT(LUTI2_c_4s, aa64_sme2, do_lut, a, gen_helper_sme2_luti2_4s, false)

TRANS_FEAT(LUTI4_c_1b, aa64_sme2, do_lut, a, gen_helper_sme2_luti4_1b, false)
TRANS_FEAT(LUTI4_c_1h, aa64_sme2, do_lut, a, gen_helper_sme2_luti4_1h, false)
TRANS_FEAT(LUTI4_c_1s, aa64_sme2, do_lut, a, gen_helper_sme2_luti4_1s, false)

TRANS_FEAT(LUTI4_c_2b, aa64_sme2, do_lut, a, gen_helper_sme2_luti4_2b, false)
TRANS_FEAT(LUTI4_c_2h, aa64_sme2, do_lut, a, gen_helper_sme2_luti4_2h, false)
TRANS_FEAT(LUTI4_c_2s, aa64_sme2, do_lut, a, gen_helper_sme2_luti4_2s, false)

TRANS_FEAT(LUTI4_c_4h, aa64_sme2, do_lut, a, gen_helper_sme2_luti4_4h, false)
TRANS_FEAT(LUTI4_c_4s, aa64_sme2, do_lut, a, gen_helper_sme2_luti4_4s, false)

static bool do_lut_s4(DisasContext *s, arg_lut *a, gen_helper_gvec_2_ptr *fn)
{
    return !(a->zd & 0b01100) && do_lut(s, a, fn, true);
}

static bool do_lut_s8(DisasContext *s, arg_lut *a, gen_helper_gvec_2_ptr *fn)
{
    return !(a->zd & 0b01000) && do_lut(s, a, fn, true);
}

TRANS_FEAT(LUTI2_s_2b, aa64_sme2p1, do_lut_s8, a, gen_helper_sme2_luti2_2b)
TRANS_FEAT(LUTI2_s_2h, aa64_sme2p1, do_lut_s8, a, gen_helper_sme2_luti2_2h)

TRANS_FEAT(LUTI2_s_4b, aa64_sme2p1, do_lut_s4, a, gen_helper_sme2_luti2_4b)
TRANS_FEAT(LUTI2_s_4h, aa64_sme2p1, do_lut_s4, a, gen_helper_sme2_luti2_4h)

TRANS_FEAT(LUTI4_s_2b, aa64_sme2p1, do_lut_s8, a, gen_helper_sme2_luti4_2b)
TRANS_FEAT(LUTI4_s_2h, aa64_sme2p1, do_lut_s8, a, gen_helper_sme2_luti4_2h)

TRANS_FEAT(LUTI4_s_4h, aa64_sme2p1, do_lut_s4, a, gen_helper_sme2_luti4_4h)
