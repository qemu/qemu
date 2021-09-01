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
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "exec/exec-all.h"
#include "exec/gen-icount.h"
#include "translate.h"
#include "translate-a32.h"

/* Include the generated Neon decoder */
#include "decode-neon-dp.c.inc"
#include "decode-neon-ls.c.inc"
#include "decode-neon-shared.c.inc"

static TCGv_ptr vfp_reg_ptr(bool dp, int reg)
{
    TCGv_ptr ret = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(ret, cpu_env, vfp_reg_offset(dp, reg));
    return ret;
}

static void neon_load_element(TCGv_i32 var, int reg, int ele, MemOp mop)
{
    long offset = neon_element_offset(reg, ele, mop & MO_SIZE);

    switch (mop) {
    case MO_UB:
        tcg_gen_ld8u_i32(var, cpu_env, offset);
        break;
    case MO_UW:
        tcg_gen_ld16u_i32(var, cpu_env, offset);
        break;
    case MO_UL:
        tcg_gen_ld_i32(var, cpu_env, offset);
        break;
    default:
        g_assert_not_reached();
    }
}

static void neon_load_element64(TCGv_i64 var, int reg, int ele, MemOp mop)
{
    long offset = neon_element_offset(reg, ele, mop & MO_SIZE);

    switch (mop) {
    case MO_UB:
        tcg_gen_ld8u_i64(var, cpu_env, offset);
        break;
    case MO_UW:
        tcg_gen_ld16u_i64(var, cpu_env, offset);
        break;
    case MO_UL:
        tcg_gen_ld32u_i64(var, cpu_env, offset);
        break;
    case MO_Q:
        tcg_gen_ld_i64(var, cpu_env, offset);
        break;
    default:
        g_assert_not_reached();
    }
}

static void neon_store_element(int reg, int ele, MemOp size, TCGv_i32 var)
{
    long offset = neon_element_offset(reg, ele, size);

    switch (size) {
    case MO_8:
        tcg_gen_st8_i32(var, cpu_env, offset);
        break;
    case MO_16:
        tcg_gen_st16_i32(var, cpu_env, offset);
        break;
    case MO_32:
        tcg_gen_st_i32(var, cpu_env, offset);
        break;
    default:
        g_assert_not_reached();
    }
}

static void neon_store_element64(int reg, int ele, MemOp size, TCGv_i64 var)
{
    long offset = neon_element_offset(reg, ele, size);

    switch (size) {
    case MO_8:
        tcg_gen_st8_i64(var, cpu_env, offset);
        break;
    case MO_16:
        tcg_gen_st16_i64(var, cpu_env, offset);
        break;
    case MO_32:
        tcg_gen_st32_i64(var, cpu_env, offset);
        break;
    case MO_64:
        tcg_gen_st_i64(var, cpu_env, offset);
        break;
    default:
        g_assert_not_reached();
    }
}

static bool do_neon_ddda(DisasContext *s, int q, int vd, int vn, int vm,
                         int data, gen_helper_gvec_4 *fn_gvec)
{
    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (((vd | vn | vm) & 0x10) && !dc_isar_feature(aa32_simd_r32, s)) {
        return false;
    }

    /*
     * UNDEF accesses to odd registers for each bit of Q.
     * Q will be 0b111 for all Q-reg instructions, otherwise
     * when we have mixed Q- and D-reg inputs.
     */
    if (((vd & 1) * 4 | (vn & 1) * 2 | (vm & 1)) & q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    int opr_sz = q ? 16 : 8;
    tcg_gen_gvec_4_ool(vfp_reg_offset(1, vd),
                       vfp_reg_offset(1, vn),
                       vfp_reg_offset(1, vm),
                       vfp_reg_offset(1, vd),
                       opr_sz, opr_sz, data, fn_gvec);
    return true;
}

static bool do_neon_ddda_fpst(DisasContext *s, int q, int vd, int vn, int vm,
                              int data, ARMFPStatusFlavour fp_flavour,
                              gen_helper_gvec_4_ptr *fn_gvec_ptr)
{
    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (((vd | vn | vm) & 0x10) && !dc_isar_feature(aa32_simd_r32, s)) {
        return false;
    }

    /*
     * UNDEF accesses to odd registers for each bit of Q.
     * Q will be 0b111 for all Q-reg instructions, otherwise
     * when we have mixed Q- and D-reg inputs.
     */
    if (((vd & 1) * 4 | (vn & 1) * 2 | (vm & 1)) & q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    int opr_sz = q ? 16 : 8;
    TCGv_ptr fpst = fpstatus_ptr(fp_flavour);

    tcg_gen_gvec_4_ptr(vfp_reg_offset(1, vd),
                       vfp_reg_offset(1, vn),
                       vfp_reg_offset(1, vm),
                       vfp_reg_offset(1, vd),
                       fpst, opr_sz, opr_sz, data, fn_gvec_ptr);
    tcg_temp_free_ptr(fpst);
    return true;
}

static bool trans_VCMLA(DisasContext *s, arg_VCMLA *a)
{
    if (!dc_isar_feature(aa32_vcma, s)) {
        return false;
    }
    if (a->size == MO_16) {
        if (!dc_isar_feature(aa32_fp16_arith, s)) {
            return false;
        }
        return do_neon_ddda_fpst(s, a->q * 7, a->vd, a->vn, a->vm, a->rot,
                                 FPST_STD_F16, gen_helper_gvec_fcmlah);
    }
    return do_neon_ddda_fpst(s, a->q * 7, a->vd, a->vn, a->vm, a->rot,
                             FPST_STD, gen_helper_gvec_fcmlas);
}

static bool trans_VCADD(DisasContext *s, arg_VCADD *a)
{
    int opr_sz;
    TCGv_ptr fpst;
    gen_helper_gvec_3_ptr *fn_gvec_ptr;

    if (!dc_isar_feature(aa32_vcma, s)
        || (a->size == MO_16 && !dc_isar_feature(aa32_fp16_arith, s))) {
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
    fpst = fpstatus_ptr(a->size == MO_16 ? FPST_STD_F16 : FPST_STD);
    fn_gvec_ptr = (a->size == MO_16) ?
        gen_helper_gvec_fcaddh : gen_helper_gvec_fcadds;
    tcg_gen_gvec_3_ptr(vfp_reg_offset(1, a->vd),
                       vfp_reg_offset(1, a->vn),
                       vfp_reg_offset(1, a->vm),
                       fpst, opr_sz, opr_sz, a->rot,
                       fn_gvec_ptr);
    tcg_temp_free_ptr(fpst);
    return true;
}

static bool trans_VSDOT(DisasContext *s, arg_VSDOT *a)
{
    if (!dc_isar_feature(aa32_dp, s)) {
        return false;
    }
    return do_neon_ddda(s, a->q * 7, a->vd, a->vn, a->vm, 0,
                        gen_helper_gvec_sdot_b);
}

static bool trans_VUDOT(DisasContext *s, arg_VUDOT *a)
{
    if (!dc_isar_feature(aa32_dp, s)) {
        return false;
    }
    return do_neon_ddda(s, a->q * 7, a->vd, a->vn, a->vm, 0,
                        gen_helper_gvec_udot_b);
}

static bool trans_VUSDOT(DisasContext *s, arg_VUSDOT *a)
{
    if (!dc_isar_feature(aa32_i8mm, s)) {
        return false;
    }
    return do_neon_ddda(s, a->q * 7, a->vd, a->vn, a->vm, 0,
                        gen_helper_gvec_usdot_b);
}

static bool trans_VDOT_b16(DisasContext *s, arg_VDOT_b16 *a)
{
    if (!dc_isar_feature(aa32_bf16, s)) {
        return false;
    }
    return do_neon_ddda(s, a->q * 7, a->vd, a->vn, a->vm, 0,
                        gen_helper_gvec_bfdot);
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
    int data = (a->index << 2) | a->rot;

    if (!dc_isar_feature(aa32_vcma, s)) {
        return false;
    }
    if (a->size == MO_16) {
        if (!dc_isar_feature(aa32_fp16_arith, s)) {
            return false;
        }
        return do_neon_ddda_fpst(s, a->q * 6, a->vd, a->vn, a->vm, data,
                                 FPST_STD_F16, gen_helper_gvec_fcmlah_idx);
    }
    return do_neon_ddda_fpst(s, a->q * 6, a->vd, a->vn, a->vm, data,
                             FPST_STD, gen_helper_gvec_fcmlas_idx);
}

static bool trans_VSDOT_scalar(DisasContext *s, arg_VSDOT_scalar *a)
{
    if (!dc_isar_feature(aa32_dp, s)) {
        return false;
    }
    return do_neon_ddda(s, a->q * 6, a->vd, a->vn, a->vm, a->index,
                        gen_helper_gvec_sdot_idx_b);
}

static bool trans_VUDOT_scalar(DisasContext *s, arg_VUDOT_scalar *a)
{
    if (!dc_isar_feature(aa32_dp, s)) {
        return false;
    }
    return do_neon_ddda(s, a->q * 6, a->vd, a->vn, a->vm, a->index,
                        gen_helper_gvec_udot_idx_b);
}

static bool trans_VUSDOT_scalar(DisasContext *s, arg_VUSDOT_scalar *a)
{
    if (!dc_isar_feature(aa32_i8mm, s)) {
        return false;
    }
    return do_neon_ddda(s, a->q * 6, a->vd, a->vn, a->vm, a->index,
                        gen_helper_gvec_usdot_idx_b);
}

static bool trans_VSUDOT_scalar(DisasContext *s, arg_VSUDOT_scalar *a)
{
    if (!dc_isar_feature(aa32_i8mm, s)) {
        return false;
    }
    return do_neon_ddda(s, a->q * 6, a->vd, a->vn, a->vm, a->index,
                        gen_helper_gvec_sudot_idx_b);
}

static bool trans_VDOT_b16_scal(DisasContext *s, arg_VDOT_b16_scal *a)
{
    if (!dc_isar_feature(aa32_bf16, s)) {
        return false;
    }
    return do_neon_ddda(s, a->q * 6, a->vd, a->vn, a->vm, a->index,
                        gen_helper_gvec_bfdot_idx);
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
    MemOp mop, align, endian;
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
    endian = s->be_data;
    if (size == 0) {
        endian = MO_LE;
    }

    /* Enforce alignment requested by the instruction */
    if (a->align) {
        align = pow2_align(a->align + 2); /* 4 ** a->align */
    } else {
        align = s->align_mem ? MO_ALIGN : 0;
    }

    /*
     * Consecutive little-endian elements from a single register
     * can be promoted to a larger little-endian operation.
     */
    if (interleave == 1 && endian == MO_LE) {
        /* Retain any natural alignment. */
        if (align == MO_ALIGN) {
            align = pow2_align(size);
        }
        size = 3;
    }

    tmp64 = tcg_temp_new_i64();
    addr = tcg_temp_new_i32();
    tmp = tcg_const_i32(1 << size);
    load_reg_var(s, addr, a->rn);

    mop = endian | size | align;
    for (reg = 0; reg < nregs; reg++) {
        for (n = 0; n < 8 >> size; n++) {
            int xs;
            for (xs = 0; xs < interleave; xs++) {
                int tt = a->vd + reg + spacing * xs;

                if (a->l) {
                    gen_aa32_ld_internal_i64(s, tmp64, addr, mmu_idx, mop);
                    neon_store_element64(tt, n, size, tmp64);
                } else {
                    neon_load_element64(tmp64, tt, n, size);
                    gen_aa32_st_internal_i64(s, tmp64, addr, mmu_idx, mop);
                }
                tcg_gen_add_i32(addr, addr, tmp);

                /* Subsequent memory operations inherit alignment */
                mop &= ~MO_AMASK;
            }
        }
    }
    tcg_temp_free_i32(addr);
    tcg_temp_free_i32(tmp);
    tcg_temp_free_i64(tmp64);

    gen_neon_ldst_base_update(s, a->rm, a->rn, nregs * interleave * 8);
    return true;
}

static bool trans_VLD_all_lanes(DisasContext *s, arg_VLD_all_lanes *a)
{
    /* Neon load single structure to all lanes */
    int reg, stride, vec_size;
    int vd = a->vd;
    int size = a->size;
    int nregs = a->n + 1;
    TCGv_i32 addr, tmp;
    MemOp mop, align;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (!dc_isar_feature(aa32_simd_r32, s) && (a->vd & 0x10)) {
        return false;
    }

    align = 0;
    if (size == 3) {
        if (nregs != 4 || a->a == 0) {
            return false;
        }
        /* For VLD4 size == 3 a == 1 means 32 bits at 16 byte alignment */
        size = MO_32;
        align = MO_ALIGN_16;
    } else if (a->a) {
        switch (nregs) {
        case 1:
            if (size == 0) {
                return false;
            }
            align = MO_ALIGN;
            break;
        case 2:
            align = pow2_align(size + 1);
            break;
        case 3:
            return false;
        case 4:
            align = pow2_align(size + 2);
            break;
        default:
            g_assert_not_reached();
        }
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    /*
     * VLD1 to all lanes: T bit indicates how many Dregs to write.
     * VLD2/3/4 to all lanes: T bit indicates register stride.
     */
    stride = a->t ? 2 : 1;
    vec_size = nregs == 1 ? stride * 8 : 8;
    mop = size | align;
    tmp = tcg_temp_new_i32();
    addr = tcg_temp_new_i32();
    load_reg_var(s, addr, a->rn);
    for (reg = 0; reg < nregs; reg++) {
        gen_aa32_ld_i32(s, tmp, addr, get_mem_index(s), mop);
        if ((vd & 1) && vec_size == 16) {
            /*
             * We cannot write 16 bytes at once because the
             * destination is unaligned.
             */
            tcg_gen_gvec_dup_i32(size, neon_full_reg_offset(vd),
                                 8, 8, tmp);
            tcg_gen_gvec_mov(0, neon_full_reg_offset(vd + 1),
                             neon_full_reg_offset(vd), 8, 8);
        } else {
            tcg_gen_gvec_dup_i32(size, neon_full_reg_offset(vd),
                                 vec_size, vec_size, tmp);
        }
        tcg_gen_addi_i32(addr, addr, 1 << size);
        vd += stride;

        /* Subsequent memory operations inherit alignment */
        mop &= ~MO_AMASK;
    }
    tcg_temp_free_i32(tmp);
    tcg_temp_free_i32(addr);

    gen_neon_ldst_base_update(s, a->rm, a->rn, (1 << size) * nregs);

    return true;
}

static bool trans_VLDST_single(DisasContext *s, arg_VLDST_single *a)
{
    /* Neon load/store single structure to one lane */
    int reg;
    int nregs = a->n + 1;
    int vd = a->vd;
    TCGv_i32 addr, tmp;
    MemOp mop;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (!dc_isar_feature(aa32_simd_r32, s) && (a->vd & 0x10)) {
        return false;
    }

    /* Catch the UNDEF cases. This is unavoidably a bit messy. */
    switch (nregs) {
    case 1:
        if (((a->align & (1 << a->size)) != 0) ||
            (a->size == 2 && (a->align == 1 || a->align == 2))) {
            return false;
        }
        break;
    case 3:
        if ((a->align & 1) != 0) {
            return false;
        }
        /* fall through */
    case 2:
        if (a->size == 2 && (a->align & 2) != 0) {
            return false;
        }
        break;
    case 4:
        if (a->size == 2 && a->align == 3) {
            return false;
        }
        break;
    default:
        abort();
    }
    if ((vd + a->stride * (nregs - 1)) > 31) {
        /*
         * Attempts to write off the end of the register file are
         * UNPREDICTABLE; we choose to UNDEF because otherwise we would
         * access off the end of the array that holds the register data.
         */
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    /* Pick up SCTLR settings */
    mop = finalize_memop(s, a->size);

    if (a->align) {
        MemOp align_op;

        switch (nregs) {
        case 1:
            /* For VLD1, use natural alignment. */
            align_op = MO_ALIGN;
            break;
        case 2:
            /* For VLD2, use double alignment. */
            align_op = pow2_align(a->size + 1);
            break;
        case 4:
            if (a->size == MO_32) {
                /*
                 * For VLD4.32, align = 1 is double alignment, align = 2 is
                 * quad alignment; align = 3 is rejected above.
                 */
                align_op = pow2_align(a->size + a->align);
            } else {
                /* For VLD4.8 and VLD.16, we want quad alignment. */
                align_op = pow2_align(a->size + 2);
            }
            break;
        default:
            /* For VLD3, the alignment field is zero and rejected above. */
            g_assert_not_reached();
        }

        mop = (mop & ~MO_AMASK) | align_op;
    }

    tmp = tcg_temp_new_i32();
    addr = tcg_temp_new_i32();
    load_reg_var(s, addr, a->rn);

    for (reg = 0; reg < nregs; reg++) {
        if (a->l) {
            gen_aa32_ld_internal_i32(s, tmp, addr, get_mem_index(s), mop);
            neon_store_element(vd, a->reg_idx, a->size, tmp);
        } else { /* Store */
            neon_load_element(tmp, vd, a->reg_idx, a->size);
            gen_aa32_st_internal_i32(s, tmp, addr, get_mem_index(s), mop);
        }
        vd += a->stride;
        tcg_gen_addi_i32(addr, addr, 1 << a->size);

        /* Subsequent memory operations inherit alignment */
        mop &= ~MO_AMASK;
    }
    tcg_temp_free_i32(addr);
    tcg_temp_free_i32(tmp);

    gen_neon_ldst_base_update(s, a->rm, a->rn, (1 << a->size) * nregs);

    return true;
}

static bool do_3same(DisasContext *s, arg_3same *a, GVecGen3Fn fn)
{
    int vec_size = a->q ? 16 : 8;
    int rd_ofs = neon_full_reg_offset(a->vd);
    int rn_ofs = neon_full_reg_offset(a->vn);
    int rm_ofs = neon_full_reg_offset(a->vm);

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
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

    fn(a->size, rd_ofs, rn_ofs, rm_ofs, vec_size, vec_size);
    return true;
}

#define DO_3SAME(INSN, FUNC)                                            \
    static bool trans_##INSN##_3s(DisasContext *s, arg_3same *a)        \
    {                                                                   \
        return do_3same(s, a, FUNC);                                    \
    }

DO_3SAME(VADD, tcg_gen_gvec_add)
DO_3SAME(VSUB, tcg_gen_gvec_sub)
DO_3SAME(VAND, tcg_gen_gvec_and)
DO_3SAME(VBIC, tcg_gen_gvec_andc)
DO_3SAME(VORR, tcg_gen_gvec_or)
DO_3SAME(VORN, tcg_gen_gvec_orc)
DO_3SAME(VEOR, tcg_gen_gvec_xor)
DO_3SAME(VSHL_S, gen_gvec_sshl)
DO_3SAME(VSHL_U, gen_gvec_ushl)
DO_3SAME(VQADD_S, gen_gvec_sqadd_qc)
DO_3SAME(VQADD_U, gen_gvec_uqadd_qc)
DO_3SAME(VQSUB_S, gen_gvec_sqsub_qc)
DO_3SAME(VQSUB_U, gen_gvec_uqsub_qc)

/* These insns are all gvec_bitsel but with the inputs in various orders. */
#define DO_3SAME_BITSEL(INSN, O1, O2, O3)                               \
    static void gen_##INSN##_3s(unsigned vece, uint32_t rd_ofs,         \
                                uint32_t rn_ofs, uint32_t rm_ofs,       \
                                uint32_t oprsz, uint32_t maxsz)         \
    {                                                                   \
        tcg_gen_gvec_bitsel(vece, rd_ofs, O1, O2, O3, oprsz, maxsz);    \
    }                                                                   \
    DO_3SAME(INSN, gen_##INSN##_3s)

DO_3SAME_BITSEL(VBSL, rd_ofs, rn_ofs, rm_ofs)
DO_3SAME_BITSEL(VBIT, rm_ofs, rn_ofs, rd_ofs)
DO_3SAME_BITSEL(VBIF, rm_ofs, rd_ofs, rn_ofs)

#define DO_3SAME_NO_SZ_3(INSN, FUNC)                                    \
    static bool trans_##INSN##_3s(DisasContext *s, arg_3same *a)        \
    {                                                                   \
        if (a->size == 3) {                                             \
            return false;                                               \
        }                                                               \
        return do_3same(s, a, FUNC);                                    \
    }

DO_3SAME_NO_SZ_3(VMAX_S, tcg_gen_gvec_smax)
DO_3SAME_NO_SZ_3(VMAX_U, tcg_gen_gvec_umax)
DO_3SAME_NO_SZ_3(VMIN_S, tcg_gen_gvec_smin)
DO_3SAME_NO_SZ_3(VMIN_U, tcg_gen_gvec_umin)
DO_3SAME_NO_SZ_3(VMUL, tcg_gen_gvec_mul)
DO_3SAME_NO_SZ_3(VMLA, gen_gvec_mla)
DO_3SAME_NO_SZ_3(VMLS, gen_gvec_mls)
DO_3SAME_NO_SZ_3(VTST, gen_gvec_cmtst)
DO_3SAME_NO_SZ_3(VABD_S, gen_gvec_sabd)
DO_3SAME_NO_SZ_3(VABA_S, gen_gvec_saba)
DO_3SAME_NO_SZ_3(VABD_U, gen_gvec_uabd)
DO_3SAME_NO_SZ_3(VABA_U, gen_gvec_uaba)

#define DO_3SAME_CMP(INSN, COND)                                        \
    static void gen_##INSN##_3s(unsigned vece, uint32_t rd_ofs,         \
                                uint32_t rn_ofs, uint32_t rm_ofs,       \
                                uint32_t oprsz, uint32_t maxsz)         \
    {                                                                   \
        tcg_gen_gvec_cmp(COND, vece, rd_ofs, rn_ofs, rm_ofs, oprsz, maxsz); \
    }                                                                   \
    DO_3SAME_NO_SZ_3(INSN, gen_##INSN##_3s)

DO_3SAME_CMP(VCGT_S, TCG_COND_GT)
DO_3SAME_CMP(VCGT_U, TCG_COND_GTU)
DO_3SAME_CMP(VCGE_S, TCG_COND_GE)
DO_3SAME_CMP(VCGE_U, TCG_COND_GEU)
DO_3SAME_CMP(VCEQ, TCG_COND_EQ)

#define WRAP_OOL_FN(WRAPNAME, FUNC)                                        \
    static void WRAPNAME(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,  \
                         uint32_t rm_ofs, uint32_t oprsz, uint32_t maxsz)  \
    {                                                                      \
        tcg_gen_gvec_3_ool(rd_ofs, rn_ofs, rm_ofs, oprsz, maxsz, 0, FUNC); \
    }

WRAP_OOL_FN(gen_VMUL_p_3s, gen_helper_gvec_pmul_b)

static bool trans_VMUL_p_3s(DisasContext *s, arg_3same *a)
{
    if (a->size != 0) {
        return false;
    }
    return do_3same(s, a, gen_VMUL_p_3s);
}

#define DO_VQRDMLAH(INSN, FUNC)                                         \
    static bool trans_##INSN##_3s(DisasContext *s, arg_3same *a)        \
    {                                                                   \
        if (!dc_isar_feature(aa32_rdm, s)) {                            \
            return false;                                               \
        }                                                               \
        if (a->size != 1 && a->size != 2) {                             \
            return false;                                               \
        }                                                               \
        return do_3same(s, a, FUNC);                                    \
    }

DO_VQRDMLAH(VQRDMLAH, gen_gvec_sqrdmlah_qc)
DO_VQRDMLAH(VQRDMLSH, gen_gvec_sqrdmlsh_qc)

#define DO_SHA1(NAME, FUNC)                                             \
    WRAP_OOL_FN(gen_##NAME##_3s, FUNC)                                  \
    static bool trans_##NAME##_3s(DisasContext *s, arg_3same *a)        \
    {                                                                   \
        if (!dc_isar_feature(aa32_sha1, s)) {                           \
            return false;                                               \
        }                                                               \
        return do_3same(s, a, gen_##NAME##_3s);                         \
    }

DO_SHA1(SHA1C, gen_helper_crypto_sha1c)
DO_SHA1(SHA1P, gen_helper_crypto_sha1p)
DO_SHA1(SHA1M, gen_helper_crypto_sha1m)
DO_SHA1(SHA1SU0, gen_helper_crypto_sha1su0)

#define DO_SHA2(NAME, FUNC)                                             \
    WRAP_OOL_FN(gen_##NAME##_3s, FUNC)                                  \
    static bool trans_##NAME##_3s(DisasContext *s, arg_3same *a)        \
    {                                                                   \
        if (!dc_isar_feature(aa32_sha2, s)) {                           \
            return false;                                               \
        }                                                               \
        return do_3same(s, a, gen_##NAME##_3s);                         \
    }

DO_SHA2(SHA256H, gen_helper_crypto_sha256h)
DO_SHA2(SHA256H2, gen_helper_crypto_sha256h2)
DO_SHA2(SHA256SU1, gen_helper_crypto_sha256su1)

#define DO_3SAME_64(INSN, FUNC)                                         \
    static void gen_##INSN##_3s(unsigned vece, uint32_t rd_ofs,         \
                                uint32_t rn_ofs, uint32_t rm_ofs,       \
                                uint32_t oprsz, uint32_t maxsz)         \
    {                                                                   \
        static const GVecGen3 op = { .fni8 = FUNC };                    \
        tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, oprsz, maxsz, &op);      \
    }                                                                   \
    DO_3SAME(INSN, gen_##INSN##_3s)

#define DO_3SAME_64_ENV(INSN, FUNC)                                     \
    static void gen_##INSN##_elt(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m)    \
    {                                                                   \
        FUNC(d, cpu_env, n, m);                                         \
    }                                                                   \
    DO_3SAME_64(INSN, gen_##INSN##_elt)

DO_3SAME_64(VRSHL_S64, gen_helper_neon_rshl_s64)
DO_3SAME_64(VRSHL_U64, gen_helper_neon_rshl_u64)
DO_3SAME_64_ENV(VQSHL_S64, gen_helper_neon_qshl_s64)
DO_3SAME_64_ENV(VQSHL_U64, gen_helper_neon_qshl_u64)
DO_3SAME_64_ENV(VQRSHL_S64, gen_helper_neon_qrshl_s64)
DO_3SAME_64_ENV(VQRSHL_U64, gen_helper_neon_qrshl_u64)

#define DO_3SAME_32(INSN, FUNC)                                         \
    static void gen_##INSN##_3s(unsigned vece, uint32_t rd_ofs,         \
                                uint32_t rn_ofs, uint32_t rm_ofs,       \
                                uint32_t oprsz, uint32_t maxsz)         \
    {                                                                   \
        static const GVecGen3 ops[4] = {                                \
            { .fni4 = gen_helper_neon_##FUNC##8 },                      \
            { .fni4 = gen_helper_neon_##FUNC##16 },                     \
            { .fni4 = gen_helper_neon_##FUNC##32 },                     \
            { 0 },                                                      \
        };                                                              \
        tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, oprsz, maxsz, &ops[vece]); \
    }                                                                   \
    static bool trans_##INSN##_3s(DisasContext *s, arg_3same *a)        \
    {                                                                   \
        if (a->size > 2) {                                              \
            return false;                                               \
        }                                                               \
        return do_3same(s, a, gen_##INSN##_3s);                         \
    }

/*
 * Some helper functions need to be passed the cpu_env. In order
 * to use those with the gvec APIs like tcg_gen_gvec_3() we need
 * to create wrapper functions whose prototype is a NeonGenTwoOpFn()
 * and which call a NeonGenTwoOpEnvFn().
 */
#define WRAP_ENV_FN(WRAPNAME, FUNC)                                     \
    static void WRAPNAME(TCGv_i32 d, TCGv_i32 n, TCGv_i32 m)            \
    {                                                                   \
        FUNC(d, cpu_env, n, m);                                         \
    }

#define DO_3SAME_32_ENV(INSN, FUNC)                                     \
    WRAP_ENV_FN(gen_##INSN##_tramp8, gen_helper_neon_##FUNC##8);        \
    WRAP_ENV_FN(gen_##INSN##_tramp16, gen_helper_neon_##FUNC##16);      \
    WRAP_ENV_FN(gen_##INSN##_tramp32, gen_helper_neon_##FUNC##32);      \
    static void gen_##INSN##_3s(unsigned vece, uint32_t rd_ofs,         \
                                uint32_t rn_ofs, uint32_t rm_ofs,       \
                                uint32_t oprsz, uint32_t maxsz)         \
    {                                                                   \
        static const GVecGen3 ops[4] = {                                \
            { .fni4 = gen_##INSN##_tramp8 },                            \
            { .fni4 = gen_##INSN##_tramp16 },                           \
            { .fni4 = gen_##INSN##_tramp32 },                           \
            { 0 },                                                      \
        };                                                              \
        tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, oprsz, maxsz, &ops[vece]); \
    }                                                                   \
    static bool trans_##INSN##_3s(DisasContext *s, arg_3same *a)        \
    {                                                                   \
        if (a->size > 2) {                                              \
            return false;                                               \
        }                                                               \
        return do_3same(s, a, gen_##INSN##_3s);                         \
    }

DO_3SAME_32(VHADD_S, hadd_s)
DO_3SAME_32(VHADD_U, hadd_u)
DO_3SAME_32(VHSUB_S, hsub_s)
DO_3SAME_32(VHSUB_U, hsub_u)
DO_3SAME_32(VRHADD_S, rhadd_s)
DO_3SAME_32(VRHADD_U, rhadd_u)
DO_3SAME_32(VRSHL_S, rshl_s)
DO_3SAME_32(VRSHL_U, rshl_u)

DO_3SAME_32_ENV(VQSHL_S, qshl_s)
DO_3SAME_32_ENV(VQSHL_U, qshl_u)
DO_3SAME_32_ENV(VQRSHL_S, qrshl_s)
DO_3SAME_32_ENV(VQRSHL_U, qrshl_u)

static bool do_3same_pair(DisasContext *s, arg_3same *a, NeonGenTwoOpFn *fn)
{
    /* Operations handled pairwise 32 bits at a time */
    TCGv_i32 tmp, tmp2, tmp3;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if (a->size == 3) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    assert(a->q == 0); /* enforced by decode patterns */

    /*
     * Note that we have to be careful not to clobber the source operands
     * in the "vm == vd" case by storing the result of the first pass too
     * early. Since Q is 0 there are always just two passes, so instead
     * of a complicated loop over each pass we just unroll.
     */
    tmp = tcg_temp_new_i32();
    tmp2 = tcg_temp_new_i32();
    tmp3 = tcg_temp_new_i32();

    read_neon_element32(tmp, a->vn, 0, MO_32);
    read_neon_element32(tmp2, a->vn, 1, MO_32);
    fn(tmp, tmp, tmp2);

    read_neon_element32(tmp3, a->vm, 0, MO_32);
    read_neon_element32(tmp2, a->vm, 1, MO_32);
    fn(tmp3, tmp3, tmp2);

    write_neon_element32(tmp, a->vd, 0, MO_32);
    write_neon_element32(tmp3, a->vd, 1, MO_32);

    tcg_temp_free_i32(tmp);
    tcg_temp_free_i32(tmp2);
    tcg_temp_free_i32(tmp3);
    return true;
}

#define DO_3SAME_PAIR(INSN, func)                                       \
    static bool trans_##INSN##_3s(DisasContext *s, arg_3same *a)        \
    {                                                                   \
        static NeonGenTwoOpFn * const fns[] = {                         \
            gen_helper_neon_##func##8,                                  \
            gen_helper_neon_##func##16,                                 \
            gen_helper_neon_##func##32,                                 \
        };                                                              \
        if (a->size > 2) {                                              \
            return false;                                               \
        }                                                               \
        return do_3same_pair(s, a, fns[a->size]);                       \
    }

/* 32-bit pairwise ops end up the same as the elementwise versions.  */
#define gen_helper_neon_pmax_s32  tcg_gen_smax_i32
#define gen_helper_neon_pmax_u32  tcg_gen_umax_i32
#define gen_helper_neon_pmin_s32  tcg_gen_smin_i32
#define gen_helper_neon_pmin_u32  tcg_gen_umin_i32
#define gen_helper_neon_padd_u32  tcg_gen_add_i32

DO_3SAME_PAIR(VPMAX_S, pmax_s)
DO_3SAME_PAIR(VPMIN_S, pmin_s)
DO_3SAME_PAIR(VPMAX_U, pmax_u)
DO_3SAME_PAIR(VPMIN_U, pmin_u)
DO_3SAME_PAIR(VPADD, padd_u)

#define DO_3SAME_VQDMULH(INSN, FUNC)                                    \
    WRAP_ENV_FN(gen_##INSN##_tramp16, gen_helper_neon_##FUNC##_s16);    \
    WRAP_ENV_FN(gen_##INSN##_tramp32, gen_helper_neon_##FUNC##_s32);    \
    static void gen_##INSN##_3s(unsigned vece, uint32_t rd_ofs,         \
                                uint32_t rn_ofs, uint32_t rm_ofs,       \
                                uint32_t oprsz, uint32_t maxsz)         \
    {                                                                   \
        static const GVecGen3 ops[2] = {                                \
            { .fni4 = gen_##INSN##_tramp16 },                           \
            { .fni4 = gen_##INSN##_tramp32 },                           \
        };                                                              \
        tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, oprsz, maxsz, &ops[vece - 1]); \
    }                                                                   \
    static bool trans_##INSN##_3s(DisasContext *s, arg_3same *a)        \
    {                                                                   \
        if (a->size != 1 && a->size != 2) {                             \
            return false;                                               \
        }                                                               \
        return do_3same(s, a, gen_##INSN##_3s);                         \
    }

DO_3SAME_VQDMULH(VQDMULH, qdmulh)
DO_3SAME_VQDMULH(VQRDMULH, qrdmulh)

#define WRAP_FP_GVEC(WRAPNAME, FPST, FUNC)                              \
    static void WRAPNAME(unsigned vece, uint32_t rd_ofs,                \
                         uint32_t rn_ofs, uint32_t rm_ofs,              \
                         uint32_t oprsz, uint32_t maxsz)                \
    {                                                                   \
        TCGv_ptr fpst = fpstatus_ptr(FPST);                             \
        tcg_gen_gvec_3_ptr(rd_ofs, rn_ofs, rm_ofs, fpst,                \
                           oprsz, maxsz, 0, FUNC);                      \
        tcg_temp_free_ptr(fpst);                                        \
    }

#define DO_3S_FP_GVEC(INSN,SFUNC,HFUNC)                                 \
    WRAP_FP_GVEC(gen_##INSN##_fp32_3s, FPST_STD, SFUNC)                 \
    WRAP_FP_GVEC(gen_##INSN##_fp16_3s, FPST_STD_F16, HFUNC)             \
    static bool trans_##INSN##_fp_3s(DisasContext *s, arg_3same *a)     \
    {                                                                   \
        if (a->size == MO_16) {                                         \
            if (!dc_isar_feature(aa32_fp16_arith, s)) {                 \
                return false;                                           \
            }                                                           \
            return do_3same(s, a, gen_##INSN##_fp16_3s);                \
        }                                                               \
        return do_3same(s, a, gen_##INSN##_fp32_3s);                    \
    }


DO_3S_FP_GVEC(VADD, gen_helper_gvec_fadd_s, gen_helper_gvec_fadd_h)
DO_3S_FP_GVEC(VSUB, gen_helper_gvec_fsub_s, gen_helper_gvec_fsub_h)
DO_3S_FP_GVEC(VABD, gen_helper_gvec_fabd_s, gen_helper_gvec_fabd_h)
DO_3S_FP_GVEC(VMUL, gen_helper_gvec_fmul_s, gen_helper_gvec_fmul_h)
DO_3S_FP_GVEC(VCEQ, gen_helper_gvec_fceq_s, gen_helper_gvec_fceq_h)
DO_3S_FP_GVEC(VCGE, gen_helper_gvec_fcge_s, gen_helper_gvec_fcge_h)
DO_3S_FP_GVEC(VCGT, gen_helper_gvec_fcgt_s, gen_helper_gvec_fcgt_h)
DO_3S_FP_GVEC(VACGE, gen_helper_gvec_facge_s, gen_helper_gvec_facge_h)
DO_3S_FP_GVEC(VACGT, gen_helper_gvec_facgt_s, gen_helper_gvec_facgt_h)
DO_3S_FP_GVEC(VMAX, gen_helper_gvec_fmax_s, gen_helper_gvec_fmax_h)
DO_3S_FP_GVEC(VMIN, gen_helper_gvec_fmin_s, gen_helper_gvec_fmin_h)
DO_3S_FP_GVEC(VMLA, gen_helper_gvec_fmla_s, gen_helper_gvec_fmla_h)
DO_3S_FP_GVEC(VMLS, gen_helper_gvec_fmls_s, gen_helper_gvec_fmls_h)
DO_3S_FP_GVEC(VFMA, gen_helper_gvec_vfma_s, gen_helper_gvec_vfma_h)
DO_3S_FP_GVEC(VFMS, gen_helper_gvec_vfms_s, gen_helper_gvec_vfms_h)
DO_3S_FP_GVEC(VRECPS, gen_helper_gvec_recps_nf_s, gen_helper_gvec_recps_nf_h)
DO_3S_FP_GVEC(VRSQRTS, gen_helper_gvec_rsqrts_nf_s, gen_helper_gvec_rsqrts_nf_h)

WRAP_FP_GVEC(gen_VMAXNM_fp32_3s, FPST_STD, gen_helper_gvec_fmaxnum_s)
WRAP_FP_GVEC(gen_VMAXNM_fp16_3s, FPST_STD_F16, gen_helper_gvec_fmaxnum_h)
WRAP_FP_GVEC(gen_VMINNM_fp32_3s, FPST_STD, gen_helper_gvec_fminnum_s)
WRAP_FP_GVEC(gen_VMINNM_fp16_3s, FPST_STD_F16, gen_helper_gvec_fminnum_h)

static bool trans_VMAXNM_fp_3s(DisasContext *s, arg_3same *a)
{
    if (!arm_dc_feature(s, ARM_FEATURE_V8)) {
        return false;
    }

    if (a->size == MO_16) {
        if (!dc_isar_feature(aa32_fp16_arith, s)) {
            return false;
        }
        return do_3same(s, a, gen_VMAXNM_fp16_3s);
    }
    return do_3same(s, a, gen_VMAXNM_fp32_3s);
}

static bool trans_VMINNM_fp_3s(DisasContext *s, arg_3same *a)
{
    if (!arm_dc_feature(s, ARM_FEATURE_V8)) {
        return false;
    }

    if (a->size == MO_16) {
        if (!dc_isar_feature(aa32_fp16_arith, s)) {
            return false;
        }
        return do_3same(s, a, gen_VMINNM_fp16_3s);
    }
    return do_3same(s, a, gen_VMINNM_fp32_3s);
}

static bool do_3same_fp_pair(DisasContext *s, arg_3same *a,
                             gen_helper_gvec_3_ptr *fn)
{
    /* FP pairwise operations */
    TCGv_ptr fpstatus;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    assert(a->q == 0); /* enforced by decode patterns */


    fpstatus = fpstatus_ptr(a->size == MO_16 ? FPST_STD_F16 : FPST_STD);
    tcg_gen_gvec_3_ptr(vfp_reg_offset(1, a->vd),
                       vfp_reg_offset(1, a->vn),
                       vfp_reg_offset(1, a->vm),
                       fpstatus, 8, 8, 0, fn);
    tcg_temp_free_ptr(fpstatus);

    return true;
}

/*
 * For all the functions using this macro, size == 1 means fp16,
 * which is an architecture extension we don't implement yet.
 */
#define DO_3S_FP_PAIR(INSN,FUNC)                                    \
    static bool trans_##INSN##_fp_3s(DisasContext *s, arg_3same *a) \
    {                                                               \
        if (a->size == MO_16) {                                     \
            if (!dc_isar_feature(aa32_fp16_arith, s)) {             \
                return false;                                       \
            }                                                       \
            return do_3same_fp_pair(s, a, FUNC##h);                 \
        }                                                           \
        return do_3same_fp_pair(s, a, FUNC##s);                     \
    }

DO_3S_FP_PAIR(VPADD, gen_helper_neon_padd)
DO_3S_FP_PAIR(VPMAX, gen_helper_neon_pmax)
DO_3S_FP_PAIR(VPMIN, gen_helper_neon_pmin)

static bool do_vector_2sh(DisasContext *s, arg_2reg_shift *a, GVecGen2iFn *fn)
{
    /* Handle a 2-reg-shift insn which can be vectorized. */
    int vec_size = a->q ? 16 : 8;
    int rd_ofs = neon_full_reg_offset(a->vd);
    int rm_ofs = neon_full_reg_offset(a->vm);

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vm | a->vd) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    fn(a->size, rd_ofs, rm_ofs, a->shift, vec_size, vec_size);
    return true;
}

#define DO_2SH(INSN, FUNC)                                              \
    static bool trans_##INSN##_2sh(DisasContext *s, arg_2reg_shift *a)  \
    {                                                                   \
        return do_vector_2sh(s, a, FUNC);                               \
    }                                                                   \

DO_2SH(VSHL, tcg_gen_gvec_shli)
DO_2SH(VSLI, gen_gvec_sli)
DO_2SH(VSRI, gen_gvec_sri)
DO_2SH(VSRA_S, gen_gvec_ssra)
DO_2SH(VSRA_U, gen_gvec_usra)
DO_2SH(VRSHR_S, gen_gvec_srshr)
DO_2SH(VRSHR_U, gen_gvec_urshr)
DO_2SH(VRSRA_S, gen_gvec_srsra)
DO_2SH(VRSRA_U, gen_gvec_ursra)

static bool trans_VSHR_S_2sh(DisasContext *s, arg_2reg_shift *a)
{
    /* Signed shift out of range results in all-sign-bits */
    a->shift = MIN(a->shift, (8 << a->size) - 1);
    return do_vector_2sh(s, a, tcg_gen_gvec_sari);
}

static void gen_zero_rd_2sh(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                            int64_t shift, uint32_t oprsz, uint32_t maxsz)
{
    tcg_gen_gvec_dup_imm(vece, rd_ofs, oprsz, maxsz, 0);
}

static bool trans_VSHR_U_2sh(DisasContext *s, arg_2reg_shift *a)
{
    /* Shift out of range is architecturally valid and results in zero. */
    if (a->shift >= (8 << a->size)) {
        return do_vector_2sh(s, a, gen_zero_rd_2sh);
    } else {
        return do_vector_2sh(s, a, tcg_gen_gvec_shri);
    }
}

static bool do_2shift_env_64(DisasContext *s, arg_2reg_shift *a,
                             NeonGenTwo64OpEnvFn *fn)
{
    /*
     * 2-reg-and-shift operations, size == 3 case, where the
     * function needs to be passed cpu_env.
     */
    TCGv_i64 constimm;
    int pass;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vm | a->vd) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    /*
     * To avoid excessive duplication of ops we implement shift
     * by immediate using the variable shift operations.
     */
    constimm = tcg_const_i64(dup_const(a->size, a->shift));

    for (pass = 0; pass < a->q + 1; pass++) {
        TCGv_i64 tmp = tcg_temp_new_i64();

        read_neon_element64(tmp, a->vm, pass, MO_64);
        fn(tmp, cpu_env, tmp, constimm);
        write_neon_element64(tmp, a->vd, pass, MO_64);
        tcg_temp_free_i64(tmp);
    }
    tcg_temp_free_i64(constimm);
    return true;
}

static bool do_2shift_env_32(DisasContext *s, arg_2reg_shift *a,
                             NeonGenTwoOpEnvFn *fn)
{
    /*
     * 2-reg-and-shift operations, size < 3 case, where the
     * helper needs to be passed cpu_env.
     */
    TCGv_i32 constimm, tmp;
    int pass;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vm | a->vd) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    /*
     * To avoid excessive duplication of ops we implement shift
     * by immediate using the variable shift operations.
     */
    constimm = tcg_const_i32(dup_const(a->size, a->shift));
    tmp = tcg_temp_new_i32();

    for (pass = 0; pass < (a->q ? 4 : 2); pass++) {
        read_neon_element32(tmp, a->vm, pass, MO_32);
        fn(tmp, cpu_env, tmp, constimm);
        write_neon_element32(tmp, a->vd, pass, MO_32);
    }
    tcg_temp_free_i32(tmp);
    tcg_temp_free_i32(constimm);
    return true;
}

#define DO_2SHIFT_ENV(INSN, FUNC)                                       \
    static bool trans_##INSN##_64_2sh(DisasContext *s, arg_2reg_shift *a) \
    {                                                                   \
        return do_2shift_env_64(s, a, gen_helper_neon_##FUNC##64);      \
    }                                                                   \
    static bool trans_##INSN##_2sh(DisasContext *s, arg_2reg_shift *a)  \
    {                                                                   \
        static NeonGenTwoOpEnvFn * const fns[] = {                      \
            gen_helper_neon_##FUNC##8,                                  \
            gen_helper_neon_##FUNC##16,                                 \
            gen_helper_neon_##FUNC##32,                                 \
        };                                                              \
        assert(a->size < ARRAY_SIZE(fns));                              \
        return do_2shift_env_32(s, a, fns[a->size]);                    \
    }

DO_2SHIFT_ENV(VQSHLU, qshlu_s)
DO_2SHIFT_ENV(VQSHL_U, qshl_u)
DO_2SHIFT_ENV(VQSHL_S, qshl_s)

static bool do_2shift_narrow_64(DisasContext *s, arg_2reg_shift *a,
                                NeonGenTwo64OpFn *shiftfn,
                                NeonGenNarrowEnvFn *narrowfn)
{
    /* 2-reg-and-shift narrowing-shift operations, size == 3 case */
    TCGv_i64 constimm, rm1, rm2;
    TCGv_i32 rd;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if (a->vm & 1) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    /*
     * This is always a right shift, and the shiftfn is always a
     * left-shift helper, which thus needs the negated shift count.
     */
    constimm = tcg_const_i64(-a->shift);
    rm1 = tcg_temp_new_i64();
    rm2 = tcg_temp_new_i64();
    rd = tcg_temp_new_i32();

    /* Load both inputs first to avoid potential overwrite if rm == rd */
    read_neon_element64(rm1, a->vm, 0, MO_64);
    read_neon_element64(rm2, a->vm, 1, MO_64);

    shiftfn(rm1, rm1, constimm);
    narrowfn(rd, cpu_env, rm1);
    write_neon_element32(rd, a->vd, 0, MO_32);

    shiftfn(rm2, rm2, constimm);
    narrowfn(rd, cpu_env, rm2);
    write_neon_element32(rd, a->vd, 1, MO_32);

    tcg_temp_free_i32(rd);
    tcg_temp_free_i64(rm1);
    tcg_temp_free_i64(rm2);
    tcg_temp_free_i64(constimm);

    return true;
}

static bool do_2shift_narrow_32(DisasContext *s, arg_2reg_shift *a,
                                NeonGenTwoOpFn *shiftfn,
                                NeonGenNarrowEnvFn *narrowfn)
{
    /* 2-reg-and-shift narrowing-shift operations, size < 3 case */
    TCGv_i32 constimm, rm1, rm2, rm3, rm4;
    TCGv_i64 rtmp;
    uint32_t imm;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if (a->vm & 1) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    /*
     * This is always a right shift, and the shiftfn is always a
     * left-shift helper, which thus needs the negated shift count
     * duplicated into each lane of the immediate value.
     */
    if (a->size == 1) {
        imm = (uint16_t)(-a->shift);
        imm |= imm << 16;
    } else {
        /* size == 2 */
        imm = -a->shift;
    }
    constimm = tcg_const_i32(imm);

    /* Load all inputs first to avoid potential overwrite */
    rm1 = tcg_temp_new_i32();
    rm2 = tcg_temp_new_i32();
    rm3 = tcg_temp_new_i32();
    rm4 = tcg_temp_new_i32();
    read_neon_element32(rm1, a->vm, 0, MO_32);
    read_neon_element32(rm2, a->vm, 1, MO_32);
    read_neon_element32(rm3, a->vm, 2, MO_32);
    read_neon_element32(rm4, a->vm, 3, MO_32);
    rtmp = tcg_temp_new_i64();

    shiftfn(rm1, rm1, constimm);
    shiftfn(rm2, rm2, constimm);

    tcg_gen_concat_i32_i64(rtmp, rm1, rm2);
    tcg_temp_free_i32(rm2);

    narrowfn(rm1, cpu_env, rtmp);
    write_neon_element32(rm1, a->vd, 0, MO_32);
    tcg_temp_free_i32(rm1);

    shiftfn(rm3, rm3, constimm);
    shiftfn(rm4, rm4, constimm);
    tcg_temp_free_i32(constimm);

    tcg_gen_concat_i32_i64(rtmp, rm3, rm4);
    tcg_temp_free_i32(rm4);

    narrowfn(rm3, cpu_env, rtmp);
    tcg_temp_free_i64(rtmp);
    write_neon_element32(rm3, a->vd, 1, MO_32);
    tcg_temp_free_i32(rm3);
    return true;
}

#define DO_2SN_64(INSN, FUNC, NARROWFUNC)                               \
    static bool trans_##INSN##_2sh(DisasContext *s, arg_2reg_shift *a)  \
    {                                                                   \
        return do_2shift_narrow_64(s, a, FUNC, NARROWFUNC);             \
    }
#define DO_2SN_32(INSN, FUNC, NARROWFUNC)                               \
    static bool trans_##INSN##_2sh(DisasContext *s, arg_2reg_shift *a)  \
    {                                                                   \
        return do_2shift_narrow_32(s, a, FUNC, NARROWFUNC);             \
    }

static void gen_neon_narrow_u32(TCGv_i32 dest, TCGv_ptr env, TCGv_i64 src)
{
    tcg_gen_extrl_i64_i32(dest, src);
}

static void gen_neon_narrow_u16(TCGv_i32 dest, TCGv_ptr env, TCGv_i64 src)
{
    gen_helper_neon_narrow_u16(dest, src);
}

static void gen_neon_narrow_u8(TCGv_i32 dest, TCGv_ptr env, TCGv_i64 src)
{
    gen_helper_neon_narrow_u8(dest, src);
}

DO_2SN_64(VSHRN_64, gen_ushl_i64, gen_neon_narrow_u32)
DO_2SN_32(VSHRN_32, gen_ushl_i32, gen_neon_narrow_u16)
DO_2SN_32(VSHRN_16, gen_helper_neon_shl_u16, gen_neon_narrow_u8)

DO_2SN_64(VRSHRN_64, gen_helper_neon_rshl_u64, gen_neon_narrow_u32)
DO_2SN_32(VRSHRN_32, gen_helper_neon_rshl_u32, gen_neon_narrow_u16)
DO_2SN_32(VRSHRN_16, gen_helper_neon_rshl_u16, gen_neon_narrow_u8)

DO_2SN_64(VQSHRUN_64, gen_sshl_i64, gen_helper_neon_unarrow_sat32)
DO_2SN_32(VQSHRUN_32, gen_sshl_i32, gen_helper_neon_unarrow_sat16)
DO_2SN_32(VQSHRUN_16, gen_helper_neon_shl_s16, gen_helper_neon_unarrow_sat8)

DO_2SN_64(VQRSHRUN_64, gen_helper_neon_rshl_s64, gen_helper_neon_unarrow_sat32)
DO_2SN_32(VQRSHRUN_32, gen_helper_neon_rshl_s32, gen_helper_neon_unarrow_sat16)
DO_2SN_32(VQRSHRUN_16, gen_helper_neon_rshl_s16, gen_helper_neon_unarrow_sat8)
DO_2SN_64(VQSHRN_S64, gen_sshl_i64, gen_helper_neon_narrow_sat_s32)
DO_2SN_32(VQSHRN_S32, gen_sshl_i32, gen_helper_neon_narrow_sat_s16)
DO_2SN_32(VQSHRN_S16, gen_helper_neon_shl_s16, gen_helper_neon_narrow_sat_s8)

DO_2SN_64(VQRSHRN_S64, gen_helper_neon_rshl_s64, gen_helper_neon_narrow_sat_s32)
DO_2SN_32(VQRSHRN_S32, gen_helper_neon_rshl_s32, gen_helper_neon_narrow_sat_s16)
DO_2SN_32(VQRSHRN_S16, gen_helper_neon_rshl_s16, gen_helper_neon_narrow_sat_s8)

DO_2SN_64(VQSHRN_U64, gen_ushl_i64, gen_helper_neon_narrow_sat_u32)
DO_2SN_32(VQSHRN_U32, gen_ushl_i32, gen_helper_neon_narrow_sat_u16)
DO_2SN_32(VQSHRN_U16, gen_helper_neon_shl_u16, gen_helper_neon_narrow_sat_u8)

DO_2SN_64(VQRSHRN_U64, gen_helper_neon_rshl_u64, gen_helper_neon_narrow_sat_u32)
DO_2SN_32(VQRSHRN_U32, gen_helper_neon_rshl_u32, gen_helper_neon_narrow_sat_u16)
DO_2SN_32(VQRSHRN_U16, gen_helper_neon_rshl_u16, gen_helper_neon_narrow_sat_u8)

static bool do_vshll_2sh(DisasContext *s, arg_2reg_shift *a,
                         NeonGenWidenFn *widenfn, bool u)
{
    TCGv_i64 tmp;
    TCGv_i32 rm0, rm1;
    uint64_t widen_mask = 0;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if (a->vd & 1) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    /*
     * This is a widen-and-shift operation. The shift is always less
     * than the width of the source type, so after widening the input
     * vector we can simply shift the whole 64-bit widened register,
     * and then clear the potential overflow bits resulting from left
     * bits of the narrow input appearing as right bits of the left
     * neighbour narrow input. Calculate a mask of bits to clear.
     */
    if ((a->shift != 0) && (a->size < 2 || u)) {
        int esize = 8 << a->size;
        widen_mask = MAKE_64BIT_MASK(0, esize);
        widen_mask >>= esize - a->shift;
        widen_mask = dup_const(a->size + 1, widen_mask);
    }

    rm0 = tcg_temp_new_i32();
    rm1 = tcg_temp_new_i32();
    read_neon_element32(rm0, a->vm, 0, MO_32);
    read_neon_element32(rm1, a->vm, 1, MO_32);
    tmp = tcg_temp_new_i64();

    widenfn(tmp, rm0);
    tcg_temp_free_i32(rm0);
    if (a->shift != 0) {
        tcg_gen_shli_i64(tmp, tmp, a->shift);
        tcg_gen_andi_i64(tmp, tmp, ~widen_mask);
    }
    write_neon_element64(tmp, a->vd, 0, MO_64);

    widenfn(tmp, rm1);
    tcg_temp_free_i32(rm1);
    if (a->shift != 0) {
        tcg_gen_shli_i64(tmp, tmp, a->shift);
        tcg_gen_andi_i64(tmp, tmp, ~widen_mask);
    }
    write_neon_element64(tmp, a->vd, 1, MO_64);
    tcg_temp_free_i64(tmp);
    return true;
}

static bool trans_VSHLL_S_2sh(DisasContext *s, arg_2reg_shift *a)
{
    static NeonGenWidenFn * const widenfn[] = {
        gen_helper_neon_widen_s8,
        gen_helper_neon_widen_s16,
        tcg_gen_ext_i32_i64,
    };
    return do_vshll_2sh(s, a, widenfn[a->size], false);
}

static bool trans_VSHLL_U_2sh(DisasContext *s, arg_2reg_shift *a)
{
    static NeonGenWidenFn * const widenfn[] = {
        gen_helper_neon_widen_u8,
        gen_helper_neon_widen_u16,
        tcg_gen_extu_i32_i64,
    };
    return do_vshll_2sh(s, a, widenfn[a->size], true);
}

static bool do_fp_2sh(DisasContext *s, arg_2reg_shift *a,
                      gen_helper_gvec_2_ptr *fn)
{
    /* FP operations in 2-reg-and-shift group */
    int vec_size = a->q ? 16 : 8;
    int rd_ofs = neon_full_reg_offset(a->vd);
    int rm_ofs = neon_full_reg_offset(a->vm);
    TCGv_ptr fpst;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    if (a->size == MO_16) {
        if (!dc_isar_feature(aa32_fp16_arith, s)) {
            return false;
        }
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vm | a->vd) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    fpst = fpstatus_ptr(a->size == MO_16 ? FPST_STD_F16 : FPST_STD);
    tcg_gen_gvec_2_ptr(rd_ofs, rm_ofs, fpst, vec_size, vec_size, a->shift, fn);
    tcg_temp_free_ptr(fpst);
    return true;
}

#define DO_FP_2SH(INSN, FUNC)                                           \
    static bool trans_##INSN##_2sh(DisasContext *s, arg_2reg_shift *a)  \
    {                                                                   \
        return do_fp_2sh(s, a, FUNC);                                   \
    }

DO_FP_2SH(VCVT_SF, gen_helper_gvec_vcvt_sf)
DO_FP_2SH(VCVT_UF, gen_helper_gvec_vcvt_uf)
DO_FP_2SH(VCVT_FS, gen_helper_gvec_vcvt_fs)
DO_FP_2SH(VCVT_FU, gen_helper_gvec_vcvt_fu)

DO_FP_2SH(VCVT_SH, gen_helper_gvec_vcvt_sh)
DO_FP_2SH(VCVT_UH, gen_helper_gvec_vcvt_uh)
DO_FP_2SH(VCVT_HS, gen_helper_gvec_vcvt_hs)
DO_FP_2SH(VCVT_HU, gen_helper_gvec_vcvt_hu)

static bool do_1reg_imm(DisasContext *s, arg_1reg_imm *a,
                        GVecGen2iFn *fn)
{
    uint64_t imm;
    int reg_ofs, vec_size;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) && (a->vd & 0x10)) {
        return false;
    }

    if (a->vd & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    reg_ofs = neon_full_reg_offset(a->vd);
    vec_size = a->q ? 16 : 8;
    imm = asimd_imm_const(a->imm, a->cmode, a->op);

    fn(MO_64, reg_ofs, reg_ofs, imm, vec_size, vec_size);
    return true;
}

static void gen_VMOV_1r(unsigned vece, uint32_t dofs, uint32_t aofs,
                        int64_t c, uint32_t oprsz, uint32_t maxsz)
{
    tcg_gen_gvec_dup_imm(MO_64, dofs, oprsz, maxsz, c);
}

static bool trans_Vimm_1r(DisasContext *s, arg_1reg_imm *a)
{
    /* Handle decode of cmode/op here between VORR/VBIC/VMOV */
    GVecGen2iFn *fn;

    if ((a->cmode & 1) && a->cmode < 12) {
        /* for op=1, the imm will be inverted, so BIC becomes AND. */
        fn = a->op ? tcg_gen_gvec_andi : tcg_gen_gvec_ori;
    } else {
        /* There is one unallocated cmode/op combination in this space */
        if (a->cmode == 15 && a->op == 1) {
            return false;
        }
        fn = gen_VMOV_1r;
    }
    return do_1reg_imm(s, a, fn);
}

static bool do_prewiden_3d(DisasContext *s, arg_3diff *a,
                           NeonGenWidenFn *widenfn,
                           NeonGenTwo64OpFn *opfn,
                           int src1_mop, int src2_mop)
{
    /* 3-regs different lengths, prewidening case (VADDL/VSUBL/VAADW/VSUBW) */
    TCGv_i64 rn0_64, rn1_64, rm_64;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if (!opfn) {
        /* size == 3 case, which is an entirely different insn group */
        return false;
    }

    if ((a->vd & 1) || (src1_mop == MO_Q && (a->vn & 1))) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    rn0_64 = tcg_temp_new_i64();
    rn1_64 = tcg_temp_new_i64();
    rm_64 = tcg_temp_new_i64();

    if (src1_mop >= 0) {
        read_neon_element64(rn0_64, a->vn, 0, src1_mop);
    } else {
        TCGv_i32 tmp = tcg_temp_new_i32();
        read_neon_element32(tmp, a->vn, 0, MO_32);
        widenfn(rn0_64, tmp);
        tcg_temp_free_i32(tmp);
    }
    if (src2_mop >= 0) {
        read_neon_element64(rm_64, a->vm, 0, src2_mop);
    } else {
        TCGv_i32 tmp = tcg_temp_new_i32();
        read_neon_element32(tmp, a->vm, 0, MO_32);
        widenfn(rm_64, tmp);
        tcg_temp_free_i32(tmp);
    }

    opfn(rn0_64, rn0_64, rm_64);

    /*
     * Load second pass inputs before storing the first pass result, to
     * avoid incorrect results if a narrow input overlaps with the result.
     */
    if (src1_mop >= 0) {
        read_neon_element64(rn1_64, a->vn, 1, src1_mop);
    } else {
        TCGv_i32 tmp = tcg_temp_new_i32();
        read_neon_element32(tmp, a->vn, 1, MO_32);
        widenfn(rn1_64, tmp);
        tcg_temp_free_i32(tmp);
    }
    if (src2_mop >= 0) {
        read_neon_element64(rm_64, a->vm, 1, src2_mop);
    } else {
        TCGv_i32 tmp = tcg_temp_new_i32();
        read_neon_element32(tmp, a->vm, 1, MO_32);
        widenfn(rm_64, tmp);
        tcg_temp_free_i32(tmp);
    }

    write_neon_element64(rn0_64, a->vd, 0, MO_64);

    opfn(rn1_64, rn1_64, rm_64);
    write_neon_element64(rn1_64, a->vd, 1, MO_64);

    tcg_temp_free_i64(rn0_64);
    tcg_temp_free_i64(rn1_64);
    tcg_temp_free_i64(rm_64);

    return true;
}

#define DO_PREWIDEN(INSN, S, OP, SRC1WIDE, SIGN)                        \
    static bool trans_##INSN##_3d(DisasContext *s, arg_3diff *a)        \
    {                                                                   \
        static NeonGenWidenFn * const widenfn[] = {                     \
            gen_helper_neon_widen_##S##8,                               \
            gen_helper_neon_widen_##S##16,                              \
            NULL, NULL,                                                 \
        };                                                              \
        static NeonGenTwo64OpFn * const addfn[] = {                     \
            gen_helper_neon_##OP##l_u16,                                \
            gen_helper_neon_##OP##l_u32,                                \
            tcg_gen_##OP##_i64,                                         \
            NULL,                                                       \
        };                                                              \
        int narrow_mop = a->size == MO_32 ? MO_32 | SIGN : -1;          \
        return do_prewiden_3d(s, a, widenfn[a->size], addfn[a->size],   \
                              SRC1WIDE ? MO_Q : narrow_mop,             \
                              narrow_mop);                              \
    }

DO_PREWIDEN(VADDL_S, s, add, false, MO_SIGN)
DO_PREWIDEN(VADDL_U, u, add, false, 0)
DO_PREWIDEN(VSUBL_S, s, sub, false, MO_SIGN)
DO_PREWIDEN(VSUBL_U, u, sub, false, 0)
DO_PREWIDEN(VADDW_S, s, add, true, MO_SIGN)
DO_PREWIDEN(VADDW_U, u, add, true, 0)
DO_PREWIDEN(VSUBW_S, s, sub, true, MO_SIGN)
DO_PREWIDEN(VSUBW_U, u, sub, true, 0)

static bool do_narrow_3d(DisasContext *s, arg_3diff *a,
                         NeonGenTwo64OpFn *opfn, NeonGenNarrowFn *narrowfn)
{
    /* 3-regs different lengths, narrowing (VADDHN/VSUBHN/VRADDHN/VRSUBHN) */
    TCGv_i64 rn_64, rm_64;
    TCGv_i32 rd0, rd1;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if (!opfn || !narrowfn) {
        /* size == 3 case, which is an entirely different insn group */
        return false;
    }

    if ((a->vn | a->vm) & 1) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    rn_64 = tcg_temp_new_i64();
    rm_64 = tcg_temp_new_i64();
    rd0 = tcg_temp_new_i32();
    rd1 = tcg_temp_new_i32();

    read_neon_element64(rn_64, a->vn, 0, MO_64);
    read_neon_element64(rm_64, a->vm, 0, MO_64);

    opfn(rn_64, rn_64, rm_64);

    narrowfn(rd0, rn_64);

    read_neon_element64(rn_64, a->vn, 1, MO_64);
    read_neon_element64(rm_64, a->vm, 1, MO_64);

    opfn(rn_64, rn_64, rm_64);

    narrowfn(rd1, rn_64);

    write_neon_element32(rd0, a->vd, 0, MO_32);
    write_neon_element32(rd1, a->vd, 1, MO_32);

    tcg_temp_free_i32(rd0);
    tcg_temp_free_i32(rd1);
    tcg_temp_free_i64(rn_64);
    tcg_temp_free_i64(rm_64);

    return true;
}

#define DO_NARROW_3D(INSN, OP, NARROWTYPE, EXTOP)                       \
    static bool trans_##INSN##_3d(DisasContext *s, arg_3diff *a)        \
    {                                                                   \
        static NeonGenTwo64OpFn * const addfn[] = {                     \
            gen_helper_neon_##OP##l_u16,                                \
            gen_helper_neon_##OP##l_u32,                                \
            tcg_gen_##OP##_i64,                                         \
            NULL,                                                       \
        };                                                              \
        static NeonGenNarrowFn * const narrowfn[] = {                   \
            gen_helper_neon_##NARROWTYPE##_high_u8,                     \
            gen_helper_neon_##NARROWTYPE##_high_u16,                    \
            EXTOP,                                                      \
            NULL,                                                       \
        };                                                              \
        return do_narrow_3d(s, a, addfn[a->size], narrowfn[a->size]);   \
    }

static void gen_narrow_round_high_u32(TCGv_i32 rd, TCGv_i64 rn)
{
    tcg_gen_addi_i64(rn, rn, 1u << 31);
    tcg_gen_extrh_i64_i32(rd, rn);
}

DO_NARROW_3D(VADDHN, add, narrow, tcg_gen_extrh_i64_i32)
DO_NARROW_3D(VSUBHN, sub, narrow, tcg_gen_extrh_i64_i32)
DO_NARROW_3D(VRADDHN, add, narrow_round, gen_narrow_round_high_u32)
DO_NARROW_3D(VRSUBHN, sub, narrow_round, gen_narrow_round_high_u32)

static bool do_long_3d(DisasContext *s, arg_3diff *a,
                       NeonGenTwoOpWidenFn *opfn,
                       NeonGenTwo64OpFn *accfn)
{
    /*
     * 3-regs different lengths, long operations.
     * These perform an operation on two inputs that returns a double-width
     * result, and then possibly perform an accumulation operation of
     * that result into the double-width destination.
     */
    TCGv_i64 rd0, rd1, tmp;
    TCGv_i32 rn, rm;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if (!opfn) {
        /* size == 3 case, which is an entirely different insn group */
        return false;
    }

    if (a->vd & 1) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    rd0 = tcg_temp_new_i64();
    rd1 = tcg_temp_new_i64();

    rn = tcg_temp_new_i32();
    rm = tcg_temp_new_i32();
    read_neon_element32(rn, a->vn, 0, MO_32);
    read_neon_element32(rm, a->vm, 0, MO_32);
    opfn(rd0, rn, rm);

    read_neon_element32(rn, a->vn, 1, MO_32);
    read_neon_element32(rm, a->vm, 1, MO_32);
    opfn(rd1, rn, rm);
    tcg_temp_free_i32(rn);
    tcg_temp_free_i32(rm);

    /* Don't store results until after all loads: they might overlap */
    if (accfn) {
        tmp = tcg_temp_new_i64();
        read_neon_element64(tmp, a->vd, 0, MO_64);
        accfn(rd0, tmp, rd0);
        read_neon_element64(tmp, a->vd, 1, MO_64);
        accfn(rd1, tmp, rd1);
        tcg_temp_free_i64(tmp);
    }

    write_neon_element64(rd0, a->vd, 0, MO_64);
    write_neon_element64(rd1, a->vd, 1, MO_64);
    tcg_temp_free_i64(rd0);
    tcg_temp_free_i64(rd1);

    return true;
}

static bool trans_VABDL_S_3d(DisasContext *s, arg_3diff *a)
{
    static NeonGenTwoOpWidenFn * const opfn[] = {
        gen_helper_neon_abdl_s16,
        gen_helper_neon_abdl_s32,
        gen_helper_neon_abdl_s64,
        NULL,
    };

    return do_long_3d(s, a, opfn[a->size], NULL);
}

static bool trans_VABDL_U_3d(DisasContext *s, arg_3diff *a)
{
    static NeonGenTwoOpWidenFn * const opfn[] = {
        gen_helper_neon_abdl_u16,
        gen_helper_neon_abdl_u32,
        gen_helper_neon_abdl_u64,
        NULL,
    };

    return do_long_3d(s, a, opfn[a->size], NULL);
}

static bool trans_VABAL_S_3d(DisasContext *s, arg_3diff *a)
{
    static NeonGenTwoOpWidenFn * const opfn[] = {
        gen_helper_neon_abdl_s16,
        gen_helper_neon_abdl_s32,
        gen_helper_neon_abdl_s64,
        NULL,
    };
    static NeonGenTwo64OpFn * const addfn[] = {
        gen_helper_neon_addl_u16,
        gen_helper_neon_addl_u32,
        tcg_gen_add_i64,
        NULL,
    };

    return do_long_3d(s, a, opfn[a->size], addfn[a->size]);
}

static bool trans_VABAL_U_3d(DisasContext *s, arg_3diff *a)
{
    static NeonGenTwoOpWidenFn * const opfn[] = {
        gen_helper_neon_abdl_u16,
        gen_helper_neon_abdl_u32,
        gen_helper_neon_abdl_u64,
        NULL,
    };
    static NeonGenTwo64OpFn * const addfn[] = {
        gen_helper_neon_addl_u16,
        gen_helper_neon_addl_u32,
        tcg_gen_add_i64,
        NULL,
    };

    return do_long_3d(s, a, opfn[a->size], addfn[a->size]);
}

static void gen_mull_s32(TCGv_i64 rd, TCGv_i32 rn, TCGv_i32 rm)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();

    tcg_gen_muls2_i32(lo, hi, rn, rm);
    tcg_gen_concat_i32_i64(rd, lo, hi);

    tcg_temp_free_i32(lo);
    tcg_temp_free_i32(hi);
}

static void gen_mull_u32(TCGv_i64 rd, TCGv_i32 rn, TCGv_i32 rm)
{
    TCGv_i32 lo = tcg_temp_new_i32();
    TCGv_i32 hi = tcg_temp_new_i32();

    tcg_gen_mulu2_i32(lo, hi, rn, rm);
    tcg_gen_concat_i32_i64(rd, lo, hi);

    tcg_temp_free_i32(lo);
    tcg_temp_free_i32(hi);
}

static bool trans_VMULL_S_3d(DisasContext *s, arg_3diff *a)
{
    static NeonGenTwoOpWidenFn * const opfn[] = {
        gen_helper_neon_mull_s8,
        gen_helper_neon_mull_s16,
        gen_mull_s32,
        NULL,
    };

    return do_long_3d(s, a, opfn[a->size], NULL);
}

static bool trans_VMULL_U_3d(DisasContext *s, arg_3diff *a)
{
    static NeonGenTwoOpWidenFn * const opfn[] = {
        gen_helper_neon_mull_u8,
        gen_helper_neon_mull_u16,
        gen_mull_u32,
        NULL,
    };

    return do_long_3d(s, a, opfn[a->size], NULL);
}

#define DO_VMLAL(INSN,MULL,ACC)                                         \
    static bool trans_##INSN##_3d(DisasContext *s, arg_3diff *a)        \
    {                                                                   \
        static NeonGenTwoOpWidenFn * const opfn[] = {                   \
            gen_helper_neon_##MULL##8,                                  \
            gen_helper_neon_##MULL##16,                                 \
            gen_##MULL##32,                                             \
            NULL,                                                       \
        };                                                              \
        static NeonGenTwo64OpFn * const accfn[] = {                     \
            gen_helper_neon_##ACC##l_u16,                               \
            gen_helper_neon_##ACC##l_u32,                               \
            tcg_gen_##ACC##_i64,                                        \
            NULL,                                                       \
        };                                                              \
        return do_long_3d(s, a, opfn[a->size], accfn[a->size]);         \
    }

DO_VMLAL(VMLAL_S,mull_s,add)
DO_VMLAL(VMLAL_U,mull_u,add)
DO_VMLAL(VMLSL_S,mull_s,sub)
DO_VMLAL(VMLSL_U,mull_u,sub)

static void gen_VQDMULL_16(TCGv_i64 rd, TCGv_i32 rn, TCGv_i32 rm)
{
    gen_helper_neon_mull_s16(rd, rn, rm);
    gen_helper_neon_addl_saturate_s32(rd, cpu_env, rd, rd);
}

static void gen_VQDMULL_32(TCGv_i64 rd, TCGv_i32 rn, TCGv_i32 rm)
{
    gen_mull_s32(rd, rn, rm);
    gen_helper_neon_addl_saturate_s64(rd, cpu_env, rd, rd);
}

static bool trans_VQDMULL_3d(DisasContext *s, arg_3diff *a)
{
    static NeonGenTwoOpWidenFn * const opfn[] = {
        NULL,
        gen_VQDMULL_16,
        gen_VQDMULL_32,
        NULL,
    };

    return do_long_3d(s, a, opfn[a->size], NULL);
}

static void gen_VQDMLAL_acc_16(TCGv_i64 rd, TCGv_i64 rn, TCGv_i64 rm)
{
    gen_helper_neon_addl_saturate_s32(rd, cpu_env, rn, rm);
}

static void gen_VQDMLAL_acc_32(TCGv_i64 rd, TCGv_i64 rn, TCGv_i64 rm)
{
    gen_helper_neon_addl_saturate_s64(rd, cpu_env, rn, rm);
}

static bool trans_VQDMLAL_3d(DisasContext *s, arg_3diff *a)
{
    static NeonGenTwoOpWidenFn * const opfn[] = {
        NULL,
        gen_VQDMULL_16,
        gen_VQDMULL_32,
        NULL,
    };
    static NeonGenTwo64OpFn * const accfn[] = {
        NULL,
        gen_VQDMLAL_acc_16,
        gen_VQDMLAL_acc_32,
        NULL,
    };

    return do_long_3d(s, a, opfn[a->size], accfn[a->size]);
}

static void gen_VQDMLSL_acc_16(TCGv_i64 rd, TCGv_i64 rn, TCGv_i64 rm)
{
    gen_helper_neon_negl_u32(rm, rm);
    gen_helper_neon_addl_saturate_s32(rd, cpu_env, rn, rm);
}

static void gen_VQDMLSL_acc_32(TCGv_i64 rd, TCGv_i64 rn, TCGv_i64 rm)
{
    tcg_gen_neg_i64(rm, rm);
    gen_helper_neon_addl_saturate_s64(rd, cpu_env, rn, rm);
}

static bool trans_VQDMLSL_3d(DisasContext *s, arg_3diff *a)
{
    static NeonGenTwoOpWidenFn * const opfn[] = {
        NULL,
        gen_VQDMULL_16,
        gen_VQDMULL_32,
        NULL,
    };
    static NeonGenTwo64OpFn * const accfn[] = {
        NULL,
        gen_VQDMLSL_acc_16,
        gen_VQDMLSL_acc_32,
        NULL,
    };

    return do_long_3d(s, a, opfn[a->size], accfn[a->size]);
}

static bool trans_VMULL_P_3d(DisasContext *s, arg_3diff *a)
{
    gen_helper_gvec_3 *fn_gvec;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if (a->vd & 1) {
        return false;
    }

    switch (a->size) {
    case 0:
        fn_gvec = gen_helper_neon_pmull_h;
        break;
    case 2:
        if (!dc_isar_feature(aa32_pmull, s)) {
            return false;
        }
        fn_gvec = gen_helper_gvec_pmull_q;
        break;
    default:
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    tcg_gen_gvec_3_ool(neon_full_reg_offset(a->vd),
                       neon_full_reg_offset(a->vn),
                       neon_full_reg_offset(a->vm),
                       16, 16, 0, fn_gvec);
    return true;
}

static void gen_neon_dup_low16(TCGv_i32 var)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(var, var);
    tcg_gen_shli_i32(tmp, var, 16);
    tcg_gen_or_i32(var, var, tmp);
    tcg_temp_free_i32(tmp);
}

static void gen_neon_dup_high16(TCGv_i32 var)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_andi_i32(var, var, 0xffff0000);
    tcg_gen_shri_i32(tmp, var, 16);
    tcg_gen_or_i32(var, var, tmp);
    tcg_temp_free_i32(tmp);
}

static inline TCGv_i32 neon_get_scalar(int size, int reg)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    if (size == MO_16) {
        read_neon_element32(tmp, reg & 7, reg >> 4, MO_32);
        if (reg & 8) {
            gen_neon_dup_high16(tmp);
        } else {
            gen_neon_dup_low16(tmp);
        }
    } else {
        read_neon_element32(tmp, reg & 15, reg >> 4, MO_32);
    }
    return tmp;
}

static bool do_2scalar(DisasContext *s, arg_2scalar *a,
                       NeonGenTwoOpFn *opfn, NeonGenTwoOpFn *accfn)
{
    /*
     * Two registers and a scalar: perform an operation between
     * the input elements and the scalar, and then possibly
     * perform an accumulation operation of that result into the
     * destination.
     */
    TCGv_i32 scalar, tmp;
    int pass;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if (!opfn) {
        /* Bad size (including size == 3, which is a different insn group) */
        return false;
    }

    if (a->q && ((a->vd | a->vn) & 1)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    scalar = neon_get_scalar(a->size, a->vm);
    tmp = tcg_temp_new_i32();

    for (pass = 0; pass < (a->q ? 4 : 2); pass++) {
        read_neon_element32(tmp, a->vn, pass, MO_32);
        opfn(tmp, tmp, scalar);
        if (accfn) {
            TCGv_i32 rd = tcg_temp_new_i32();
            read_neon_element32(rd, a->vd, pass, MO_32);
            accfn(tmp, rd, tmp);
            tcg_temp_free_i32(rd);
        }
        write_neon_element32(tmp, a->vd, pass, MO_32);
    }
    tcg_temp_free_i32(tmp);
    tcg_temp_free_i32(scalar);
    return true;
}

static bool trans_VMUL_2sc(DisasContext *s, arg_2scalar *a)
{
    static NeonGenTwoOpFn * const opfn[] = {
        NULL,
        gen_helper_neon_mul_u16,
        tcg_gen_mul_i32,
        NULL,
    };

    return do_2scalar(s, a, opfn[a->size], NULL);
}

static bool trans_VMLA_2sc(DisasContext *s, arg_2scalar *a)
{
    static NeonGenTwoOpFn * const opfn[] = {
        NULL,
        gen_helper_neon_mul_u16,
        tcg_gen_mul_i32,
        NULL,
    };
    static NeonGenTwoOpFn * const accfn[] = {
        NULL,
        gen_helper_neon_add_u16,
        tcg_gen_add_i32,
        NULL,
    };

    return do_2scalar(s, a, opfn[a->size], accfn[a->size]);
}

static bool trans_VMLS_2sc(DisasContext *s, arg_2scalar *a)
{
    static NeonGenTwoOpFn * const opfn[] = {
        NULL,
        gen_helper_neon_mul_u16,
        tcg_gen_mul_i32,
        NULL,
    };
    static NeonGenTwoOpFn * const accfn[] = {
        NULL,
        gen_helper_neon_sub_u16,
        tcg_gen_sub_i32,
        NULL,
    };

    return do_2scalar(s, a, opfn[a->size], accfn[a->size]);
}

static bool do_2scalar_fp_vec(DisasContext *s, arg_2scalar *a,
                              gen_helper_gvec_3_ptr *fn)
{
    /* Two registers and a scalar, using gvec */
    int vec_size = a->q ? 16 : 8;
    int rd_ofs = neon_full_reg_offset(a->vd);
    int rn_ofs = neon_full_reg_offset(a->vn);
    int rm_ofs;
    int idx;
    TCGv_ptr fpstatus;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if (!fn) {
        /* Bad size (including size == 3, which is a different insn group) */
        return false;
    }

    if (a->q && ((a->vd | a->vn) & 1)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    /* a->vm is M:Vm, which encodes both register and index */
    idx = extract32(a->vm, a->size + 2, 2);
    a->vm = extract32(a->vm, 0, a->size + 2);
    rm_ofs = neon_full_reg_offset(a->vm);

    fpstatus = fpstatus_ptr(a->size == 1 ? FPST_STD_F16 : FPST_STD);
    tcg_gen_gvec_3_ptr(rd_ofs, rn_ofs, rm_ofs, fpstatus,
                       vec_size, vec_size, idx, fn);
    tcg_temp_free_ptr(fpstatus);
    return true;
}

#define DO_VMUL_F_2sc(NAME, FUNC)                                       \
    static bool trans_##NAME##_F_2sc(DisasContext *s, arg_2scalar *a)   \
    {                                                                   \
        static gen_helper_gvec_3_ptr * const opfn[] = {                 \
            NULL,                                                       \
            gen_helper_##FUNC##_h,                                      \
            gen_helper_##FUNC##_s,                                      \
            NULL,                                                       \
        };                                                              \
        if (a->size == MO_16 && !dc_isar_feature(aa32_fp16_arith, s)) { \
            return false;                                               \
        }                                                               \
        return do_2scalar_fp_vec(s, a, opfn[a->size]);                  \
    }

DO_VMUL_F_2sc(VMUL, gvec_fmul_idx)
DO_VMUL_F_2sc(VMLA, gvec_fmla_nf_idx)
DO_VMUL_F_2sc(VMLS, gvec_fmls_nf_idx)

WRAP_ENV_FN(gen_VQDMULH_16, gen_helper_neon_qdmulh_s16)
WRAP_ENV_FN(gen_VQDMULH_32, gen_helper_neon_qdmulh_s32)
WRAP_ENV_FN(gen_VQRDMULH_16, gen_helper_neon_qrdmulh_s16)
WRAP_ENV_FN(gen_VQRDMULH_32, gen_helper_neon_qrdmulh_s32)

static bool trans_VQDMULH_2sc(DisasContext *s, arg_2scalar *a)
{
    static NeonGenTwoOpFn * const opfn[] = {
        NULL,
        gen_VQDMULH_16,
        gen_VQDMULH_32,
        NULL,
    };

    return do_2scalar(s, a, opfn[a->size], NULL);
}

static bool trans_VQRDMULH_2sc(DisasContext *s, arg_2scalar *a)
{
    static NeonGenTwoOpFn * const opfn[] = {
        NULL,
        gen_VQRDMULH_16,
        gen_VQRDMULH_32,
        NULL,
    };

    return do_2scalar(s, a, opfn[a->size], NULL);
}

static bool do_vqrdmlah_2sc(DisasContext *s, arg_2scalar *a,
                            NeonGenThreeOpEnvFn *opfn)
{
    /*
     * VQRDMLAH/VQRDMLSH: this is like do_2scalar, but the opfn
     * performs a kind of fused op-then-accumulate using a helper
     * function that takes all of rd, rn and the scalar at once.
     */
    TCGv_i32 scalar, rn, rd;
    int pass;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    if (!dc_isar_feature(aa32_rdm, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if (!opfn) {
        /* Bad size (including size == 3, which is a different insn group) */
        return false;
    }

    if (a->q && ((a->vd | a->vn) & 1)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    scalar = neon_get_scalar(a->size, a->vm);
    rn = tcg_temp_new_i32();
    rd = tcg_temp_new_i32();

    for (pass = 0; pass < (a->q ? 4 : 2); pass++) {
        read_neon_element32(rn, a->vn, pass, MO_32);
        read_neon_element32(rd, a->vd, pass, MO_32);
        opfn(rd, cpu_env, rn, scalar, rd);
        write_neon_element32(rd, a->vd, pass, MO_32);
    }
    tcg_temp_free_i32(rn);
    tcg_temp_free_i32(rd);
    tcg_temp_free_i32(scalar);

    return true;
}

static bool trans_VQRDMLAH_2sc(DisasContext *s, arg_2scalar *a)
{
    static NeonGenThreeOpEnvFn *opfn[] = {
        NULL,
        gen_helper_neon_qrdmlah_s16,
        gen_helper_neon_qrdmlah_s32,
        NULL,
    };
    return do_vqrdmlah_2sc(s, a, opfn[a->size]);
}

static bool trans_VQRDMLSH_2sc(DisasContext *s, arg_2scalar *a)
{
    static NeonGenThreeOpEnvFn *opfn[] = {
        NULL,
        gen_helper_neon_qrdmlsh_s16,
        gen_helper_neon_qrdmlsh_s32,
        NULL,
    };
    return do_vqrdmlah_2sc(s, a, opfn[a->size]);
}

static bool do_2scalar_long(DisasContext *s, arg_2scalar *a,
                            NeonGenTwoOpWidenFn *opfn,
                            NeonGenTwo64OpFn *accfn)
{
    /*
     * Two registers and a scalar, long operations: perform an
     * operation on the input elements and the scalar which produces
     * a double-width result, and then possibly perform an accumulation
     * operation of that result into the destination.
     */
    TCGv_i32 scalar, rn;
    TCGv_i64 rn0_64, rn1_64;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if (!opfn) {
        /* Bad size (including size == 3, which is a different insn group) */
        return false;
    }

    if (a->vd & 1) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    scalar = neon_get_scalar(a->size, a->vm);

    /* Load all inputs before writing any outputs, in case of overlap */
    rn = tcg_temp_new_i32();
    read_neon_element32(rn, a->vn, 0, MO_32);
    rn0_64 = tcg_temp_new_i64();
    opfn(rn0_64, rn, scalar);

    read_neon_element32(rn, a->vn, 1, MO_32);
    rn1_64 = tcg_temp_new_i64();
    opfn(rn1_64, rn, scalar);
    tcg_temp_free_i32(rn);
    tcg_temp_free_i32(scalar);

    if (accfn) {
        TCGv_i64 t64 = tcg_temp_new_i64();
        read_neon_element64(t64, a->vd, 0, MO_64);
        accfn(rn0_64, t64, rn0_64);
        read_neon_element64(t64, a->vd, 1, MO_64);
        accfn(rn1_64, t64, rn1_64);
        tcg_temp_free_i64(t64);
    }

    write_neon_element64(rn0_64, a->vd, 0, MO_64);
    write_neon_element64(rn1_64, a->vd, 1, MO_64);
    tcg_temp_free_i64(rn0_64);
    tcg_temp_free_i64(rn1_64);
    return true;
}

static bool trans_VMULL_S_2sc(DisasContext *s, arg_2scalar *a)
{
    static NeonGenTwoOpWidenFn * const opfn[] = {
        NULL,
        gen_helper_neon_mull_s16,
        gen_mull_s32,
        NULL,
    };

    return do_2scalar_long(s, a, opfn[a->size], NULL);
}

static bool trans_VMULL_U_2sc(DisasContext *s, arg_2scalar *a)
{
    static NeonGenTwoOpWidenFn * const opfn[] = {
        NULL,
        gen_helper_neon_mull_u16,
        gen_mull_u32,
        NULL,
    };

    return do_2scalar_long(s, a, opfn[a->size], NULL);
}

#define DO_VMLAL_2SC(INSN, MULL, ACC)                                   \
    static bool trans_##INSN##_2sc(DisasContext *s, arg_2scalar *a)     \
    {                                                                   \
        static NeonGenTwoOpWidenFn * const opfn[] = {                   \
            NULL,                                                       \
            gen_helper_neon_##MULL##16,                                 \
            gen_##MULL##32,                                             \
            NULL,                                                       \
        };                                                              \
        static NeonGenTwo64OpFn * const accfn[] = {                     \
            NULL,                                                       \
            gen_helper_neon_##ACC##l_u32,                               \
            tcg_gen_##ACC##_i64,                                        \
            NULL,                                                       \
        };                                                              \
        return do_2scalar_long(s, a, opfn[a->size], accfn[a->size]);    \
    }

DO_VMLAL_2SC(VMLAL_S, mull_s, add)
DO_VMLAL_2SC(VMLAL_U, mull_u, add)
DO_VMLAL_2SC(VMLSL_S, mull_s, sub)
DO_VMLAL_2SC(VMLSL_U, mull_u, sub)

static bool trans_VQDMULL_2sc(DisasContext *s, arg_2scalar *a)
{
    static NeonGenTwoOpWidenFn * const opfn[] = {
        NULL,
        gen_VQDMULL_16,
        gen_VQDMULL_32,
        NULL,
    };

    return do_2scalar_long(s, a, opfn[a->size], NULL);
}

static bool trans_VQDMLAL_2sc(DisasContext *s, arg_2scalar *a)
{
    static NeonGenTwoOpWidenFn * const opfn[] = {
        NULL,
        gen_VQDMULL_16,
        gen_VQDMULL_32,
        NULL,
    };
    static NeonGenTwo64OpFn * const accfn[] = {
        NULL,
        gen_VQDMLAL_acc_16,
        gen_VQDMLAL_acc_32,
        NULL,
    };

    return do_2scalar_long(s, a, opfn[a->size], accfn[a->size]);
}

static bool trans_VQDMLSL_2sc(DisasContext *s, arg_2scalar *a)
{
    static NeonGenTwoOpWidenFn * const opfn[] = {
        NULL,
        gen_VQDMULL_16,
        gen_VQDMULL_32,
        NULL,
    };
    static NeonGenTwo64OpFn * const accfn[] = {
        NULL,
        gen_VQDMLSL_acc_16,
        gen_VQDMLSL_acc_32,
        NULL,
    };

    return do_2scalar_long(s, a, opfn[a->size], accfn[a->size]);
}

static bool trans_VEXT(DisasContext *s, arg_VEXT *a)
{
    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
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

    if (a->imm > 7 && !a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    if (!a->q) {
        /* Extract 64 bits from <Vm:Vn> */
        TCGv_i64 left, right, dest;

        left = tcg_temp_new_i64();
        right = tcg_temp_new_i64();
        dest = tcg_temp_new_i64();

        read_neon_element64(right, a->vn, 0, MO_64);
        read_neon_element64(left, a->vm, 0, MO_64);
        tcg_gen_extract2_i64(dest, right, left, a->imm * 8);
        write_neon_element64(dest, a->vd, 0, MO_64);

        tcg_temp_free_i64(left);
        tcg_temp_free_i64(right);
        tcg_temp_free_i64(dest);
    } else {
        /* Extract 128 bits from <Vm+1:Vm:Vn+1:Vn> */
        TCGv_i64 left, middle, right, destleft, destright;

        left = tcg_temp_new_i64();
        middle = tcg_temp_new_i64();
        right = tcg_temp_new_i64();
        destleft = tcg_temp_new_i64();
        destright = tcg_temp_new_i64();

        if (a->imm < 8) {
            read_neon_element64(right, a->vn, 0, MO_64);
            read_neon_element64(middle, a->vn, 1, MO_64);
            tcg_gen_extract2_i64(destright, right, middle, a->imm * 8);
            read_neon_element64(left, a->vm, 0, MO_64);
            tcg_gen_extract2_i64(destleft, middle, left, a->imm * 8);
        } else {
            read_neon_element64(right, a->vn, 1, MO_64);
            read_neon_element64(middle, a->vm, 0, MO_64);
            tcg_gen_extract2_i64(destright, right, middle, (a->imm - 8) * 8);
            read_neon_element64(left, a->vm, 1, MO_64);
            tcg_gen_extract2_i64(destleft, middle, left, (a->imm - 8) * 8);
        }

        write_neon_element64(destright, a->vd, 0, MO_64);
        write_neon_element64(destleft, a->vd, 1, MO_64);

        tcg_temp_free_i64(destright);
        tcg_temp_free_i64(destleft);
        tcg_temp_free_i64(right);
        tcg_temp_free_i64(middle);
        tcg_temp_free_i64(left);
    }
    return true;
}

static bool trans_VTBL(DisasContext *s, arg_VTBL *a)
{
    TCGv_i64 val, def;
    TCGv_i32 desc;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vn + a->len + 1) > 32) {
        /*
         * This is UNPREDICTABLE; we choose to UNDEF to avoid the
         * helper function running off the end of the register file.
         */
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    desc = tcg_const_i32((a->vn << 2) | a->len);
    def = tcg_temp_new_i64();
    if (a->op) {
        read_neon_element64(def, a->vd, 0, MO_64);
    } else {
        tcg_gen_movi_i64(def, 0);
    }
    val = tcg_temp_new_i64();
    read_neon_element64(val, a->vm, 0, MO_64);

    gen_helper_neon_tbl(val, cpu_env, desc, val, def);
    write_neon_element64(val, a->vd, 0, MO_64);

    tcg_temp_free_i64(def);
    tcg_temp_free_i64(val);
    tcg_temp_free_i32(desc);
    return true;
}

static bool trans_VDUP_scalar(DisasContext *s, arg_VDUP_scalar *a)
{
    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if (a->vd & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    tcg_gen_gvec_dup_mem(a->size, neon_full_reg_offset(a->vd),
                         neon_element_offset(a->vm, a->index, a->size),
                         a->q ? 16 : 8, a->q ? 16 : 8);
    return true;
}

static bool trans_VREV64(DisasContext *s, arg_VREV64 *a)
{
    int pass, half;
    TCGv_i32 tmp[2];

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vd | a->vm) & a->q) {
        return false;
    }

    if (a->size == 3) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    tmp[0] = tcg_temp_new_i32();
    tmp[1] = tcg_temp_new_i32();

    for (pass = 0; pass < (a->q ? 2 : 1); pass++) {
        for (half = 0; half < 2; half++) {
            read_neon_element32(tmp[half], a->vm, pass * 2 + half, MO_32);
            switch (a->size) {
            case 0:
                tcg_gen_bswap32_i32(tmp[half], tmp[half]);
                break;
            case 1:
                gen_swap_half(tmp[half], tmp[half]);
                break;
            case 2:
                break;
            default:
                g_assert_not_reached();
            }
        }
        write_neon_element32(tmp[1], a->vd, pass * 2, MO_32);
        write_neon_element32(tmp[0], a->vd, pass * 2 + 1, MO_32);
    }

    tcg_temp_free_i32(tmp[0]);
    tcg_temp_free_i32(tmp[1]);
    return true;
}

static bool do_2misc_pairwise(DisasContext *s, arg_2misc *a,
                              NeonGenWidenFn *widenfn,
                              NeonGenTwo64OpFn *opfn,
                              NeonGenTwo64OpFn *accfn)
{
    /*
     * Pairwise long operations: widen both halves of the pair,
     * combine the pairs with the opfn, and then possibly accumulate
     * into the destination with the accfn.
     */
    int pass;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vd | a->vm) & a->q) {
        return false;
    }

    if (!widenfn) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    for (pass = 0; pass < a->q + 1; pass++) {
        TCGv_i32 tmp;
        TCGv_i64 rm0_64, rm1_64, rd_64;

        rm0_64 = tcg_temp_new_i64();
        rm1_64 = tcg_temp_new_i64();
        rd_64 = tcg_temp_new_i64();

        tmp = tcg_temp_new_i32();
        read_neon_element32(tmp, a->vm, pass * 2, MO_32);
        widenfn(rm0_64, tmp);
        read_neon_element32(tmp, a->vm, pass * 2 + 1, MO_32);
        widenfn(rm1_64, tmp);
        tcg_temp_free_i32(tmp);

        opfn(rd_64, rm0_64, rm1_64);
        tcg_temp_free_i64(rm0_64);
        tcg_temp_free_i64(rm1_64);

        if (accfn) {
            TCGv_i64 tmp64 = tcg_temp_new_i64();
            read_neon_element64(tmp64, a->vd, pass, MO_64);
            accfn(rd_64, tmp64, rd_64);
            tcg_temp_free_i64(tmp64);
        }
        write_neon_element64(rd_64, a->vd, pass, MO_64);
        tcg_temp_free_i64(rd_64);
    }
    return true;
}

static bool trans_VPADDL_S(DisasContext *s, arg_2misc *a)
{
    static NeonGenWidenFn * const widenfn[] = {
        gen_helper_neon_widen_s8,
        gen_helper_neon_widen_s16,
        tcg_gen_ext_i32_i64,
        NULL,
    };
    static NeonGenTwo64OpFn * const opfn[] = {
        gen_helper_neon_paddl_u16,
        gen_helper_neon_paddl_u32,
        tcg_gen_add_i64,
        NULL,
    };

    return do_2misc_pairwise(s, a, widenfn[a->size], opfn[a->size], NULL);
}

static bool trans_VPADDL_U(DisasContext *s, arg_2misc *a)
{
    static NeonGenWidenFn * const widenfn[] = {
        gen_helper_neon_widen_u8,
        gen_helper_neon_widen_u16,
        tcg_gen_extu_i32_i64,
        NULL,
    };
    static NeonGenTwo64OpFn * const opfn[] = {
        gen_helper_neon_paddl_u16,
        gen_helper_neon_paddl_u32,
        tcg_gen_add_i64,
        NULL,
    };

    return do_2misc_pairwise(s, a, widenfn[a->size], opfn[a->size], NULL);
}

static bool trans_VPADAL_S(DisasContext *s, arg_2misc *a)
{
    static NeonGenWidenFn * const widenfn[] = {
        gen_helper_neon_widen_s8,
        gen_helper_neon_widen_s16,
        tcg_gen_ext_i32_i64,
        NULL,
    };
    static NeonGenTwo64OpFn * const opfn[] = {
        gen_helper_neon_paddl_u16,
        gen_helper_neon_paddl_u32,
        tcg_gen_add_i64,
        NULL,
    };
    static NeonGenTwo64OpFn * const accfn[] = {
        gen_helper_neon_addl_u16,
        gen_helper_neon_addl_u32,
        tcg_gen_add_i64,
        NULL,
    };

    return do_2misc_pairwise(s, a, widenfn[a->size], opfn[a->size],
                             accfn[a->size]);
}

static bool trans_VPADAL_U(DisasContext *s, arg_2misc *a)
{
    static NeonGenWidenFn * const widenfn[] = {
        gen_helper_neon_widen_u8,
        gen_helper_neon_widen_u16,
        tcg_gen_extu_i32_i64,
        NULL,
    };
    static NeonGenTwo64OpFn * const opfn[] = {
        gen_helper_neon_paddl_u16,
        gen_helper_neon_paddl_u32,
        tcg_gen_add_i64,
        NULL,
    };
    static NeonGenTwo64OpFn * const accfn[] = {
        gen_helper_neon_addl_u16,
        gen_helper_neon_addl_u32,
        tcg_gen_add_i64,
        NULL,
    };

    return do_2misc_pairwise(s, a, widenfn[a->size], opfn[a->size],
                             accfn[a->size]);
}

typedef void ZipFn(TCGv_ptr, TCGv_ptr);

static bool do_zip_uzp(DisasContext *s, arg_2misc *a,
                       ZipFn *fn)
{
    TCGv_ptr pd, pm;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vd | a->vm) & a->q) {
        return false;
    }

    if (!fn) {
        /* Bad size or size/q combination */
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    pd = vfp_reg_ptr(true, a->vd);
    pm = vfp_reg_ptr(true, a->vm);
    fn(pd, pm);
    tcg_temp_free_ptr(pd);
    tcg_temp_free_ptr(pm);
    return true;
}

static bool trans_VUZP(DisasContext *s, arg_2misc *a)
{
    static ZipFn * const fn[2][4] = {
        {
            gen_helper_neon_unzip8,
            gen_helper_neon_unzip16,
            NULL,
            NULL,
        }, {
            gen_helper_neon_qunzip8,
            gen_helper_neon_qunzip16,
            gen_helper_neon_qunzip32,
            NULL,
        }
    };
    return do_zip_uzp(s, a, fn[a->q][a->size]);
}

static bool trans_VZIP(DisasContext *s, arg_2misc *a)
{
    static ZipFn * const fn[2][4] = {
        {
            gen_helper_neon_zip8,
            gen_helper_neon_zip16,
            NULL,
            NULL,
        }, {
            gen_helper_neon_qzip8,
            gen_helper_neon_qzip16,
            gen_helper_neon_qzip32,
            NULL,
        }
    };
    return do_zip_uzp(s, a, fn[a->q][a->size]);
}

static bool do_vmovn(DisasContext *s, arg_2misc *a,
                     NeonGenNarrowEnvFn *narrowfn)
{
    TCGv_i64 rm;
    TCGv_i32 rd0, rd1;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if (a->vm & 1) {
        return false;
    }

    if (!narrowfn) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    rm = tcg_temp_new_i64();
    rd0 = tcg_temp_new_i32();
    rd1 = tcg_temp_new_i32();

    read_neon_element64(rm, a->vm, 0, MO_64);
    narrowfn(rd0, cpu_env, rm);
    read_neon_element64(rm, a->vm, 1, MO_64);
    narrowfn(rd1, cpu_env, rm);
    write_neon_element32(rd0, a->vd, 0, MO_32);
    write_neon_element32(rd1, a->vd, 1, MO_32);
    tcg_temp_free_i32(rd0);
    tcg_temp_free_i32(rd1);
    tcg_temp_free_i64(rm);
    return true;
}

#define DO_VMOVN(INSN, FUNC)                                    \
    static bool trans_##INSN(DisasContext *s, arg_2misc *a)     \
    {                                                           \
        static NeonGenNarrowEnvFn * const narrowfn[] = {        \
            FUNC##8,                                            \
            FUNC##16,                                           \
            FUNC##32,                                           \
            NULL,                                               \
        };                                                      \
        return do_vmovn(s, a, narrowfn[a->size]);               \
    }

DO_VMOVN(VMOVN, gen_neon_narrow_u)
DO_VMOVN(VQMOVUN, gen_helper_neon_unarrow_sat)
DO_VMOVN(VQMOVN_S, gen_helper_neon_narrow_sat_s)
DO_VMOVN(VQMOVN_U, gen_helper_neon_narrow_sat_u)

static bool trans_VSHLL(DisasContext *s, arg_2misc *a)
{
    TCGv_i32 rm0, rm1;
    TCGv_i64 rd;
    static NeonGenWidenFn * const widenfns[] = {
        gen_helper_neon_widen_u8,
        gen_helper_neon_widen_u16,
        tcg_gen_extu_i32_i64,
        NULL,
    };
    NeonGenWidenFn *widenfn = widenfns[a->size];

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if (a->vd & 1) {
        return false;
    }

    if (!widenfn) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    rd = tcg_temp_new_i64();
    rm0 = tcg_temp_new_i32();
    rm1 = tcg_temp_new_i32();

    read_neon_element32(rm0, a->vm, 0, MO_32);
    read_neon_element32(rm1, a->vm, 1, MO_32);

    widenfn(rd, rm0);
    tcg_gen_shli_i64(rd, rd, 8 << a->size);
    write_neon_element64(rd, a->vd, 0, MO_64);
    widenfn(rd, rm1);
    tcg_gen_shli_i64(rd, rd, 8 << a->size);
    write_neon_element64(rd, a->vd, 1, MO_64);

    tcg_temp_free_i64(rd);
    tcg_temp_free_i32(rm0);
    tcg_temp_free_i32(rm1);
    return true;
}

static bool trans_VCVT_B16_F32(DisasContext *s, arg_2misc *a)
{
    TCGv_ptr fpst;
    TCGv_i64 tmp;
    TCGv_i32 dst0, dst1;

    if (!dc_isar_feature(aa32_bf16, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vm & 1) || (a->size != 1)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    fpst = fpstatus_ptr(FPST_STD);
    tmp = tcg_temp_new_i64();
    dst0 = tcg_temp_new_i32();
    dst1 = tcg_temp_new_i32();

    read_neon_element64(tmp, a->vm, 0, MO_64);
    gen_helper_bfcvt_pair(dst0, tmp, fpst);

    read_neon_element64(tmp, a->vm, 1, MO_64);
    gen_helper_bfcvt_pair(dst1, tmp, fpst);

    write_neon_element32(dst0, a->vd, 0, MO_32);
    write_neon_element32(dst1, a->vd, 1, MO_32);

    tcg_temp_free_i64(tmp);
    tcg_temp_free_i32(dst0);
    tcg_temp_free_i32(dst1);
    tcg_temp_free_ptr(fpst);
    return true;
}

static bool trans_VCVT_F16_F32(DisasContext *s, arg_2misc *a)
{
    TCGv_ptr fpst;
    TCGv_i32 ahp, tmp, tmp2, tmp3;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON) ||
        !dc_isar_feature(aa32_fp16_spconv, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vm & 1) || (a->size != 1)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    fpst = fpstatus_ptr(FPST_STD);
    ahp = get_ahp_flag();
    tmp = tcg_temp_new_i32();
    read_neon_element32(tmp, a->vm, 0, MO_32);
    gen_helper_vfp_fcvt_f32_to_f16(tmp, tmp, fpst, ahp);
    tmp2 = tcg_temp_new_i32();
    read_neon_element32(tmp2, a->vm, 1, MO_32);
    gen_helper_vfp_fcvt_f32_to_f16(tmp2, tmp2, fpst, ahp);
    tcg_gen_shli_i32(tmp2, tmp2, 16);
    tcg_gen_or_i32(tmp2, tmp2, tmp);
    read_neon_element32(tmp, a->vm, 2, MO_32);
    gen_helper_vfp_fcvt_f32_to_f16(tmp, tmp, fpst, ahp);
    tmp3 = tcg_temp_new_i32();
    read_neon_element32(tmp3, a->vm, 3, MO_32);
    write_neon_element32(tmp2, a->vd, 0, MO_32);
    tcg_temp_free_i32(tmp2);
    gen_helper_vfp_fcvt_f32_to_f16(tmp3, tmp3, fpst, ahp);
    tcg_gen_shli_i32(tmp3, tmp3, 16);
    tcg_gen_or_i32(tmp3, tmp3, tmp);
    write_neon_element32(tmp3, a->vd, 1, MO_32);
    tcg_temp_free_i32(tmp3);
    tcg_temp_free_i32(tmp);
    tcg_temp_free_i32(ahp);
    tcg_temp_free_ptr(fpst);

    return true;
}

static bool trans_VCVT_F32_F16(DisasContext *s, arg_2misc *a)
{
    TCGv_ptr fpst;
    TCGv_i32 ahp, tmp, tmp2, tmp3;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON) ||
        !dc_isar_feature(aa32_fp16_spconv, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vd & 1) || (a->size != 1)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    fpst = fpstatus_ptr(FPST_STD);
    ahp = get_ahp_flag();
    tmp3 = tcg_temp_new_i32();
    tmp2 = tcg_temp_new_i32();
    tmp = tcg_temp_new_i32();
    read_neon_element32(tmp, a->vm, 0, MO_32);
    read_neon_element32(tmp2, a->vm, 1, MO_32);
    tcg_gen_ext16u_i32(tmp3, tmp);
    gen_helper_vfp_fcvt_f16_to_f32(tmp3, tmp3, fpst, ahp);
    write_neon_element32(tmp3, a->vd, 0, MO_32);
    tcg_gen_shri_i32(tmp, tmp, 16);
    gen_helper_vfp_fcvt_f16_to_f32(tmp, tmp, fpst, ahp);
    write_neon_element32(tmp, a->vd, 1, MO_32);
    tcg_temp_free_i32(tmp);
    tcg_gen_ext16u_i32(tmp3, tmp2);
    gen_helper_vfp_fcvt_f16_to_f32(tmp3, tmp3, fpst, ahp);
    write_neon_element32(tmp3, a->vd, 2, MO_32);
    tcg_temp_free_i32(tmp3);
    tcg_gen_shri_i32(tmp2, tmp2, 16);
    gen_helper_vfp_fcvt_f16_to_f32(tmp2, tmp2, fpst, ahp);
    write_neon_element32(tmp2, a->vd, 3, MO_32);
    tcg_temp_free_i32(tmp2);
    tcg_temp_free_i32(ahp);
    tcg_temp_free_ptr(fpst);

    return true;
}

static bool do_2misc_vec(DisasContext *s, arg_2misc *a, GVecGen2Fn *fn)
{
    int vec_size = a->q ? 16 : 8;
    int rd_ofs = neon_full_reg_offset(a->vd);
    int rm_ofs = neon_full_reg_offset(a->vm);

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if (a->size == 3) {
        return false;
    }

    if ((a->vd | a->vm) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    fn(a->size, rd_ofs, rm_ofs, vec_size, vec_size);

    return true;
}

#define DO_2MISC_VEC(INSN, FN)                                  \
    static bool trans_##INSN(DisasContext *s, arg_2misc *a)     \
    {                                                           \
        return do_2misc_vec(s, a, FN);                          \
    }

DO_2MISC_VEC(VNEG, tcg_gen_gvec_neg)
DO_2MISC_VEC(VABS, tcg_gen_gvec_abs)
DO_2MISC_VEC(VCEQ0, gen_gvec_ceq0)
DO_2MISC_VEC(VCGT0, gen_gvec_cgt0)
DO_2MISC_VEC(VCLE0, gen_gvec_cle0)
DO_2MISC_VEC(VCGE0, gen_gvec_cge0)
DO_2MISC_VEC(VCLT0, gen_gvec_clt0)

static bool trans_VMVN(DisasContext *s, arg_2misc *a)
{
    if (a->size != 0) {
        return false;
    }
    return do_2misc_vec(s, a, tcg_gen_gvec_not);
}

#define WRAP_2M_3_OOL_FN(WRAPNAME, FUNC, DATA)                          \
    static void WRAPNAME(unsigned vece, uint32_t rd_ofs,                \
                         uint32_t rm_ofs, uint32_t oprsz,               \
                         uint32_t maxsz)                                \
    {                                                                   \
        tcg_gen_gvec_3_ool(rd_ofs, rd_ofs, rm_ofs, oprsz, maxsz,        \
                           DATA, FUNC);                                 \
    }

#define WRAP_2M_2_OOL_FN(WRAPNAME, FUNC, DATA)                          \
    static void WRAPNAME(unsigned vece, uint32_t rd_ofs,                \
                         uint32_t rm_ofs, uint32_t oprsz,               \
                         uint32_t maxsz)                                \
    {                                                                   \
        tcg_gen_gvec_2_ool(rd_ofs, rm_ofs, oprsz, maxsz, DATA, FUNC);   \
    }

WRAP_2M_3_OOL_FN(gen_AESE, gen_helper_crypto_aese, 0)
WRAP_2M_3_OOL_FN(gen_AESD, gen_helper_crypto_aese, 1)
WRAP_2M_2_OOL_FN(gen_AESMC, gen_helper_crypto_aesmc, 0)
WRAP_2M_2_OOL_FN(gen_AESIMC, gen_helper_crypto_aesmc, 1)
WRAP_2M_2_OOL_FN(gen_SHA1H, gen_helper_crypto_sha1h, 0)
WRAP_2M_2_OOL_FN(gen_SHA1SU1, gen_helper_crypto_sha1su1, 0)
WRAP_2M_2_OOL_FN(gen_SHA256SU0, gen_helper_crypto_sha256su0, 0)

#define DO_2M_CRYPTO(INSN, FEATURE, SIZE)                       \
    static bool trans_##INSN(DisasContext *s, arg_2misc *a)     \
    {                                                           \
        if (!dc_isar_feature(FEATURE, s) || a->size != SIZE) {  \
            return false;                                       \
        }                                                       \
        return do_2misc_vec(s, a, gen_##INSN);                  \
    }

DO_2M_CRYPTO(AESE, aa32_aes, 0)
DO_2M_CRYPTO(AESD, aa32_aes, 0)
DO_2M_CRYPTO(AESMC, aa32_aes, 0)
DO_2M_CRYPTO(AESIMC, aa32_aes, 0)
DO_2M_CRYPTO(SHA1H, aa32_sha1, 2)
DO_2M_CRYPTO(SHA1SU1, aa32_sha1, 2)
DO_2M_CRYPTO(SHA256SU0, aa32_sha2, 2)

static bool do_2misc(DisasContext *s, arg_2misc *a, NeonGenOneOpFn *fn)
{
    TCGv_i32 tmp;
    int pass;

    /* Handle a 2-reg-misc operation by iterating 32 bits at a time */
    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if (!fn) {
        return false;
    }

    if ((a->vd | a->vm) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    tmp = tcg_temp_new_i32();
    for (pass = 0; pass < (a->q ? 4 : 2); pass++) {
        read_neon_element32(tmp, a->vm, pass, MO_32);
        fn(tmp, tmp);
        write_neon_element32(tmp, a->vd, pass, MO_32);
    }
    tcg_temp_free_i32(tmp);

    return true;
}

static bool trans_VREV32(DisasContext *s, arg_2misc *a)
{
    static NeonGenOneOpFn * const fn[] = {
        tcg_gen_bswap32_i32,
        gen_swap_half,
        NULL,
        NULL,
    };
    return do_2misc(s, a, fn[a->size]);
}

static bool trans_VREV16(DisasContext *s, arg_2misc *a)
{
    if (a->size != 0) {
        return false;
    }
    return do_2misc(s, a, gen_rev16);
}

static bool trans_VCLS(DisasContext *s, arg_2misc *a)
{
    static NeonGenOneOpFn * const fn[] = {
        gen_helper_neon_cls_s8,
        gen_helper_neon_cls_s16,
        gen_helper_neon_cls_s32,
        NULL,
    };
    return do_2misc(s, a, fn[a->size]);
}

static void do_VCLZ_32(TCGv_i32 rd, TCGv_i32 rm)
{
    tcg_gen_clzi_i32(rd, rm, 32);
}

static bool trans_VCLZ(DisasContext *s, arg_2misc *a)
{
    static NeonGenOneOpFn * const fn[] = {
        gen_helper_neon_clz_u8,
        gen_helper_neon_clz_u16,
        do_VCLZ_32,
        NULL,
    };
    return do_2misc(s, a, fn[a->size]);
}

static bool trans_VCNT(DisasContext *s, arg_2misc *a)
{
    if (a->size != 0) {
        return false;
    }
    return do_2misc(s, a, gen_helper_neon_cnt_u8);
}

static void gen_VABS_F(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                       uint32_t oprsz, uint32_t maxsz)
{
    tcg_gen_gvec_andi(vece, rd_ofs, rm_ofs,
                      vece == MO_16 ? 0x7fff : 0x7fffffff,
                      oprsz, maxsz);
}

static bool trans_VABS_F(DisasContext *s, arg_2misc *a)
{
    if (a->size == MO_16) {
        if (!dc_isar_feature(aa32_fp16_arith, s)) {
            return false;
        }
    } else if (a->size != MO_32) {
        return false;
    }
    return do_2misc_vec(s, a, gen_VABS_F);
}

static void gen_VNEG_F(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                       uint32_t oprsz, uint32_t maxsz)
{
    tcg_gen_gvec_xori(vece, rd_ofs, rm_ofs,
                      vece == MO_16 ? 0x8000 : 0x80000000,
                      oprsz, maxsz);
}

static bool trans_VNEG_F(DisasContext *s, arg_2misc *a)
{
    if (a->size == MO_16) {
        if (!dc_isar_feature(aa32_fp16_arith, s)) {
            return false;
        }
    } else if (a->size != MO_32) {
        return false;
    }
    return do_2misc_vec(s, a, gen_VNEG_F);
}

static bool trans_VRECPE(DisasContext *s, arg_2misc *a)
{
    if (a->size != 2) {
        return false;
    }
    return do_2misc(s, a, gen_helper_recpe_u32);
}

static bool trans_VRSQRTE(DisasContext *s, arg_2misc *a)
{
    if (a->size != 2) {
        return false;
    }
    return do_2misc(s, a, gen_helper_rsqrte_u32);
}

#define WRAP_1OP_ENV_FN(WRAPNAME, FUNC) \
    static void WRAPNAME(TCGv_i32 d, TCGv_i32 m)        \
    {                                                   \
        FUNC(d, cpu_env, m);                            \
    }

WRAP_1OP_ENV_FN(gen_VQABS_s8, gen_helper_neon_qabs_s8)
WRAP_1OP_ENV_FN(gen_VQABS_s16, gen_helper_neon_qabs_s16)
WRAP_1OP_ENV_FN(gen_VQABS_s32, gen_helper_neon_qabs_s32)
WRAP_1OP_ENV_FN(gen_VQNEG_s8, gen_helper_neon_qneg_s8)
WRAP_1OP_ENV_FN(gen_VQNEG_s16, gen_helper_neon_qneg_s16)
WRAP_1OP_ENV_FN(gen_VQNEG_s32, gen_helper_neon_qneg_s32)

static bool trans_VQABS(DisasContext *s, arg_2misc *a)
{
    static NeonGenOneOpFn * const fn[] = {
        gen_VQABS_s8,
        gen_VQABS_s16,
        gen_VQABS_s32,
        NULL,
    };
    return do_2misc(s, a, fn[a->size]);
}

static bool trans_VQNEG(DisasContext *s, arg_2misc *a)
{
    static NeonGenOneOpFn * const fn[] = {
        gen_VQNEG_s8,
        gen_VQNEG_s16,
        gen_VQNEG_s32,
        NULL,
    };
    return do_2misc(s, a, fn[a->size]);
}

#define DO_2MISC_FP_VEC(INSN, HFUNC, SFUNC)                             \
    static void gen_##INSN(unsigned vece, uint32_t rd_ofs,              \
                           uint32_t rm_ofs,                             \
                           uint32_t oprsz, uint32_t maxsz)              \
    {                                                                   \
        static gen_helper_gvec_2_ptr * const fns[4] = {                 \
            NULL, HFUNC, SFUNC, NULL,                                   \
        };                                                              \
        TCGv_ptr fpst;                                                  \
        fpst = fpstatus_ptr(vece == MO_16 ? FPST_STD_F16 : FPST_STD);   \
        tcg_gen_gvec_2_ptr(rd_ofs, rm_ofs, fpst, oprsz, maxsz, 0,       \
                           fns[vece]);                                  \
        tcg_temp_free_ptr(fpst);                                        \
    }                                                                   \
    static bool trans_##INSN(DisasContext *s, arg_2misc *a)             \
    {                                                                   \
        if (a->size == MO_16) {                                         \
            if (!dc_isar_feature(aa32_fp16_arith, s)) {                 \
                return false;                                           \
            }                                                           \
        } else if (a->size != MO_32) {                                  \
            return false;                                               \
        }                                                               \
        return do_2misc_vec(s, a, gen_##INSN);                          \
    }

DO_2MISC_FP_VEC(VRECPE_F, gen_helper_gvec_frecpe_h, gen_helper_gvec_frecpe_s)
DO_2MISC_FP_VEC(VRSQRTE_F, gen_helper_gvec_frsqrte_h, gen_helper_gvec_frsqrte_s)
DO_2MISC_FP_VEC(VCGT0_F, gen_helper_gvec_fcgt0_h, gen_helper_gvec_fcgt0_s)
DO_2MISC_FP_VEC(VCGE0_F, gen_helper_gvec_fcge0_h, gen_helper_gvec_fcge0_s)
DO_2MISC_FP_VEC(VCEQ0_F, gen_helper_gvec_fceq0_h, gen_helper_gvec_fceq0_s)
DO_2MISC_FP_VEC(VCLT0_F, gen_helper_gvec_fclt0_h, gen_helper_gvec_fclt0_s)
DO_2MISC_FP_VEC(VCLE0_F, gen_helper_gvec_fcle0_h, gen_helper_gvec_fcle0_s)
DO_2MISC_FP_VEC(VCVT_FS, gen_helper_gvec_sstoh, gen_helper_gvec_sitos)
DO_2MISC_FP_VEC(VCVT_FU, gen_helper_gvec_ustoh, gen_helper_gvec_uitos)
DO_2MISC_FP_VEC(VCVT_SF, gen_helper_gvec_tosszh, gen_helper_gvec_tosizs)
DO_2MISC_FP_VEC(VCVT_UF, gen_helper_gvec_touszh, gen_helper_gvec_touizs)

DO_2MISC_FP_VEC(VRINTX_impl, gen_helper_gvec_vrintx_h, gen_helper_gvec_vrintx_s)

static bool trans_VRINTX(DisasContext *s, arg_2misc *a)
{
    if (!arm_dc_feature(s, ARM_FEATURE_V8)) {
        return false;
    }
    return trans_VRINTX_impl(s, a);
}

#define DO_VEC_RMODE(INSN, RMODE, OP)                                   \
    static void gen_##INSN(unsigned vece, uint32_t rd_ofs,              \
                           uint32_t rm_ofs,                             \
                           uint32_t oprsz, uint32_t maxsz)              \
    {                                                                   \
        static gen_helper_gvec_2_ptr * const fns[4] = {                 \
            NULL,                                                       \
            gen_helper_gvec_##OP##h,                                    \
            gen_helper_gvec_##OP##s,                                    \
            NULL,                                                       \
        };                                                              \
        TCGv_ptr fpst;                                                  \
        fpst = fpstatus_ptr(vece == 1 ? FPST_STD_F16 : FPST_STD);       \
        tcg_gen_gvec_2_ptr(rd_ofs, rm_ofs, fpst, oprsz, maxsz,          \
                           arm_rmode_to_sf(RMODE), fns[vece]);          \
        tcg_temp_free_ptr(fpst);                                        \
    }                                                                   \
    static bool trans_##INSN(DisasContext *s, arg_2misc *a)             \
    {                                                                   \
        if (!arm_dc_feature(s, ARM_FEATURE_V8)) {                       \
            return false;                                               \
        }                                                               \
        if (a->size == MO_16) {                                         \
            if (!dc_isar_feature(aa32_fp16_arith, s)) {                 \
                return false;                                           \
            }                                                           \
        } else if (a->size != MO_32) {                                  \
            return false;                                               \
        }                                                               \
        return do_2misc_vec(s, a, gen_##INSN);                          \
    }

DO_VEC_RMODE(VCVTAU, FPROUNDING_TIEAWAY, vcvt_rm_u)
DO_VEC_RMODE(VCVTAS, FPROUNDING_TIEAWAY, vcvt_rm_s)
DO_VEC_RMODE(VCVTNU, FPROUNDING_TIEEVEN, vcvt_rm_u)
DO_VEC_RMODE(VCVTNS, FPROUNDING_TIEEVEN, vcvt_rm_s)
DO_VEC_RMODE(VCVTPU, FPROUNDING_POSINF, vcvt_rm_u)
DO_VEC_RMODE(VCVTPS, FPROUNDING_POSINF, vcvt_rm_s)
DO_VEC_RMODE(VCVTMU, FPROUNDING_NEGINF, vcvt_rm_u)
DO_VEC_RMODE(VCVTMS, FPROUNDING_NEGINF, vcvt_rm_s)

DO_VEC_RMODE(VRINTN, FPROUNDING_TIEEVEN, vrint_rm_)
DO_VEC_RMODE(VRINTA, FPROUNDING_TIEAWAY, vrint_rm_)
DO_VEC_RMODE(VRINTZ, FPROUNDING_ZERO, vrint_rm_)
DO_VEC_RMODE(VRINTM, FPROUNDING_NEGINF, vrint_rm_)
DO_VEC_RMODE(VRINTP, FPROUNDING_POSINF, vrint_rm_)

static bool trans_VSWP(DisasContext *s, arg_2misc *a)
{
    TCGv_i64 rm, rd;
    int pass;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if (a->size != 0) {
        return false;
    }

    if ((a->vd | a->vm) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    rm = tcg_temp_new_i64();
    rd = tcg_temp_new_i64();
    for (pass = 0; pass < (a->q ? 2 : 1); pass++) {
        read_neon_element64(rm, a->vm, pass, MO_64);
        read_neon_element64(rd, a->vd, pass, MO_64);
        write_neon_element64(rm, a->vd, pass, MO_64);
        write_neon_element64(rd, a->vm, pass, MO_64);
    }
    tcg_temp_free_i64(rm);
    tcg_temp_free_i64(rd);

    return true;
}
static void gen_neon_trn_u8(TCGv_i32 t0, TCGv_i32 t1)
{
    TCGv_i32 rd, tmp;

    rd = tcg_temp_new_i32();
    tmp = tcg_temp_new_i32();

    tcg_gen_shli_i32(rd, t0, 8);
    tcg_gen_andi_i32(rd, rd, 0xff00ff00);
    tcg_gen_andi_i32(tmp, t1, 0x00ff00ff);
    tcg_gen_or_i32(rd, rd, tmp);

    tcg_gen_shri_i32(t1, t1, 8);
    tcg_gen_andi_i32(t1, t1, 0x00ff00ff);
    tcg_gen_andi_i32(tmp, t0, 0xff00ff00);
    tcg_gen_or_i32(t1, t1, tmp);
    tcg_gen_mov_i32(t0, rd);

    tcg_temp_free_i32(tmp);
    tcg_temp_free_i32(rd);
}

static void gen_neon_trn_u16(TCGv_i32 t0, TCGv_i32 t1)
{
    TCGv_i32 rd, tmp;

    rd = tcg_temp_new_i32();
    tmp = tcg_temp_new_i32();

    tcg_gen_shli_i32(rd, t0, 16);
    tcg_gen_andi_i32(tmp, t1, 0xffff);
    tcg_gen_or_i32(rd, rd, tmp);
    tcg_gen_shri_i32(t1, t1, 16);
    tcg_gen_andi_i32(tmp, t0, 0xffff0000);
    tcg_gen_or_i32(t1, t1, tmp);
    tcg_gen_mov_i32(t0, rd);

    tcg_temp_free_i32(tmp);
    tcg_temp_free_i32(rd);
}

static bool trans_VTRN(DisasContext *s, arg_2misc *a)
{
    TCGv_i32 tmp, tmp2;
    int pass;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if ((a->vd | a->vm) & a->q) {
        return false;
    }

    if (a->size == 3) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    tmp = tcg_temp_new_i32();
    tmp2 = tcg_temp_new_i32();
    if (a->size == MO_32) {
        for (pass = 0; pass < (a->q ? 4 : 2); pass += 2) {
            read_neon_element32(tmp, a->vm, pass, MO_32);
            read_neon_element32(tmp2, a->vd, pass + 1, MO_32);
            write_neon_element32(tmp2, a->vm, pass, MO_32);
            write_neon_element32(tmp, a->vd, pass + 1, MO_32);
        }
    } else {
        for (pass = 0; pass < (a->q ? 4 : 2); pass++) {
            read_neon_element32(tmp, a->vm, pass, MO_32);
            read_neon_element32(tmp2, a->vd, pass, MO_32);
            if (a->size == MO_8) {
                gen_neon_trn_u8(tmp, tmp2);
            } else {
                gen_neon_trn_u16(tmp, tmp2);
            }
            write_neon_element32(tmp2, a->vm, pass, MO_32);
            write_neon_element32(tmp, a->vd, pass, MO_32);
        }
    }
    tcg_temp_free_i32(tmp);
    tcg_temp_free_i32(tmp2);
    return true;
}

static bool trans_VSMMLA(DisasContext *s, arg_VSMMLA *a)
{
    if (!dc_isar_feature(aa32_i8mm, s)) {
        return false;
    }
    return do_neon_ddda(s, 7, a->vd, a->vn, a->vm, 0,
                        gen_helper_gvec_smmla_b);
}

static bool trans_VUMMLA(DisasContext *s, arg_VUMMLA *a)
{
    if (!dc_isar_feature(aa32_i8mm, s)) {
        return false;
    }
    return do_neon_ddda(s, 7, a->vd, a->vn, a->vm, 0,
                        gen_helper_gvec_ummla_b);
}

static bool trans_VUSMMLA(DisasContext *s, arg_VUSMMLA *a)
{
    if (!dc_isar_feature(aa32_i8mm, s)) {
        return false;
    }
    return do_neon_ddda(s, 7, a->vd, a->vn, a->vm, 0,
                        gen_helper_gvec_usmmla_b);
}

static bool trans_VMMLA_b16(DisasContext *s, arg_VMMLA_b16 *a)
{
    if (!dc_isar_feature(aa32_bf16, s)) {
        return false;
    }
    return do_neon_ddda(s, 7, a->vd, a->vn, a->vm, 0,
                        gen_helper_gvec_bfmmla);
}

static bool trans_VFMA_b16(DisasContext *s, arg_VFMA_b16 *a)
{
    if (!dc_isar_feature(aa32_bf16, s)) {
        return false;
    }
    return do_neon_ddda_fpst(s, 7, a->vd, a->vn, a->vm, a->q, FPST_STD,
                             gen_helper_gvec_bfmlal);
}

static bool trans_VFMA_b16_scal(DisasContext *s, arg_VFMA_b16_scal *a)
{
    if (!dc_isar_feature(aa32_bf16, s)) {
        return false;
    }
    return do_neon_ddda_fpst(s, 6, a->vd, a->vn, a->vm,
                             (a->index << 1) | a->q, FPST_STD,
                             gen_helper_gvec_bfmlal_idx);
}
