/*
 *  ARM generic vector expansion
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *  Copyright (c) 2005-2007 CodeSourcery
 *  Copyright (c) 2007 OpenedHand, Ltd.
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


static void gen_gvec_fn3_qc(uint32_t rd_ofs, uint32_t rn_ofs, uint32_t rm_ofs,
                            uint32_t opr_sz, uint32_t max_sz,
                            gen_helper_gvec_3_ptr *fn)
{
    TCGv_ptr qc_ptr = tcg_temp_new_ptr();

    tcg_debug_assert(opr_sz <= sizeof_field(CPUARMState, vfp.qc));
    tcg_gen_addi_ptr(qc_ptr, tcg_env, offsetof(CPUARMState, vfp.qc));
    tcg_gen_gvec_3_ptr(rd_ofs, rn_ofs, rm_ofs, qc_ptr,
                       opr_sz, max_sz, 0, fn);
}

void gen_gvec_sqdmulh_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                         uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static gen_helper_gvec_3_ptr * const fns[2] = {
        gen_helper_neon_sqdmulh_h, gen_helper_neon_sqdmulh_s
    };
    tcg_debug_assert(vece >= 1 && vece <= 2);
    gen_gvec_fn3_qc(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, fns[vece - 1]);
}

void gen_gvec_sqrdmulh_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                         uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static gen_helper_gvec_3_ptr * const fns[2] = {
        gen_helper_neon_sqrdmulh_h, gen_helper_neon_sqrdmulh_s
    };
    tcg_debug_assert(vece >= 1 && vece <= 2);
    gen_gvec_fn3_qc(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, fns[vece - 1]);
}

void gen_gvec_sqrdmlah_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                          uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static gen_helper_gvec_3_ptr * const fns[2] = {
        gen_helper_gvec_qrdmlah_s16, gen_helper_gvec_qrdmlah_s32
    };
    tcg_debug_assert(vece >= 1 && vece <= 2);
    gen_gvec_fn3_qc(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, fns[vece - 1]);
}

void gen_gvec_sqrdmlsh_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                          uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static gen_helper_gvec_3_ptr * const fns[2] = {
        gen_helper_gvec_qrdmlsh_s16, gen_helper_gvec_qrdmlsh_s32
    };
    tcg_debug_assert(vece >= 1 && vece <= 2);
    gen_gvec_fn3_qc(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, fns[vece - 1]);
}

#define GEN_CMP0(NAME, COND)                              \
    void NAME(unsigned vece, uint32_t d, uint32_t m,      \
              uint32_t opr_sz, uint32_t max_sz)           \
    { tcg_gen_gvec_cmpi(COND, vece, d, m, 0, opr_sz, max_sz); }

GEN_CMP0(gen_gvec_ceq0, TCG_COND_EQ)
GEN_CMP0(gen_gvec_cle0, TCG_COND_LE)
GEN_CMP0(gen_gvec_cge0, TCG_COND_GE)
GEN_CMP0(gen_gvec_clt0, TCG_COND_LT)
GEN_CMP0(gen_gvec_cgt0, TCG_COND_GT)

#undef GEN_CMP0

void gen_gvec_sshr(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                   int64_t shift, uint32_t opr_sz, uint32_t max_sz)
{
    /* Signed shift out of range results in all-sign-bits */
    shift = MIN(shift, (8 << vece) - 1);
    tcg_gen_gvec_sari(vece, rd_ofs, rm_ofs, shift, opr_sz, max_sz);
}

void gen_gvec_ushr(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                   int64_t shift, uint32_t opr_sz, uint32_t max_sz)
{
    /* Unsigned shift out of range results in all-zero-bits */
    if (shift >= (8 << vece)) {
        tcg_gen_gvec_dup_imm(vece, rd_ofs, opr_sz, max_sz, 0);
    } else {
        tcg_gen_gvec_shri(vece, rd_ofs, rm_ofs, shift, opr_sz, max_sz);
    }
}

static void gen_ssra8_i64(TCGv_i64 d, TCGv_i64 a, int64_t shift)
{
    tcg_gen_vec_sar8i_i64(a, a, shift);
    tcg_gen_vec_add8_i64(d, d, a);
}

static void gen_ssra16_i64(TCGv_i64 d, TCGv_i64 a, int64_t shift)
{
    tcg_gen_vec_sar16i_i64(a, a, shift);
    tcg_gen_vec_add16_i64(d, d, a);
}

static void gen_ssra32_i32(TCGv_i32 d, TCGv_i32 a, int32_t shift)
{
    tcg_gen_sari_i32(a, a, shift);
    tcg_gen_add_i32(d, d, a);
}

static void gen_ssra64_i64(TCGv_i64 d, TCGv_i64 a, int64_t shift)
{
    tcg_gen_sari_i64(a, a, shift);
    tcg_gen_add_i64(d, d, a);
}

static void gen_ssra_vec(unsigned vece, TCGv_vec d, TCGv_vec a, int64_t sh)
{
    tcg_gen_sari_vec(vece, a, a, sh);
    tcg_gen_add_vec(vece, d, d, a);
}

void gen_gvec_ssra(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                   int64_t shift, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sari_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen2i ops[4] = {
        { .fni8 = gen_ssra8_i64,
          .fniv = gen_ssra_vec,
          .fno = gen_helper_gvec_ssra_b,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fni8 = gen_ssra16_i64,
          .fniv = gen_ssra_vec,
          .fno = gen_helper_gvec_ssra_h,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_ssra32_i32,
          .fniv = gen_ssra_vec,
          .fno = gen_helper_gvec_ssra_s,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_ssra64_i64,
          .fniv = gen_ssra_vec,
          .fno = gen_helper_gvec_ssra_d,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_64 },
    };

    /* tszimm encoding produces immediates in the range [1..esize]. */
    tcg_debug_assert(shift > 0);
    tcg_debug_assert(shift <= (8 << vece));

    /*
     * Shifts larger than the element size are architecturally valid.
     * Signed results in all sign bits.
     */
    shift = MIN(shift, (8 << vece) - 1);
    tcg_gen_gvec_2i(rd_ofs, rm_ofs, opr_sz, max_sz, shift, &ops[vece]);
}

static void gen_usra8_i64(TCGv_i64 d, TCGv_i64 a, int64_t shift)
{
    tcg_gen_vec_shr8i_i64(a, a, shift);
    tcg_gen_vec_add8_i64(d, d, a);
}

static void gen_usra16_i64(TCGv_i64 d, TCGv_i64 a, int64_t shift)
{
    tcg_gen_vec_shr16i_i64(a, a, shift);
    tcg_gen_vec_add16_i64(d, d, a);
}

static void gen_usra32_i32(TCGv_i32 d, TCGv_i32 a, int32_t shift)
{
    tcg_gen_shri_i32(a, a, shift);
    tcg_gen_add_i32(d, d, a);
}

static void gen_usra64_i64(TCGv_i64 d, TCGv_i64 a, int64_t shift)
{
    tcg_gen_shri_i64(a, a, shift);
    tcg_gen_add_i64(d, d, a);
}

static void gen_usra_vec(unsigned vece, TCGv_vec d, TCGv_vec a, int64_t sh)
{
    tcg_gen_shri_vec(vece, a, a, sh);
    tcg_gen_add_vec(vece, d, d, a);
}

void gen_gvec_usra(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                   int64_t shift, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen2i ops[4] = {
        { .fni8 = gen_usra8_i64,
          .fniv = gen_usra_vec,
          .fno = gen_helper_gvec_usra_b,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_8, },
        { .fni8 = gen_usra16_i64,
          .fniv = gen_usra_vec,
          .fno = gen_helper_gvec_usra_h,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_16, },
        { .fni4 = gen_usra32_i32,
          .fniv = gen_usra_vec,
          .fno = gen_helper_gvec_usra_s,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_32, },
        { .fni8 = gen_usra64_i64,
          .fniv = gen_usra_vec,
          .fno = gen_helper_gvec_usra_d,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_64, },
    };

    /* tszimm encoding produces immediates in the range [1..esize]. */
    tcg_debug_assert(shift > 0);
    tcg_debug_assert(shift <= (8 << vece));

    /*
     * Shifts larger than the element size are architecturally valid.
     * Unsigned results in all zeros as input to accumulate: nop.
     */
    if (shift < (8 << vece)) {
        tcg_gen_gvec_2i(rd_ofs, rm_ofs, opr_sz, max_sz, shift, &ops[vece]);
    } else {
        /* Nop, but we do need to clear the tail. */
        tcg_gen_gvec_mov(vece, rd_ofs, rd_ofs, opr_sz, max_sz);
    }
}

/*
 * Shift one less than the requested amount, and the low bit is
 * the rounding bit.  For the 8 and 16-bit operations, because we
 * mask the low bit, we can perform a normal integer shift instead
 * of a vector shift.
 */
static void gen_srshr8_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_shri_i64(t, a, sh - 1);
    tcg_gen_andi_i64(t, t, dup_const(MO_8, 1));
    tcg_gen_vec_sar8i_i64(d, a, sh);
    tcg_gen_vec_add8_i64(d, d, t);
}

static void gen_srshr16_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_shri_i64(t, a, sh - 1);
    tcg_gen_andi_i64(t, t, dup_const(MO_16, 1));
    tcg_gen_vec_sar16i_i64(d, a, sh);
    tcg_gen_vec_add16_i64(d, d, t);
}

void gen_srshr32_i32(TCGv_i32 d, TCGv_i32 a, int32_t sh)
{
    TCGv_i32 t;

    /* Handle shift by the input size for the benefit of trans_SRSHR_ri */
    if (sh == 32) {
        tcg_gen_movi_i32(d, 0);
        return;
    }
    t = tcg_temp_new_i32();
    tcg_gen_extract_i32(t, a, sh - 1, 1);
    tcg_gen_sari_i32(d, a, sh);
    tcg_gen_add_i32(d, d, t);
}

void gen_srshr64_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_extract_i64(t, a, sh - 1, 1);
    tcg_gen_sari_i64(d, a, sh);
    tcg_gen_add_i64(d, d, t);
}

static void gen_srshr_vec(unsigned vece, TCGv_vec d, TCGv_vec a, int64_t sh)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    TCGv_vec ones = tcg_constant_vec_matching(d, vece, 1);

    tcg_gen_shri_vec(vece, t, a, sh - 1);
    tcg_gen_and_vec(vece, t, t, ones);
    tcg_gen_sari_vec(vece, d, a, sh);
    tcg_gen_add_vec(vece, d, d, t);
}

void gen_gvec_srshr(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                    int64_t shift, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_sari_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen2i ops[4] = {
        { .fni8 = gen_srshr8_i64,
          .fniv = gen_srshr_vec,
          .fno = gen_helper_gvec_srshr_b,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fni8 = gen_srshr16_i64,
          .fniv = gen_srshr_vec,
          .fno = gen_helper_gvec_srshr_h,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_srshr32_i32,
          .fniv = gen_srshr_vec,
          .fno = gen_helper_gvec_srshr_s,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_srshr64_i64,
          .fniv = gen_srshr_vec,
          .fno = gen_helper_gvec_srshr_d,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };

    /* tszimm encoding produces immediates in the range [1..esize] */
    tcg_debug_assert(shift > 0);
    tcg_debug_assert(shift <= (8 << vece));

    if (shift == (8 << vece)) {
        /*
         * Shifts larger than the element size are architecturally valid.
         * Signed results in all sign bits.  With rounding, this produces
         *   (-1 + 1) >> 1 == 0, or (0 + 1) >> 1 == 0.
         * I.e. always zero.
         */
        tcg_gen_gvec_dup_imm(vece, rd_ofs, opr_sz, max_sz, 0);
    } else {
        tcg_gen_gvec_2i(rd_ofs, rm_ofs, opr_sz, max_sz, shift, &ops[vece]);
    }
}

static void gen_srsra8_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();

    gen_srshr8_i64(t, a, sh);
    tcg_gen_vec_add8_i64(d, d, t);
}

static void gen_srsra16_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();

    gen_srshr16_i64(t, a, sh);
    tcg_gen_vec_add16_i64(d, d, t);
}

static void gen_srsra32_i32(TCGv_i32 d, TCGv_i32 a, int32_t sh)
{
    TCGv_i32 t = tcg_temp_new_i32();

    gen_srshr32_i32(t, a, sh);
    tcg_gen_add_i32(d, d, t);
}

static void gen_srsra64_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();

    gen_srshr64_i64(t, a, sh);
    tcg_gen_add_i64(d, d, t);
}

static void gen_srsra_vec(unsigned vece, TCGv_vec d, TCGv_vec a, int64_t sh)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);

    gen_srshr_vec(vece, t, a, sh);
    tcg_gen_add_vec(vece, d, d, t);
}

void gen_gvec_srsra(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                    int64_t shift, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_sari_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen2i ops[4] = {
        { .fni8 = gen_srsra8_i64,
          .fniv = gen_srsra_vec,
          .fno = gen_helper_gvec_srsra_b,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_8 },
        { .fni8 = gen_srsra16_i64,
          .fniv = gen_srsra_vec,
          .fno = gen_helper_gvec_srsra_h,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_16 },
        { .fni4 = gen_srsra32_i32,
          .fniv = gen_srsra_vec,
          .fno = gen_helper_gvec_srsra_s,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_32 },
        { .fni8 = gen_srsra64_i64,
          .fniv = gen_srsra_vec,
          .fno = gen_helper_gvec_srsra_d,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_64 },
    };

    /* tszimm encoding produces immediates in the range [1..esize] */
    tcg_debug_assert(shift > 0);
    tcg_debug_assert(shift <= (8 << vece));

    /*
     * Shifts larger than the element size are architecturally valid.
     * Signed results in all sign bits.  With rounding, this produces
     *   (-1 + 1) >> 1 == 0, or (0 + 1) >> 1 == 0.
     * I.e. always zero.  With accumulation, this leaves D unchanged.
     */
    if (shift == (8 << vece)) {
        /* Nop, but we do need to clear the tail. */
        tcg_gen_gvec_mov(vece, rd_ofs, rd_ofs, opr_sz, max_sz);
    } else {
        tcg_gen_gvec_2i(rd_ofs, rm_ofs, opr_sz, max_sz, shift, &ops[vece]);
    }
}

static void gen_urshr8_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_shri_i64(t, a, sh - 1);
    tcg_gen_andi_i64(t, t, dup_const(MO_8, 1));
    tcg_gen_vec_shr8i_i64(d, a, sh);
    tcg_gen_vec_add8_i64(d, d, t);
}

static void gen_urshr16_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_shri_i64(t, a, sh - 1);
    tcg_gen_andi_i64(t, t, dup_const(MO_16, 1));
    tcg_gen_vec_shr16i_i64(d, a, sh);
    tcg_gen_vec_add16_i64(d, d, t);
}

void gen_urshr32_i32(TCGv_i32 d, TCGv_i32 a, int32_t sh)
{
    TCGv_i32 t;

    /* Handle shift by the input size for the benefit of trans_URSHR_ri */
    if (sh == 32) {
        tcg_gen_extract_i32(d, a, sh - 1, 1);
        return;
    }
    t = tcg_temp_new_i32();
    tcg_gen_extract_i32(t, a, sh - 1, 1);
    tcg_gen_shri_i32(d, a, sh);
    tcg_gen_add_i32(d, d, t);
}

void gen_urshr64_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_extract_i64(t, a, sh - 1, 1);
    tcg_gen_shri_i64(d, a, sh);
    tcg_gen_add_i64(d, d, t);
}

static void gen_urshr_vec(unsigned vece, TCGv_vec d, TCGv_vec a, int64_t shift)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    TCGv_vec ones = tcg_constant_vec_matching(d, vece, 1);

    tcg_gen_shri_vec(vece, t, a, shift - 1);
    tcg_gen_and_vec(vece, t, t, ones);
    tcg_gen_shri_vec(vece, d, a, shift);
    tcg_gen_add_vec(vece, d, d, t);
}

void gen_gvec_urshr(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                    int64_t shift, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen2i ops[4] = {
        { .fni8 = gen_urshr8_i64,
          .fniv = gen_urshr_vec,
          .fno = gen_helper_gvec_urshr_b,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fni8 = gen_urshr16_i64,
          .fniv = gen_urshr_vec,
          .fno = gen_helper_gvec_urshr_h,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_urshr32_i32,
          .fniv = gen_urshr_vec,
          .fno = gen_helper_gvec_urshr_s,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_urshr64_i64,
          .fniv = gen_urshr_vec,
          .fno = gen_helper_gvec_urshr_d,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };

    /* tszimm encoding produces immediates in the range [1..esize] */
    tcg_debug_assert(shift > 0);
    tcg_debug_assert(shift <= (8 << vece));

    if (shift == (8 << vece)) {
        /*
         * Shifts larger than the element size are architecturally valid.
         * Unsigned results in zero.  With rounding, this produces a
         * copy of the most significant bit.
         */
        tcg_gen_gvec_shri(vece, rd_ofs, rm_ofs, shift - 1, opr_sz, max_sz);
    } else {
        tcg_gen_gvec_2i(rd_ofs, rm_ofs, opr_sz, max_sz, shift, &ops[vece]);
    }
}

static void gen_ursra8_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();

    if (sh == 8) {
        tcg_gen_vec_shr8i_i64(t, a, 7);
    } else {
        gen_urshr8_i64(t, a, sh);
    }
    tcg_gen_vec_add8_i64(d, d, t);
}

static void gen_ursra16_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();

    if (sh == 16) {
        tcg_gen_vec_shr16i_i64(t, a, 15);
    } else {
        gen_urshr16_i64(t, a, sh);
    }
    tcg_gen_vec_add16_i64(d, d, t);
}

static void gen_ursra32_i32(TCGv_i32 d, TCGv_i32 a, int32_t sh)
{
    TCGv_i32 t = tcg_temp_new_i32();

    if (sh == 32) {
        tcg_gen_shri_i32(t, a, 31);
    } else {
        gen_urshr32_i32(t, a, sh);
    }
    tcg_gen_add_i32(d, d, t);
}

static void gen_ursra64_i64(TCGv_i64 d, TCGv_i64 a, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();

    if (sh == 64) {
        tcg_gen_shri_i64(t, a, 63);
    } else {
        gen_urshr64_i64(t, a, sh);
    }
    tcg_gen_add_i64(d, d, t);
}

static void gen_ursra_vec(unsigned vece, TCGv_vec d, TCGv_vec a, int64_t sh)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);

    if (sh == (8 << vece)) {
        tcg_gen_shri_vec(vece, t, a, sh - 1);
    } else {
        gen_urshr_vec(vece, t, a, sh);
    }
    tcg_gen_add_vec(vece, d, d, t);
}

void gen_gvec_ursra(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                    int64_t shift, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen2i ops[4] = {
        { .fni8 = gen_ursra8_i64,
          .fniv = gen_ursra_vec,
          .fno = gen_helper_gvec_ursra_b,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_8 },
        { .fni8 = gen_ursra16_i64,
          .fniv = gen_ursra_vec,
          .fno = gen_helper_gvec_ursra_h,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_16 },
        { .fni4 = gen_ursra32_i32,
          .fniv = gen_ursra_vec,
          .fno = gen_helper_gvec_ursra_s,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_32 },
        { .fni8 = gen_ursra64_i64,
          .fniv = gen_ursra_vec,
          .fno = gen_helper_gvec_ursra_d,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_64 },
    };

    /* tszimm encoding produces immediates in the range [1..esize] */
    tcg_debug_assert(shift > 0);
    tcg_debug_assert(shift <= (8 << vece));

    tcg_gen_gvec_2i(rd_ofs, rm_ofs, opr_sz, max_sz, shift, &ops[vece]);
}

static void gen_shr8_ins_i64(TCGv_i64 d, TCGv_i64 a, int64_t shift)
{
    uint64_t mask = dup_const(MO_8, 0xff >> shift);
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_shri_i64(t, a, shift);
    tcg_gen_andi_i64(t, t, mask);
    tcg_gen_andi_i64(d, d, ~mask);
    tcg_gen_or_i64(d, d, t);
}

static void gen_shr16_ins_i64(TCGv_i64 d, TCGv_i64 a, int64_t shift)
{
    uint64_t mask = dup_const(MO_16, 0xffff >> shift);
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_shri_i64(t, a, shift);
    tcg_gen_andi_i64(t, t, mask);
    tcg_gen_andi_i64(d, d, ~mask);
    tcg_gen_or_i64(d, d, t);
}

static void gen_shr32_ins_i32(TCGv_i32 d, TCGv_i32 a, int32_t shift)
{
    tcg_gen_shri_i32(a, a, shift);
    tcg_gen_deposit_i32(d, d, a, 0, 32 - shift);
}

static void gen_shr64_ins_i64(TCGv_i64 d, TCGv_i64 a, int64_t shift)
{
    tcg_gen_shri_i64(a, a, shift);
    tcg_gen_deposit_i64(d, d, a, 0, 64 - shift);
}

static void gen_shr_ins_vec(unsigned vece, TCGv_vec d, TCGv_vec a, int64_t sh)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    int64_t mi = MAKE_64BIT_MASK((8 << vece) - sh, sh);
    TCGv_vec m = tcg_constant_vec_matching(d, vece, mi);

    tcg_gen_shri_vec(vece, t, a, sh);
    tcg_gen_and_vec(vece, d, d, m);
    tcg_gen_or_vec(vece, d, d, t);
}

void gen_gvec_sri(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                  int64_t shift, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = { INDEX_op_shri_vec, 0 };
    const GVecGen2i ops[4] = {
        { .fni8 = gen_shr8_ins_i64,
          .fniv = gen_shr_ins_vec,
          .fno = gen_helper_gvec_sri_b,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fni8 = gen_shr16_ins_i64,
          .fniv = gen_shr_ins_vec,
          .fno = gen_helper_gvec_sri_h,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_shr32_ins_i32,
          .fniv = gen_shr_ins_vec,
          .fno = gen_helper_gvec_sri_s,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_shr64_ins_i64,
          .fniv = gen_shr_ins_vec,
          .fno = gen_helper_gvec_sri_d,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };

    /* tszimm encoding produces immediates in the range [1..esize]. */
    tcg_debug_assert(shift > 0);
    tcg_debug_assert(shift <= (8 << vece));

    /* Shift of esize leaves destination unchanged. */
    if (shift < (8 << vece)) {
        tcg_gen_gvec_2i(rd_ofs, rm_ofs, opr_sz, max_sz, shift, &ops[vece]);
    } else {
        /* Nop, but we do need to clear the tail. */
        tcg_gen_gvec_mov(vece, rd_ofs, rd_ofs, opr_sz, max_sz);
    }
}

static void gen_shl8_ins_i64(TCGv_i64 d, TCGv_i64 a, int64_t shift)
{
    uint64_t mask = dup_const(MO_8, 0xff << shift);
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_shli_i64(t, a, shift);
    tcg_gen_andi_i64(t, t, mask);
    tcg_gen_andi_i64(d, d, ~mask);
    tcg_gen_or_i64(d, d, t);
}

static void gen_shl16_ins_i64(TCGv_i64 d, TCGv_i64 a, int64_t shift)
{
    uint64_t mask = dup_const(MO_16, 0xffff << shift);
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_shli_i64(t, a, shift);
    tcg_gen_andi_i64(t, t, mask);
    tcg_gen_andi_i64(d, d, ~mask);
    tcg_gen_or_i64(d, d, t);
}

static void gen_shl32_ins_i32(TCGv_i32 d, TCGv_i32 a, int32_t shift)
{
    tcg_gen_deposit_i32(d, d, a, shift, 32 - shift);
}

static void gen_shl64_ins_i64(TCGv_i64 d, TCGv_i64 a, int64_t shift)
{
    tcg_gen_deposit_i64(d, d, a, shift, 64 - shift);
}

static void gen_shl_ins_vec(unsigned vece, TCGv_vec d, TCGv_vec a, int64_t sh)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    TCGv_vec m = tcg_constant_vec_matching(d, vece, MAKE_64BIT_MASK(0, sh));

    tcg_gen_shli_vec(vece, t, a, sh);
    tcg_gen_and_vec(vece, d, d, m);
    tcg_gen_or_vec(vece, d, d, t);
}

void gen_gvec_sli(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                  int64_t shift, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = { INDEX_op_shli_vec, 0 };
    const GVecGen2i ops[4] = {
        { .fni8 = gen_shl8_ins_i64,
          .fniv = gen_shl_ins_vec,
          .fno = gen_helper_gvec_sli_b,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fni8 = gen_shl16_ins_i64,
          .fniv = gen_shl_ins_vec,
          .fno = gen_helper_gvec_sli_h,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_shl32_ins_i32,
          .fniv = gen_shl_ins_vec,
          .fno = gen_helper_gvec_sli_s,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_shl64_ins_i64,
          .fniv = gen_shl_ins_vec,
          .fno = gen_helper_gvec_sli_d,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };

    /* tszimm encoding produces immediates in the range [0..esize-1]. */
    tcg_debug_assert(shift >= 0);
    tcg_debug_assert(shift < (8 << vece));

    if (shift == 0) {
        tcg_gen_gvec_mov(vece, rd_ofs, rm_ofs, opr_sz, max_sz);
    } else {
        tcg_gen_gvec_2i(rd_ofs, rm_ofs, opr_sz, max_sz, shift, &ops[vece]);
    }
}

static void gen_mla8_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    gen_helper_neon_mul_u8(a, a, b);
    gen_helper_neon_add_u8(d, d, a);
}

static void gen_mls8_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    gen_helper_neon_mul_u8(a, a, b);
    gen_helper_neon_sub_u8(d, d, a);
}

static void gen_mla16_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    gen_helper_neon_mul_u16(a, a, b);
    gen_helper_neon_add_u16(d, d, a);
}

static void gen_mls16_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    gen_helper_neon_mul_u16(a, a, b);
    gen_helper_neon_sub_u16(d, d, a);
}

static void gen_mla32_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    tcg_gen_mul_i32(a, a, b);
    tcg_gen_add_i32(d, d, a);
}

static void gen_mls32_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    tcg_gen_mul_i32(a, a, b);
    tcg_gen_sub_i32(d, d, a);
}

static void gen_mla64_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    tcg_gen_mul_i64(a, a, b);
    tcg_gen_add_i64(d, d, a);
}

static void gen_mls64_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    tcg_gen_mul_i64(a, a, b);
    tcg_gen_sub_i64(d, d, a);
}

static void gen_mla_vec(unsigned vece, TCGv_vec d, TCGv_vec a, TCGv_vec b)
{
    tcg_gen_mul_vec(vece, a, a, b);
    tcg_gen_add_vec(vece, d, d, a);
}

static void gen_mls_vec(unsigned vece, TCGv_vec d, TCGv_vec a, TCGv_vec b)
{
    tcg_gen_mul_vec(vece, a, a, b);
    tcg_gen_sub_vec(vece, d, d, a);
}

/* Note that while NEON does not support VMLA and VMLS as 64-bit ops,
 * these tables are shared with AArch64 which does support them.
 */
void gen_gvec_mla(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                  uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_mul_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen3 ops[4] = {
        { .fni4 = gen_mla8_i32,
          .fniv = gen_mla_vec,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fni4 = gen_mla16_i32,
          .fniv = gen_mla_vec,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_mla32_i32,
          .fniv = gen_mla_vec,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_mla64_i64,
          .fniv = gen_mla_vec,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

void gen_gvec_mls(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                  uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_mul_vec, INDEX_op_sub_vec, 0
    };
    static const GVecGen3 ops[4] = {
        { .fni4 = gen_mls8_i32,
          .fniv = gen_mls_vec,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fni4 = gen_mls16_i32,
          .fniv = gen_mls_vec,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_mls32_i32,
          .fniv = gen_mls_vec,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_mls64_i64,
          .fniv = gen_mls_vec,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

/* CMTST : test is "if (X & Y != 0)". */
static void gen_cmtst_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    tcg_gen_negsetcond_i32(TCG_COND_TSTNE, d, a, b);
}

void gen_cmtst_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    tcg_gen_negsetcond_i64(TCG_COND_TSTNE, d, a, b);
}

static void gen_cmtst_vec(unsigned vece, TCGv_vec d, TCGv_vec a, TCGv_vec b)
{
    tcg_gen_cmp_vec(TCG_COND_TSTNE, vece, d, a, b);
}

void gen_gvec_cmtst(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = { INDEX_op_cmp_vec, 0 };
    static const GVecGen3 ops[4] = {
        { .fni4 = gen_helper_neon_tst_u8,
          .fniv = gen_cmtst_vec,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fni4 = gen_helper_neon_tst_u16,
          .fniv = gen_cmtst_vec,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_cmtst_i32,
          .fniv = gen_cmtst_vec,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_cmtst_i64,
          .fniv = gen_cmtst_vec,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

void gen_ushl_i32(TCGv_i32 dst, TCGv_i32 src, TCGv_i32 shift)
{
    TCGv_i32 lval = tcg_temp_new_i32();
    TCGv_i32 rval = tcg_temp_new_i32();
    TCGv_i32 lsh = tcg_temp_new_i32();
    TCGv_i32 rsh = tcg_temp_new_i32();
    TCGv_i32 zero = tcg_constant_i32(0);
    TCGv_i32 max = tcg_constant_i32(32);

    /*
     * Rely on the TCG guarantee that out of range shifts produce
     * unspecified results, not undefined behaviour (i.e. no trap).
     * Discard out-of-range results after the fact.
     */
    tcg_gen_ext8s_i32(lsh, shift);
    tcg_gen_neg_i32(rsh, lsh);
    tcg_gen_shl_i32(lval, src, lsh);
    tcg_gen_shr_i32(rval, src, rsh);
    tcg_gen_movcond_i32(TCG_COND_LTU, dst, lsh, max, lval, zero);
    tcg_gen_movcond_i32(TCG_COND_LTU, dst, rsh, max, rval, dst);
}

void gen_ushl_i64(TCGv_i64 dst, TCGv_i64 src, TCGv_i64 shift)
{
    TCGv_i64 lval = tcg_temp_new_i64();
    TCGv_i64 rval = tcg_temp_new_i64();
    TCGv_i64 lsh = tcg_temp_new_i64();
    TCGv_i64 rsh = tcg_temp_new_i64();
    TCGv_i64 zero = tcg_constant_i64(0);
    TCGv_i64 max = tcg_constant_i64(64);

    /*
     * Rely on the TCG guarantee that out of range shifts produce
     * unspecified results, not undefined behaviour (i.e. no trap).
     * Discard out-of-range results after the fact.
     */
    tcg_gen_ext8s_i64(lsh, shift);
    tcg_gen_neg_i64(rsh, lsh);
    tcg_gen_shl_i64(lval, src, lsh);
    tcg_gen_shr_i64(rval, src, rsh);
    tcg_gen_movcond_i64(TCG_COND_LTU, dst, lsh, max, lval, zero);
    tcg_gen_movcond_i64(TCG_COND_LTU, dst, rsh, max, rval, dst);
}

static void gen_ushl_vec(unsigned vece, TCGv_vec dst,
                         TCGv_vec src, TCGv_vec shift)
{
    TCGv_vec lval = tcg_temp_new_vec_matching(dst);
    TCGv_vec rval = tcg_temp_new_vec_matching(dst);
    TCGv_vec lsh = tcg_temp_new_vec_matching(dst);
    TCGv_vec rsh = tcg_temp_new_vec_matching(dst);
    TCGv_vec max, zero;

    tcg_gen_neg_vec(vece, rsh, shift);
    if (vece == MO_8) {
        tcg_gen_mov_vec(lsh, shift);
    } else {
        TCGv_vec msk = tcg_constant_vec_matching(dst, vece, 0xff);
        tcg_gen_and_vec(vece, lsh, shift, msk);
        tcg_gen_and_vec(vece, rsh, rsh, msk);
    }

    /*
     * Rely on the TCG guarantee that out of range shifts produce
     * unspecified results, not undefined behaviour (i.e. no trap).
     * Discard out-of-range results after the fact.
     */
    tcg_gen_shlv_vec(vece, lval, src, lsh);
    tcg_gen_shrv_vec(vece, rval, src, rsh);

    /*
     * The choice of GE (signed) and GEU (unsigned) are biased toward
     * the instructions of the x86_64 host.  For MO_8, the whole byte
     * is significant so we must use an unsigned compare; otherwise we
     * have already masked to a byte and so a signed compare works.
     * Other tcg hosts have a full set of comparisons and do not care.
     */
    zero = tcg_constant_vec_matching(dst, vece, 0);
    max = tcg_constant_vec_matching(dst, vece, 8 << vece);
    if (vece == MO_8) {
        tcg_gen_cmpsel_vec(TCG_COND_GEU, vece, lval, lsh, max, zero, lval);
        tcg_gen_cmpsel_vec(TCG_COND_GEU, vece, rval, rsh, max, zero, rval);
    } else {
        tcg_gen_cmpsel_vec(TCG_COND_GE, vece, lval, lsh, max, zero, lval);
        tcg_gen_cmpsel_vec(TCG_COND_GE, vece, rval, rsh, max, zero, rval);
    }
    tcg_gen_or_vec(vece, dst, lval, rval);
}

void gen_gvec_ushl(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_neg_vec, INDEX_op_shlv_vec,
        INDEX_op_shrv_vec, INDEX_op_cmpsel_vec, 0
    };
    static const GVecGen3 ops[4] = {
        { .fniv = gen_ushl_vec,
          .fno = gen_helper_gvec_ushl_b,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fniv = gen_ushl_vec,
          .fno = gen_helper_gvec_ushl_h,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_ushl_i32,
          .fniv = gen_ushl_vec,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_ushl_i64,
          .fniv = gen_ushl_vec,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

void gen_sshl_i32(TCGv_i32 dst, TCGv_i32 src, TCGv_i32 shift)
{
    TCGv_i32 lval = tcg_temp_new_i32();
    TCGv_i32 rval = tcg_temp_new_i32();
    TCGv_i32 lsh = tcg_temp_new_i32();
    TCGv_i32 rsh = tcg_temp_new_i32();
    TCGv_i32 zero = tcg_constant_i32(0);
    TCGv_i32 max = tcg_constant_i32(31);

    /*
     * Rely on the TCG guarantee that out of range shifts produce
     * unspecified results, not undefined behaviour (i.e. no trap).
     * Discard out-of-range results after the fact.
     */
    tcg_gen_ext8s_i32(lsh, shift);
    tcg_gen_neg_i32(rsh, lsh);
    tcg_gen_shl_i32(lval, src, lsh);
    tcg_gen_umin_i32(rsh, rsh, max);
    tcg_gen_sar_i32(rval, src, rsh);
    tcg_gen_movcond_i32(TCG_COND_LEU, lval, lsh, max, lval, zero);
    tcg_gen_movcond_i32(TCG_COND_LT, dst, lsh, zero, rval, lval);
}

void gen_sshl_i64(TCGv_i64 dst, TCGv_i64 src, TCGv_i64 shift)
{
    TCGv_i64 lval = tcg_temp_new_i64();
    TCGv_i64 rval = tcg_temp_new_i64();
    TCGv_i64 lsh = tcg_temp_new_i64();
    TCGv_i64 rsh = tcg_temp_new_i64();
    TCGv_i64 zero = tcg_constant_i64(0);
    TCGv_i64 max = tcg_constant_i64(63);

    /*
     * Rely on the TCG guarantee that out of range shifts produce
     * unspecified results, not undefined behaviour (i.e. no trap).
     * Discard out-of-range results after the fact.
     */
    tcg_gen_ext8s_i64(lsh, shift);
    tcg_gen_neg_i64(rsh, lsh);
    tcg_gen_shl_i64(lval, src, lsh);
    tcg_gen_umin_i64(rsh, rsh, max);
    tcg_gen_sar_i64(rval, src, rsh);
    tcg_gen_movcond_i64(TCG_COND_LEU, lval, lsh, max, lval, zero);
    tcg_gen_movcond_i64(TCG_COND_LT, dst, lsh, zero, rval, lval);
}

static void gen_sshl_vec(unsigned vece, TCGv_vec dst,
                         TCGv_vec src, TCGv_vec shift)
{
    TCGv_vec lval = tcg_temp_new_vec_matching(dst);
    TCGv_vec rval = tcg_temp_new_vec_matching(dst);
    TCGv_vec lsh = tcg_temp_new_vec_matching(dst);
    TCGv_vec rsh = tcg_temp_new_vec_matching(dst);
    TCGv_vec max, zero;

    /*
     * Rely on the TCG guarantee that out of range shifts produce
     * unspecified results, not undefined behaviour (i.e. no trap).
     * Discard out-of-range results after the fact.
     */
    tcg_gen_neg_vec(vece, rsh, shift);
    if (vece == MO_8) {
        tcg_gen_mov_vec(lsh, shift);
    } else {
        TCGv_vec msk = tcg_constant_vec_matching(dst, vece, 0xff);
        tcg_gen_and_vec(vece, lsh, shift, msk);
        tcg_gen_and_vec(vece, rsh, rsh, msk);
    }

    /* Bound rsh so out of bound right shift gets -1.  */
    max = tcg_constant_vec_matching(dst, vece, (8 << vece) - 1);
    tcg_gen_umin_vec(vece, rsh, rsh, max);

    tcg_gen_shlv_vec(vece, lval, src, lsh);
    tcg_gen_sarv_vec(vece, rval, src, rsh);

    /* Select in-bound left shift.  */
    zero = tcg_constant_vec_matching(dst, vece, 0);
    tcg_gen_cmpsel_vec(TCG_COND_GT, vece, lval, lsh, max, zero, lval);

    /* Select between left and right shift.  */
    if (vece == MO_8) {
        tcg_gen_cmpsel_vec(TCG_COND_LT, vece, dst, lsh, zero, rval, lval);
    } else {
        TCGv_vec sgn = tcg_constant_vec_matching(dst, vece, 0x80);
        tcg_gen_cmpsel_vec(TCG_COND_LT, vece, dst, lsh, sgn, lval, rval);
    }
}

void gen_gvec_sshl(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_neg_vec, INDEX_op_umin_vec, INDEX_op_shlv_vec,
        INDEX_op_sarv_vec, INDEX_op_cmpsel_vec, 0
    };
    static const GVecGen3 ops[4] = {
        { .fniv = gen_sshl_vec,
          .fno = gen_helper_gvec_sshl_b,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fniv = gen_sshl_vec,
          .fno = gen_helper_gvec_sshl_h,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_sshl_i32,
          .fniv = gen_sshl_vec,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_sshl_i64,
          .fniv = gen_sshl_vec,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

void gen_gvec_srshl(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static gen_helper_gvec_3 * const fns[] = {
        gen_helper_gvec_srshl_b, gen_helper_gvec_srshl_h,
        gen_helper_gvec_srshl_s, gen_helper_gvec_srshl_d,
    };
    tcg_debug_assert(vece <= MO_64);
    tcg_gen_gvec_3_ool(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, 0, fns[vece]);
}

void gen_gvec_urshl(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static gen_helper_gvec_3 * const fns[] = {
        gen_helper_gvec_urshl_b, gen_helper_gvec_urshl_h,
        gen_helper_gvec_urshl_s, gen_helper_gvec_urshl_d,
    };
    tcg_debug_assert(vece <= MO_64);
    tcg_gen_gvec_3_ool(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, 0, fns[vece]);
}

void gen_neon_sqshl(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static gen_helper_gvec_3_ptr * const fns[] = {
        gen_helper_neon_sqshl_b, gen_helper_neon_sqshl_h,
        gen_helper_neon_sqshl_s, gen_helper_neon_sqshl_d,
    };
    tcg_debug_assert(vece <= MO_64);
    tcg_gen_gvec_3_ptr(rd_ofs, rn_ofs, rm_ofs, tcg_env,
                       opr_sz, max_sz, 0, fns[vece]);
}

void gen_neon_uqshl(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static gen_helper_gvec_3_ptr * const fns[] = {
        gen_helper_neon_uqshl_b, gen_helper_neon_uqshl_h,
        gen_helper_neon_uqshl_s, gen_helper_neon_uqshl_d,
    };
    tcg_debug_assert(vece <= MO_64);
    tcg_gen_gvec_3_ptr(rd_ofs, rn_ofs, rm_ofs, tcg_env,
                       opr_sz, max_sz, 0, fns[vece]);
}

void gen_neon_sqrshl(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                     uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static gen_helper_gvec_3_ptr * const fns[] = {
        gen_helper_neon_sqrshl_b, gen_helper_neon_sqrshl_h,
        gen_helper_neon_sqrshl_s, gen_helper_neon_sqrshl_d,
    };
    tcg_debug_assert(vece <= MO_64);
    tcg_gen_gvec_3_ptr(rd_ofs, rn_ofs, rm_ofs, tcg_env,
                       opr_sz, max_sz, 0, fns[vece]);
}

void gen_neon_uqrshl(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                     uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static gen_helper_gvec_3_ptr * const fns[] = {
        gen_helper_neon_uqrshl_b, gen_helper_neon_uqrshl_h,
        gen_helper_neon_uqrshl_s, gen_helper_neon_uqrshl_d,
    };
    tcg_debug_assert(vece <= MO_64);
    tcg_gen_gvec_3_ptr(rd_ofs, rn_ofs, rm_ofs, tcg_env,
                       opr_sz, max_sz, 0, fns[vece]);
}

void gen_neon_sqshli(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                     int64_t c, uint32_t opr_sz, uint32_t max_sz)
{
    static gen_helper_gvec_2_ptr * const fns[] = {
        gen_helper_neon_sqshli_b, gen_helper_neon_sqshli_h,
        gen_helper_neon_sqshli_s, gen_helper_neon_sqshli_d,
    };
    tcg_debug_assert(vece <= MO_64);
    tcg_debug_assert(c >= 0 && c <= (8 << vece));
    tcg_gen_gvec_2_ptr(rd_ofs, rn_ofs, tcg_env, opr_sz, max_sz, c, fns[vece]);
}

void gen_neon_uqshli(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                     int64_t c, uint32_t opr_sz, uint32_t max_sz)
{
    static gen_helper_gvec_2_ptr * const fns[] = {
        gen_helper_neon_uqshli_b, gen_helper_neon_uqshli_h,
        gen_helper_neon_uqshli_s, gen_helper_neon_uqshli_d,
    };
    tcg_debug_assert(vece <= MO_64);
    tcg_debug_assert(c >= 0 && c <= (8 << vece));
    tcg_gen_gvec_2_ptr(rd_ofs, rn_ofs, tcg_env, opr_sz, max_sz, c, fns[vece]);
}

void gen_neon_sqshlui(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                      int64_t c, uint32_t opr_sz, uint32_t max_sz)
{
    static gen_helper_gvec_2_ptr * const fns[] = {
        gen_helper_neon_sqshlui_b, gen_helper_neon_sqshlui_h,
        gen_helper_neon_sqshlui_s, gen_helper_neon_sqshlui_d,
    };
    tcg_debug_assert(vece <= MO_64);
    tcg_debug_assert(c >= 0 && c <= (8 << vece));
    tcg_gen_gvec_2_ptr(rd_ofs, rn_ofs, tcg_env, opr_sz, max_sz, c, fns[vece]);
}

void gen_uqadd_bhs(TCGv_i64 res, TCGv_i64 qc, TCGv_i64 a, TCGv_i64 b, MemOp esz)
{
    uint64_t max = MAKE_64BIT_MASK(0, 8 << esz);
    TCGv_i64 tmp = tcg_temp_new_i64();

    tcg_gen_add_i64(tmp, a, b);
    tcg_gen_umin_i64(res, tmp, tcg_constant_i64(max));
    tcg_gen_xor_i64(tmp, tmp, res);
    tcg_gen_or_i64(qc, qc, tmp);
}

void gen_uqadd_d(TCGv_i64 res, TCGv_i64 qc, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_add_i64(t, a, b);
    tcg_gen_movcond_i64(TCG_COND_LTU, res, t, a,
                        tcg_constant_i64(UINT64_MAX), t);
    tcg_gen_xor_i64(t, t, res);
    tcg_gen_or_i64(qc, qc, t);
}

static void gen_uqadd_vec(unsigned vece, TCGv_vec t, TCGv_vec qc,
                          TCGv_vec a, TCGv_vec b)
{
    TCGv_vec x = tcg_temp_new_vec_matching(t);
    tcg_gen_add_vec(vece, x, a, b);
    tcg_gen_usadd_vec(vece, t, a, b);
    tcg_gen_xor_vec(vece, x, x, t);
    tcg_gen_or_vec(vece, qc, qc, x);
}

void gen_gvec_uqadd_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                       uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_usadd_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen4 ops[4] = {
        { .fniv = gen_uqadd_vec,
          .fno = gen_helper_gvec_uqadd_b,
          .write_aofs = true,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fniv = gen_uqadd_vec,
          .fno = gen_helper_gvec_uqadd_h,
          .write_aofs = true,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fniv = gen_uqadd_vec,
          .fno = gen_helper_gvec_uqadd_s,
          .write_aofs = true,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fniv = gen_uqadd_vec,
          .fni8 = gen_uqadd_d,
          .fno = gen_helper_gvec_uqadd_d,
          .write_aofs = true,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };

    tcg_debug_assert(opr_sz <= sizeof_field(CPUARMState, vfp.qc));
    tcg_gen_gvec_4(rd_ofs, offsetof(CPUARMState, vfp.qc),
                   rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

void gen_sqadd_bhs(TCGv_i64 res, TCGv_i64 qc, TCGv_i64 a, TCGv_i64 b, MemOp esz)
{
    int64_t max = MAKE_64BIT_MASK(0, (8 << esz) - 1);
    int64_t min = -1ll - max;
    TCGv_i64 tmp = tcg_temp_new_i64();

    tcg_gen_add_i64(tmp, a, b);
    tcg_gen_smin_i64(res, tmp, tcg_constant_i64(max));
    tcg_gen_smax_i64(res, res, tcg_constant_i64(min));
    tcg_gen_xor_i64(tmp, tmp, res);
    tcg_gen_or_i64(qc, qc, tmp);
}

void gen_sqadd_d(TCGv_i64 res, TCGv_i64 qc, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();

    tcg_gen_add_i64(t0, a, b);

    /* Compute signed overflow indication into T1 */
    tcg_gen_xor_i64(t1, a, b);
    tcg_gen_xor_i64(t2, t0, a);
    tcg_gen_andc_i64(t1, t2, t1);

    /* Compute saturated value into T2 */
    tcg_gen_sari_i64(t2, a, 63);
    tcg_gen_xori_i64(t2, t2, INT64_MAX);

    tcg_gen_movcond_i64(TCG_COND_LT, res, t1, tcg_constant_i64(0), t2, t0);
    tcg_gen_xor_i64(t0, t0, res);
    tcg_gen_or_i64(qc, qc, t0);
}

static void gen_sqadd_vec(unsigned vece, TCGv_vec t, TCGv_vec qc,
                          TCGv_vec a, TCGv_vec b)
{
    TCGv_vec x = tcg_temp_new_vec_matching(t);
    tcg_gen_add_vec(vece, x, a, b);
    tcg_gen_ssadd_vec(vece, t, a, b);
    tcg_gen_xor_vec(vece, x, x, t);
    tcg_gen_or_vec(vece, qc, qc, x);
}

void gen_gvec_sqadd_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                       uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_ssadd_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen4 ops[4] = {
        { .fniv = gen_sqadd_vec,
          .fno = gen_helper_gvec_sqadd_b,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_8 },
        { .fniv = gen_sqadd_vec,
          .fno = gen_helper_gvec_sqadd_h,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_16 },
        { .fniv = gen_sqadd_vec,
          .fno = gen_helper_gvec_sqadd_s,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_32 },
        { .fniv = gen_sqadd_vec,
          .fni8 = gen_sqadd_d,
          .fno = gen_helper_gvec_sqadd_d,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_64 },
    };

    tcg_debug_assert(opr_sz <= sizeof_field(CPUARMState, vfp.qc));
    tcg_gen_gvec_4(rd_ofs, offsetof(CPUARMState, vfp.qc),
                   rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

void gen_uqsub_bhs(TCGv_i64 res, TCGv_i64 qc, TCGv_i64 a, TCGv_i64 b, MemOp esz)
{
    TCGv_i64 tmp = tcg_temp_new_i64();

    tcg_gen_sub_i64(tmp, a, b);
    tcg_gen_smax_i64(res, tmp, tcg_constant_i64(0));
    tcg_gen_xor_i64(tmp, tmp, res);
    tcg_gen_or_i64(qc, qc, tmp);
}

void gen_uqsub_d(TCGv_i64 res, TCGv_i64 qc, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_sub_i64(t, a, b);
    tcg_gen_movcond_i64(TCG_COND_LTU, res, a, b, tcg_constant_i64(0), t);
    tcg_gen_xor_i64(t, t, res);
    tcg_gen_or_i64(qc, qc, t);
}

static void gen_uqsub_vec(unsigned vece, TCGv_vec t, TCGv_vec qc,
                          TCGv_vec a, TCGv_vec b)
{
    TCGv_vec x = tcg_temp_new_vec_matching(t);
    tcg_gen_sub_vec(vece, x, a, b);
    tcg_gen_ussub_vec(vece, t, a, b);
    tcg_gen_xor_vec(vece, x, x, t);
    tcg_gen_or_vec(vece, qc, qc, x);
}

void gen_gvec_uqsub_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                       uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_ussub_vec, INDEX_op_sub_vec, 0
    };
    static const GVecGen4 ops[4] = {
        { .fniv = gen_uqsub_vec,
          .fno = gen_helper_gvec_uqsub_b,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_8 },
        { .fniv = gen_uqsub_vec,
          .fno = gen_helper_gvec_uqsub_h,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_16 },
        { .fniv = gen_uqsub_vec,
          .fno = gen_helper_gvec_uqsub_s,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_32 },
        { .fniv = gen_uqsub_vec,
          .fni8 = gen_uqsub_d,
          .fno = gen_helper_gvec_uqsub_d,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_64 },
    };

    tcg_debug_assert(opr_sz <= sizeof_field(CPUARMState, vfp.qc));
    tcg_gen_gvec_4(rd_ofs, offsetof(CPUARMState, vfp.qc),
                   rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

void gen_sqsub_bhs(TCGv_i64 res, TCGv_i64 qc, TCGv_i64 a, TCGv_i64 b, MemOp esz)
{
    int64_t max = MAKE_64BIT_MASK(0, (8 << esz) - 1);
    int64_t min = -1ll - max;
    TCGv_i64 tmp = tcg_temp_new_i64();

    tcg_gen_sub_i64(tmp, a, b);
    tcg_gen_smin_i64(res, tmp, tcg_constant_i64(max));
    tcg_gen_smax_i64(res, res, tcg_constant_i64(min));
    tcg_gen_xor_i64(tmp, tmp, res);
    tcg_gen_or_i64(qc, qc, tmp);
}

void gen_sqsub_d(TCGv_i64 res, TCGv_i64 qc, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();

    tcg_gen_sub_i64(t0, a, b);

    /* Compute signed overflow indication into T1 */
    tcg_gen_xor_i64(t1, a, b);
    tcg_gen_xor_i64(t2, t0, a);
    tcg_gen_and_i64(t1, t1, t2);

    /* Compute saturated value into T2 */
    tcg_gen_sari_i64(t2, a, 63);
    tcg_gen_xori_i64(t2, t2, INT64_MAX);

    tcg_gen_movcond_i64(TCG_COND_LT, res, t1, tcg_constant_i64(0), t2, t0);
    tcg_gen_xor_i64(t0, t0, res);
    tcg_gen_or_i64(qc, qc, t0);
}

static void gen_sqsub_vec(unsigned vece, TCGv_vec t, TCGv_vec qc,
                          TCGv_vec a, TCGv_vec b)
{
    TCGv_vec x = tcg_temp_new_vec_matching(t);
    tcg_gen_sub_vec(vece, x, a, b);
    tcg_gen_sssub_vec(vece, t, a, b);
    tcg_gen_xor_vec(vece, x, x, t);
    tcg_gen_or_vec(vece, qc, qc, x);
}

void gen_gvec_sqsub_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                       uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sssub_vec, INDEX_op_sub_vec, 0
    };
    static const GVecGen4 ops[4] = {
        { .fniv = gen_sqsub_vec,
          .fno = gen_helper_gvec_sqsub_b,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_8 },
        { .fniv = gen_sqsub_vec,
          .fno = gen_helper_gvec_sqsub_h,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_16 },
        { .fniv = gen_sqsub_vec,
          .fno = gen_helper_gvec_sqsub_s,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_32 },
        { .fniv = gen_sqsub_vec,
          .fni8 = gen_sqsub_d,
          .fno = gen_helper_gvec_sqsub_d,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_64 },
    };

    tcg_debug_assert(opr_sz <= sizeof_field(CPUARMState, vfp.qc));
    tcg_gen_gvec_4(rd_ofs, offsetof(CPUARMState, vfp.qc),
                   rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

static void gen_sabd_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_sub_i32(t, a, b);
    tcg_gen_sub_i32(d, b, a);
    tcg_gen_movcond_i32(TCG_COND_LT, d, a, b, d, t);
}

static void gen_sabd_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_sub_i64(t, a, b);
    tcg_gen_sub_i64(d, b, a);
    tcg_gen_movcond_i64(TCG_COND_LT, d, a, b, d, t);
}

static void gen_sabd_vec(unsigned vece, TCGv_vec d, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);

    tcg_gen_smin_vec(vece, t, a, b);
    tcg_gen_smax_vec(vece, d, a, b);
    tcg_gen_sub_vec(vece, d, d, t);
}

void gen_gvec_sabd(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sub_vec, INDEX_op_smin_vec, INDEX_op_smax_vec, 0
    };
    static const GVecGen3 ops[4] = {
        { .fniv = gen_sabd_vec,
          .fno = gen_helper_gvec_sabd_b,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fniv = gen_sabd_vec,
          .fno = gen_helper_gvec_sabd_h,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_sabd_i32,
          .fniv = gen_sabd_vec,
          .fno = gen_helper_gvec_sabd_s,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_sabd_i64,
          .fniv = gen_sabd_vec,
          .fno = gen_helper_gvec_sabd_d,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

static void gen_uabd_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_sub_i32(t, a, b);
    tcg_gen_sub_i32(d, b, a);
    tcg_gen_movcond_i32(TCG_COND_LTU, d, a, b, d, t);
}

static void gen_uabd_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_sub_i64(t, a, b);
    tcg_gen_sub_i64(d, b, a);
    tcg_gen_movcond_i64(TCG_COND_LTU, d, a, b, d, t);
}

static void gen_uabd_vec(unsigned vece, TCGv_vec d, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);

    tcg_gen_umin_vec(vece, t, a, b);
    tcg_gen_umax_vec(vece, d, a, b);
    tcg_gen_sub_vec(vece, d, d, t);
}

void gen_gvec_uabd(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sub_vec, INDEX_op_umin_vec, INDEX_op_umax_vec, 0
    };
    static const GVecGen3 ops[4] = {
        { .fniv = gen_uabd_vec,
          .fno = gen_helper_gvec_uabd_b,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fniv = gen_uabd_vec,
          .fno = gen_helper_gvec_uabd_h,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_uabd_i32,
          .fniv = gen_uabd_vec,
          .fno = gen_helper_gvec_uabd_s,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_uabd_i64,
          .fniv = gen_uabd_vec,
          .fno = gen_helper_gvec_uabd_d,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

static void gen_saba_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t = tcg_temp_new_i32();
    gen_sabd_i32(t, a, b);
    tcg_gen_add_i32(d, d, t);
}

static void gen_saba_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t = tcg_temp_new_i64();
    gen_sabd_i64(t, a, b);
    tcg_gen_add_i64(d, d, t);
}

static void gen_saba_vec(unsigned vece, TCGv_vec d, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    gen_sabd_vec(vece, t, a, b);
    tcg_gen_add_vec(vece, d, d, t);
}

void gen_gvec_saba(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sub_vec, INDEX_op_add_vec,
        INDEX_op_smin_vec, INDEX_op_smax_vec, 0
    };
    static const GVecGen3 ops[4] = {
        { .fniv = gen_saba_vec,
          .fno = gen_helper_gvec_saba_b,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_8 },
        { .fniv = gen_saba_vec,
          .fno = gen_helper_gvec_saba_h,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_16 },
        { .fni4 = gen_saba_i32,
          .fniv = gen_saba_vec,
          .fno = gen_helper_gvec_saba_s,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_32 },
        { .fni8 = gen_saba_i64,
          .fniv = gen_saba_vec,
          .fno = gen_helper_gvec_saba_d,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_64 },
    };
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

static void gen_uaba_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t = tcg_temp_new_i32();
    gen_uabd_i32(t, a, b);
    tcg_gen_add_i32(d, d, t);
}

static void gen_uaba_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t = tcg_temp_new_i64();
    gen_uabd_i64(t, a, b);
    tcg_gen_add_i64(d, d, t);
}

static void gen_uaba_vec(unsigned vece, TCGv_vec d, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    gen_uabd_vec(vece, t, a, b);
    tcg_gen_add_vec(vece, d, d, t);
}

void gen_gvec_uaba(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sub_vec, INDEX_op_add_vec,
        INDEX_op_umin_vec, INDEX_op_umax_vec, 0
    };
    static const GVecGen3 ops[4] = {
        { .fniv = gen_uaba_vec,
          .fno = gen_helper_gvec_uaba_b,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_8 },
        { .fniv = gen_uaba_vec,
          .fno = gen_helper_gvec_uaba_h,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_16 },
        { .fni4 = gen_uaba_i32,
          .fniv = gen_uaba_vec,
          .fno = gen_helper_gvec_uaba_s,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_32 },
        { .fni8 = gen_uaba_i64,
          .fniv = gen_uaba_vec,
          .fno = gen_helper_gvec_uaba_d,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_64 },
    };
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

void gen_gvec_addp(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_gvec_addp_b,
        gen_helper_gvec_addp_h,
        gen_helper_gvec_addp_s,
        gen_helper_gvec_addp_d,
    };
    tcg_gen_gvec_3_ool(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, 0, fns[vece]);
}

void gen_gvec_smaxp(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_gvec_smaxp_b,
        gen_helper_gvec_smaxp_h,
        gen_helper_gvec_smaxp_s,
    };
    tcg_debug_assert(vece <= MO_32);
    tcg_gen_gvec_3_ool(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, 0, fns[vece]);
}

void gen_gvec_sminp(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_gvec_sminp_b,
        gen_helper_gvec_sminp_h,
        gen_helper_gvec_sminp_s,
    };
    tcg_debug_assert(vece <= MO_32);
    tcg_gen_gvec_3_ool(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, 0, fns[vece]);
}

void gen_gvec_umaxp(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_gvec_umaxp_b,
        gen_helper_gvec_umaxp_h,
        gen_helper_gvec_umaxp_s,
    };
    tcg_debug_assert(vece <= MO_32);
    tcg_gen_gvec_3_ool(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, 0, fns[vece]);
}

void gen_gvec_uminp(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_gvec_uminp_b,
        gen_helper_gvec_uminp_h,
        gen_helper_gvec_uminp_s,
    };
    tcg_debug_assert(vece <= MO_32);
    tcg_gen_gvec_3_ool(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, 0, fns[vece]);
}

static void gen_shadd8_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_and_i64(t, a, b);
    tcg_gen_vec_sar8i_i64(a, a, 1);
    tcg_gen_vec_sar8i_i64(b, b, 1);
    tcg_gen_andi_i64(t, t, dup_const(MO_8, 1));
    tcg_gen_vec_add8_i64(d, a, b);
    tcg_gen_vec_add8_i64(d, d, t);
}

static void gen_shadd16_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_and_i64(t, a, b);
    tcg_gen_vec_sar16i_i64(a, a, 1);
    tcg_gen_vec_sar16i_i64(b, b, 1);
    tcg_gen_andi_i64(t, t, dup_const(MO_16, 1));
    tcg_gen_vec_add16_i64(d, a, b);
    tcg_gen_vec_add16_i64(d, d, t);
}

static void gen_shadd_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_and_i32(t, a, b);
    tcg_gen_sari_i32(a, a, 1);
    tcg_gen_sari_i32(b, b, 1);
    tcg_gen_andi_i32(t, t, 1);
    tcg_gen_add_i32(d, a, b);
    tcg_gen_add_i32(d, d, t);
}

static void gen_shadd_vec(unsigned vece, TCGv_vec d, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);

    tcg_gen_and_vec(vece, t, a, b);
    tcg_gen_sari_vec(vece, a, a, 1);
    tcg_gen_sari_vec(vece, b, b, 1);
    tcg_gen_and_vec(vece, t, t, tcg_constant_vec_matching(d, vece, 1));
    tcg_gen_add_vec(vece, d, a, b);
    tcg_gen_add_vec(vece, d, d, t);
}

void gen_gvec_shadd(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sari_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen3 g[] = {
        { .fni8 = gen_shadd8_i64,
          .fniv = gen_shadd_vec,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fni8 = gen_shadd16_i64,
          .fniv = gen_shadd_vec,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_shadd_i32,
          .fniv = gen_shadd_vec,
          .opt_opc = vecop_list,
          .vece = MO_32 },
    };
    tcg_debug_assert(vece <= MO_32);
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &g[vece]);
}

static void gen_uhadd8_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_and_i64(t, a, b);
    tcg_gen_vec_shr8i_i64(a, a, 1);
    tcg_gen_vec_shr8i_i64(b, b, 1);
    tcg_gen_andi_i64(t, t, dup_const(MO_8, 1));
    tcg_gen_vec_add8_i64(d, a, b);
    tcg_gen_vec_add8_i64(d, d, t);
}

static void gen_uhadd16_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_and_i64(t, a, b);
    tcg_gen_vec_shr16i_i64(a, a, 1);
    tcg_gen_vec_shr16i_i64(b, b, 1);
    tcg_gen_andi_i64(t, t, dup_const(MO_16, 1));
    tcg_gen_vec_add16_i64(d, a, b);
    tcg_gen_vec_add16_i64(d, d, t);
}

static void gen_uhadd_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_and_i32(t, a, b);
    tcg_gen_shri_i32(a, a, 1);
    tcg_gen_shri_i32(b, b, 1);
    tcg_gen_andi_i32(t, t, 1);
    tcg_gen_add_i32(d, a, b);
    tcg_gen_add_i32(d, d, t);
}

static void gen_uhadd_vec(unsigned vece, TCGv_vec d, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);

    tcg_gen_and_vec(vece, t, a, b);
    tcg_gen_shri_vec(vece, a, a, 1);
    tcg_gen_shri_vec(vece, b, b, 1);
    tcg_gen_and_vec(vece, t, t, tcg_constant_vec_matching(d, vece, 1));
    tcg_gen_add_vec(vece, d, a, b);
    tcg_gen_add_vec(vece, d, d, t);
}

void gen_gvec_uhadd(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen3 g[] = {
        { .fni8 = gen_uhadd8_i64,
          .fniv = gen_uhadd_vec,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fni8 = gen_uhadd16_i64,
          .fniv = gen_uhadd_vec,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_uhadd_i32,
          .fniv = gen_uhadd_vec,
          .opt_opc = vecop_list,
          .vece = MO_32 },
    };
    tcg_debug_assert(vece <= MO_32);
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &g[vece]);
}

static void gen_shsub8_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_andc_i64(t, b, a);
    tcg_gen_vec_sar8i_i64(a, a, 1);
    tcg_gen_vec_sar8i_i64(b, b, 1);
    tcg_gen_andi_i64(t, t, dup_const(MO_8, 1));
    tcg_gen_vec_sub8_i64(d, a, b);
    tcg_gen_vec_sub8_i64(d, d, t);
}

static void gen_shsub16_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_andc_i64(t, b, a);
    tcg_gen_vec_sar16i_i64(a, a, 1);
    tcg_gen_vec_sar16i_i64(b, b, 1);
    tcg_gen_andi_i64(t, t, dup_const(MO_16, 1));
    tcg_gen_vec_sub16_i64(d, a, b);
    tcg_gen_vec_sub16_i64(d, d, t);
}

static void gen_shsub_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_andc_i32(t, b, a);
    tcg_gen_sari_i32(a, a, 1);
    tcg_gen_sari_i32(b, b, 1);
    tcg_gen_andi_i32(t, t, 1);
    tcg_gen_sub_i32(d, a, b);
    tcg_gen_sub_i32(d, d, t);
}

static void gen_shsub_vec(unsigned vece, TCGv_vec d, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);

    tcg_gen_andc_vec(vece, t, b, a);
    tcg_gen_sari_vec(vece, a, a, 1);
    tcg_gen_sari_vec(vece, b, b, 1);
    tcg_gen_and_vec(vece, t, t, tcg_constant_vec_matching(d, vece, 1));
    tcg_gen_sub_vec(vece, d, a, b);
    tcg_gen_sub_vec(vece, d, d, t);
}

void gen_gvec_shsub(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sari_vec, INDEX_op_sub_vec, 0
    };
    static const GVecGen3 g[4] = {
        { .fni8 = gen_shsub8_i64,
          .fniv = gen_shsub_vec,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fni8 = gen_shsub16_i64,
          .fniv = gen_shsub_vec,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_shsub_i32,
          .fniv = gen_shsub_vec,
          .opt_opc = vecop_list,
          .vece = MO_32 },
    };
    assert(vece <= MO_32);
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &g[vece]);
}

static void gen_uhsub8_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_andc_i64(t, b, a);
    tcg_gen_vec_shr8i_i64(a, a, 1);
    tcg_gen_vec_shr8i_i64(b, b, 1);
    tcg_gen_andi_i64(t, t, dup_const(MO_8, 1));
    tcg_gen_vec_sub8_i64(d, a, b);
    tcg_gen_vec_sub8_i64(d, d, t);
}

static void gen_uhsub16_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_andc_i64(t, b, a);
    tcg_gen_vec_shr16i_i64(a, a, 1);
    tcg_gen_vec_shr16i_i64(b, b, 1);
    tcg_gen_andi_i64(t, t, dup_const(MO_16, 1));
    tcg_gen_vec_sub16_i64(d, a, b);
    tcg_gen_vec_sub16_i64(d, d, t);
}

static void gen_uhsub_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_andc_i32(t, b, a);
    tcg_gen_shri_i32(a, a, 1);
    tcg_gen_shri_i32(b, b, 1);
    tcg_gen_andi_i32(t, t, 1);
    tcg_gen_sub_i32(d, a, b);
    tcg_gen_sub_i32(d, d, t);
}

static void gen_uhsub_vec(unsigned vece, TCGv_vec d, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);

    tcg_gen_andc_vec(vece, t, b, a);
    tcg_gen_shri_vec(vece, a, a, 1);
    tcg_gen_shri_vec(vece, b, b, 1);
    tcg_gen_and_vec(vece, t, t, tcg_constant_vec_matching(d, vece, 1));
    tcg_gen_sub_vec(vece, d, a, b);
    tcg_gen_sub_vec(vece, d, d, t);
}

void gen_gvec_uhsub(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_sub_vec, 0
    };
    static const GVecGen3 g[4] = {
        { .fni8 = gen_uhsub8_i64,
          .fniv = gen_uhsub_vec,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fni8 = gen_uhsub16_i64,
          .fniv = gen_uhsub_vec,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_uhsub_i32,
          .fniv = gen_uhsub_vec,
          .opt_opc = vecop_list,
          .vece = MO_32 },
    };
    assert(vece <= MO_32);
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &g[vece]);
}

static void gen_srhadd8_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_or_i64(t, a, b);
    tcg_gen_vec_sar8i_i64(a, a, 1);
    tcg_gen_vec_sar8i_i64(b, b, 1);
    tcg_gen_andi_i64(t, t, dup_const(MO_8, 1));
    tcg_gen_vec_add8_i64(d, a, b);
    tcg_gen_vec_add8_i64(d, d, t);
}

static void gen_srhadd16_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_or_i64(t, a, b);
    tcg_gen_vec_sar16i_i64(a, a, 1);
    tcg_gen_vec_sar16i_i64(b, b, 1);
    tcg_gen_andi_i64(t, t, dup_const(MO_16, 1));
    tcg_gen_vec_add16_i64(d, a, b);
    tcg_gen_vec_add16_i64(d, d, t);
}

static void gen_srhadd_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_or_i32(t, a, b);
    tcg_gen_sari_i32(a, a, 1);
    tcg_gen_sari_i32(b, b, 1);
    tcg_gen_andi_i32(t, t, 1);
    tcg_gen_add_i32(d, a, b);
    tcg_gen_add_i32(d, d, t);
}

static void gen_srhadd_vec(unsigned vece, TCGv_vec d, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);

    tcg_gen_or_vec(vece, t, a, b);
    tcg_gen_sari_vec(vece, a, a, 1);
    tcg_gen_sari_vec(vece, b, b, 1);
    tcg_gen_and_vec(vece, t, t, tcg_constant_vec_matching(d, vece, 1));
    tcg_gen_add_vec(vece, d, a, b);
    tcg_gen_add_vec(vece, d, d, t);
}

void gen_gvec_srhadd(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                     uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sari_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen3 g[] = {
        { .fni8 = gen_srhadd8_i64,
          .fniv = gen_srhadd_vec,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fni8 = gen_srhadd16_i64,
          .fniv = gen_srhadd_vec,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_srhadd_i32,
          .fniv = gen_srhadd_vec,
          .opt_opc = vecop_list,
          .vece = MO_32 },
    };
    assert(vece <= MO_32);
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &g[vece]);
}

static void gen_urhadd8_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_or_i64(t, a, b);
    tcg_gen_vec_shr8i_i64(a, a, 1);
    tcg_gen_vec_shr8i_i64(b, b, 1);
    tcg_gen_andi_i64(t, t, dup_const(MO_8, 1));
    tcg_gen_vec_add8_i64(d, a, b);
    tcg_gen_vec_add8_i64(d, d, t);
}

static void gen_urhadd16_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_or_i64(t, a, b);
    tcg_gen_vec_shr16i_i64(a, a, 1);
    tcg_gen_vec_shr16i_i64(b, b, 1);
    tcg_gen_andi_i64(t, t, dup_const(MO_16, 1));
    tcg_gen_vec_add16_i64(d, a, b);
    tcg_gen_vec_add16_i64(d, d, t);
}

static void gen_urhadd_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_or_i32(t, a, b);
    tcg_gen_shri_i32(a, a, 1);
    tcg_gen_shri_i32(b, b, 1);
    tcg_gen_andi_i32(t, t, 1);
    tcg_gen_add_i32(d, a, b);
    tcg_gen_add_i32(d, d, t);
}

static void gen_urhadd_vec(unsigned vece, TCGv_vec d, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);

    tcg_gen_or_vec(vece, t, a, b);
    tcg_gen_shri_vec(vece, a, a, 1);
    tcg_gen_shri_vec(vece, b, b, 1);
    tcg_gen_and_vec(vece, t, t, tcg_constant_vec_matching(d, vece, 1));
    tcg_gen_add_vec(vece, d, a, b);
    tcg_gen_add_vec(vece, d, d, t);
}

void gen_gvec_urhadd(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                     uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen3 g[] = {
        { .fni8 = gen_urhadd8_i64,
          .fniv = gen_urhadd_vec,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fni8 = gen_urhadd16_i64,
          .fniv = gen_urhadd_vec,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_urhadd_i32,
          .fniv = gen_urhadd_vec,
          .opt_opc = vecop_list,
          .vece = MO_32 },
    };
    assert(vece <= MO_32);
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &g[vece]);
}

void gen_gvec_cls(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                  uint32_t opr_sz, uint32_t max_sz)
{
    static const GVecGen2 g[] = {
        { .fni4 = gen_helper_neon_cls_s8,
          .vece = MO_8 },
        { .fni4 = gen_helper_neon_cls_s16,
          .vece = MO_16 },
        { .fni4 = tcg_gen_clrsb_i32,
          .vece = MO_32 },
    };
    assert(vece <= MO_32);
    tcg_gen_gvec_2(rd_ofs, rn_ofs, opr_sz, max_sz, &g[vece]);
}

static void gen_clz32_i32(TCGv_i32 d, TCGv_i32 n)
{
    tcg_gen_clzi_i32(d, n, 32);
}

void gen_gvec_clz(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                  uint32_t opr_sz, uint32_t max_sz)
{
    static const GVecGen2 g[] = {
        { .fni4 = gen_helper_neon_clz_u8,
          .vece = MO_8 },
        { .fni4 = gen_helper_neon_clz_u16,
          .vece = MO_16 },
        { .fni4 = gen_clz32_i32,
          .vece = MO_32 },
    };
    assert(vece <= MO_32);
    tcg_gen_gvec_2(rd_ofs, rn_ofs, opr_sz, max_sz, &g[vece]);
}

void gen_gvec_cnt(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                  uint32_t opr_sz, uint32_t max_sz)
{
    assert(vece == MO_8);
    tcg_gen_gvec_2_ool(rd_ofs, rn_ofs, opr_sz, max_sz, 0,
                       gen_helper_gvec_cnt_b);
}

void gen_gvec_rbit(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                  uint32_t opr_sz, uint32_t max_sz)
{
    assert(vece == MO_8);
    tcg_gen_gvec_2_ool(rd_ofs, rn_ofs, opr_sz, max_sz, 0,
                       gen_helper_gvec_rbit_b);
}

void gen_gvec_rev16(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t opr_sz, uint32_t max_sz)
{
    assert(vece == MO_8);
    tcg_gen_gvec_rotli(MO_16, rd_ofs, rn_ofs, 8, opr_sz, max_sz);
}

static void gen_bswap32_i64(TCGv_i64 d, TCGv_i64 n)
{
    tcg_gen_bswap64_i64(d, n);
    tcg_gen_rotli_i64(d, d, 32);
}

void gen_gvec_rev32(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t opr_sz, uint32_t max_sz)
{
    static const GVecGen2 g = {
        .fni8 = gen_bswap32_i64,
        .fni4 = tcg_gen_bswap32_i32,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
        .vece = MO_32
    };

    switch (vece) {
    case MO_16:
        tcg_gen_gvec_rotli(MO_32, rd_ofs, rn_ofs, 16, opr_sz, max_sz);
        break;
    case MO_8:
        tcg_gen_gvec_2(rd_ofs, rn_ofs, opr_sz, max_sz, &g);
        break;
    default:
        g_assert_not_reached();
    }
}

void gen_gvec_rev64(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t opr_sz, uint32_t max_sz)
{
    static const GVecGen2 g[] = {
        { .fni8 = tcg_gen_bswap64_i64,
          .vece = MO_64 },
        { .fni8 = tcg_gen_hswap_i64,
          .vece = MO_64 },
    };

    switch (vece) {
    case MO_32:
        tcg_gen_gvec_rotli(MO_64, rd_ofs, rn_ofs, 32, opr_sz, max_sz);
        break;
    case MO_8:
    case MO_16:
        tcg_gen_gvec_2(rd_ofs, rn_ofs, opr_sz, max_sz, &g[vece]);
        break;
    default:
        g_assert_not_reached();
    }
}

static void gen_saddlp_vec(unsigned vece, TCGv_vec d, TCGv_vec n)
{
    int half = 4 << vece;
    TCGv_vec t = tcg_temp_new_vec_matching(d);

    tcg_gen_shli_vec(vece, t, n, half);
    tcg_gen_sari_vec(vece, d, n, half);
    tcg_gen_sari_vec(vece, t, t, half);
    tcg_gen_add_vec(vece, d, d, t);
}

static void gen_saddlp_s_i64(TCGv_i64 d, TCGv_i64 n)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_ext32s_i64(t, n);
    tcg_gen_sari_i64(d, n, 32);
    tcg_gen_add_i64(d, d, t);
}

void gen_gvec_saddlp(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                     uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sari_vec, INDEX_op_shli_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen2 g[] = {
        { .fniv = gen_saddlp_vec,
          .fni8 = gen_helper_neon_addlp_s8,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fniv = gen_saddlp_vec,
          .fni8 = gen_helper_neon_addlp_s16,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fniv = gen_saddlp_vec,
          .fni8 = gen_saddlp_s_i64,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };
    assert(vece <= MO_32);
    tcg_gen_gvec_2(rd_ofs, rn_ofs, opr_sz, max_sz, &g[vece]);
}

static void gen_sadalp_vec(unsigned vece, TCGv_vec d, TCGv_vec n)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);

    gen_saddlp_vec(vece, t, n);
    tcg_gen_add_vec(vece, d, d, t);
}

static void gen_sadalp_b_i64(TCGv_i64 d, TCGv_i64 n)
{
    TCGv_i64 t = tcg_temp_new_i64();

    gen_helper_neon_addlp_s8(t, n);
    tcg_gen_vec_add16_i64(d, d, t);
}

static void gen_sadalp_h_i64(TCGv_i64 d, TCGv_i64 n)
{
    TCGv_i64 t = tcg_temp_new_i64();

    gen_helper_neon_addlp_s16(t, n);
    tcg_gen_vec_add32_i64(d, d, t);
}

static void gen_sadalp_s_i64(TCGv_i64 d, TCGv_i64 n)
{
    TCGv_i64 t = tcg_temp_new_i64();

    gen_saddlp_s_i64(t, n);
    tcg_gen_add_i64(d, d, t);
}

void gen_gvec_sadalp(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                     uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sari_vec, INDEX_op_shli_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen2 g[] = {
        { .fniv = gen_sadalp_vec,
          .fni8 = gen_sadalp_b_i64,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_16 },
        { .fniv = gen_sadalp_vec,
          .fni8 = gen_sadalp_h_i64,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_32 },
        { .fniv = gen_sadalp_vec,
          .fni8 = gen_sadalp_s_i64,
          .opt_opc = vecop_list,
          .load_dest = true,
          .vece = MO_64 },
    };
    assert(vece <= MO_32);
    tcg_gen_gvec_2(rd_ofs, rn_ofs, opr_sz, max_sz, &g[vece]);
}

static void gen_uaddlp_vec(unsigned vece, TCGv_vec d, TCGv_vec n)
{
    int half = 4 << vece;
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    TCGv_vec m = tcg_constant_vec_matching(d, vece, MAKE_64BIT_MASK(0, half));

    tcg_gen_shri_vec(vece, t, n, half);
    tcg_gen_and_vec(vece, d, n, m);
    tcg_gen_add_vec(vece, d, d, t);
}

static void gen_uaddlp_b_i64(TCGv_i64 d, TCGv_i64 n)
{
    TCGv_i64 t = tcg_temp_new_i64();
    TCGv_i64 m = tcg_constant_i64(dup_const(MO_16, 0xff));

    tcg_gen_shri_i64(t, n, 8);
    tcg_gen_and_i64(d, n, m);
    tcg_gen_and_i64(t, t, m);
    /* No carry between widened unsigned elements. */
    tcg_gen_add_i64(d, d, t);
}

static void gen_uaddlp_h_i64(TCGv_i64 d, TCGv_i64 n)
{
    TCGv_i64 t = tcg_temp_new_i64();
    TCGv_i64 m = tcg_constant_i64(dup_const(MO_32, 0xffff));

    tcg_gen_shri_i64(t, n, 16);
    tcg_gen_and_i64(d, n, m);
    tcg_gen_and_i64(t, t, m);
    /* No carry between widened unsigned elements. */
    tcg_gen_add_i64(d, d, t);
}

static void gen_uaddlp_s_i64(TCGv_i64 d, TCGv_i64 n)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_ext32u_i64(t, n);
    tcg_gen_shri_i64(d, n, 32);
    tcg_gen_add_i64(d, d, t);
}

void gen_gvec_uaddlp(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                     uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen2 g[] = {
        { .fniv = gen_uaddlp_vec,
          .fni8 = gen_uaddlp_b_i64,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fniv = gen_uaddlp_vec,
          .fni8 = gen_uaddlp_h_i64,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fniv = gen_uaddlp_vec,
          .fni8 = gen_uaddlp_s_i64,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };
    assert(vece <= MO_32);
    tcg_gen_gvec_2(rd_ofs, rn_ofs, opr_sz, max_sz, &g[vece]);
}

static void gen_uadalp_vec(unsigned vece, TCGv_vec d, TCGv_vec n)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);

    gen_uaddlp_vec(vece, t, n);
    tcg_gen_add_vec(vece, d, d, t);
}

static void gen_uadalp_b_i64(TCGv_i64 d, TCGv_i64 n)
{
    TCGv_i64 t = tcg_temp_new_i64();

    gen_uaddlp_b_i64(t, n);
    tcg_gen_vec_add16_i64(d, d, t);
}

static void gen_uadalp_h_i64(TCGv_i64 d, TCGv_i64 n)
{
    TCGv_i64 t = tcg_temp_new_i64();

    gen_uaddlp_h_i64(t, n);
    tcg_gen_vec_add32_i64(d, d, t);
}

static void gen_uadalp_s_i64(TCGv_i64 d, TCGv_i64 n)
{
    TCGv_i64 t = tcg_temp_new_i64();

    gen_uaddlp_s_i64(t, n);
    tcg_gen_add_i64(d, d, t);
}

void gen_gvec_uadalp(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                     uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_add_vec, 0
    };
    static const GVecGen2 g[] = {
        { .fniv = gen_uadalp_vec,
          .fni8 = gen_uadalp_b_i64,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fniv = gen_uadalp_vec,
          .fni8 = gen_uadalp_h_i64,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fniv = gen_uadalp_vec,
          .fni8 = gen_uadalp_s_i64,
          .load_dest = true,
          .opt_opc = vecop_list,
          .vece = MO_64 },
    };
    assert(vece <= MO_32);
    tcg_gen_gvec_2(rd_ofs, rn_ofs, opr_sz, max_sz, &g[vece]);
}

void gen_gvec_fabs(unsigned vece, uint32_t dofs, uint32_t aofs,
                   uint32_t oprsz, uint32_t maxsz)
{
    uint64_t s_bit = 1ull << ((8 << vece) - 1);
    tcg_gen_gvec_andi(vece, dofs, aofs, s_bit - 1, oprsz, maxsz);
}

void gen_gvec_fneg(unsigned vece, uint32_t dofs, uint32_t aofs,
                   uint32_t oprsz, uint32_t maxsz)
{
    uint64_t s_bit = 1ull << ((8 << vece) - 1);
    tcg_gen_gvec_xori(vece, dofs, aofs, s_bit, oprsz, maxsz);
}

void gen_gvec_urecpe(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                     uint32_t opr_sz, uint32_t max_sz)
{
    assert(vece == MO_32);
    tcg_gen_gvec_2_ool(rd_ofs, rn_ofs, opr_sz, max_sz, 0,
                       gen_helper_gvec_urecpe_s);
}

void gen_gvec_ursqrte(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                      uint32_t opr_sz, uint32_t max_sz)
{
    assert(vece == MO_32);
    tcg_gen_gvec_2_ool(rd_ofs, rn_ofs, opr_sz, max_sz, 0,
                       gen_helper_gvec_ursqrte_s);
}
