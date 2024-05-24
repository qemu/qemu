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

