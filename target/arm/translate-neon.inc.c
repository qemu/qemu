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

/* Return the offset of a 2**SIZE piece of a NEON register, at index ELE,
 * where 0 is the least significant end of the register.
 */
static inline long
neon_element_offset(int reg, int element, MemOp size)
{
    int element_size = 1 << size;
    int ofs = element * element_size;
#ifdef HOST_WORDS_BIGENDIAN
    /* Calculate the offset assuming fully little-endian,
     * then XOR to account for the order of the 8-byte units.
     */
    if (element_size < 8) {
        ofs ^= 8 - element_size;
    }
#endif
    return neon_reg_offset(reg, 0) + ofs;
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
    tcg_temp_free_i32(rm0);
    if (a->shift != 0) {
        tcg_gen_shli_i64(tmp, tmp, a->shift);
        tcg_gen_andi_i64(tmp, tmp, ~widen_mask);
    }
    neon_store_reg64(tmp, a->vd);

    widenfn(tmp, rm1);
    tcg_temp_free_i32(rm1);
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
                      NeonGenTwoSingleOpFn *fn)
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

static bool do_prewiden_3d(DisasContext *s, arg_3diff *a,
                           NeonGenWidenFn *widenfn,
                           NeonGenTwo64OpFn *opfn,
                           bool src1_wide)
{
    /* 3-regs different lengths, prewidening case (VADDL/VSUBL/VAADW/VSUBW) */
    TCGv_i64 rn0_64, rn1_64, rm_64;
    TCGv_i32 rm;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if (!widenfn || !opfn) {
        /* size == 3 case, which is an entirely different insn group */
        return false;
    }

    if ((a->vd & 1) || (src1_wide && (a->vn & 1))) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    rn0_64 = tcg_temp_new_i64();
    rn1_64 = tcg_temp_new_i64();
    rm_64 = tcg_temp_new_i64();

    if (src1_wide) {
        neon_load_reg64(rn0_64, a->vn);
    } else {
        TCGv_i32 tmp = neon_load_reg(a->vn, 0);
        widenfn(rn0_64, tmp);
        tcg_temp_free_i32(tmp);
    }
    rm = neon_load_reg(a->vm, 0);

    widenfn(rm_64, rm);
    tcg_temp_free_i32(rm);
    opfn(rn0_64, rn0_64, rm_64);

    /*
     * Load second pass inputs before storing the first pass result, to
     * avoid incorrect results if a narrow input overlaps with the result.
     */
    if (src1_wide) {
        neon_load_reg64(rn1_64, a->vn + 1);
    } else {
        TCGv_i32 tmp = neon_load_reg(a->vn, 1);
        widenfn(rn1_64, tmp);
        tcg_temp_free_i32(tmp);
    }
    rm = neon_load_reg(a->vm, 1);

    neon_store_reg64(rn0_64, a->vd);

    widenfn(rm_64, rm);
    tcg_temp_free_i32(rm);
    opfn(rn1_64, rn1_64, rm_64);
    neon_store_reg64(rn1_64, a->vd + 1);

    tcg_temp_free_i64(rn0_64);
    tcg_temp_free_i64(rn1_64);
    tcg_temp_free_i64(rm_64);

    return true;
}

#define DO_PREWIDEN(INSN, S, EXT, OP, SRC1WIDE)                         \
    static bool trans_##INSN##_3d(DisasContext *s, arg_3diff *a)        \
    {                                                                   \
        static NeonGenWidenFn * const widenfn[] = {                     \
            gen_helper_neon_widen_##S##8,                               \
            gen_helper_neon_widen_##S##16,                              \
            tcg_gen_##EXT##_i32_i64,                                    \
            NULL,                                                       \
        };                                                              \
        static NeonGenTwo64OpFn * const addfn[] = {                     \
            gen_helper_neon_##OP##l_u16,                                \
            gen_helper_neon_##OP##l_u32,                                \
            tcg_gen_##OP##_i64,                                         \
            NULL,                                                       \
        };                                                              \
        return do_prewiden_3d(s, a, widenfn[a->size],                   \
                              addfn[a->size], SRC1WIDE);                \
    }

DO_PREWIDEN(VADDL_S, s, ext, add, false)
DO_PREWIDEN(VADDL_U, u, extu, add, false)
DO_PREWIDEN(VSUBL_S, s, ext, sub, false)
DO_PREWIDEN(VSUBL_U, u, extu, sub, false)
DO_PREWIDEN(VADDW_S, s, ext, add, true)
DO_PREWIDEN(VADDW_U, u, extu, add, true)
DO_PREWIDEN(VSUBW_S, s, ext, sub, true)
DO_PREWIDEN(VSUBW_U, u, extu, sub, true)

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

    neon_load_reg64(rn_64, a->vn);
    neon_load_reg64(rm_64, a->vm);

    opfn(rn_64, rn_64, rm_64);

    narrowfn(rd0, rn_64);

    neon_load_reg64(rn_64, a->vn + 1);
    neon_load_reg64(rm_64, a->vm + 1);

    opfn(rn_64, rn_64, rm_64);

    narrowfn(rd1, rn_64);

    neon_store_reg(a->vd, 0, rd0);
    neon_store_reg(a->vd, 1, rd1);

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

    rn = neon_load_reg(a->vn, 0);
    rm = neon_load_reg(a->vm, 0);
    opfn(rd0, rn, rm);
    tcg_temp_free_i32(rn);
    tcg_temp_free_i32(rm);

    rn = neon_load_reg(a->vn, 1);
    rm = neon_load_reg(a->vm, 1);
    opfn(rd1, rn, rm);
    tcg_temp_free_i32(rn);
    tcg_temp_free_i32(rm);

    /* Don't store results until after all loads: they might overlap */
    if (accfn) {
        tmp = tcg_temp_new_i64();
        neon_load_reg64(tmp, a->vd);
        accfn(tmp, tmp, rd0);
        neon_store_reg64(tmp, a->vd);
        neon_load_reg64(tmp, a->vd + 1);
        accfn(tmp, tmp, rd1);
        neon_store_reg64(tmp, a->vd + 1);
        tcg_temp_free_i64(tmp);
    } else {
        neon_store_reg64(rd0, a->vd);
        neon_store_reg64(rd1, a->vd + 1);
    }

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

    tcg_gen_gvec_3_ool(neon_reg_offset(a->vd, 0),
                       neon_reg_offset(a->vn, 0),
                       neon_reg_offset(a->vm, 0),
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
    TCGv_i32 tmp;
    if (size == 1) {
        tmp = neon_load_reg(reg & 7, reg >> 4);
        if (reg & 8) {
            gen_neon_dup_high16(tmp);
        } else {
            gen_neon_dup_low16(tmp);
        }
    } else {
        tmp = neon_load_reg(reg & 15, reg >> 4);
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
    TCGv_i32 scalar;
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

    for (pass = 0; pass < (a->q ? 4 : 2); pass++) {
        TCGv_i32 tmp = neon_load_reg(a->vn, pass);
        opfn(tmp, tmp, scalar);
        if (accfn) {
            TCGv_i32 rd = neon_load_reg(a->vd, pass);
            accfn(tmp, rd, tmp);
            tcg_temp_free_i32(rd);
        }
        neon_store_reg(a->vd, pass, tmp);
    }
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

/*
 * Rather than have a float-specific version of do_2scalar just for
 * three insns, we wrap a NeonGenTwoSingleOpFn to turn it into
 * a NeonGenTwoOpFn.
 */
#define WRAP_FP_FN(WRAPNAME, FUNC)                              \
    static void WRAPNAME(TCGv_i32 rd, TCGv_i32 rn, TCGv_i32 rm) \
    {                                                           \
        TCGv_ptr fpstatus = get_fpstatus_ptr(1);                \
        FUNC(rd, rn, rm, fpstatus);                             \
        tcg_temp_free_ptr(fpstatus);                            \
    }

WRAP_FP_FN(gen_VMUL_F_mul, gen_helper_vfp_muls)
WRAP_FP_FN(gen_VMUL_F_add, gen_helper_vfp_adds)
WRAP_FP_FN(gen_VMUL_F_sub, gen_helper_vfp_subs)

static bool trans_VMUL_F_2sc(DisasContext *s, arg_2scalar *a)
{
    static NeonGenTwoOpFn * const opfn[] = {
        NULL,
        NULL, /* TODO: fp16 support */
        gen_VMUL_F_mul,
        NULL,
    };

    return do_2scalar(s, a, opfn[a->size], NULL);
}

static bool trans_VMLA_F_2sc(DisasContext *s, arg_2scalar *a)
{
    static NeonGenTwoOpFn * const opfn[] = {
        NULL,
        NULL, /* TODO: fp16 support */
        gen_VMUL_F_mul,
        NULL,
    };
    static NeonGenTwoOpFn * const accfn[] = {
        NULL,
        NULL, /* TODO: fp16 support */
        gen_VMUL_F_add,
        NULL,
    };

    return do_2scalar(s, a, opfn[a->size], accfn[a->size]);
}

static bool trans_VMLS_F_2sc(DisasContext *s, arg_2scalar *a)
{
    static NeonGenTwoOpFn * const opfn[] = {
        NULL,
        NULL, /* TODO: fp16 support */
        gen_VMUL_F_mul,
        NULL,
    };
    static NeonGenTwoOpFn * const accfn[] = {
        NULL,
        NULL, /* TODO: fp16 support */
        gen_VMUL_F_sub,
        NULL,
    };

    return do_2scalar(s, a, opfn[a->size], accfn[a->size]);
}

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
    TCGv_i32 scalar;
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

    for (pass = 0; pass < (a->q ? 4 : 2); pass++) {
        TCGv_i32 rn = neon_load_reg(a->vn, pass);
        TCGv_i32 rd = neon_load_reg(a->vd, pass);
        opfn(rd, cpu_env, rn, scalar, rd);
        tcg_temp_free_i32(rn);
        neon_store_reg(a->vd, pass, rd);
    }
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
    rn = neon_load_reg(a->vn, 0);
    rn0_64 = tcg_temp_new_i64();
    opfn(rn0_64, rn, scalar);
    tcg_temp_free_i32(rn);

    rn = neon_load_reg(a->vn, 1);
    rn1_64 = tcg_temp_new_i64();
    opfn(rn1_64, rn, scalar);
    tcg_temp_free_i32(rn);
    tcg_temp_free_i32(scalar);

    if (accfn) {
        TCGv_i64 t64 = tcg_temp_new_i64();
        neon_load_reg64(t64, a->vd);
        accfn(t64, t64, rn0_64);
        neon_store_reg64(t64, a->vd);
        neon_load_reg64(t64, a->vd + 1);
        accfn(t64, t64, rn1_64);
        neon_store_reg64(t64, a->vd + 1);
        tcg_temp_free_i64(t64);
    } else {
        neon_store_reg64(rn0_64, a->vd);
        neon_store_reg64(rn1_64, a->vd + 1);
    }
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

        neon_load_reg64(right, a->vn);
        neon_load_reg64(left, a->vm);
        tcg_gen_extract2_i64(dest, right, left, a->imm * 8);
        neon_store_reg64(dest, a->vd);

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
            neon_load_reg64(right, a->vn);
            neon_load_reg64(middle, a->vn + 1);
            tcg_gen_extract2_i64(destright, right, middle, a->imm * 8);
            neon_load_reg64(left, a->vm);
            tcg_gen_extract2_i64(destleft, middle, left, a->imm * 8);
        } else {
            neon_load_reg64(right, a->vn + 1);
            neon_load_reg64(middle, a->vm);
            tcg_gen_extract2_i64(destright, right, middle, (a->imm - 8) * 8);
            neon_load_reg64(left, a->vm + 1);
            tcg_gen_extract2_i64(destleft, middle, left, (a->imm - 8) * 8);
        }

        neon_store_reg64(destright, a->vd);
        neon_store_reg64(destleft, a->vd + 1);

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
    int n;
    TCGv_i32 tmp, tmp2, tmp3, tmp4;
    TCGv_ptr ptr1;

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

    n = a->len + 1;
    if ((a->vn + n) > 32) {
        /*
         * This is UNPREDICTABLE; we choose to UNDEF to avoid the
         * helper function running off the end of the register file.
         */
        return false;
    }
    n <<= 3;
    if (a->op) {
        tmp = neon_load_reg(a->vd, 0);
    } else {
        tmp = tcg_temp_new_i32();
        tcg_gen_movi_i32(tmp, 0);
    }
    tmp2 = neon_load_reg(a->vm, 0);
    ptr1 = vfp_reg_ptr(true, a->vn);
    tmp4 = tcg_const_i32(n);
    gen_helper_neon_tbl(tmp2, tmp2, tmp, ptr1, tmp4);
    tcg_temp_free_i32(tmp);
    if (a->op) {
        tmp = neon_load_reg(a->vd, 1);
    } else {
        tmp = tcg_temp_new_i32();
        tcg_gen_movi_i32(tmp, 0);
    }
    tmp3 = neon_load_reg(a->vm, 1);
    gen_helper_neon_tbl(tmp3, tmp3, tmp, ptr1, tmp4);
    tcg_temp_free_i32(tmp4);
    tcg_temp_free_ptr(ptr1);
    neon_store_reg(a->vd, 0, tmp2);
    neon_store_reg(a->vd, 1, tmp3);
    tcg_temp_free_i32(tmp);
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

    tcg_gen_gvec_dup_mem(a->size, neon_reg_offset(a->vd, 0),
                         neon_element_offset(a->vm, a->index, a->size),
                         a->q ? 16 : 8, a->q ? 16 : 8);
    return true;
}

static bool trans_VREV64(DisasContext *s, arg_VREV64 *a)
{
    int pass, half;

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

    for (pass = 0; pass < (a->q ? 2 : 1); pass++) {
        TCGv_i32 tmp[2];

        for (half = 0; half < 2; half++) {
            tmp[half] = neon_load_reg(a->vm, pass * 2 + half);
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
        neon_store_reg(a->vd, pass * 2, tmp[1]);
        neon_store_reg(a->vd, pass * 2 + 1, tmp[0]);
    }
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
        tmp = neon_load_reg(a->vm, pass * 2);
        widenfn(rm0_64, tmp);
        tcg_temp_free_i32(tmp);
        tmp = neon_load_reg(a->vm, pass * 2 + 1);
        widenfn(rm1_64, tmp);
        tcg_temp_free_i32(tmp);
        opfn(rd_64, rm0_64, rm1_64);
        tcg_temp_free_i64(rm0_64);
        tcg_temp_free_i64(rm1_64);

        if (accfn) {
            TCGv_i64 tmp64 = tcg_temp_new_i64();
            neon_load_reg64(tmp64, a->vd + pass);
            accfn(rd_64, tmp64, rd_64);
            tcg_temp_free_i64(tmp64);
        }
        neon_store_reg64(rd_64, a->vd + pass);
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

    neon_load_reg64(rm, a->vm);
    narrowfn(rd0, cpu_env, rm);
    neon_load_reg64(rm, a->vm + 1);
    narrowfn(rd1, cpu_env, rm);
    neon_store_reg(a->vd, 0, rd0);
    neon_store_reg(a->vd, 1, rd1);
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

    rm0 = neon_load_reg(a->vm, 0);
    rm1 = neon_load_reg(a->vm, 1);

    widenfn(rd, rm0);
    tcg_gen_shli_i64(rd, rd, 8 << a->size);
    neon_store_reg64(rd, a->vd);
    widenfn(rd, rm1);
    tcg_gen_shli_i64(rd, rd, 8 << a->size);
    neon_store_reg64(rd, a->vd + 1);

    tcg_temp_free_i64(rd);
    tcg_temp_free_i32(rm0);
    tcg_temp_free_i32(rm1);
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

    fpst = get_fpstatus_ptr(true);
    ahp = get_ahp_flag();
    tmp = neon_load_reg(a->vm, 0);
    gen_helper_vfp_fcvt_f32_to_f16(tmp, tmp, fpst, ahp);
    tmp2 = neon_load_reg(a->vm, 1);
    gen_helper_vfp_fcvt_f32_to_f16(tmp2, tmp2, fpst, ahp);
    tcg_gen_shli_i32(tmp2, tmp2, 16);
    tcg_gen_or_i32(tmp2, tmp2, tmp);
    tcg_temp_free_i32(tmp);
    tmp = neon_load_reg(a->vm, 2);
    gen_helper_vfp_fcvt_f32_to_f16(tmp, tmp, fpst, ahp);
    tmp3 = neon_load_reg(a->vm, 3);
    neon_store_reg(a->vd, 0, tmp2);
    gen_helper_vfp_fcvt_f32_to_f16(tmp3, tmp3, fpst, ahp);
    tcg_gen_shli_i32(tmp3, tmp3, 16);
    tcg_gen_or_i32(tmp3, tmp3, tmp);
    neon_store_reg(a->vd, 1, tmp3);
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

    fpst = get_fpstatus_ptr(true);
    ahp = get_ahp_flag();
    tmp3 = tcg_temp_new_i32();
    tmp = neon_load_reg(a->vm, 0);
    tmp2 = neon_load_reg(a->vm, 1);
    tcg_gen_ext16u_i32(tmp3, tmp);
    gen_helper_vfp_fcvt_f16_to_f32(tmp3, tmp3, fpst, ahp);
    neon_store_reg(a->vd, 0, tmp3);
    tcg_gen_shri_i32(tmp, tmp, 16);
    gen_helper_vfp_fcvt_f16_to_f32(tmp, tmp, fpst, ahp);
    neon_store_reg(a->vd, 1, tmp);
    tmp3 = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(tmp3, tmp2);
    gen_helper_vfp_fcvt_f16_to_f32(tmp3, tmp3, fpst, ahp);
    neon_store_reg(a->vd, 2, tmp3);
    tcg_gen_shri_i32(tmp2, tmp2, 16);
    gen_helper_vfp_fcvt_f16_to_f32(tmp2, tmp2, fpst, ahp);
    neon_store_reg(a->vd, 3, tmp2);
    tcg_temp_free_i32(ahp);
    tcg_temp_free_ptr(fpst);

    return true;
}

static bool do_2misc_vec(DisasContext *s, arg_2misc *a, GVecGen2Fn *fn)
{
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

    for (pass = 0; pass < (a->q ? 4 : 2); pass++) {
        TCGv_i32 tmp = neon_load_reg(a->vm, pass);
        fn(tmp, tmp);
        neon_store_reg(a->vd, pass, tmp);
    }

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

static bool trans_VABS_F(DisasContext *s, arg_2misc *a)
{
    if (a->size != 2) {
        return false;
    }
    /* TODO: FP16 : size == 1 */
    return do_2misc(s, a, gen_helper_vfp_abss);
}

static bool trans_VNEG_F(DisasContext *s, arg_2misc *a)
{
    if (a->size != 2) {
        return false;
    }
    /* TODO: FP16 : size == 1 */
    return do_2misc(s, a, gen_helper_vfp_negs);
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

static bool do_2misc_fp(DisasContext *s, arg_2misc *a,
                        NeonGenOneSingleOpFn *fn)
{
    int pass;
    TCGv_ptr fpst;

    /* Handle a 2-reg-misc operation by iterating 32 bits at a time */
    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if (a->size != 2) {
        /* TODO: FP16 will be the size == 1 case */
        return false;
    }

    if ((a->vd | a->vm) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    fpst = get_fpstatus_ptr(1);
    for (pass = 0; pass < (a->q ? 4 : 2); pass++) {
        TCGv_i32 tmp = neon_load_reg(a->vm, pass);
        fn(tmp, tmp, fpst);
        neon_store_reg(a->vd, pass, tmp);
    }
    tcg_temp_free_ptr(fpst);

    return true;
}

#define DO_2MISC_FP(INSN, FUNC)                                 \
    static bool trans_##INSN(DisasContext *s, arg_2misc *a)     \
    {                                                           \
        return do_2misc_fp(s, a, FUNC);                         \
    }

DO_2MISC_FP(VRECPE_F, gen_helper_recpe_f32)
DO_2MISC_FP(VRSQRTE_F, gen_helper_rsqrte_f32)
DO_2MISC_FP(VCVT_FS, gen_helper_vfp_sitos)
DO_2MISC_FP(VCVT_FU, gen_helper_vfp_uitos)
DO_2MISC_FP(VCVT_SF, gen_helper_vfp_tosizs)
DO_2MISC_FP(VCVT_UF, gen_helper_vfp_touizs)

static bool trans_VRINTX(DisasContext *s, arg_2misc *a)
{
    if (!arm_dc_feature(s, ARM_FEATURE_V8)) {
        return false;
    }
    return do_2misc_fp(s, a, gen_helper_rints_exact);
}

#define WRAP_FP_CMP0_FWD(WRAPNAME, FUNC)                        \
    static void WRAPNAME(TCGv_i32 d, TCGv_i32 m, TCGv_ptr fpst) \
    {                                                           \
        TCGv_i32 zero = tcg_const_i32(0);                       \
        FUNC(d, m, zero, fpst);                                 \
        tcg_temp_free_i32(zero);                                \
    }
#define WRAP_FP_CMP0_REV(WRAPNAME, FUNC)                        \
    static void WRAPNAME(TCGv_i32 d, TCGv_i32 m, TCGv_ptr fpst) \
    {                                                           \
        TCGv_i32 zero = tcg_const_i32(0);                       \
        FUNC(d, zero, m, fpst);                                 \
        tcg_temp_free_i32(zero);                                \
    }

#define DO_FP_CMP0(INSN, FUNC, REV)                             \
    WRAP_FP_CMP0_##REV(gen_##INSN, FUNC)                        \
    static bool trans_##INSN(DisasContext *s, arg_2misc *a)     \
    {                                                           \
        return do_2misc_fp(s, a, gen_##INSN);                   \
    }

DO_FP_CMP0(VCGT0_F, gen_helper_neon_cgt_f32, FWD)
DO_FP_CMP0(VCGE0_F, gen_helper_neon_cge_f32, FWD)
DO_FP_CMP0(VCEQ0_F, gen_helper_neon_ceq_f32, FWD)
DO_FP_CMP0(VCLE0_F, gen_helper_neon_cge_f32, REV)
DO_FP_CMP0(VCLT0_F, gen_helper_neon_cgt_f32, REV)

static bool do_vrint(DisasContext *s, arg_2misc *a, int rmode)
{
    /*
     * Handle a VRINT* operation by iterating 32 bits at a time,
     * with a specified rounding mode in operation.
     */
    int pass;
    TCGv_ptr fpst;
    TCGv_i32 tcg_rmode;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON) ||
        !arm_dc_feature(s, ARM_FEATURE_V8)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if (a->size != 2) {
        /* TODO: FP16 will be the size == 1 case */
        return false;
    }

    if ((a->vd | a->vm) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    fpst = get_fpstatus_ptr(1);
    tcg_rmode = tcg_const_i32(arm_rmode_to_sf(rmode));
    gen_helper_set_neon_rmode(tcg_rmode, tcg_rmode, cpu_env);
    for (pass = 0; pass < (a->q ? 4 : 2); pass++) {
        TCGv_i32 tmp = neon_load_reg(a->vm, pass);
        gen_helper_rints(tmp, tmp, fpst);
        neon_store_reg(a->vd, pass, tmp);
    }
    gen_helper_set_neon_rmode(tcg_rmode, tcg_rmode, cpu_env);
    tcg_temp_free_i32(tcg_rmode);
    tcg_temp_free_ptr(fpst);

    return true;
}

#define DO_VRINT(INSN, RMODE)                                   \
    static bool trans_##INSN(DisasContext *s, arg_2misc *a)     \
    {                                                           \
        return do_vrint(s, a, RMODE);                           \
    }

DO_VRINT(VRINTN, FPROUNDING_TIEEVEN)
DO_VRINT(VRINTA, FPROUNDING_TIEAWAY)
DO_VRINT(VRINTZ, FPROUNDING_ZERO)
DO_VRINT(VRINTM, FPROUNDING_NEGINF)
DO_VRINT(VRINTP, FPROUNDING_POSINF)

static bool do_vcvt(DisasContext *s, arg_2misc *a, int rmode, bool is_signed)
{
    /*
     * Handle a VCVT* operation by iterating 32 bits at a time,
     * with a specified rounding mode in operation.
     */
    int pass;
    TCGv_ptr fpst;
    TCGv_i32 tcg_rmode, tcg_shift;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON) ||
        !arm_dc_feature(s, ARM_FEATURE_V8)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if (a->size != 2) {
        /* TODO: FP16 will be the size == 1 case */
        return false;
    }

    if ((a->vd | a->vm) & a->q) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    fpst = get_fpstatus_ptr(1);
    tcg_shift = tcg_const_i32(0);
    tcg_rmode = tcg_const_i32(arm_rmode_to_sf(rmode));
    gen_helper_set_neon_rmode(tcg_rmode, tcg_rmode, cpu_env);
    for (pass = 0; pass < (a->q ? 4 : 2); pass++) {
        TCGv_i32 tmp = neon_load_reg(a->vm, pass);
        if (is_signed) {
            gen_helper_vfp_tosls(tmp, tmp, tcg_shift, fpst);
        } else {
            gen_helper_vfp_touls(tmp, tmp, tcg_shift, fpst);
        }
        neon_store_reg(a->vd, pass, tmp);
    }
    gen_helper_set_neon_rmode(tcg_rmode, tcg_rmode, cpu_env);
    tcg_temp_free_i32(tcg_rmode);
    tcg_temp_free_i32(tcg_shift);
    tcg_temp_free_ptr(fpst);

    return true;
}

#define DO_VCVT(INSN, RMODE, SIGNED)                            \
    static bool trans_##INSN(DisasContext *s, arg_2misc *a)     \
    {                                                           \
        return do_vcvt(s, a, RMODE, SIGNED);                    \
    }

DO_VCVT(VCVTAU, FPROUNDING_TIEAWAY, false)
DO_VCVT(VCVTAS, FPROUNDING_TIEAWAY, true)
DO_VCVT(VCVTNU, FPROUNDING_TIEEVEN, false)
DO_VCVT(VCVTNS, FPROUNDING_TIEEVEN, true)
DO_VCVT(VCVTPU, FPROUNDING_POSINF, false)
DO_VCVT(VCVTPS, FPROUNDING_POSINF, true)
DO_VCVT(VCVTMU, FPROUNDING_NEGINF, false)
DO_VCVT(VCVTMS, FPROUNDING_NEGINF, true)

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
        neon_load_reg64(rm, a->vm + pass);
        neon_load_reg64(rd, a->vd + pass);
        neon_store_reg64(rm, a->vd + pass);
        neon_store_reg64(rd, a->vm + pass);
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

    if (a->size == 2) {
        for (pass = 0; pass < (a->q ? 4 : 2); pass += 2) {
            tmp = neon_load_reg(a->vm, pass);
            tmp2 = neon_load_reg(a->vd, pass + 1);
            neon_store_reg(a->vm, pass, tmp2);
            neon_store_reg(a->vd, pass + 1, tmp);
        }
    } else {
        for (pass = 0; pass < (a->q ? 4 : 2); pass++) {
            tmp = neon_load_reg(a->vm, pass);
            tmp2 = neon_load_reg(a->vd, pass);
            if (a->size == 0) {
                gen_neon_trn_u8(tmp, tmp2);
            } else {
                gen_neon_trn_u16(tmp, tmp2);
            }
            neon_store_reg(a->vm, pass, tmp2);
            neon_store_reg(a->vd, pass, tmp);
        }
    }
    return true;
}
