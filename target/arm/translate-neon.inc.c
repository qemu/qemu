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

static inline int rsub_64(DisasContext *s, int x)
{
    return 64 - x;
}

static inline int rsub_32(DisasContext *s, int x)
{
    return 32 - x;
}
static inline int rsub_16(DisasContext *s, int x)
{
    return 16 - x;
}
static inline int rsub_8(DisasContext *s, int x)
{
    return 8 - x;
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
    tmp = neon_load_reg(a->vn, 0);
    tmp2 = neon_load_reg(a->vn, 1);
    fn(tmp, tmp, tmp2);
    tcg_temp_free_i32(tmp2);

    tmp3 = neon_load_reg(a->vm, 0);
    tmp2 = neon_load_reg(a->vm, 1);
    fn(tmp3, tmp3, tmp2);
    tcg_temp_free_i32(tmp2);

    neon_store_reg(a->vd, 0, tmp);
    neon_store_reg(a->vd, 1, tmp3);
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

static bool do_3same_fp(DisasContext *s, arg_3same *a, VFPGen3OpSPFn *fn,
                        bool reads_vd)
{
    /*
     * FP operations handled elementwise 32 bits at a time.
     * If reads_vd is true then the old value of Vd will be
     * loaded before calling the callback function. This is
     * used for multiply-accumulate type operations.
     */
    TCGv_i32 tmp, tmp2;
    int pass;

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

    TCGv_ptr fpstatus = get_fpstatus_ptr(1);
    for (pass = 0; pass < (a->q ? 4 : 2); pass++) {
        tmp = neon_load_reg(a->vn, pass);
        tmp2 = neon_load_reg(a->vm, pass);
        if (reads_vd) {
            TCGv_i32 tmp_rd = neon_load_reg(a->vd, pass);
            fn(tmp_rd, tmp, tmp2, fpstatus);
            neon_store_reg(a->vd, pass, tmp_rd);
            tcg_temp_free_i32(tmp);
        } else {
            fn(tmp, tmp, tmp2, fpstatus);
            neon_store_reg(a->vd, pass, tmp);
        }
        tcg_temp_free_i32(tmp2);
    }
    tcg_temp_free_ptr(fpstatus);
    return true;
}

/*
 * For all the functions using this macro, size == 1 means fp16,
 * which is an architecture extension we don't implement yet.
 */
#define DO_3S_FP_GVEC(INSN,FUNC)                                        \
    static void gen_##INSN##_3s(unsigned vece, uint32_t rd_ofs,         \
                                uint32_t rn_ofs, uint32_t rm_ofs,       \
                                uint32_t oprsz, uint32_t maxsz)         \
    {                                                                   \
        TCGv_ptr fpst = get_fpstatus_ptr(1);                            \
        tcg_gen_gvec_3_ptr(rd_ofs, rn_ofs, rm_ofs, fpst,                \
                           oprsz, maxsz, 0, FUNC);                      \
        tcg_temp_free_ptr(fpst);                                        \
    }                                                                   \
    static bool trans_##INSN##_fp_3s(DisasContext *s, arg_3same *a)     \
    {                                                                   \
        if (a->size != 0) {                                             \
            /* TODO fp16 support */                                     \
            return false;                                               \
        }                                                               \
        return do_3same(s, a, gen_##INSN##_3s);                         \
    }


DO_3S_FP_GVEC(VADD, gen_helper_gvec_fadd_s)
DO_3S_FP_GVEC(VSUB, gen_helper_gvec_fsub_s)
DO_3S_FP_GVEC(VABD, gen_helper_gvec_fabd_s)
DO_3S_FP_GVEC(VMUL, gen_helper_gvec_fmul_s)

/*
 * For all the functions using this macro, size == 1 means fp16,
 * which is an architecture extension we don't implement yet.
 */
#define DO_3S_FP(INSN,FUNC,READS_VD)                                \
    static bool trans_##INSN##_fp_3s(DisasContext *s, arg_3same *a) \
    {                                                               \
        if (a->size != 0) {                                         \
            /* TODO fp16 support */                                 \
            return false;                                           \
        }                                                           \
        return do_3same_fp(s, a, FUNC, READS_VD);                   \
    }

DO_3S_FP(VCEQ, gen_helper_neon_ceq_f32, false)
DO_3S_FP(VCGE, gen_helper_neon_cge_f32, false)
DO_3S_FP(VCGT, gen_helper_neon_cgt_f32, false)
DO_3S_FP(VACGE, gen_helper_neon_acge_f32, false)
DO_3S_FP(VACGT, gen_helper_neon_acgt_f32, false)
DO_3S_FP(VMAX, gen_helper_vfp_maxs, false)
DO_3S_FP(VMIN, gen_helper_vfp_mins, false)

static void gen_VMLA_fp_3s(TCGv_i32 vd, TCGv_i32 vn, TCGv_i32 vm,
                            TCGv_ptr fpstatus)
{
    gen_helper_vfp_muls(vn, vn, vm, fpstatus);
    gen_helper_vfp_adds(vd, vd, vn, fpstatus);
}

static void gen_VMLS_fp_3s(TCGv_i32 vd, TCGv_i32 vn, TCGv_i32 vm,
                            TCGv_ptr fpstatus)
{
    gen_helper_vfp_muls(vn, vn, vm, fpstatus);
    gen_helper_vfp_subs(vd, vd, vn, fpstatus);
}

DO_3S_FP(VMLA, gen_VMLA_fp_3s, true)
DO_3S_FP(VMLS, gen_VMLS_fp_3s, true)

static bool trans_VMAXNM_fp_3s(DisasContext *s, arg_3same *a)
{
    if (!arm_dc_feature(s, ARM_FEATURE_V8)) {
        return false;
    }

    if (a->size != 0) {
        /* TODO fp16 support */
        return false;
    }

    return do_3same_fp(s, a, gen_helper_vfp_maxnums, false);
}

static bool trans_VMINNM_fp_3s(DisasContext *s, arg_3same *a)
{
    if (!arm_dc_feature(s, ARM_FEATURE_V8)) {
        return false;
    }

    if (a->size != 0) {
        /* TODO fp16 support */
        return false;
    }

    return do_3same_fp(s, a, gen_helper_vfp_minnums, false);
}

WRAP_ENV_FN(gen_VRECPS_tramp, gen_helper_recps_f32)

static void gen_VRECPS_fp_3s(unsigned vece, uint32_t rd_ofs,
                             uint32_t rn_ofs, uint32_t rm_ofs,
                             uint32_t oprsz, uint32_t maxsz)
{
    static const GVecGen3 ops = { .fni4 = gen_VRECPS_tramp };
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, oprsz, maxsz, &ops);
}

static bool trans_VRECPS_fp_3s(DisasContext *s, arg_3same *a)
{
    if (a->size != 0) {
        /* TODO fp16 support */
        return false;
    }

    return do_3same(s, a, gen_VRECPS_fp_3s);
}

WRAP_ENV_FN(gen_VRSQRTS_tramp, gen_helper_rsqrts_f32)

static void gen_VRSQRTS_fp_3s(unsigned vece, uint32_t rd_ofs,
                              uint32_t rn_ofs, uint32_t rm_ofs,
                              uint32_t oprsz, uint32_t maxsz)
{
    static const GVecGen3 ops = { .fni4 = gen_VRSQRTS_tramp };
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, oprsz, maxsz, &ops);
}

static bool trans_VRSQRTS_fp_3s(DisasContext *s, arg_3same *a)
{
    if (a->size != 0) {
        /* TODO fp16 support */
        return false;
    }

    return do_3same(s, a, gen_VRSQRTS_fp_3s);
}

static void gen_VFMA_fp_3s(TCGv_i32 vd, TCGv_i32 vn, TCGv_i32 vm,
                            TCGv_ptr fpstatus)
{
    gen_helper_vfp_muladds(vd, vn, vm, vd, fpstatus);
}

static bool trans_VFMA_fp_3s(DisasContext *s, arg_3same *a)
{
    if (!dc_isar_feature(aa32_simdfmac, s)) {
        return false;
    }

    if (a->size != 0) {
        /* TODO fp16 support */
        return false;
    }

    return do_3same_fp(s, a, gen_VFMA_fp_3s, true);
}

static void gen_VFMS_fp_3s(TCGv_i32 vd, TCGv_i32 vn, TCGv_i32 vm,
                            TCGv_ptr fpstatus)
{
    gen_helper_vfp_negs(vn, vn);
    gen_helper_vfp_muladds(vd, vn, vm, vd, fpstatus);
}

static bool trans_VFMS_fp_3s(DisasContext *s, arg_3same *a)
{
    if (!dc_isar_feature(aa32_simdfmac, s)) {
        return false;
    }

    if (a->size != 0) {
        /* TODO fp16 support */
        return false;
    }

    return do_3same_fp(s, a, gen_VFMS_fp_3s, true);
}

static bool do_3same_fp_pair(DisasContext *s, arg_3same *a, VFPGen3OpSPFn *fn)
{
    /* FP operations handled pairwise 32 bits at a time */
    TCGv_i32 tmp, tmp2, tmp3;
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

    /*
     * Note that we have to be careful not to clobber the source operands
     * in the "vm == vd" case by storing the result of the first pass too
     * early. Since Q is 0 there are always just two passes, so instead
     * of a complicated loop over each pass we just unroll.
     */
    fpstatus = get_fpstatus_ptr(1);
    tmp = neon_load_reg(a->vn, 0);
    tmp2 = neon_load_reg(a->vn, 1);
    fn(tmp, tmp, tmp2, fpstatus);
    tcg_temp_free_i32(tmp2);

    tmp3 = neon_load_reg(a->vm, 0);
    tmp2 = neon_load_reg(a->vm, 1);
    fn(tmp3, tmp3, tmp2, fpstatus);
    tcg_temp_free_i32(tmp2);
    tcg_temp_free_ptr(fpstatus);

    neon_store_reg(a->vd, 0, tmp);
    neon_store_reg(a->vd, 1, tmp3);
    return true;
}

/*
 * For all the functions using this macro, size == 1 means fp16,
 * which is an architecture extension we don't implement yet.
 */
#define DO_3S_FP_PAIR(INSN,FUNC)                                    \
    static bool trans_##INSN##_fp_3s(DisasContext *s, arg_3same *a) \
    {                                                               \
        if (a->size != 0) {                                         \
            /* TODO fp16 support */                                 \
            return false;                                           \
        }                                                           \
        return do_3same_fp_pair(s, a, FUNC);                        \
    }

DO_3S_FP_PAIR(VPADD, gen_helper_vfp_adds)
DO_3S_FP_PAIR(VPMAX, gen_helper_vfp_maxs)
DO_3S_FP_PAIR(VPMIN, gen_helper_vfp_mins)

static bool do_vector_2sh(DisasContext *s, arg_2reg_shift *a, GVecGen2iFn *fn)
{
    /* Handle a 2-reg-shift insn which can be vectorized. */
    int vec_size = a->q ? 16 : 8;
    int rd_ofs = neon_reg_offset(a->vd, 0);
    int rm_ofs = neon_reg_offset(a->vm, 0);

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

        neon_load_reg64(tmp, a->vm + pass);
        fn(tmp, cpu_env, tmp, constimm);
        neon_store_reg64(tmp, a->vd + pass);
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
    TCGv_i32 constimm;
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

    for (pass = 0; pass < (a->q ? 4 : 2); pass++) {
        TCGv_i32 tmp = neon_load_reg(a->vm, pass);
        fn(tmp, cpu_env, tmp, constimm);
        neon_store_reg(a->vd, pass, tmp);
    }
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

    /* Load both inputs first to avoid potential overwrite if rm == rd */
    neon_load_reg64(rm1, a->vm);
    neon_load_reg64(rm2, a->vm + 1);

    shiftfn(rm1, rm1, constimm);
    rd = tcg_temp_new_i32();
    narrowfn(rd, cpu_env, rm1);
    neon_store_reg(a->vd, 0, rd);

    shiftfn(rm2, rm2, constimm);
    rd = tcg_temp_new_i32();
    narrowfn(rd, cpu_env, rm2);
    neon_store_reg(a->vd, 1, rd);

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
    rm1 = neon_load_reg(a->vm, 0);
    rm2 = neon_load_reg(a->vm, 1);
    rm3 = neon_load_reg(a->vm + 1, 0);
    rm4 = neon_load_reg(a->vm + 1, 1);
    rtmp = tcg_temp_new_i64();

    shiftfn(rm1, rm1, constimm);
    shiftfn(rm2, rm2, constimm);

    tcg_gen_concat_i32_i64(rtmp, rm1, rm2);
    tcg_temp_free_i32(rm2);

    narrowfn(rm1, cpu_env, rtmp);
    neon_store_reg(a->vd, 0, rm1);

    shiftfn(rm3, rm3, constimm);
    shiftfn(rm4, rm4, constimm);
    tcg_temp_free_i32(constimm);

    tcg_gen_concat_i32_i64(rtmp, rm3, rm4);
    tcg_temp_free_i32(rm4);

    narrowfn(rm3, cpu_env, rtmp);
    tcg_temp_free_i64(rtmp);
    neon_store_reg(a->vd, 1, rm3);
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

    rm0 = neon_load_reg(a->vm, 0);
    rm1 = neon_load_reg(a->vm, 1);
    tmp = tcg_temp_new_i64();

    widenfn(tmp, rm0);
    if (a->shift != 0) {
        tcg_gen_shli_i64(tmp, tmp, a->shift);
        tcg_gen_andi_i64(tmp, tmp, ~widen_mask);
    }
    neon_store_reg64(tmp, a->vd);

    widenfn(tmp, rm1);
    if (a->shift != 0) {
        tcg_gen_shli_i64(tmp, tmp, a->shift);
        tcg_gen_andi_i64(tmp, tmp, ~widen_mask);
    }
    neon_store_reg64(tmp, a->vd + 1);
    tcg_temp_free_i64(tmp);
    return true;
}

static bool trans_VSHLL_S_2sh(DisasContext *s, arg_2reg_shift *a)
{
    NeonGenWidenFn *widenfn[] = {
        gen_helper_neon_widen_s8,
        gen_helper_neon_widen_s16,
        tcg_gen_ext_i32_i64,
    };
    return do_vshll_2sh(s, a, widenfn[a->size], false);
}

static bool trans_VSHLL_U_2sh(DisasContext *s, arg_2reg_shift *a)
{
    NeonGenWidenFn *widenfn[] = {
        gen_helper_neon_widen_u8,
        gen_helper_neon_widen_u16,
        tcg_gen_extu_i32_i64,
    };
    return do_vshll_2sh(s, a, widenfn[a->size], true);
}

static bool do_fp_2sh(DisasContext *s, arg_2reg_shift *a,
                      NeonGenTwoSingleOPFn *fn)
{
    /* FP operations in 2-reg-and-shift group */
    TCGv_i32 tmp, shiftv;
    TCGv_ptr fpstatus;
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

    fpstatus = get_fpstatus_ptr(1);
    shiftv = tcg_const_i32(a->shift);
    for (pass = 0; pass < (a->q ? 4 : 2); pass++) {
        tmp = neon_load_reg(a->vm, pass);
        fn(tmp, tmp, shiftv, fpstatus);
        neon_store_reg(a->vd, pass, tmp);
    }
    tcg_temp_free_ptr(fpstatus);
    tcg_temp_free_i32(shiftv);
    return true;
}

#define DO_FP_2SH(INSN, FUNC)                                           \
    static bool trans_##INSN##_2sh(DisasContext *s, arg_2reg_shift *a)  \
    {                                                                   \
        return do_fp_2sh(s, a, FUNC);                                   \
    }

DO_FP_2SH(VCVT_SF, gen_helper_vfp_sltos)
DO_FP_2SH(VCVT_UF, gen_helper_vfp_ultos)
DO_FP_2SH(VCVT_FS, gen_helper_vfp_tosls_round_to_zero)
DO_FP_2SH(VCVT_FU, gen_helper_vfp_touls_round_to_zero)

static uint64_t asimd_imm_const(uint32_t imm, int cmode, int op)
{
    /*
     * Expand the encoded constant.
     * Note that cmode = 2,3,4,5,6,7,10,11,12,13 imm=0 is UNPREDICTABLE.
     * We choose to not special-case this and will behave as if a
     * valid constant encoding of 0 had been given.
     * cmode = 15 op = 1 must UNDEF; we assume decode has handled that.
     */
    switch (cmode) {
    case 0: case 1:
        /* no-op */
        break;
    case 2: case 3:
        imm <<= 8;
        break;
    case 4: case 5:
        imm <<= 16;
        break;
    case 6: case 7:
        imm <<= 24;
        break;
    case 8: case 9:
        imm |= imm << 16;
        break;
    case 10: case 11:
        imm = (imm << 8) | (imm << 24);
        break;
    case 12:
        imm = (imm << 8) | 0xff;
        break;
    case 13:
        imm = (imm << 16) | 0xffff;
        break;
    case 14:
        if (op) {
            /*
             * This is the only case where the top and bottom 32 bits
             * of the encoded constant differ.
             */
            uint64_t imm64 = 0;
            int n;

            for (n = 0; n < 8; n++) {
                if (imm & (1 << n)) {
                    imm64 |= (0xffULL << (n * 8));
                }
            }
            return imm64;
        }
        imm |= (imm << 8) | (imm << 16) | (imm << 24);
        break;
    case 15:
        imm = ((imm & 0x80) << 24) | ((imm & 0x3f) << 19)
            | ((imm & 0x40) ? (0x1f << 25) : (1 << 30));
        break;
    }
    if (op) {
        imm = ~imm;
    }
    return dup_const(MO_32, imm);
}

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

    reg_ofs = neon_reg_offset(a->vd, 0);
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
