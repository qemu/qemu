/*
 *  ARM translation: AArch32 Neon instructions
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *  Copyright (c) 2005-2007 CodeSourcery
 *  Copyright (c) 2007 OpenedHand, Ltd.
 *  Copyright (c) 2020 Linaro, Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * This file is intended to be included from translate.c; it uses
 * some macros and definitions provided by that file.
 * It might be possible to convert it to a standalone .c file eventually.
 */

/* Include the generated Neon decoder */
#include "decode-neon-dp.inc.c"
#include "decode-neon-ls.inc.c"
#include "decode-neon-shared.inc.c"

static bool trans_VCMLA(DisasContext *s, arg_VCMLA *a)
{
    int opr_sz;
    TCGv_ptr fpst;
    gen_helper_gvec_3_ptr *fn_gvec_ptr;

    if (!dc_isar_feature(aa32_vcma, s)
        || (!a->size && !dc_isar_feature(aa32_fp16_arith, s))) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vn | a->vm | a->vd) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    opr_sz = (1 + a->q) * 8;
    fpst = get_fpstatus_ptr(1);
    fn_gvec_ptr = a->size ? gen_helper_gvec_fcmlas : gen_helper_gvec_fcmlah;
    tcg_gen_gvec_3_ptr(vfp_reg_offset(1, a->vd),
                       vfp_reg_offset(1, a->vn),
                       vfp_reg_offset(1, a->vm),
                       fpst, opr_sz, opr_sz, a->rot,
                       fn_gvec_ptr);
    tcg_temp_free_ptr(fpst);
    return true;
}

static bool trans_VCADD(DisasContext *s, arg_VCADD *a)
{
    int opr_sz;
    TCGv_ptr fpst;
    gen_helper_gvec_3_ptr *fn_gvec_ptr;

    if (!dc_isar_feature(aa32_vcma, s)
        || (!a->size && !dc_isar_feature(aa32_fp16_arith, s))) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vn | a->vm | a->vd) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    opr_sz = (1 + a->q) * 8;
    fpst = get_fpstatus_ptr(1);
    fn_gvec_ptr = a->size ? gen_helper_gvec_fcadds : gen_helper_gvec_fcaddh;
    tcg_gen_gvec_3_ptr(vfp_reg_offset(1, a->vd),
                       vfp_reg_offset(1, a->vn),
                       vfp_reg_offset(1, a->vm),
                       fpst, opr_sz, opr_sz, a->rot,
                       fn_gvec_ptr);
    tcg_temp_free_ptr(fpst);
    return true;
}

static bool trans_VDOT(DisasContext *s, arg_VDOT *a)
{
    int opr_sz;
    gen_helper_gvec_3 *fn_gvec;

    if (!dc_isar_feature(aa32_dp, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vn | a->vm | a->vd) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    opr_sz = (1 + a->q) * 8;
    fn_gvec = a->u ? gen_helper_gvec_udot_b : gen_helper_gvec_sdot_b;
    tcg_gen_gvec_3_ool(vfp_reg_offset(1, a->vd),
                       vfp_reg_offset(1, a->vn),
                       vfp_reg_offset(1, a->vm),
                       opr_sz, opr_sz, 0, fn_gvec);
    return true;
}

static bool trans_VFML(DisasContext *s, arg_VFML *a)
{
    int opr_sz;

    if (!dc_isar_feature(aa32_fhm, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        (a->vd & 0x10)) {
        return false;
    }

    if (a->vd & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    opr_sz = (1 + a->q) * 8;
    tcg_gen_gvec_3_ptr(vfp_reg_offset(1, a->vd),
                       vfp_reg_offset(a->q, a->vn),
                       vfp_reg_offset(a->q, a->vm),
                       cpu_env, opr_sz, opr_sz, a->s, /* is_2 == 0 */
                       gen_helper_gvec_fmlal_a32);
    return true;
}

static bool trans_VCMLA_scalar(DisasContext *s, arg_VCMLA_scalar *a)
{
    gen_helper_gvec_3_ptr *fn_gvec_ptr;
    int opr_sz;
    TCGv_ptr fpst;

    if (!dc_isar_feature(aa32_vcma, s)) {
        return false;
    }
    if (a->size == 0 && !dc_isar_feature(aa32_fp16_arith, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vd | a->vn) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    fn_gvec_ptr = (a->size ? gen_helper_gvec_fcmlas_idx
                   : gen_helper_gvec_fcmlah_idx);
    opr_sz = (1 + a->q) * 8;
    fpst = get_fpstatus_ptr(1);
    tcg_gen_gvec_3_ptr(vfp_reg_offset(1, a->vd),
                       vfp_reg_offset(1, a->vn),
                       vfp_reg_offset(1, a->vm),
                       fpst, opr_sz, opr_sz,
                       (a->index << 2) | a->rot, fn_gvec_ptr);
    tcg_temp_free_ptr(fpst);
    return true;
}

static bool trans_VDOT_scalar(DisasContext *s, arg_VDOT_scalar *a)
{
    gen_helper_gvec_3 *fn_gvec;
    int opr_sz;
    TCGv_ptr fpst;

    if (!dc_isar_feature(aa32_dp, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn) & 0x10)) {
        return false;
    }

    if ((a->vd | a->vn) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    fn_gvec = a->u ? gen_helper_gvec_udot_idx_b : gen_helper_gvec_sdot_idx_b;
    opr_sz = (1 + a->q) * 8;
    fpst = get_fpstatus_ptr(1);
    tcg_gen_gvec_3_ool(vfp_reg_offset(1, a->vd),
                       vfp_reg_offset(1, a->vn),
                       vfp_reg_offset(1, a->rm),
                       opr_sz, opr_sz, a->index, fn_gvec);
    tcg_temp_free_ptr(fpst);
    return true;
}

static bool trans_VFML_scalar(DisasContext *s, arg_VFML_scalar *a)
{
    int opr_sz;

    if (!dc_isar_feature(aa32_fhm, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd & 0x10) || (a->q && (a->vn & 0x10)))) {
        return false;
    }

    if (a->vd & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    opr_sz = (1 + a->q) * 8;
    tcg_gen_gvec_3_ptr(vfp_reg_offset(1, a->vd),
                       vfp_reg_offset(a->q, a->vn),
                       vfp_reg_offset(a->q, a->rm),
                       cpu_env, opr_sz, opr_sz,
                       (a->index << 2) | a->s, /* is_2 == 0 */
                       gen_helper_gvec_fmlal_idx_a32);
    return true;
}

static struct {
    int nregs;
    int interleave;
    int spacing;
} const neon_ls_element_type[11] = {
    {1, 4, 1},
    {1, 4, 2},
    {4, 1, 1},
    {2, 2, 2},
    {1, 3, 1},
    {1, 3, 2},
    {3, 1, 1},
    {1, 1, 1},
    {1, 2, 1},
    {1, 2, 2},
    {2, 1, 1}
};

static void gen_neon_ldst_base_update(DisasContext *s, int rm, int rn,
                                      int stride)
{
    if (rm != 15) {
        TCGv_i32 base;

        base = load_reg(s, rn);
        if (rm == 13) {
            tcg_gen_addi_i32(base, base, stride);
        } else {
            TCGv_i32 index;
            index = load_reg(s, rm);
            tcg_gen_add_i32(base, base, index);
            tcg_temp_free_i32(index);
        }
        store_reg(s, rn, base);
    }
}

static bool trans_VLDST_multiple(DisasContext *s, arg_VLDST_multiple *a)
{
    /* Neon load/store multiple structures */
    int nregs, interleave, spacing, reg, n;
    MemOp endian = s->be_data;
    int mmu_idx = get_mem_index(s);
    int size = a->size;
    TCGv_i64 tmp64;
    TCGv_i32 addr, tmp;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (!dc_isar_feature(aa32_simd_r32, s) && (a->vd & 0x10)) {
        return false;
    }
    if (a->itype > 10) {
        return false;
    }
    /* Catch UNDEF cases for bad values of align field */
    switch (a->itype & 0xc) {
    case 4:
        if (a->align >= 2) {
            return false;
        }
        break;
    case 8:
        if (a->align == 3) {
            return false;
        }
        break;
    default:
        break;
    }
    nregs = neon_ls_element_type[a->itype].nregs;
    interleave = neon_ls_element_type[a->itype].interleave;
    spacing = neon_ls_element_type[a->itype].spacing;
    if (size == 3 && (interleave | spacing) != 1) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    /* For our purposes, bytes are always little-endian.  */
    if (size == 0) {
        endian = MO_LE;
    }
    /*
     * Consecutive little-endian elements from a single register
     * can be promoted to a larger little-endian operation.
     */
    if (interleave == 1 && endian == MO_LE) {
        size = 3;
    }
    tmp64 = tcg_temp_new_i64();
    addr = tcg_temp_new_i32();
    tmp = tcg_const_i32(1 << size);
    load_reg_var(s, addr, a->rn);
    for (reg = 0; reg < nregs; reg++) {
        for (n = 0; n < 8 >> size; n++) {
            int xs;
            for (xs = 0; xs < interleave; xs++) {
                int tt = a->vd + reg + spacing * xs;

                if (a->l) {
                    gen_aa32_ld_i64(s, tmp64, addr, mmu_idx, endian | size);
                    neon_store_element64(tt, n, size, tmp64);
                } else {
                    neon_load_element64(tmp64, tt, n, size);
                    gen_aa32_st_i64(s, tmp64, addr, mmu_idx, endian | size);
                }
                tcg_gen_add_i32(addr, addr, tmp);
            }
        }
    }
    tcg_temp_free_i32(addr);
    tcg_temp_free_i32(tmp);
    tcg_temp_free_i64(tmp64);

    gen_neon_ldst_base_update(s, a->rm, a->rn, nregs * interleave * 8);
    return true;
}
