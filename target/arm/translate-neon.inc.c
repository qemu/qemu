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

static inline int plus1(DisasContext *s, int x)
{
    return x + 1;
}

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

static bool trans_VLD_all_lanes(DisasContext *s, arg_VLD_all_lanes *a)
{
    /* Neon load single structure to all lanes */
    int reg, stride, vec_size;
    int vd = a->vd;
    int size = a->size;
    int nregs = a->n + 1;
    TCGv_i32 addr, tmp;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (!dc_isar_feature(aa32_simd_r32, s) && (a->vd & 0x10)) {
        return false;
    }

    if (size == 3) {
        if (nregs != 4 || a->a == 0) {
            return false;
        }
        /* For VLD4 size == 3 a == 1 means 32 bits at 16 byte alignment */
        size = 2;
    }
    if (nregs == 1 && a->a == 1 && size == 0) {
        return false;
    }
    if (nregs == 3 && a->a == 1) {
        return false;
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

    tmp = tcg_temp_new_i32();
    addr = tcg_temp_new_i32();
    load_reg_var(s, addr, a->rn);
    for (reg = 0; reg < nregs; reg++) {
        gen_aa32_ld_i32(s, tmp, addr, get_mem_index(s),
                        s->be_data | size);
        if ((vd & 1) && vec_size == 16) {
            /*
             * We cannot write 16 bytes at once because the
             * destination is unaligned.
             */
            tcg_gen_gvec_dup_i32(size, neon_reg_offset(vd, 0),
                                 8, 8, tmp);
            tcg_gen_gvec_mov(0, neon_reg_offset(vd + 1, 0),
                             neon_reg_offset(vd, 0), 8, 8);
        } else {
            tcg_gen_gvec_dup_i32(size, neon_reg_offset(vd, 0),
                                 vec_size, vec_size, tmp);
        }
        tcg_gen_addi_i32(addr, addr, 1 << size);
        vd += stride;
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
            (a->size == 2 && ((a->align & 3) == 1 || (a->align & 3) == 2))) {
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
        if ((a->size == 2) && ((a->align & 3) == 3)) {
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

    tmp = tcg_temp_new_i32();
    addr = tcg_temp_new_i32();
    load_reg_var(s, addr, a->rn);
    /*
     * TODO: if we implemented alignment exceptions, we should check
     * addr against the alignment encoded in a->align here.
     */
    for (reg = 0; reg < nregs; reg++) {
        if (a->l) {
            gen_aa32_ld_i32(s, tmp, addr, get_mem_index(s),
                            s->be_data | a->size);
            neon_store_element(vd, a->reg_idx, a->size, tmp);
        } else { /* Store */
            neon_load_element(tmp, vd, a->reg_idx, a->size);
            gen_aa32_st_i32(s, tmp, addr, get_mem_index(s),
                            s->be_data | a->size);
        }
        vd += a->stride;
        tcg_gen_addi_i32(addr, addr, 1 << a->size);
    }
    tcg_temp_free_i32(addr);
    tcg_temp_free_i32(tmp);

    gen_neon_ldst_base_update(s, a->rm, a->rn, (1 << a->size) * nregs);

    return true;
}

static bool do_3same(DisasContext *s, arg_3same *a, GVecGen3Fn fn)
{
    int vec_size = a->q ? 16 : 8;
    int rd_ofs = neon_reg_offset(a->vd, 0);
    int rn_ofs = neon_reg_offset(a->vn, 0);
    int rm_ofs = neon_reg_offset(a->vm, 0);

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

static void gen_VTST_3s(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                         uint32_t rm_ofs, uint32_t oprsz, uint32_t maxsz)
{
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, oprsz, maxsz, &cmtst_op[vece]);
}
DO_3SAME_NO_SZ_3(VTST, gen_VTST_3s)

#define DO_3SAME_GVEC4(INSN, OPARRAY)                                   \
    static void gen_##INSN##_3s(unsigned vece, uint32_t rd_ofs,         \
                                uint32_t rn_ofs, uint32_t rm_ofs,       \
                                uint32_t oprsz, uint32_t maxsz)         \
    {                                                                   \
        tcg_gen_gvec_4(rd_ofs, offsetof(CPUARMState, vfp.qc),           \
                       rn_ofs, rm_ofs, oprsz, maxsz, &OPARRAY[vece]);   \
    }                                                                   \
    DO_3SAME(INSN, gen_##INSN##_3s)

DO_3SAME_GVEC4(VQADD_S, sqadd_op)
DO_3SAME_GVEC4(VQADD_U, uqadd_op)
DO_3SAME_GVEC4(VQSUB_S, sqsub_op)
DO_3SAME_GVEC4(VQSUB_U, uqsub_op)

static void gen_VMUL_p_3s(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                           uint32_t rm_ofs, uint32_t oprsz, uint32_t maxsz)
{
    tcg_gen_gvec_3_ool(rd_ofs, rn_ofs, rm_ofs, oprsz, maxsz,
                       0, gen_helper_gvec_pmul_b);
}

static bool trans_VMUL_p_3s(DisasContext *s, arg_3same *a)
{
    if (a->size != 0) {
        return false;
    }
    return do_3same(s, a, gen_VMUL_p_3s);
}

#define DO_3SAME_GVEC3_NO_SZ_3(INSN, OPARRAY)                           \
    static void gen_##INSN##_3s(unsigned vece, uint32_t rd_ofs,         \
                                uint32_t rn_ofs, uint32_t rm_ofs,       \
                                uint32_t oprsz, uint32_t maxsz)         \
    {                                                                   \
        tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs,                          \
                       oprsz, maxsz, &OPARRAY[vece]);                   \
    }                                                                   \
    DO_3SAME_NO_SZ_3(INSN, gen_##INSN##_3s)


DO_3SAME_GVEC3_NO_SZ_3(VMLA, mla_op)
DO_3SAME_GVEC3_NO_SZ_3(VMLS, mls_op)

#define DO_3SAME_GVEC3_SHIFT(INSN, OPARRAY)                             \
    static void gen_##INSN##_3s(unsigned vece, uint32_t rd_ofs,         \
                                uint32_t rn_ofs, uint32_t rm_ofs,       \
                                uint32_t oprsz, uint32_t maxsz)         \
    {                                                                   \
        /* Note the operation is vshl vd,vm,vn */                       \
        tcg_gen_gvec_3(rd_ofs, rm_ofs, rn_ofs,                          \
                       oprsz, maxsz, &OPARRAY[vece]);                   \
    }                                                                   \
    DO_3SAME(INSN, gen_##INSN##_3s)

DO_3SAME_GVEC3_SHIFT(VSHL_S, sshl_op)
DO_3SAME_GVEC3_SHIFT(VSHL_U, ushl_op)
