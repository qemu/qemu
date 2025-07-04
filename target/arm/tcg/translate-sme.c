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

static bool do_mova_tile_n(DisasContext *s, arg_mova_t *a, int n, bool to_vec)
{
    static gen_helper_gvec_2 * const cz_fns[] = {
        gen_helper_sme2_mova_cz_b, gen_helper_sme2_mova_cz_h,
        gen_helper_sme2_mova_cz_s, gen_helper_sme2_mova_cz_d,
    };
    static gen_helper_gvec_2 * const zc_fns[] = {
        gen_helper_sme2_mova_zc_b, gen_helper_sme2_mova_zc_h,
        gen_helper_sme2_mova_zc_s, gen_helper_sme2_mova_zc_d,
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
            if (to_vec) {
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
            } else {
                tcg_gen_gvec_mov_var(MO_8, t_za, 0, tcg_env, o_zr, svl, svl);
            }
        }
    }
    return true;
}

TRANS_FEAT(MOVA_tz2, aa64_sme2, do_mova_tile_n, a, 2, false)
TRANS_FEAT(MOVA_tz4, aa64_sme2, do_mova_tile_n, a, 4, false)
TRANS_FEAT(MOVA_zt2, aa64_sme2, do_mova_tile_n, a, 2, true)
TRANS_FEAT(MOVA_zt4, aa64_sme2, do_mova_tile_n, a, 4, true)

static bool do_mova_array_n(DisasContext *s, arg_mova_a *a, int n, bool to_vec)
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
        } else {
            tcg_gen_gvec_mov_var(MO_8, t_za, o_za, tcg_env, o_zr, svl, svl);
        }
    }
    return true;
}

TRANS_FEAT(MOVA_az2, aa64_sme2, do_mova_array_n, a, 2, false)
TRANS_FEAT(MOVA_az4, aa64_sme2, do_mova_array_n, a, 4, false)
TRANS_FEAT(MOVA_za2, aa64_sme2, do_mova_array_n, a, 2, true)
TRANS_FEAT(MOVA_za4, aa64_sme2, do_mova_array_n, a, 4, true)

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
    typedef void GenLdSt1(TCGv_env, TCGv_ptr, TCGv_ptr, TCGv, TCGv_i32);

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
    uint32_t desc;
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
                                      tcg_constant_i32(desc));
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
    uint32_t desc = simd_desc(svl, svl, a->sub);
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

    fn(za, zn, zm, pn, pm, tcg_env, tcg_constant_i32(desc));
    return true;
}

TRANS_FEAT(FMOPA_h, aa64_sme, do_outprod_env, a,
           MO_32, gen_helper_sme_fmopa_h)
TRANS_FEAT(FMOPA_s, aa64_sme, do_outprod_fpst, a,
           MO_32, FPST_ZA, gen_helper_sme_fmopa_s)
TRANS_FEAT(FMOPA_d, aa64_sme_f64f64, do_outprod_fpst, a,
           MO_64, FPST_ZA, gen_helper_sme_fmopa_d)

TRANS_FEAT(BFMOPA, aa64_sme, do_outprod_env, a, MO_32, gen_helper_sme_bfmopa)

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
