/*
 *  AArch64 generic vector expansion
 *
 *  Copyright (c) 2013 Alexander Graf <agraf@suse.de>
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


static void gen_rax1_i64(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m)
{
    tcg_gen_rotli_i64(d, m, 1);
    tcg_gen_xor_i64(d, d, n);
}

static void gen_rax1_vec(unsigned vece, TCGv_vec d, TCGv_vec n, TCGv_vec m)
{
    tcg_gen_rotli_vec(vece, d, m, 1);
    tcg_gen_xor_vec(vece, d, d, n);
}

void gen_gvec_rax1(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = { INDEX_op_rotli_vec, 0 };
    static const GVecGen3 op = {
        .fni8 = gen_rax1_i64,
        .fniv = gen_rax1_vec,
        .opt_opc = vecop_list,
        .fno = gen_helper_crypto_rax1,
        .vece = MO_64,
    };
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &op);
}

static void gen_xar8_i64(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();
    uint64_t mask = dup_const(MO_8, 0xff >> sh);

    tcg_gen_xor_i64(t, n, m);
    tcg_gen_shri_i64(d, t, sh);
    tcg_gen_shli_i64(t, t, 8 - sh);
    tcg_gen_andi_i64(d, d, mask);
    tcg_gen_andi_i64(t, t, ~mask);
    tcg_gen_or_i64(d, d, t);
}

static void gen_xar16_i64(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m, int64_t sh)
{
    TCGv_i64 t = tcg_temp_new_i64();
    uint64_t mask = dup_const(MO_16, 0xffff >> sh);

    tcg_gen_xor_i64(t, n, m);
    tcg_gen_shri_i64(d, t, sh);
    tcg_gen_shli_i64(t, t, 16 - sh);
    tcg_gen_andi_i64(d, d, mask);
    tcg_gen_andi_i64(t, t, ~mask);
    tcg_gen_or_i64(d, d, t);
}

static void gen_xar_i32(TCGv_i32 d, TCGv_i32 n, TCGv_i32 m, int32_t sh)
{
    tcg_gen_xor_i32(d, n, m);
    tcg_gen_rotri_i32(d, d, sh);
}

static void gen_xar_i64(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m, int64_t sh)
{
    tcg_gen_xor_i64(d, n, m);
    tcg_gen_rotri_i64(d, d, sh);
}

static void gen_xar_vec(unsigned vece, TCGv_vec d, TCGv_vec n,
                        TCGv_vec m, int64_t sh)
{
    tcg_gen_xor_vec(vece, d, n, m);
    tcg_gen_rotri_vec(vece, d, d, sh);
}

void gen_gvec_xar(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                  uint32_t rm_ofs, int64_t shift,
                  uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop[] = { INDEX_op_rotli_vec, 0 };
    static const GVecGen3i ops[4] = {
        { .fni8 = gen_xar8_i64,
          .fniv = gen_xar_vec,
          .fno = gen_helper_sve2_xar_b,
          .opt_opc = vecop,
          .vece = MO_8 },
        { .fni8 = gen_xar16_i64,
          .fniv = gen_xar_vec,
          .fno = gen_helper_sve2_xar_h,
          .opt_opc = vecop,
          .vece = MO_16 },
        { .fni4 = gen_xar_i32,
          .fniv = gen_xar_vec,
          .fno = gen_helper_sve2_xar_s,
          .opt_opc = vecop,
          .vece = MO_32 },
        { .fni8 = gen_xar_i64,
          .fniv = gen_xar_vec,
          .fno = gen_helper_gvec_xar_d,
          .opt_opc = vecop,
          .vece = MO_64 }
    };
    int esize = 8 << vece;

    /* The SVE2 range is 1 .. esize; the AdvSIMD range is 0 .. esize-1. */
    tcg_debug_assert(shift >= 0);
    tcg_debug_assert(shift <= esize);
    shift &= esize - 1;

    if (shift == 0) {
        /* xar with no rotate devolves to xor. */
        tcg_gen_gvec_xor(vece, rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz);
    } else {
        tcg_gen_gvec_3i(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz,
                        shift, &ops[vece]);
    }
}

static void gen_eor3_i64(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m, TCGv_i64 k)
{
    tcg_gen_xor_i64(d, n, m);
    tcg_gen_xor_i64(d, d, k);
}

static void gen_eor3_vec(unsigned vece, TCGv_vec d, TCGv_vec n,
                         TCGv_vec m, TCGv_vec k)
{
    tcg_gen_xor_vec(vece, d, n, m);
    tcg_gen_xor_vec(vece, d, d, k);
}

void gen_gvec_eor3(unsigned vece, uint32_t d, uint32_t n, uint32_t m,
                   uint32_t a, uint32_t oprsz, uint32_t maxsz)
{
    static const GVecGen4 op = {
        .fni8 = gen_eor3_i64,
        .fniv = gen_eor3_vec,
        .fno = gen_helper_sve2_eor3,
        .vece = MO_64,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    tcg_gen_gvec_4(d, n, m, a, oprsz, maxsz, &op);
}

static void gen_bcax_i64(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m, TCGv_i64 k)
{
    tcg_gen_andc_i64(d, m, k);
    tcg_gen_xor_i64(d, d, n);
}

static void gen_bcax_vec(unsigned vece, TCGv_vec d, TCGv_vec n,
                         TCGv_vec m, TCGv_vec k)
{
    tcg_gen_andc_vec(vece, d, m, k);
    tcg_gen_xor_vec(vece, d, d, n);
}

void gen_gvec_bcax(unsigned vece, uint32_t d, uint32_t n, uint32_t m,
                   uint32_t a, uint32_t oprsz, uint32_t maxsz)
{
    static const GVecGen4 op = {
        .fni8 = gen_bcax_i64,
        .fniv = gen_bcax_vec,
        .fno = gen_helper_sve2_bcax,
        .vece = MO_64,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    tcg_gen_gvec_4(d, n, m, a, oprsz, maxsz, &op);
}

/*
 * Set @res to the correctly saturated result.
 * Set @qc non-zero if saturation occured.
 */
void gen_suqadd_bhs(TCGv_i64 res, TCGv_i64 qc,
                    TCGv_i64 a, TCGv_i64 b, MemOp esz)
{
    TCGv_i64 max = tcg_constant_i64((1ull << ((8 << esz) - 1)) - 1);
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_add_i64(t, a, b);
    tcg_gen_smin_i64(res, t, max);
    tcg_gen_xor_i64(t, t, res);
    tcg_gen_or_i64(qc, qc, t);
}

void gen_suqadd_d(TCGv_i64 res, TCGv_i64 qc, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 max = tcg_constant_i64(INT64_MAX);
    TCGv_i64 t = tcg_temp_new_i64();

    /* Maximum value that can be added to @a without overflow. */
    tcg_gen_sub_i64(t, max, a);

    /* Constrain addend so that the next addition never overflows. */
    tcg_gen_umin_i64(t, t, b);
    tcg_gen_add_i64(res, a, t);

    tcg_gen_xor_i64(t, t, b);
    tcg_gen_or_i64(qc, qc, t);
}

static void gen_suqadd_vec(unsigned vece, TCGv_vec t, TCGv_vec qc,
                           TCGv_vec a, TCGv_vec b)
{
    TCGv_vec max =
        tcg_constant_vec_matching(t, vece, (1ull << ((8 << vece) - 1)) - 1);
    TCGv_vec u = tcg_temp_new_vec_matching(t);

    /* Maximum value that can be added to @a without overflow. */
    tcg_gen_sub_vec(vece, u, max, a);

    /* Constrain addend so that the next addition never overflows. */
    tcg_gen_umin_vec(vece, u, u, b);
    tcg_gen_add_vec(vece, t, u, a);

    /* Compute QC by comparing the adjusted @b. */
    tcg_gen_xor_vec(vece, u, u, b);
    tcg_gen_or_vec(vece, qc, qc, u);
}

void gen_gvec_suqadd_qc(unsigned vece, uint32_t rd_ofs,
                        uint32_t rn_ofs, uint32_t rm_ofs,
                        uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_add_vec, INDEX_op_sub_vec, INDEX_op_umin_vec, 0
    };
    static const GVecGen4 ops[4] = {
        { .fniv = gen_suqadd_vec,
          .fno = gen_helper_gvec_suqadd_b,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_8 },
        { .fniv = gen_suqadd_vec,
          .fno = gen_helper_gvec_suqadd_h,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_16 },
        { .fniv = gen_suqadd_vec,
          .fno = gen_helper_gvec_suqadd_s,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_32 },
        { .fniv = gen_suqadd_vec,
          .fni8 = gen_suqadd_d,
          .fno = gen_helper_gvec_suqadd_d,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_64 },
    };

    tcg_debug_assert(opr_sz <= sizeof_field(CPUARMState, vfp.qc));
    tcg_gen_gvec_4(rd_ofs, offsetof(CPUARMState, vfp.qc),
                   rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}

void gen_usqadd_bhs(TCGv_i64 res, TCGv_i64 qc,
                    TCGv_i64 a, TCGv_i64 b, MemOp esz)
{
    TCGv_i64 max = tcg_constant_i64(MAKE_64BIT_MASK(0, 8 << esz));
    TCGv_i64 zero = tcg_constant_i64(0);
    TCGv_i64 tmp = tcg_temp_new_i64();

    tcg_gen_add_i64(tmp, a, b);
    tcg_gen_smin_i64(res, tmp, max);
    tcg_gen_smax_i64(res, res, zero);
    tcg_gen_xor_i64(tmp, tmp, res);
    tcg_gen_or_i64(qc, qc, tmp);
}

void gen_usqadd_d(TCGv_i64 res, TCGv_i64 qc, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 tmp = tcg_temp_new_i64();
    TCGv_i64 tneg = tcg_temp_new_i64();
    TCGv_i64 tpos = tcg_temp_new_i64();
    TCGv_i64 max = tcg_constant_i64(UINT64_MAX);
    TCGv_i64 zero = tcg_constant_i64(0);

    tcg_gen_add_i64(tmp, a, b);

    /* If @b is positive, saturate if (a + b) < a, aka unsigned overflow. */
    tcg_gen_movcond_i64(TCG_COND_LTU, tpos, tmp, a, max, tmp);

    /* If @b is negative, saturate if a < -b, ie subtraction is negative. */
    tcg_gen_neg_i64(tneg, b);
    tcg_gen_movcond_i64(TCG_COND_LTU, tneg, a, tneg, zero, tmp);

    /* Select correct result from sign of @b. */
    tcg_gen_movcond_i64(TCG_COND_LT, res, b, zero, tneg, tpos);
    tcg_gen_xor_i64(tmp, tmp, res);
    tcg_gen_or_i64(qc, qc, tmp);
}

static void gen_usqadd_vec(unsigned vece, TCGv_vec t, TCGv_vec qc,
                           TCGv_vec a, TCGv_vec b)
{
    TCGv_vec u = tcg_temp_new_vec_matching(t);
    TCGv_vec z = tcg_constant_vec_matching(t, vece, 0);

    /* Compute unsigned saturation of add for +b and sub for -b. */
    tcg_gen_neg_vec(vece, t, b);
    tcg_gen_usadd_vec(vece, u, a, b);
    tcg_gen_ussub_vec(vece, t, a, t);

    /* Select the correct result depending on the sign of b. */
    tcg_gen_cmpsel_vec(TCG_COND_LT, vece, t, b, z, t, u);

    /* Compute QC by comparing against the non-saturated result. */
    tcg_gen_add_vec(vece, u, a, b);
    tcg_gen_xor_vec(vece, u, u, t);
    tcg_gen_or_vec(vece, qc, qc, u);
}

void gen_gvec_usqadd_qc(unsigned vece, uint32_t rd_ofs,
                        uint32_t rn_ofs, uint32_t rm_ofs,
                        uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_neg_vec, INDEX_op_add_vec,
        INDEX_op_usadd_vec, INDEX_op_ussub_vec,
        INDEX_op_cmpsel_vec, 0
    };
    static const GVecGen4 ops[4] = {
        { .fniv = gen_usqadd_vec,
          .fno = gen_helper_gvec_usqadd_b,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_8 },
        { .fniv = gen_usqadd_vec,
          .fno = gen_helper_gvec_usqadd_h,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_16 },
        { .fniv = gen_usqadd_vec,
          .fno = gen_helper_gvec_usqadd_s,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_32 },
        { .fniv = gen_usqadd_vec,
          .fni8 = gen_usqadd_d,
          .fno = gen_helper_gvec_usqadd_d,
          .opt_opc = vecop_list,
          .write_aofs = true,
          .vece = MO_64 },
    };

    tcg_debug_assert(opr_sz <= sizeof_field(CPUARMState, vfp.qc));
    tcg_gen_gvec_4(rd_ofs, offsetof(CPUARMState, vfp.qc),
                   rn_ofs, rm_ofs, opr_sz, max_sz, &ops[vece]);
}
