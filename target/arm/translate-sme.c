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
#include "cpu.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "tcg/tcg-gvec-desc.h"
#include "translate.h"
#include "exec/helper-gen.h"
#include "translate-a64.h"
#include "fpu/softfloat.h"


/*
 * Include the generated decoder.
 */

#include "decode-sme.c.inc"


/*
 * Resolve tile.size[index] to a host pointer, where tile and index
 * are always decoded together, dependent on the element size.
 */
static TCGv_ptr get_tile_rowcol(DisasContext *s, int esz, int rs,
                                int tile_index, bool vertical)
{
    int tile = tile_index >> (4 - esz);
    int index = esz == MO_128 ? 0 : extract32(tile_index, 0, 4 - esz);
    int pos, len, offset;
    TCGv_i32 tmp;
    TCGv_ptr addr;

    /* Compute the final index, which is Rs+imm. */
    tmp = tcg_temp_new_i32();
    tcg_gen_trunc_tl_i32(tmp, cpu_reg(s, rs));
    tcg_gen_addi_i32(tmp, tmp, index);

    /* Prepare a power-of-two modulo via extraction of @len bits. */
    len = ctz32(streaming_vec_reg_size(s)) - esz;

    if (vertical) {
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
    offset += offsetof(CPUARMState, zarray);
    tcg_gen_addi_i32(tmp, tmp, offset);

    /* Add the byte offset to env to produce the final pointer. */
    addr = tcg_temp_new_ptr();
    tcg_gen_ext_i32_ptr(addr, tmp);
    tcg_temp_free_i32(tmp);
    tcg_gen_add_ptr(addr, addr, cpu_env);

    return addr;
}

static bool trans_ZERO(DisasContext *s, arg_ZERO *a)
{
    if (!dc_isar_feature(aa64_sme, s)) {
        return false;
    }
    if (sme_za_enabled_check(s)) {
        gen_helper_sme_zero(cpu_env, tcg_constant_i32(a->imm),
                            tcg_constant_i32(streaming_vec_reg_size(s)));
    }
    return true;
}

static bool trans_MOVA(DisasContext *s, arg_MOVA *a)
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

    if (!dc_isar_feature(aa64_sme, s)) {
        return false;
    }
    if (!sme_smza_enabled_check(s)) {
        return true;
    }

    t_za = get_tile_rowcol(s, a->esz, a->rs, a->za_imm, a->v);
    t_zr = vec_full_reg_ptr(s, a->zr);
    t_pg = pred_full_reg_ptr(s, a->pg);

    svl = streaming_vec_reg_size(s);
    t_desc = tcg_constant_i32(simd_desc(svl, svl, 0));

    if (a->v) {
        /* Vertical slice -- use sme mova helpers. */
        if (a->to_vec) {
            zc_fns[a->esz](t_zr, t_za, t_pg, t_desc);
        } else {
            cz_fns[a->esz](t_za, t_zr, t_pg, t_desc);
        }
    } else {
        /* Horizontal slice -- reuse sve sel helpers. */
        if (a->to_vec) {
            h_fns[a->esz](t_zr, t_za, t_zr, t_pg, t_desc);
        } else {
            h_fns[a->esz](t_za, t_zr, t_za, t_pg, t_desc);
        }
    }

    tcg_temp_free_ptr(t_za);
    tcg_temp_free_ptr(t_zr);
    tcg_temp_free_ptr(t_pg);

    return true;
}

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
    int svl, desc = 0;
    bool be = s->be_data == MO_BE;
    bool mte = s->mte_active[0];

    if (!dc_isar_feature(aa64_sme, s)) {
        return false;
    }
    if (!sme_smza_enabled_check(s)) {
        return true;
    }

    t_za = get_tile_rowcol(s, a->esz, a->rs, a->za_imm, a->v);
    t_pg = pred_full_reg_ptr(s, a->pg);
    addr = tcg_temp_new_i64();

    tcg_gen_shli_i64(addr, cpu_reg(s, a->rm), a->esz);
    tcg_gen_add_i64(addr, addr, cpu_reg_sp(s, a->rn));

    if (mte) {
        desc = FIELD_DP32(desc, MTEDESC, MIDX, get_mem_index(s));
        desc = FIELD_DP32(desc, MTEDESC, TBI, s->tbid);
        desc = FIELD_DP32(desc, MTEDESC, TCMA, s->tcma);
        desc = FIELD_DP32(desc, MTEDESC, WRITE, a->st);
        desc = FIELD_DP32(desc, MTEDESC, SIZEM1, (1 << a->esz) - 1);
        desc <<= SVE_MTEDESC_SHIFT;
    } else {
        addr = clean_data_tbi(s, addr);
    }
    svl = streaming_vec_reg_size(s);
    desc = simd_desc(svl, svl, desc);

    fns[a->esz][be][a->v][mte][a->st](cpu_env, t_za, t_pg, addr,
                                      tcg_constant_i32(desc));

    tcg_temp_free_ptr(t_za);
    tcg_temp_free_ptr(t_pg);
    tcg_temp_free_i64(addr);
    return true;
}
