/*
 * AArch64 SVE translation
 *
 * Copyright (c) 2018 Linaro, Ltd
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
#include "exec/exec-all.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "tcg/tcg-gvec-desc.h"
#include "qemu/log.h"
#include "arm_ldst.h"
#include "translate.h"
#include "internals.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/log.h"
#include "translate-a64.h"
#include "fpu/softfloat.h"


typedef void GVecGen2sFn(unsigned, uint32_t, uint32_t,
                         TCGv_i64, uint32_t, uint32_t);

typedef void gen_helper_gvec_flags_3(TCGv_i32, TCGv_ptr, TCGv_ptr,
                                     TCGv_ptr, TCGv_i32);
typedef void gen_helper_gvec_flags_4(TCGv_i32, TCGv_ptr, TCGv_ptr,
                                     TCGv_ptr, TCGv_ptr, TCGv_i32);

typedef void gen_helper_gvec_mem(TCGv_env, TCGv_ptr, TCGv_i64, TCGv_i32);
typedef void gen_helper_gvec_mem_scatter(TCGv_env, TCGv_ptr, TCGv_ptr,
                                         TCGv_ptr, TCGv_i64, TCGv_i32);

/*
 * Helpers for extracting complex instruction fields.
 */

/* See e.g. ASR (immediate, predicated).
 * Returns -1 for unallocated encoding; diagnose later.
 */
static int tszimm_esz(DisasContext *s, int x)
{
    x >>= 3;  /* discard imm3 */
    return 31 - clz32(x);
}

static int tszimm_shr(DisasContext *s, int x)
{
    return (16 << tszimm_esz(s, x)) - x;
}

/* See e.g. LSL (immediate, predicated).  */
static int tszimm_shl(DisasContext *s, int x)
{
    return x - (8 << tszimm_esz(s, x));
}

/* The SH bit is in bit 8.  Extract the low 8 and shift.  */
static inline int expand_imm_sh8s(DisasContext *s, int x)
{
    return (int8_t)x << (x & 0x100 ? 8 : 0);
}

static inline int expand_imm_sh8u(DisasContext *s, int x)
{
    return (uint8_t)x << (x & 0x100 ? 8 : 0);
}

/* Convert a 2-bit memory size (msz) to a 4-bit data type (dtype)
 * with unsigned data.  C.f. SVE Memory Contiguous Load Group.
 */
static inline int msz_dtype(DisasContext *s, int msz)
{
    static const uint8_t dtype[4] = { 0, 5, 10, 15 };
    return dtype[msz];
}

/*
 * Include the generated decoder.
 */

#include "decode-sve.c.inc"

/*
 * Implement all of the translator functions referenced by the decoder.
 */

/* Return the offset info CPUARMState of the predicate vector register Pn.
 * Note for this purpose, FFR is P16.
 */
static inline int pred_full_reg_offset(DisasContext *s, int regno)
{
    return offsetof(CPUARMState, vfp.pregs[regno]);
}

/* Return the byte size of the whole predicate register, VL / 64.  */
static inline int pred_full_reg_size(DisasContext *s)
{
    return s->sve_len >> 3;
}

/* Round up the size of a register to a size allowed by
 * the tcg vector infrastructure.  Any operation which uses this
 * size may assume that the bits above pred_full_reg_size are zero,
 * and must leave them the same way.
 *
 * Note that this is not needed for the vector registers as they
 * are always properly sized for tcg vectors.
 */
static int size_for_gvec(int size)
{
    if (size <= 8) {
        return 8;
    } else {
        return QEMU_ALIGN_UP(size, 16);
    }
}

static int pred_gvec_reg_size(DisasContext *s)
{
    return size_for_gvec(pred_full_reg_size(s));
}

/* Invoke an out-of-line helper on 2 Zregs. */
static void gen_gvec_ool_zz(DisasContext *s, gen_helper_gvec_2 *fn,
                            int rd, int rn, int data)
{
    unsigned vsz = vec_full_reg_size(s);
    tcg_gen_gvec_2_ool(vec_full_reg_offset(s, rd),
                       vec_full_reg_offset(s, rn),
                       vsz, vsz, data, fn);
}

/* Invoke an out-of-line helper on 3 Zregs. */
static void gen_gvec_ool_zzz(DisasContext *s, gen_helper_gvec_3 *fn,
                             int rd, int rn, int rm, int data)
{
    unsigned vsz = vec_full_reg_size(s);
    tcg_gen_gvec_3_ool(vec_full_reg_offset(s, rd),
                       vec_full_reg_offset(s, rn),
                       vec_full_reg_offset(s, rm),
                       vsz, vsz, data, fn);
}

/* Invoke an out-of-line helper on 4 Zregs. */
static void gen_gvec_ool_zzzz(DisasContext *s, gen_helper_gvec_4 *fn,
                              int rd, int rn, int rm, int ra, int data)
{
    unsigned vsz = vec_full_reg_size(s);
    tcg_gen_gvec_4_ool(vec_full_reg_offset(s, rd),
                       vec_full_reg_offset(s, rn),
                       vec_full_reg_offset(s, rm),
                       vec_full_reg_offset(s, ra),
                       vsz, vsz, data, fn);
}

/* Invoke an out-of-line helper on 2 Zregs and a predicate. */
static void gen_gvec_ool_zzp(DisasContext *s, gen_helper_gvec_3 *fn,
                             int rd, int rn, int pg, int data)
{
    unsigned vsz = vec_full_reg_size(s);
    tcg_gen_gvec_3_ool(vec_full_reg_offset(s, rd),
                       vec_full_reg_offset(s, rn),
                       pred_full_reg_offset(s, pg),
                       vsz, vsz, data, fn);
}

/* Invoke an out-of-line helper on 3 Zregs and a predicate. */
static void gen_gvec_ool_zzzp(DisasContext *s, gen_helper_gvec_4 *fn,
                              int rd, int rn, int rm, int pg, int data)
{
    unsigned vsz = vec_full_reg_size(s);
    tcg_gen_gvec_4_ool(vec_full_reg_offset(s, rd),
                       vec_full_reg_offset(s, rn),
                       vec_full_reg_offset(s, rm),
                       pred_full_reg_offset(s, pg),
                       vsz, vsz, data, fn);
}

/* Invoke a vector expander on two Zregs.  */
static void gen_gvec_fn_zz(DisasContext *s, GVecGen2Fn *gvec_fn,
                           int esz, int rd, int rn)
{
    unsigned vsz = vec_full_reg_size(s);
    gvec_fn(esz, vec_full_reg_offset(s, rd),
            vec_full_reg_offset(s, rn), vsz, vsz);
}

/* Invoke a vector expander on three Zregs.  */
static void gen_gvec_fn_zzz(DisasContext *s, GVecGen3Fn *gvec_fn,
                            int esz, int rd, int rn, int rm)
{
    unsigned vsz = vec_full_reg_size(s);
    gvec_fn(esz, vec_full_reg_offset(s, rd),
            vec_full_reg_offset(s, rn),
            vec_full_reg_offset(s, rm), vsz, vsz);
}

/* Invoke a vector expander on four Zregs.  */
static void gen_gvec_fn_zzzz(DisasContext *s, GVecGen4Fn *gvec_fn,
                             int esz, int rd, int rn, int rm, int ra)
{
    unsigned vsz = vec_full_reg_size(s);
    gvec_fn(esz, vec_full_reg_offset(s, rd),
            vec_full_reg_offset(s, rn),
            vec_full_reg_offset(s, rm),
            vec_full_reg_offset(s, ra), vsz, vsz);
}

/* Invoke a vector move on two Zregs.  */
static bool do_mov_z(DisasContext *s, int rd, int rn)
{
    if (sve_access_check(s)) {
        gen_gvec_fn_zz(s, tcg_gen_gvec_mov, MO_8, rd, rn);
    }
    return true;
}

/* Initialize a Zreg with replications of a 64-bit immediate.  */
static void do_dupi_z(DisasContext *s, int rd, uint64_t word)
{
    unsigned vsz = vec_full_reg_size(s);
    tcg_gen_gvec_dup_imm(MO_64, vec_full_reg_offset(s, rd), vsz, vsz, word);
}

/* Invoke a vector expander on three Pregs.  */
static void gen_gvec_fn_ppp(DisasContext *s, GVecGen3Fn *gvec_fn,
                            int rd, int rn, int rm)
{
    unsigned psz = pred_gvec_reg_size(s);
    gvec_fn(MO_64, pred_full_reg_offset(s, rd),
            pred_full_reg_offset(s, rn),
            pred_full_reg_offset(s, rm), psz, psz);
}

/* Invoke a vector move on two Pregs.  */
static bool do_mov_p(DisasContext *s, int rd, int rn)
{
    if (sve_access_check(s)) {
        unsigned psz = pred_gvec_reg_size(s);
        tcg_gen_gvec_mov(MO_8, pred_full_reg_offset(s, rd),
                         pred_full_reg_offset(s, rn), psz, psz);
    }
    return true;
}

/* Set the cpu flags as per a return from an SVE helper.  */
static void do_pred_flags(TCGv_i32 t)
{
    tcg_gen_mov_i32(cpu_NF, t);
    tcg_gen_andi_i32(cpu_ZF, t, 2);
    tcg_gen_andi_i32(cpu_CF, t, 1);
    tcg_gen_movi_i32(cpu_VF, 0);
}

/* Subroutines computing the ARM PredTest psuedofunction.  */
static void do_predtest1(TCGv_i64 d, TCGv_i64 g)
{
    TCGv_i32 t = tcg_temp_new_i32();

    gen_helper_sve_predtest1(t, d, g);
    do_pred_flags(t);
    tcg_temp_free_i32(t);
}

static void do_predtest(DisasContext *s, int dofs, int gofs, int words)
{
    TCGv_ptr dptr = tcg_temp_new_ptr();
    TCGv_ptr gptr = tcg_temp_new_ptr();
    TCGv_i32 t;

    tcg_gen_addi_ptr(dptr, cpu_env, dofs);
    tcg_gen_addi_ptr(gptr, cpu_env, gofs);
    t = tcg_const_i32(words);

    gen_helper_sve_predtest(t, dptr, gptr, t);
    tcg_temp_free_ptr(dptr);
    tcg_temp_free_ptr(gptr);

    do_pred_flags(t);
    tcg_temp_free_i32(t);
}

/* For each element size, the bits within a predicate word that are active.  */
const uint64_t pred_esz_masks[4] = {
    0xffffffffffffffffull, 0x5555555555555555ull,
    0x1111111111111111ull, 0x0101010101010101ull
};

/*
 *** SVE Logical - Unpredicated Group
 */

static bool do_zzz_fn(DisasContext *s, arg_rrr_esz *a, GVecGen3Fn *gvec_fn)
{
    if (sve_access_check(s)) {
        gen_gvec_fn_zzz(s, gvec_fn, a->esz, a->rd, a->rn, a->rm);
    }
    return true;
}

static bool trans_AND_zzz(DisasContext *s, arg_rrr_esz *a)
{
    return do_zzz_fn(s, a, tcg_gen_gvec_and);
}

static bool trans_ORR_zzz(DisasContext *s, arg_rrr_esz *a)
{
    return do_zzz_fn(s, a, tcg_gen_gvec_or);
}

static bool trans_EOR_zzz(DisasContext *s, arg_rrr_esz *a)
{
    return do_zzz_fn(s, a, tcg_gen_gvec_xor);
}

static bool trans_BIC_zzz(DisasContext *s, arg_rrr_esz *a)
{
    return do_zzz_fn(s, a, tcg_gen_gvec_andc);
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
    tcg_temp_free_i64(t);
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
    tcg_temp_free_i64(t);
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

static bool trans_XAR(DisasContext *s, arg_rrri_esz *a)
{
    if (a->esz < 0 || !dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        gen_gvec_xar(a->esz, vec_full_reg_offset(s, a->rd),
                     vec_full_reg_offset(s, a->rn),
                     vec_full_reg_offset(s, a->rm), a->imm, vsz, vsz);
    }
    return true;
}

static bool do_sve2_zzzz_fn(DisasContext *s, arg_rrrr_esz *a, GVecGen4Fn *fn)
{
    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_gvec_fn_zzzz(s, fn, a->esz, a->rd, a->rn, a->rm, a->ra);
    }
    return true;
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

static void gen_eor3(unsigned vece, uint32_t d, uint32_t n, uint32_t m,
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

static bool trans_EOR3(DisasContext *s, arg_rrrr_esz *a)
{
    return do_sve2_zzzz_fn(s, a, gen_eor3);
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

static void gen_bcax(unsigned vece, uint32_t d, uint32_t n, uint32_t m,
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

static bool trans_BCAX(DisasContext *s, arg_rrrr_esz *a)
{
    return do_sve2_zzzz_fn(s, a, gen_bcax);
}

static void gen_bsl(unsigned vece, uint32_t d, uint32_t n, uint32_t m,
                    uint32_t a, uint32_t oprsz, uint32_t maxsz)
{
    /* BSL differs from the generic bitsel in argument ordering. */
    tcg_gen_gvec_bitsel(vece, d, a, n, m, oprsz, maxsz);
}

static bool trans_BSL(DisasContext *s, arg_rrrr_esz *a)
{
    return do_sve2_zzzz_fn(s, a, gen_bsl);
}

static void gen_bsl1n_i64(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m, TCGv_i64 k)
{
    tcg_gen_andc_i64(n, k, n);
    tcg_gen_andc_i64(m, m, k);
    tcg_gen_or_i64(d, n, m);
}

static void gen_bsl1n_vec(unsigned vece, TCGv_vec d, TCGv_vec n,
                          TCGv_vec m, TCGv_vec k)
{
    if (TCG_TARGET_HAS_bitsel_vec) {
        tcg_gen_not_vec(vece, n, n);
        tcg_gen_bitsel_vec(vece, d, k, n, m);
    } else {
        tcg_gen_andc_vec(vece, n, k, n);
        tcg_gen_andc_vec(vece, m, m, k);
        tcg_gen_or_vec(vece, d, n, m);
    }
}

static void gen_bsl1n(unsigned vece, uint32_t d, uint32_t n, uint32_t m,
                      uint32_t a, uint32_t oprsz, uint32_t maxsz)
{
    static const GVecGen4 op = {
        .fni8 = gen_bsl1n_i64,
        .fniv = gen_bsl1n_vec,
        .fno = gen_helper_sve2_bsl1n,
        .vece = MO_64,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    tcg_gen_gvec_4(d, n, m, a, oprsz, maxsz, &op);
}

static bool trans_BSL1N(DisasContext *s, arg_rrrr_esz *a)
{
    return do_sve2_zzzz_fn(s, a, gen_bsl1n);
}

static void gen_bsl2n_i64(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m, TCGv_i64 k)
{
    /*
     * Z[dn] = (n & k) | (~m & ~k)
     *       =         | ~(m | k)
     */
    tcg_gen_and_i64(n, n, k);
    if (TCG_TARGET_HAS_orc_i64) {
        tcg_gen_or_i64(m, m, k);
        tcg_gen_orc_i64(d, n, m);
    } else {
        tcg_gen_nor_i64(m, m, k);
        tcg_gen_or_i64(d, n, m);
    }
}

static void gen_bsl2n_vec(unsigned vece, TCGv_vec d, TCGv_vec n,
                          TCGv_vec m, TCGv_vec k)
{
    if (TCG_TARGET_HAS_bitsel_vec) {
        tcg_gen_not_vec(vece, m, m);
        tcg_gen_bitsel_vec(vece, d, k, n, m);
    } else {
        tcg_gen_and_vec(vece, n, n, k);
        tcg_gen_or_vec(vece, m, m, k);
        tcg_gen_orc_vec(vece, d, n, m);
    }
}

static void gen_bsl2n(unsigned vece, uint32_t d, uint32_t n, uint32_t m,
                      uint32_t a, uint32_t oprsz, uint32_t maxsz)
{
    static const GVecGen4 op = {
        .fni8 = gen_bsl2n_i64,
        .fniv = gen_bsl2n_vec,
        .fno = gen_helper_sve2_bsl2n,
        .vece = MO_64,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    tcg_gen_gvec_4(d, n, m, a, oprsz, maxsz, &op);
}

static bool trans_BSL2N(DisasContext *s, arg_rrrr_esz *a)
{
    return do_sve2_zzzz_fn(s, a, gen_bsl2n);
}

static void gen_nbsl_i64(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m, TCGv_i64 k)
{
    tcg_gen_and_i64(n, n, k);
    tcg_gen_andc_i64(m, m, k);
    tcg_gen_nor_i64(d, n, m);
}

static void gen_nbsl_vec(unsigned vece, TCGv_vec d, TCGv_vec n,
                          TCGv_vec m, TCGv_vec k)
{
    tcg_gen_bitsel_vec(vece, d, k, n, m);
    tcg_gen_not_vec(vece, d, d);
}

static void gen_nbsl(unsigned vece, uint32_t d, uint32_t n, uint32_t m,
                     uint32_t a, uint32_t oprsz, uint32_t maxsz)
{
    static const GVecGen4 op = {
        .fni8 = gen_nbsl_i64,
        .fniv = gen_nbsl_vec,
        .fno = gen_helper_sve2_nbsl,
        .vece = MO_64,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    tcg_gen_gvec_4(d, n, m, a, oprsz, maxsz, &op);
}

static bool trans_NBSL(DisasContext *s, arg_rrrr_esz *a)
{
    return do_sve2_zzzz_fn(s, a, gen_nbsl);
}

/*
 *** SVE Integer Arithmetic - Unpredicated Group
 */

static bool trans_ADD_zzz(DisasContext *s, arg_rrr_esz *a)
{
    return do_zzz_fn(s, a, tcg_gen_gvec_add);
}

static bool trans_SUB_zzz(DisasContext *s, arg_rrr_esz *a)
{
    return do_zzz_fn(s, a, tcg_gen_gvec_sub);
}

static bool trans_SQADD_zzz(DisasContext *s, arg_rrr_esz *a)
{
    return do_zzz_fn(s, a, tcg_gen_gvec_ssadd);
}

static bool trans_SQSUB_zzz(DisasContext *s, arg_rrr_esz *a)
{
    return do_zzz_fn(s, a, tcg_gen_gvec_sssub);
}

static bool trans_UQADD_zzz(DisasContext *s, arg_rrr_esz *a)
{
    return do_zzz_fn(s, a, tcg_gen_gvec_usadd);
}

static bool trans_UQSUB_zzz(DisasContext *s, arg_rrr_esz *a)
{
    return do_zzz_fn(s, a, tcg_gen_gvec_ussub);
}

/*
 *** SVE Integer Arithmetic - Binary Predicated Group
 */

static bool do_zpzz_ool(DisasContext *s, arg_rprr_esz *a, gen_helper_gvec_4 *fn)
{
    if (fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_gvec_ool_zzzp(s, fn, a->rd, a->rn, a->rm, a->pg, 0);
    }
    return true;
}

/* Select active elememnts from Zn and inactive elements from Zm,
 * storing the result in Zd.
 */
static void do_sel_z(DisasContext *s, int rd, int rn, int rm, int pg, int esz)
{
    static gen_helper_gvec_4 * const fns[4] = {
        gen_helper_sve_sel_zpzz_b, gen_helper_sve_sel_zpzz_h,
        gen_helper_sve_sel_zpzz_s, gen_helper_sve_sel_zpzz_d
    };
    gen_gvec_ool_zzzp(s, fns[esz], rd, rn, rm, pg, 0);
}

#define DO_ZPZZ(NAME, name) \
static bool trans_##NAME##_zpzz(DisasContext *s, arg_rprr_esz *a)         \
{                                                                         \
    static gen_helper_gvec_4 * const fns[4] = {                           \
        gen_helper_sve_##name##_zpzz_b, gen_helper_sve_##name##_zpzz_h,   \
        gen_helper_sve_##name##_zpzz_s, gen_helper_sve_##name##_zpzz_d,   \
    };                                                                    \
    return do_zpzz_ool(s, a, fns[a->esz]);                                \
}

DO_ZPZZ(AND, and)
DO_ZPZZ(EOR, eor)
DO_ZPZZ(ORR, orr)
DO_ZPZZ(BIC, bic)

DO_ZPZZ(ADD, add)
DO_ZPZZ(SUB, sub)

DO_ZPZZ(SMAX, smax)
DO_ZPZZ(UMAX, umax)
DO_ZPZZ(SMIN, smin)
DO_ZPZZ(UMIN, umin)
DO_ZPZZ(SABD, sabd)
DO_ZPZZ(UABD, uabd)

DO_ZPZZ(MUL, mul)
DO_ZPZZ(SMULH, smulh)
DO_ZPZZ(UMULH, umulh)

DO_ZPZZ(ASR, asr)
DO_ZPZZ(LSR, lsr)
DO_ZPZZ(LSL, lsl)

static bool trans_SDIV_zpzz(DisasContext *s, arg_rprr_esz *a)
{
    static gen_helper_gvec_4 * const fns[4] = {
        NULL, NULL, gen_helper_sve_sdiv_zpzz_s, gen_helper_sve_sdiv_zpzz_d
    };
    return do_zpzz_ool(s, a, fns[a->esz]);
}

static bool trans_UDIV_zpzz(DisasContext *s, arg_rprr_esz *a)
{
    static gen_helper_gvec_4 * const fns[4] = {
        NULL, NULL, gen_helper_sve_udiv_zpzz_s, gen_helper_sve_udiv_zpzz_d
    };
    return do_zpzz_ool(s, a, fns[a->esz]);
}

static bool trans_SEL_zpzz(DisasContext *s, arg_rprr_esz *a)
{
    if (sve_access_check(s)) {
        do_sel_z(s, a->rd, a->rn, a->rm, a->pg, a->esz);
    }
    return true;
}

#undef DO_ZPZZ

/*
 *** SVE Integer Arithmetic - Unary Predicated Group
 */

static bool do_zpz_ool(DisasContext *s, arg_rpr_esz *a, gen_helper_gvec_3 *fn)
{
    if (fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_gvec_ool_zzp(s, fn, a->rd, a->rn, a->pg, 0);
    }
    return true;
}

#define DO_ZPZ(NAME, name) \
static bool trans_##NAME(DisasContext *s, arg_rpr_esz *a)           \
{                                                                   \
    static gen_helper_gvec_3 * const fns[4] = {                     \
        gen_helper_sve_##name##_b, gen_helper_sve_##name##_h,       \
        gen_helper_sve_##name##_s, gen_helper_sve_##name##_d,       \
    };                                                              \
    return do_zpz_ool(s, a, fns[a->esz]);                           \
}

DO_ZPZ(CLS, cls)
DO_ZPZ(CLZ, clz)
DO_ZPZ(CNT_zpz, cnt_zpz)
DO_ZPZ(CNOT, cnot)
DO_ZPZ(NOT_zpz, not_zpz)
DO_ZPZ(ABS, abs)
DO_ZPZ(NEG, neg)

static bool trans_FABS(DisasContext *s, arg_rpr_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        NULL,
        gen_helper_sve_fabs_h,
        gen_helper_sve_fabs_s,
        gen_helper_sve_fabs_d
    };
    return do_zpz_ool(s, a, fns[a->esz]);
}

static bool trans_FNEG(DisasContext *s, arg_rpr_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        NULL,
        gen_helper_sve_fneg_h,
        gen_helper_sve_fneg_s,
        gen_helper_sve_fneg_d
    };
    return do_zpz_ool(s, a, fns[a->esz]);
}

static bool trans_SXTB(DisasContext *s, arg_rpr_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        NULL,
        gen_helper_sve_sxtb_h,
        gen_helper_sve_sxtb_s,
        gen_helper_sve_sxtb_d
    };
    return do_zpz_ool(s, a, fns[a->esz]);
}

static bool trans_UXTB(DisasContext *s, arg_rpr_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        NULL,
        gen_helper_sve_uxtb_h,
        gen_helper_sve_uxtb_s,
        gen_helper_sve_uxtb_d
    };
    return do_zpz_ool(s, a, fns[a->esz]);
}

static bool trans_SXTH(DisasContext *s, arg_rpr_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        NULL, NULL,
        gen_helper_sve_sxth_s,
        gen_helper_sve_sxth_d
    };
    return do_zpz_ool(s, a, fns[a->esz]);
}

static bool trans_UXTH(DisasContext *s, arg_rpr_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        NULL, NULL,
        gen_helper_sve_uxth_s,
        gen_helper_sve_uxth_d
    };
    return do_zpz_ool(s, a, fns[a->esz]);
}

static bool trans_SXTW(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ool(s, a, a->esz == 3 ? gen_helper_sve_sxtw_d : NULL);
}

static bool trans_UXTW(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ool(s, a, a->esz == 3 ? gen_helper_sve_uxtw_d : NULL);
}

#undef DO_ZPZ

/*
 *** SVE Integer Reduction Group
 */

typedef void gen_helper_gvec_reduc(TCGv_i64, TCGv_ptr, TCGv_ptr, TCGv_i32);
static bool do_vpz_ool(DisasContext *s, arg_rpr_esz *a,
                       gen_helper_gvec_reduc *fn)
{
    unsigned vsz = vec_full_reg_size(s);
    TCGv_ptr t_zn, t_pg;
    TCGv_i32 desc;
    TCGv_i64 temp;

    if (fn == NULL) {
        return false;
    }
    if (!sve_access_check(s)) {
        return true;
    }

    desc = tcg_const_i32(simd_desc(vsz, vsz, 0));
    temp = tcg_temp_new_i64();
    t_zn = tcg_temp_new_ptr();
    t_pg = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(t_zn, cpu_env, vec_full_reg_offset(s, a->rn));
    tcg_gen_addi_ptr(t_pg, cpu_env, pred_full_reg_offset(s, a->pg));
    fn(temp, t_zn, t_pg, desc);
    tcg_temp_free_ptr(t_zn);
    tcg_temp_free_ptr(t_pg);
    tcg_temp_free_i32(desc);

    write_fp_dreg(s, a->rd, temp);
    tcg_temp_free_i64(temp);
    return true;
}

#define DO_VPZ(NAME, name) \
static bool trans_##NAME(DisasContext *s, arg_rpr_esz *a)                \
{                                                                        \
    static gen_helper_gvec_reduc * const fns[4] = {                      \
        gen_helper_sve_##name##_b, gen_helper_sve_##name##_h,            \
        gen_helper_sve_##name##_s, gen_helper_sve_##name##_d,            \
    };                                                                   \
    return do_vpz_ool(s, a, fns[a->esz]);                                \
}

DO_VPZ(ORV, orv)
DO_VPZ(ANDV, andv)
DO_VPZ(EORV, eorv)

DO_VPZ(UADDV, uaddv)
DO_VPZ(SMAXV, smaxv)
DO_VPZ(UMAXV, umaxv)
DO_VPZ(SMINV, sminv)
DO_VPZ(UMINV, uminv)

static bool trans_SADDV(DisasContext *s, arg_rpr_esz *a)
{
    static gen_helper_gvec_reduc * const fns[4] = {
        gen_helper_sve_saddv_b, gen_helper_sve_saddv_h,
        gen_helper_sve_saddv_s, NULL
    };
    return do_vpz_ool(s, a, fns[a->esz]);
}

#undef DO_VPZ

/*
 *** SVE Shift by Immediate - Predicated Group
 */

/*
 * Copy Zn into Zd, storing zeros into inactive elements.
 * If invert, store zeros into the active elements.
 */
static bool do_movz_zpz(DisasContext *s, int rd, int rn, int pg,
                        int esz, bool invert)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve_movz_b, gen_helper_sve_movz_h,
        gen_helper_sve_movz_s, gen_helper_sve_movz_d,
    };

    if (sve_access_check(s)) {
        gen_gvec_ool_zzp(s, fns[esz], rd, rn, pg, invert);
    }
    return true;
}

static bool do_zpzi_ool(DisasContext *s, arg_rpri_esz *a,
                        gen_helper_gvec_3 *fn)
{
    if (sve_access_check(s)) {
        gen_gvec_ool_zzp(s, fn, a->rd, a->rn, a->pg, a->imm);
    }
    return true;
}

static bool trans_ASR_zpzi(DisasContext *s, arg_rpri_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve_asr_zpzi_b, gen_helper_sve_asr_zpzi_h,
        gen_helper_sve_asr_zpzi_s, gen_helper_sve_asr_zpzi_d,
    };
    if (a->esz < 0) {
        /* Invalid tsz encoding -- see tszimm_esz. */
        return false;
    }
    /* Shift by element size is architecturally valid.  For
       arithmetic right-shift, it's the same as by one less. */
    a->imm = MIN(a->imm, (8 << a->esz) - 1);
    return do_zpzi_ool(s, a, fns[a->esz]);
}

static bool trans_LSR_zpzi(DisasContext *s, arg_rpri_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve_lsr_zpzi_b, gen_helper_sve_lsr_zpzi_h,
        gen_helper_sve_lsr_zpzi_s, gen_helper_sve_lsr_zpzi_d,
    };
    if (a->esz < 0) {
        return false;
    }
    /* Shift by element size is architecturally valid.
       For logical shifts, it is a zeroing operation.  */
    if (a->imm >= (8 << a->esz)) {
        return do_movz_zpz(s, a->rd, a->rd, a->pg, a->esz, true);
    } else {
        return do_zpzi_ool(s, a, fns[a->esz]);
    }
}

static bool trans_LSL_zpzi(DisasContext *s, arg_rpri_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve_lsl_zpzi_b, gen_helper_sve_lsl_zpzi_h,
        gen_helper_sve_lsl_zpzi_s, gen_helper_sve_lsl_zpzi_d,
    };
    if (a->esz < 0) {
        return false;
    }
    /* Shift by element size is architecturally valid.
       For logical shifts, it is a zeroing operation.  */
    if (a->imm >= (8 << a->esz)) {
        return do_movz_zpz(s, a->rd, a->rd, a->pg, a->esz, true);
    } else {
        return do_zpzi_ool(s, a, fns[a->esz]);
    }
}

static bool trans_ASRD(DisasContext *s, arg_rpri_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve_asrd_b, gen_helper_sve_asrd_h,
        gen_helper_sve_asrd_s, gen_helper_sve_asrd_d,
    };
    if (a->esz < 0) {
        return false;
    }
    /* Shift by element size is architecturally valid.  For arithmetic
       right shift for division, it is a zeroing operation.  */
    if (a->imm >= (8 << a->esz)) {
        return do_movz_zpz(s, a->rd, a->rd, a->pg, a->esz, true);
    } else {
        return do_zpzi_ool(s, a, fns[a->esz]);
    }
}

static bool trans_SQSHL_zpzi(DisasContext *s, arg_rpri_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve2_sqshl_zpzi_b, gen_helper_sve2_sqshl_zpzi_h,
        gen_helper_sve2_sqshl_zpzi_s, gen_helper_sve2_sqshl_zpzi_d,
    };
    if (a->esz < 0 || !dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    return do_zpzi_ool(s, a, fns[a->esz]);
}

static bool trans_UQSHL_zpzi(DisasContext *s, arg_rpri_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve2_uqshl_zpzi_b, gen_helper_sve2_uqshl_zpzi_h,
        gen_helper_sve2_uqshl_zpzi_s, gen_helper_sve2_uqshl_zpzi_d,
    };
    if (a->esz < 0 || !dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    return do_zpzi_ool(s, a, fns[a->esz]);
}

static bool trans_SRSHR(DisasContext *s, arg_rpri_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve2_srshr_b, gen_helper_sve2_srshr_h,
        gen_helper_sve2_srshr_s, gen_helper_sve2_srshr_d,
    };
    if (a->esz < 0 || !dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    return do_zpzi_ool(s, a, fns[a->esz]);
}

static bool trans_URSHR(DisasContext *s, arg_rpri_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve2_urshr_b, gen_helper_sve2_urshr_h,
        gen_helper_sve2_urshr_s, gen_helper_sve2_urshr_d,
    };
    if (a->esz < 0 || !dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    return do_zpzi_ool(s, a, fns[a->esz]);
}

static bool trans_SQSHLU(DisasContext *s, arg_rpri_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve2_sqshlu_b, gen_helper_sve2_sqshlu_h,
        gen_helper_sve2_sqshlu_s, gen_helper_sve2_sqshlu_d,
    };
    if (a->esz < 0 || !dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    return do_zpzi_ool(s, a, fns[a->esz]);
}

/*
 *** SVE Bitwise Shift - Predicated Group
 */

#define DO_ZPZW(NAME, name) \
static bool trans_##NAME##_zpzw(DisasContext *s, arg_rprr_esz *a)         \
{                                                                         \
    static gen_helper_gvec_4 * const fns[3] = {                           \
        gen_helper_sve_##name##_zpzw_b, gen_helper_sve_##name##_zpzw_h,   \
        gen_helper_sve_##name##_zpzw_s,                                   \
    };                                                                    \
    if (a->esz < 0 || a->esz >= 3) {                                      \
        return false;                                                     \
    }                                                                     \
    return do_zpzz_ool(s, a, fns[a->esz]);                                \
}

DO_ZPZW(ASR, asr)
DO_ZPZW(LSR, lsr)
DO_ZPZW(LSL, lsl)

#undef DO_ZPZW

/*
 *** SVE Bitwise Shift - Unpredicated Group
 */

static bool do_shift_imm(DisasContext *s, arg_rri_esz *a, bool asr,
                         void (*gvec_fn)(unsigned, uint32_t, uint32_t,
                                         int64_t, uint32_t, uint32_t))
{
    if (a->esz < 0) {
        /* Invalid tsz encoding -- see tszimm_esz. */
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        /* Shift by element size is architecturally valid.  For
           arithmetic right-shift, it's the same as by one less.
           Otherwise it is a zeroing operation.  */
        if (a->imm >= 8 << a->esz) {
            if (asr) {
                a->imm = (8 << a->esz) - 1;
            } else {
                do_dupi_z(s, a->rd, 0);
                return true;
            }
        }
        gvec_fn(a->esz, vec_full_reg_offset(s, a->rd),
                vec_full_reg_offset(s, a->rn), a->imm, vsz, vsz);
    }
    return true;
}

static bool trans_ASR_zzi(DisasContext *s, arg_rri_esz *a)
{
    return do_shift_imm(s, a, true, tcg_gen_gvec_sari);
}

static bool trans_LSR_zzi(DisasContext *s, arg_rri_esz *a)
{
    return do_shift_imm(s, a, false, tcg_gen_gvec_shri);
}

static bool trans_LSL_zzi(DisasContext *s, arg_rri_esz *a)
{
    return do_shift_imm(s, a, false, tcg_gen_gvec_shli);
}

static bool do_zzw_ool(DisasContext *s, arg_rrr_esz *a, gen_helper_gvec_3 *fn)
{
    if (fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_gvec_ool_zzz(s, fn, a->rd, a->rn, a->rm, 0);
    }
    return true;
}

#define DO_ZZW(NAME, name) \
static bool trans_##NAME##_zzw(DisasContext *s, arg_rrr_esz *a)           \
{                                                                         \
    static gen_helper_gvec_3 * const fns[4] = {                           \
        gen_helper_sve_##name##_zzw_b, gen_helper_sve_##name##_zzw_h,     \
        gen_helper_sve_##name##_zzw_s, NULL                               \
    };                                                                    \
    return do_zzw_ool(s, a, fns[a->esz]);                                 \
}

DO_ZZW(ASR, asr)
DO_ZZW(LSR, lsr)
DO_ZZW(LSL, lsl)

#undef DO_ZZW

/*
 *** SVE Integer Multiply-Add Group
 */

static bool do_zpzzz_ool(DisasContext *s, arg_rprrr_esz *a,
                         gen_helper_gvec_5 *fn)
{
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_5_ool(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->ra),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           pred_full_reg_offset(s, a->pg),
                           vsz, vsz, 0, fn);
    }
    return true;
}

#define DO_ZPZZZ(NAME, name) \
static bool trans_##NAME(DisasContext *s, arg_rprrr_esz *a)          \
{                                                                    \
    static gen_helper_gvec_5 * const fns[4] = {                      \
        gen_helper_sve_##name##_b, gen_helper_sve_##name##_h,        \
        gen_helper_sve_##name##_s, gen_helper_sve_##name##_d,        \
    };                                                               \
    return do_zpzzz_ool(s, a, fns[a->esz]);                          \
}

DO_ZPZZZ(MLA, mla)
DO_ZPZZZ(MLS, mls)

#undef DO_ZPZZZ

/*
 *** SVE Index Generation Group
 */

static void do_index(DisasContext *s, int esz, int rd,
                     TCGv_i64 start, TCGv_i64 incr)
{
    unsigned vsz = vec_full_reg_size(s);
    TCGv_i32 desc = tcg_const_i32(simd_desc(vsz, vsz, 0));
    TCGv_ptr t_zd = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(t_zd, cpu_env, vec_full_reg_offset(s, rd));
    if (esz == 3) {
        gen_helper_sve_index_d(t_zd, start, incr, desc);
    } else {
        typedef void index_fn(TCGv_ptr, TCGv_i32, TCGv_i32, TCGv_i32);
        static index_fn * const fns[3] = {
            gen_helper_sve_index_b,
            gen_helper_sve_index_h,
            gen_helper_sve_index_s,
        };
        TCGv_i32 s32 = tcg_temp_new_i32();
        TCGv_i32 i32 = tcg_temp_new_i32();

        tcg_gen_extrl_i64_i32(s32, start);
        tcg_gen_extrl_i64_i32(i32, incr);
        fns[esz](t_zd, s32, i32, desc);

        tcg_temp_free_i32(s32);
        tcg_temp_free_i32(i32);
    }
    tcg_temp_free_ptr(t_zd);
    tcg_temp_free_i32(desc);
}

static bool trans_INDEX_ii(DisasContext *s, arg_INDEX_ii *a)
{
    if (sve_access_check(s)) {
        TCGv_i64 start = tcg_const_i64(a->imm1);
        TCGv_i64 incr = tcg_const_i64(a->imm2);
        do_index(s, a->esz, a->rd, start, incr);
        tcg_temp_free_i64(start);
        tcg_temp_free_i64(incr);
    }
    return true;
}

static bool trans_INDEX_ir(DisasContext *s, arg_INDEX_ir *a)
{
    if (sve_access_check(s)) {
        TCGv_i64 start = tcg_const_i64(a->imm);
        TCGv_i64 incr = cpu_reg(s, a->rm);
        do_index(s, a->esz, a->rd, start, incr);
        tcg_temp_free_i64(start);
    }
    return true;
}

static bool trans_INDEX_ri(DisasContext *s, arg_INDEX_ri *a)
{
    if (sve_access_check(s)) {
        TCGv_i64 start = cpu_reg(s, a->rn);
        TCGv_i64 incr = tcg_const_i64(a->imm);
        do_index(s, a->esz, a->rd, start, incr);
        tcg_temp_free_i64(incr);
    }
    return true;
}

static bool trans_INDEX_rr(DisasContext *s, arg_INDEX_rr *a)
{
    if (sve_access_check(s)) {
        TCGv_i64 start = cpu_reg(s, a->rn);
        TCGv_i64 incr = cpu_reg(s, a->rm);
        do_index(s, a->esz, a->rd, start, incr);
    }
    return true;
}

/*
 *** SVE Stack Allocation Group
 */

static bool trans_ADDVL(DisasContext *s, arg_ADDVL *a)
{
    if (sve_access_check(s)) {
        TCGv_i64 rd = cpu_reg_sp(s, a->rd);
        TCGv_i64 rn = cpu_reg_sp(s, a->rn);
        tcg_gen_addi_i64(rd, rn, a->imm * vec_full_reg_size(s));
    }
    return true;
}

static bool trans_ADDPL(DisasContext *s, arg_ADDPL *a)
{
    if (sve_access_check(s)) {
        TCGv_i64 rd = cpu_reg_sp(s, a->rd);
        TCGv_i64 rn = cpu_reg_sp(s, a->rn);
        tcg_gen_addi_i64(rd, rn, a->imm * pred_full_reg_size(s));
    }
    return true;
}

static bool trans_RDVL(DisasContext *s, arg_RDVL *a)
{
    if (sve_access_check(s)) {
        TCGv_i64 reg = cpu_reg(s, a->rd);
        tcg_gen_movi_i64(reg, a->imm * vec_full_reg_size(s));
    }
    return true;
}

/*
 *** SVE Compute Vector Address Group
 */

static bool do_adr(DisasContext *s, arg_rrri *a, gen_helper_gvec_3 *fn)
{
    if (sve_access_check(s)) {
        gen_gvec_ool_zzz(s, fn, a->rd, a->rn, a->rm, a->imm);
    }
    return true;
}

static bool trans_ADR_p32(DisasContext *s, arg_rrri *a)
{
    return do_adr(s, a, gen_helper_sve_adr_p32);
}

static bool trans_ADR_p64(DisasContext *s, arg_rrri *a)
{
    return do_adr(s, a, gen_helper_sve_adr_p64);
}

static bool trans_ADR_s32(DisasContext *s, arg_rrri *a)
{
    return do_adr(s, a, gen_helper_sve_adr_s32);
}

static bool trans_ADR_u32(DisasContext *s, arg_rrri *a)
{
    return do_adr(s, a, gen_helper_sve_adr_u32);
}

/*
 *** SVE Integer Misc - Unpredicated Group
 */

static bool trans_FEXPA(DisasContext *s, arg_rr_esz *a)
{
    static gen_helper_gvec_2 * const fns[4] = {
        NULL,
        gen_helper_sve_fexpa_h,
        gen_helper_sve_fexpa_s,
        gen_helper_sve_fexpa_d,
    };
    if (a->esz == 0) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_gvec_ool_zz(s, fns[a->esz], a->rd, a->rn, 0);
    }
    return true;
}

static bool trans_FTSSEL(DisasContext *s, arg_rrr_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        NULL,
        gen_helper_sve_ftssel_h,
        gen_helper_sve_ftssel_s,
        gen_helper_sve_ftssel_d,
    };
    if (a->esz == 0) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_gvec_ool_zzz(s, fns[a->esz], a->rd, a->rn, a->rm, 0);
    }
    return true;
}

/*
 *** SVE Predicate Logical Operations Group
 */

static bool do_pppp_flags(DisasContext *s, arg_rprr_s *a,
                          const GVecGen4 *gvec_op)
{
    if (!sve_access_check(s)) {
        return true;
    }

    unsigned psz = pred_gvec_reg_size(s);
    int dofs = pred_full_reg_offset(s, a->rd);
    int nofs = pred_full_reg_offset(s, a->rn);
    int mofs = pred_full_reg_offset(s, a->rm);
    int gofs = pred_full_reg_offset(s, a->pg);

    if (!a->s) {
        tcg_gen_gvec_4(dofs, nofs, mofs, gofs, psz, psz, gvec_op);
        return true;
    }

    if (psz == 8) {
        /* Do the operation and the flags generation in temps.  */
        TCGv_i64 pd = tcg_temp_new_i64();
        TCGv_i64 pn = tcg_temp_new_i64();
        TCGv_i64 pm = tcg_temp_new_i64();
        TCGv_i64 pg = tcg_temp_new_i64();

        tcg_gen_ld_i64(pn, cpu_env, nofs);
        tcg_gen_ld_i64(pm, cpu_env, mofs);
        tcg_gen_ld_i64(pg, cpu_env, gofs);

        gvec_op->fni8(pd, pn, pm, pg);
        tcg_gen_st_i64(pd, cpu_env, dofs);

        do_predtest1(pd, pg);

        tcg_temp_free_i64(pd);
        tcg_temp_free_i64(pn);
        tcg_temp_free_i64(pm);
        tcg_temp_free_i64(pg);
    } else {
        /* The operation and flags generation is large.  The computation
         * of the flags depends on the original contents of the guarding
         * predicate.  If the destination overwrites the guarding predicate,
         * then the easiest way to get this right is to save a copy.
          */
        int tofs = gofs;
        if (a->rd == a->pg) {
            tofs = offsetof(CPUARMState, vfp.preg_tmp);
            tcg_gen_gvec_mov(0, tofs, gofs, psz, psz);
        }

        tcg_gen_gvec_4(dofs, nofs, mofs, gofs, psz, psz, gvec_op);
        do_predtest(s, dofs, tofs, psz / 8);
    }
    return true;
}

static void gen_and_pg_i64(TCGv_i64 pd, TCGv_i64 pn, TCGv_i64 pm, TCGv_i64 pg)
{
    tcg_gen_and_i64(pd, pn, pm);
    tcg_gen_and_i64(pd, pd, pg);
}

static void gen_and_pg_vec(unsigned vece, TCGv_vec pd, TCGv_vec pn,
                           TCGv_vec pm, TCGv_vec pg)
{
    tcg_gen_and_vec(vece, pd, pn, pm);
    tcg_gen_and_vec(vece, pd, pd, pg);
}

static bool trans_AND_pppp(DisasContext *s, arg_rprr_s *a)
{
    static const GVecGen4 op = {
        .fni8 = gen_and_pg_i64,
        .fniv = gen_and_pg_vec,
        .fno = gen_helper_sve_and_pppp,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };

    if (!a->s) {
        if (!sve_access_check(s)) {
            return true;
        }
        if (a->rn == a->rm) {
            if (a->pg == a->rn) {
                do_mov_p(s, a->rd, a->rn);
            } else {
                gen_gvec_fn_ppp(s, tcg_gen_gvec_and, a->rd, a->rn, a->pg);
            }
            return true;
        } else if (a->pg == a->rn || a->pg == a->rm) {
            gen_gvec_fn_ppp(s, tcg_gen_gvec_and, a->rd, a->rn, a->rm);
            return true;
        }
    }
    return do_pppp_flags(s, a, &op);
}

static void gen_bic_pg_i64(TCGv_i64 pd, TCGv_i64 pn, TCGv_i64 pm, TCGv_i64 pg)
{
    tcg_gen_andc_i64(pd, pn, pm);
    tcg_gen_and_i64(pd, pd, pg);
}

static void gen_bic_pg_vec(unsigned vece, TCGv_vec pd, TCGv_vec pn,
                           TCGv_vec pm, TCGv_vec pg)
{
    tcg_gen_andc_vec(vece, pd, pn, pm);
    tcg_gen_and_vec(vece, pd, pd, pg);
}

static bool trans_BIC_pppp(DisasContext *s, arg_rprr_s *a)
{
    static const GVecGen4 op = {
        .fni8 = gen_bic_pg_i64,
        .fniv = gen_bic_pg_vec,
        .fno = gen_helper_sve_bic_pppp,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };

    if (!a->s && a->pg == a->rn) {
        if (sve_access_check(s)) {
            gen_gvec_fn_ppp(s, tcg_gen_gvec_andc, a->rd, a->rn, a->rm);
        }
        return true;
    }
    return do_pppp_flags(s, a, &op);
}

static void gen_eor_pg_i64(TCGv_i64 pd, TCGv_i64 pn, TCGv_i64 pm, TCGv_i64 pg)
{
    tcg_gen_xor_i64(pd, pn, pm);
    tcg_gen_and_i64(pd, pd, pg);
}

static void gen_eor_pg_vec(unsigned vece, TCGv_vec pd, TCGv_vec pn,
                           TCGv_vec pm, TCGv_vec pg)
{
    tcg_gen_xor_vec(vece, pd, pn, pm);
    tcg_gen_and_vec(vece, pd, pd, pg);
}

static bool trans_EOR_pppp(DisasContext *s, arg_rprr_s *a)
{
    static const GVecGen4 op = {
        .fni8 = gen_eor_pg_i64,
        .fniv = gen_eor_pg_vec,
        .fno = gen_helper_sve_eor_pppp,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    return do_pppp_flags(s, a, &op);
}

static bool trans_SEL_pppp(DisasContext *s, arg_rprr_s *a)
{
    if (a->s) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned psz = pred_gvec_reg_size(s);
        tcg_gen_gvec_bitsel(MO_8, pred_full_reg_offset(s, a->rd),
                            pred_full_reg_offset(s, a->pg),
                            pred_full_reg_offset(s, a->rn),
                            pred_full_reg_offset(s, a->rm), psz, psz);
    }
    return true;
}

static void gen_orr_pg_i64(TCGv_i64 pd, TCGv_i64 pn, TCGv_i64 pm, TCGv_i64 pg)
{
    tcg_gen_or_i64(pd, pn, pm);
    tcg_gen_and_i64(pd, pd, pg);
}

static void gen_orr_pg_vec(unsigned vece, TCGv_vec pd, TCGv_vec pn,
                           TCGv_vec pm, TCGv_vec pg)
{
    tcg_gen_or_vec(vece, pd, pn, pm);
    tcg_gen_and_vec(vece, pd, pd, pg);
}

static bool trans_ORR_pppp(DisasContext *s, arg_rprr_s *a)
{
    static const GVecGen4 op = {
        .fni8 = gen_orr_pg_i64,
        .fniv = gen_orr_pg_vec,
        .fno = gen_helper_sve_orr_pppp,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };

    if (!a->s && a->pg == a->rn && a->rn == a->rm) {
        return do_mov_p(s, a->rd, a->rn);
    }
    return do_pppp_flags(s, a, &op);
}

static void gen_orn_pg_i64(TCGv_i64 pd, TCGv_i64 pn, TCGv_i64 pm, TCGv_i64 pg)
{
    tcg_gen_orc_i64(pd, pn, pm);
    tcg_gen_and_i64(pd, pd, pg);
}

static void gen_orn_pg_vec(unsigned vece, TCGv_vec pd, TCGv_vec pn,
                           TCGv_vec pm, TCGv_vec pg)
{
    tcg_gen_orc_vec(vece, pd, pn, pm);
    tcg_gen_and_vec(vece, pd, pd, pg);
}

static bool trans_ORN_pppp(DisasContext *s, arg_rprr_s *a)
{
    static const GVecGen4 op = {
        .fni8 = gen_orn_pg_i64,
        .fniv = gen_orn_pg_vec,
        .fno = gen_helper_sve_orn_pppp,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    return do_pppp_flags(s, a, &op);
}

static void gen_nor_pg_i64(TCGv_i64 pd, TCGv_i64 pn, TCGv_i64 pm, TCGv_i64 pg)
{
    tcg_gen_or_i64(pd, pn, pm);
    tcg_gen_andc_i64(pd, pg, pd);
}

static void gen_nor_pg_vec(unsigned vece, TCGv_vec pd, TCGv_vec pn,
                           TCGv_vec pm, TCGv_vec pg)
{
    tcg_gen_or_vec(vece, pd, pn, pm);
    tcg_gen_andc_vec(vece, pd, pg, pd);
}

static bool trans_NOR_pppp(DisasContext *s, arg_rprr_s *a)
{
    static const GVecGen4 op = {
        .fni8 = gen_nor_pg_i64,
        .fniv = gen_nor_pg_vec,
        .fno = gen_helper_sve_nor_pppp,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    return do_pppp_flags(s, a, &op);
}

static void gen_nand_pg_i64(TCGv_i64 pd, TCGv_i64 pn, TCGv_i64 pm, TCGv_i64 pg)
{
    tcg_gen_and_i64(pd, pn, pm);
    tcg_gen_andc_i64(pd, pg, pd);
}

static void gen_nand_pg_vec(unsigned vece, TCGv_vec pd, TCGv_vec pn,
                           TCGv_vec pm, TCGv_vec pg)
{
    tcg_gen_and_vec(vece, pd, pn, pm);
    tcg_gen_andc_vec(vece, pd, pg, pd);
}

static bool trans_NAND_pppp(DisasContext *s, arg_rprr_s *a)
{
    static const GVecGen4 op = {
        .fni8 = gen_nand_pg_i64,
        .fniv = gen_nand_pg_vec,
        .fno = gen_helper_sve_nand_pppp,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    return do_pppp_flags(s, a, &op);
}

/*
 *** SVE Predicate Misc Group
 */

static bool trans_PTEST(DisasContext *s, arg_PTEST *a)
{
    if (sve_access_check(s)) {
        int nofs = pred_full_reg_offset(s, a->rn);
        int gofs = pred_full_reg_offset(s, a->pg);
        int words = DIV_ROUND_UP(pred_full_reg_size(s), 8);

        if (words == 1) {
            TCGv_i64 pn = tcg_temp_new_i64();
            TCGv_i64 pg = tcg_temp_new_i64();

            tcg_gen_ld_i64(pn, cpu_env, nofs);
            tcg_gen_ld_i64(pg, cpu_env, gofs);
            do_predtest1(pn, pg);

            tcg_temp_free_i64(pn);
            tcg_temp_free_i64(pg);
        } else {
            do_predtest(s, nofs, gofs, words);
        }
    }
    return true;
}

/* See the ARM pseudocode DecodePredCount.  */
static unsigned decode_pred_count(unsigned fullsz, int pattern, int esz)
{
    unsigned elements = fullsz >> esz;
    unsigned bound;

    switch (pattern) {
    case 0x0: /* POW2 */
        return pow2floor(elements);
    case 0x1: /* VL1 */
    case 0x2: /* VL2 */
    case 0x3: /* VL3 */
    case 0x4: /* VL4 */
    case 0x5: /* VL5 */
    case 0x6: /* VL6 */
    case 0x7: /* VL7 */
    case 0x8: /* VL8 */
        bound = pattern;
        break;
    case 0x9: /* VL16 */
    case 0xa: /* VL32 */
    case 0xb: /* VL64 */
    case 0xc: /* VL128 */
    case 0xd: /* VL256 */
        bound = 16 << (pattern - 9);
        break;
    case 0x1d: /* MUL4 */
        return elements - elements % 4;
    case 0x1e: /* MUL3 */
        return elements - elements % 3;
    case 0x1f: /* ALL */
        return elements;
    default:   /* #uimm5 */
        return 0;
    }
    return elements >= bound ? bound : 0;
}

/* This handles all of the predicate initialization instructions,
 * PTRUE, PFALSE, SETFFR.  For PFALSE, we will have set PAT == 32
 * so that decode_pred_count returns 0.  For SETFFR, we will have
 * set RD == 16 == FFR.
 */
static bool do_predset(DisasContext *s, int esz, int rd, int pat, bool setflag)
{
    if (!sve_access_check(s)) {
        return true;
    }

    unsigned fullsz = vec_full_reg_size(s);
    unsigned ofs = pred_full_reg_offset(s, rd);
    unsigned numelem, setsz, i;
    uint64_t word, lastword;
    TCGv_i64 t;

    numelem = decode_pred_count(fullsz, pat, esz);

    /* Determine what we must store into each bit, and how many.  */
    if (numelem == 0) {
        lastword = word = 0;
        setsz = fullsz;
    } else {
        setsz = numelem << esz;
        lastword = word = pred_esz_masks[esz];
        if (setsz % 64) {
            lastword &= MAKE_64BIT_MASK(0, setsz % 64);
        }
    }

    t = tcg_temp_new_i64();
    if (fullsz <= 64) {
        tcg_gen_movi_i64(t, lastword);
        tcg_gen_st_i64(t, cpu_env, ofs);
        goto done;
    }

    if (word == lastword) {
        unsigned maxsz = size_for_gvec(fullsz / 8);
        unsigned oprsz = size_for_gvec(setsz / 8);

        if (oprsz * 8 == setsz) {
            tcg_gen_gvec_dup_imm(MO_64, ofs, oprsz, maxsz, word);
            goto done;
        }
    }

    setsz /= 8;
    fullsz /= 8;

    tcg_gen_movi_i64(t, word);
    for (i = 0; i < QEMU_ALIGN_DOWN(setsz, 8); i += 8) {
        tcg_gen_st_i64(t, cpu_env, ofs + i);
    }
    if (lastword != word) {
        tcg_gen_movi_i64(t, lastword);
        tcg_gen_st_i64(t, cpu_env, ofs + i);
        i += 8;
    }
    if (i < fullsz) {
        tcg_gen_movi_i64(t, 0);
        for (; i < fullsz; i += 8) {
            tcg_gen_st_i64(t, cpu_env, ofs + i);
        }
    }

 done:
    tcg_temp_free_i64(t);

    /* PTRUES */
    if (setflag) {
        tcg_gen_movi_i32(cpu_NF, -(word != 0));
        tcg_gen_movi_i32(cpu_CF, word == 0);
        tcg_gen_movi_i32(cpu_VF, 0);
        tcg_gen_mov_i32(cpu_ZF, cpu_NF);
    }
    return true;
}

static bool trans_PTRUE(DisasContext *s, arg_PTRUE *a)
{
    return do_predset(s, a->esz, a->rd, a->pat, a->s);
}

static bool trans_SETFFR(DisasContext *s, arg_SETFFR *a)
{
    /* Note pat == 31 is #all, to set all elements.  */
    return do_predset(s, 0, FFR_PRED_NUM, 31, false);
}

static bool trans_PFALSE(DisasContext *s, arg_PFALSE *a)
{
    /* Note pat == 32 is #unimp, to set no elements.  */
    return do_predset(s, 0, a->rd, 32, false);
}

static bool trans_RDFFR_p(DisasContext *s, arg_RDFFR_p *a)
{
    /* The path through do_pppp_flags is complicated enough to want to avoid
     * duplication.  Frob the arguments into the form of a predicated AND.
     */
    arg_rprr_s alt_a = {
        .rd = a->rd, .pg = a->pg, .s = a->s,
        .rn = FFR_PRED_NUM, .rm = FFR_PRED_NUM,
    };
    return trans_AND_pppp(s, &alt_a);
}

static bool trans_RDFFR(DisasContext *s, arg_RDFFR *a)
{
    return do_mov_p(s, a->rd, FFR_PRED_NUM);
}

static bool trans_WRFFR(DisasContext *s, arg_WRFFR *a)
{
    return do_mov_p(s, FFR_PRED_NUM, a->rn);
}

static bool do_pfirst_pnext(DisasContext *s, arg_rr_esz *a,
                            void (*gen_fn)(TCGv_i32, TCGv_ptr,
                                           TCGv_ptr, TCGv_i32))
{
    if (!sve_access_check(s)) {
        return true;
    }

    TCGv_ptr t_pd = tcg_temp_new_ptr();
    TCGv_ptr t_pg = tcg_temp_new_ptr();
    TCGv_i32 t;
    unsigned desc = 0;

    desc = FIELD_DP32(desc, PREDDESC, OPRSZ, pred_full_reg_size(s));
    desc = FIELD_DP32(desc, PREDDESC, ESZ, a->esz);

    tcg_gen_addi_ptr(t_pd, cpu_env, pred_full_reg_offset(s, a->rd));
    tcg_gen_addi_ptr(t_pg, cpu_env, pred_full_reg_offset(s, a->rn));
    t = tcg_const_i32(desc);

    gen_fn(t, t_pd, t_pg, t);
    tcg_temp_free_ptr(t_pd);
    tcg_temp_free_ptr(t_pg);

    do_pred_flags(t);
    tcg_temp_free_i32(t);
    return true;
}

static bool trans_PFIRST(DisasContext *s, arg_rr_esz *a)
{
    return do_pfirst_pnext(s, a, gen_helper_sve_pfirst);
}

static bool trans_PNEXT(DisasContext *s, arg_rr_esz *a)
{
    return do_pfirst_pnext(s, a, gen_helper_sve_pnext);
}

/*
 *** SVE Element Count Group
 */

/* Perform an inline saturating addition of a 32-bit value within
 * a 64-bit register.  The second operand is known to be positive,
 * which halves the comparisions we must perform to bound the result.
 */
static void do_sat_addsub_32(TCGv_i64 reg, TCGv_i64 val, bool u, bool d)
{
    int64_t ibound;
    TCGv_i64 bound;
    TCGCond cond;

    /* Use normal 64-bit arithmetic to detect 32-bit overflow.  */
    if (u) {
        tcg_gen_ext32u_i64(reg, reg);
    } else {
        tcg_gen_ext32s_i64(reg, reg);
    }
    if (d) {
        tcg_gen_sub_i64(reg, reg, val);
        ibound = (u ? 0 : INT32_MIN);
        cond = TCG_COND_LT;
    } else {
        tcg_gen_add_i64(reg, reg, val);
        ibound = (u ? UINT32_MAX : INT32_MAX);
        cond = TCG_COND_GT;
    }
    bound = tcg_const_i64(ibound);
    tcg_gen_movcond_i64(cond, reg, reg, bound, bound, reg);
    tcg_temp_free_i64(bound);
}

/* Similarly with 64-bit values.  */
static void do_sat_addsub_64(TCGv_i64 reg, TCGv_i64 val, bool u, bool d)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t2;

    if (u) {
        if (d) {
            tcg_gen_sub_i64(t0, reg, val);
            t2 = tcg_constant_i64(0);
            tcg_gen_movcond_i64(TCG_COND_LTU, reg, reg, val, t2, t0);
        } else {
            tcg_gen_add_i64(t0, reg, val);
            t2 = tcg_constant_i64(-1);
            tcg_gen_movcond_i64(TCG_COND_LTU, reg, t0, reg, t2, t0);
        }
    } else {
        TCGv_i64 t1 = tcg_temp_new_i64();
        if (d) {
            /* Detect signed overflow for subtraction.  */
            tcg_gen_xor_i64(t0, reg, val);
            tcg_gen_sub_i64(t1, reg, val);
            tcg_gen_xor_i64(reg, reg, t1);
            tcg_gen_and_i64(t0, t0, reg);

            /* Bound the result.  */
            tcg_gen_movi_i64(reg, INT64_MIN);
            t2 = tcg_constant_i64(0);
            tcg_gen_movcond_i64(TCG_COND_LT, reg, t0, t2, reg, t1);
        } else {
            /* Detect signed overflow for addition.  */
            tcg_gen_xor_i64(t0, reg, val);
            tcg_gen_add_i64(reg, reg, val);
            tcg_gen_xor_i64(t1, reg, val);
            tcg_gen_andc_i64(t0, t1, t0);

            /* Bound the result.  */
            tcg_gen_movi_i64(t1, INT64_MAX);
            t2 = tcg_constant_i64(0);
            tcg_gen_movcond_i64(TCG_COND_LT, reg, t0, t2, t1, reg);
        }
        tcg_temp_free_i64(t1);
    }
    tcg_temp_free_i64(t0);
}

/* Similarly with a vector and a scalar operand.  */
static void do_sat_addsub_vec(DisasContext *s, int esz, int rd, int rn,
                              TCGv_i64 val, bool u, bool d)
{
    unsigned vsz = vec_full_reg_size(s);
    TCGv_ptr dptr, nptr;
    TCGv_i32 t32, desc;
    TCGv_i64 t64;

    dptr = tcg_temp_new_ptr();
    nptr = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(dptr, cpu_env, vec_full_reg_offset(s, rd));
    tcg_gen_addi_ptr(nptr, cpu_env, vec_full_reg_offset(s, rn));
    desc = tcg_const_i32(simd_desc(vsz, vsz, 0));

    switch (esz) {
    case MO_8:
        t32 = tcg_temp_new_i32();
        tcg_gen_extrl_i64_i32(t32, val);
        if (d) {
            tcg_gen_neg_i32(t32, t32);
        }
        if (u) {
            gen_helper_sve_uqaddi_b(dptr, nptr, t32, desc);
        } else {
            gen_helper_sve_sqaddi_b(dptr, nptr, t32, desc);
        }
        tcg_temp_free_i32(t32);
        break;

    case MO_16:
        t32 = tcg_temp_new_i32();
        tcg_gen_extrl_i64_i32(t32, val);
        if (d) {
            tcg_gen_neg_i32(t32, t32);
        }
        if (u) {
            gen_helper_sve_uqaddi_h(dptr, nptr, t32, desc);
        } else {
            gen_helper_sve_sqaddi_h(dptr, nptr, t32, desc);
        }
        tcg_temp_free_i32(t32);
        break;

    case MO_32:
        t64 = tcg_temp_new_i64();
        if (d) {
            tcg_gen_neg_i64(t64, val);
        } else {
            tcg_gen_mov_i64(t64, val);
        }
        if (u) {
            gen_helper_sve_uqaddi_s(dptr, nptr, t64, desc);
        } else {
            gen_helper_sve_sqaddi_s(dptr, nptr, t64, desc);
        }
        tcg_temp_free_i64(t64);
        break;

    case MO_64:
        if (u) {
            if (d) {
                gen_helper_sve_uqsubi_d(dptr, nptr, val, desc);
            } else {
                gen_helper_sve_uqaddi_d(dptr, nptr, val, desc);
            }
        } else if (d) {
            t64 = tcg_temp_new_i64();
            tcg_gen_neg_i64(t64, val);
            gen_helper_sve_sqaddi_d(dptr, nptr, t64, desc);
            tcg_temp_free_i64(t64);
        } else {
            gen_helper_sve_sqaddi_d(dptr, nptr, val, desc);
        }
        break;

    default:
        g_assert_not_reached();
    }

    tcg_temp_free_ptr(dptr);
    tcg_temp_free_ptr(nptr);
    tcg_temp_free_i32(desc);
}

static bool trans_CNT_r(DisasContext *s, arg_CNT_r *a)
{
    if (sve_access_check(s)) {
        unsigned fullsz = vec_full_reg_size(s);
        unsigned numelem = decode_pred_count(fullsz, a->pat, a->esz);
        tcg_gen_movi_i64(cpu_reg(s, a->rd), numelem * a->imm);
    }
    return true;
}

static bool trans_INCDEC_r(DisasContext *s, arg_incdec_cnt *a)
{
    if (sve_access_check(s)) {
        unsigned fullsz = vec_full_reg_size(s);
        unsigned numelem = decode_pred_count(fullsz, a->pat, a->esz);
        int inc = numelem * a->imm * (a->d ? -1 : 1);
        TCGv_i64 reg = cpu_reg(s, a->rd);

        tcg_gen_addi_i64(reg, reg, inc);
    }
    return true;
}

static bool trans_SINCDEC_r_32(DisasContext *s, arg_incdec_cnt *a)
{
    if (!sve_access_check(s)) {
        return true;
    }

    unsigned fullsz = vec_full_reg_size(s);
    unsigned numelem = decode_pred_count(fullsz, a->pat, a->esz);
    int inc = numelem * a->imm;
    TCGv_i64 reg = cpu_reg(s, a->rd);

    /* Use normal 64-bit arithmetic to detect 32-bit overflow.  */
    if (inc == 0) {
        if (a->u) {
            tcg_gen_ext32u_i64(reg, reg);
        } else {
            tcg_gen_ext32s_i64(reg, reg);
        }
    } else {
        TCGv_i64 t = tcg_const_i64(inc);
        do_sat_addsub_32(reg, t, a->u, a->d);
        tcg_temp_free_i64(t);
    }
    return true;
}

static bool trans_SINCDEC_r_64(DisasContext *s, arg_incdec_cnt *a)
{
    if (!sve_access_check(s)) {
        return true;
    }

    unsigned fullsz = vec_full_reg_size(s);
    unsigned numelem = decode_pred_count(fullsz, a->pat, a->esz);
    int inc = numelem * a->imm;
    TCGv_i64 reg = cpu_reg(s, a->rd);

    if (inc != 0) {
        TCGv_i64 t = tcg_const_i64(inc);
        do_sat_addsub_64(reg, t, a->u, a->d);
        tcg_temp_free_i64(t);
    }
    return true;
}

static bool trans_INCDEC_v(DisasContext *s, arg_incdec2_cnt *a)
{
    if (a->esz == 0) {
        return false;
    }

    unsigned fullsz = vec_full_reg_size(s);
    unsigned numelem = decode_pred_count(fullsz, a->pat, a->esz);
    int inc = numelem * a->imm;

    if (inc != 0) {
        if (sve_access_check(s)) {
            TCGv_i64 t = tcg_const_i64(a->d ? -inc : inc);
            tcg_gen_gvec_adds(a->esz, vec_full_reg_offset(s, a->rd),
                              vec_full_reg_offset(s, a->rn),
                              t, fullsz, fullsz);
            tcg_temp_free_i64(t);
        }
    } else {
        do_mov_z(s, a->rd, a->rn);
    }
    return true;
}

static bool trans_SINCDEC_v(DisasContext *s, arg_incdec2_cnt *a)
{
    if (a->esz == 0) {
        return false;
    }

    unsigned fullsz = vec_full_reg_size(s);
    unsigned numelem = decode_pred_count(fullsz, a->pat, a->esz);
    int inc = numelem * a->imm;

    if (inc != 0) {
        if (sve_access_check(s)) {
            TCGv_i64 t = tcg_const_i64(inc);
            do_sat_addsub_vec(s, a->esz, a->rd, a->rn, t, a->u, a->d);
            tcg_temp_free_i64(t);
        }
    } else {
        do_mov_z(s, a->rd, a->rn);
    }
    return true;
}

/*
 *** SVE Bitwise Immediate Group
 */

static bool do_zz_dbm(DisasContext *s, arg_rr_dbm *a, GVecGen2iFn *gvec_fn)
{
    uint64_t imm;
    if (!logic_imm_decode_wmask(&imm, extract32(a->dbm, 12, 1),
                                extract32(a->dbm, 0, 6),
                                extract32(a->dbm, 6, 6))) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        gvec_fn(MO_64, vec_full_reg_offset(s, a->rd),
                vec_full_reg_offset(s, a->rn), imm, vsz, vsz);
    }
    return true;
}

static bool trans_AND_zzi(DisasContext *s, arg_rr_dbm *a)
{
    return do_zz_dbm(s, a, tcg_gen_gvec_andi);
}

static bool trans_ORR_zzi(DisasContext *s, arg_rr_dbm *a)
{
    return do_zz_dbm(s, a, tcg_gen_gvec_ori);
}

static bool trans_EOR_zzi(DisasContext *s, arg_rr_dbm *a)
{
    return do_zz_dbm(s, a, tcg_gen_gvec_xori);
}

static bool trans_DUPM(DisasContext *s, arg_DUPM *a)
{
    uint64_t imm;
    if (!logic_imm_decode_wmask(&imm, extract32(a->dbm, 12, 1),
                                extract32(a->dbm, 0, 6),
                                extract32(a->dbm, 6, 6))) {
        return false;
    }
    if (sve_access_check(s)) {
        do_dupi_z(s, a->rd, imm);
    }
    return true;
}

/*
 *** SVE Integer Wide Immediate - Predicated Group
 */

/* Implement all merging copies.  This is used for CPY (immediate),
 * FCPY, CPY (scalar), CPY (SIMD&FP scalar).
 */
static void do_cpy_m(DisasContext *s, int esz, int rd, int rn, int pg,
                     TCGv_i64 val)
{
    typedef void gen_cpy(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_i64, TCGv_i32);
    static gen_cpy * const fns[4] = {
        gen_helper_sve_cpy_m_b, gen_helper_sve_cpy_m_h,
        gen_helper_sve_cpy_m_s, gen_helper_sve_cpy_m_d,
    };
    unsigned vsz = vec_full_reg_size(s);
    TCGv_i32 desc = tcg_const_i32(simd_desc(vsz, vsz, 0));
    TCGv_ptr t_zd = tcg_temp_new_ptr();
    TCGv_ptr t_zn = tcg_temp_new_ptr();
    TCGv_ptr t_pg = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(t_zd, cpu_env, vec_full_reg_offset(s, rd));
    tcg_gen_addi_ptr(t_zn, cpu_env, vec_full_reg_offset(s, rn));
    tcg_gen_addi_ptr(t_pg, cpu_env, pred_full_reg_offset(s, pg));

    fns[esz](t_zd, t_zn, t_pg, val, desc);

    tcg_temp_free_ptr(t_zd);
    tcg_temp_free_ptr(t_zn);
    tcg_temp_free_ptr(t_pg);
    tcg_temp_free_i32(desc);
}

static bool trans_FCPY(DisasContext *s, arg_FCPY *a)
{
    if (a->esz == 0) {
        return false;
    }
    if (sve_access_check(s)) {
        /* Decode the VFP immediate.  */
        uint64_t imm = vfp_expand_imm(a->esz, a->imm);
        TCGv_i64 t_imm = tcg_const_i64(imm);
        do_cpy_m(s, a->esz, a->rd, a->rn, a->pg, t_imm);
        tcg_temp_free_i64(t_imm);
    }
    return true;
}

static bool trans_CPY_m_i(DisasContext *s, arg_rpri_esz *a)
{
    if (a->esz == 0 && extract32(s->insn, 13, 1)) {
        return false;
    }
    if (sve_access_check(s)) {
        TCGv_i64 t_imm = tcg_const_i64(a->imm);
        do_cpy_m(s, a->esz, a->rd, a->rn, a->pg, t_imm);
        tcg_temp_free_i64(t_imm);
    }
    return true;
}

static bool trans_CPY_z_i(DisasContext *s, arg_CPY_z_i *a)
{
    static gen_helper_gvec_2i * const fns[4] = {
        gen_helper_sve_cpy_z_b, gen_helper_sve_cpy_z_h,
        gen_helper_sve_cpy_z_s, gen_helper_sve_cpy_z_d,
    };

    if (a->esz == 0 && extract32(s->insn, 13, 1)) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_i64 t_imm = tcg_const_i64(a->imm);
        tcg_gen_gvec_2i_ool(vec_full_reg_offset(s, a->rd),
                            pred_full_reg_offset(s, a->pg),
                            t_imm, vsz, vsz, 0, fns[a->esz]);
        tcg_temp_free_i64(t_imm);
    }
    return true;
}

/*
 *** SVE Permute Extract Group
 */

static bool do_EXT(DisasContext *s, int rd, int rn, int rm, int imm)
{
    if (!sve_access_check(s)) {
        return true;
    }

    unsigned vsz = vec_full_reg_size(s);
    unsigned n_ofs = imm >= vsz ? 0 : imm;
    unsigned n_siz = vsz - n_ofs;
    unsigned d = vec_full_reg_offset(s, rd);
    unsigned n = vec_full_reg_offset(s, rn);
    unsigned m = vec_full_reg_offset(s, rm);

    /* Use host vector move insns if we have appropriate sizes
     * and no unfortunate overlap.
     */
    if (m != d
        && n_ofs == size_for_gvec(n_ofs)
        && n_siz == size_for_gvec(n_siz)
        && (d != n || n_siz <= n_ofs)) {
        tcg_gen_gvec_mov(0, d, n + n_ofs, n_siz, n_siz);
        if (n_ofs != 0) {
            tcg_gen_gvec_mov(0, d + n_siz, m, n_ofs, n_ofs);
        }
    } else {
        tcg_gen_gvec_3_ool(d, n, m, vsz, vsz, n_ofs, gen_helper_sve_ext);
    }
    return true;
}

static bool trans_EXT(DisasContext *s, arg_EXT *a)
{
    return do_EXT(s, a->rd, a->rn, a->rm, a->imm);
}

static bool trans_EXT_sve2(DisasContext *s, arg_rri *a)
{
    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    return do_EXT(s, a->rd, a->rn, (a->rn + 1) % 32, a->imm);
}

/*
 *** SVE Permute - Unpredicated Group
 */

static bool trans_DUP_s(DisasContext *s, arg_DUP_s *a)
{
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_dup_i64(a->esz, vec_full_reg_offset(s, a->rd),
                             vsz, vsz, cpu_reg_sp(s, a->rn));
    }
    return true;
}

static bool trans_DUP_x(DisasContext *s, arg_DUP_x *a)
{
    if ((a->imm & 0x1f) == 0) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        unsigned dofs = vec_full_reg_offset(s, a->rd);
        unsigned esz, index;

        esz = ctz32(a->imm);
        index = a->imm >> (esz + 1);

        if ((index << esz) < vsz) {
            unsigned nofs = vec_reg_offset(s, a->rn, index, esz);
            tcg_gen_gvec_dup_mem(esz, dofs, nofs, vsz, vsz);
        } else {
            /*
             * While dup_mem handles 128-bit elements, dup_imm does not.
             * Thankfully element size doesn't matter for splatting zero.
             */
            tcg_gen_gvec_dup_imm(MO_64, dofs, vsz, vsz, 0);
        }
    }
    return true;
}

static void do_insr_i64(DisasContext *s, arg_rrr_esz *a, TCGv_i64 val)
{
    typedef void gen_insr(TCGv_ptr, TCGv_ptr, TCGv_i64, TCGv_i32);
    static gen_insr * const fns[4] = {
        gen_helper_sve_insr_b, gen_helper_sve_insr_h,
        gen_helper_sve_insr_s, gen_helper_sve_insr_d,
    };
    unsigned vsz = vec_full_reg_size(s);
    TCGv_i32 desc = tcg_const_i32(simd_desc(vsz, vsz, 0));
    TCGv_ptr t_zd = tcg_temp_new_ptr();
    TCGv_ptr t_zn = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(t_zd, cpu_env, vec_full_reg_offset(s, a->rd));
    tcg_gen_addi_ptr(t_zn, cpu_env, vec_full_reg_offset(s, a->rn));

    fns[a->esz](t_zd, t_zn, val, desc);

    tcg_temp_free_ptr(t_zd);
    tcg_temp_free_ptr(t_zn);
    tcg_temp_free_i32(desc);
}

static bool trans_INSR_f(DisasContext *s, arg_rrr_esz *a)
{
    if (sve_access_check(s)) {
        TCGv_i64 t = tcg_temp_new_i64();
        tcg_gen_ld_i64(t, cpu_env, vec_reg_offset(s, a->rm, 0, MO_64));
        do_insr_i64(s, a, t);
        tcg_temp_free_i64(t);
    }
    return true;
}

static bool trans_INSR_r(DisasContext *s, arg_rrr_esz *a)
{
    if (sve_access_check(s)) {
        do_insr_i64(s, a, cpu_reg(s, a->rm));
    }
    return true;
}

static bool trans_REV_v(DisasContext *s, arg_rr_esz *a)
{
    static gen_helper_gvec_2 * const fns[4] = {
        gen_helper_sve_rev_b, gen_helper_sve_rev_h,
        gen_helper_sve_rev_s, gen_helper_sve_rev_d
    };

    if (sve_access_check(s)) {
        gen_gvec_ool_zz(s, fns[a->esz], a->rd, a->rn, 0);
    }
    return true;
}

static bool trans_TBL(DisasContext *s, arg_rrr_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve_tbl_b, gen_helper_sve_tbl_h,
        gen_helper_sve_tbl_s, gen_helper_sve_tbl_d
    };

    if (sve_access_check(s)) {
        gen_gvec_ool_zzz(s, fns[a->esz], a->rd, a->rn, a->rm, 0);
    }
    return true;
}

static bool trans_TBL_sve2(DisasContext *s, arg_rrr_esz *a)
{
    static gen_helper_gvec_4 * const fns[4] = {
        gen_helper_sve2_tbl_b, gen_helper_sve2_tbl_h,
        gen_helper_sve2_tbl_s, gen_helper_sve2_tbl_d
    };

    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_gvec_ool_zzzz(s, fns[a->esz], a->rd, a->rn,
                          (a->rn + 1) % 32, a->rm, 0);
    }
    return true;
}

static bool trans_TBX(DisasContext *s, arg_rrr_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve2_tbx_b, gen_helper_sve2_tbx_h,
        gen_helper_sve2_tbx_s, gen_helper_sve2_tbx_d
    };

    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_gvec_ool_zzz(s, fns[a->esz], a->rd, a->rn, a->rm, 0);
    }
    return true;
}

static bool trans_UNPK(DisasContext *s, arg_UNPK *a)
{
    static gen_helper_gvec_2 * const fns[4][2] = {
        { NULL, NULL },
        { gen_helper_sve_sunpk_h, gen_helper_sve_uunpk_h },
        { gen_helper_sve_sunpk_s, gen_helper_sve_uunpk_s },
        { gen_helper_sve_sunpk_d, gen_helper_sve_uunpk_d },
    };

    if (a->esz == 0) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_2_ool(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn)
                           + (a->h ? vsz / 2 : 0),
                           vsz, vsz, 0, fns[a->esz][a->u]);
    }
    return true;
}

/*
 *** SVE Permute - Predicates Group
 */

static bool do_perm_pred3(DisasContext *s, arg_rrr_esz *a, bool high_odd,
                          gen_helper_gvec_3 *fn)
{
    if (!sve_access_check(s)) {
        return true;
    }

    unsigned vsz = pred_full_reg_size(s);

    TCGv_ptr t_d = tcg_temp_new_ptr();
    TCGv_ptr t_n = tcg_temp_new_ptr();
    TCGv_ptr t_m = tcg_temp_new_ptr();
    TCGv_i32 t_desc;
    uint32_t desc = 0;

    desc = FIELD_DP32(desc, PREDDESC, OPRSZ, vsz);
    desc = FIELD_DP32(desc, PREDDESC, ESZ, a->esz);
    desc = FIELD_DP32(desc, PREDDESC, DATA, high_odd);

    tcg_gen_addi_ptr(t_d, cpu_env, pred_full_reg_offset(s, a->rd));
    tcg_gen_addi_ptr(t_n, cpu_env, pred_full_reg_offset(s, a->rn));
    tcg_gen_addi_ptr(t_m, cpu_env, pred_full_reg_offset(s, a->rm));
    t_desc = tcg_const_i32(desc);

    fn(t_d, t_n, t_m, t_desc);

    tcg_temp_free_ptr(t_d);
    tcg_temp_free_ptr(t_n);
    tcg_temp_free_ptr(t_m);
    tcg_temp_free_i32(t_desc);
    return true;
}

static bool do_perm_pred2(DisasContext *s, arg_rr_esz *a, bool high_odd,
                          gen_helper_gvec_2 *fn)
{
    if (!sve_access_check(s)) {
        return true;
    }

    unsigned vsz = pred_full_reg_size(s);
    TCGv_ptr t_d = tcg_temp_new_ptr();
    TCGv_ptr t_n = tcg_temp_new_ptr();
    TCGv_i32 t_desc;
    uint32_t desc = 0;

    tcg_gen_addi_ptr(t_d, cpu_env, pred_full_reg_offset(s, a->rd));
    tcg_gen_addi_ptr(t_n, cpu_env, pred_full_reg_offset(s, a->rn));

    desc = FIELD_DP32(desc, PREDDESC, OPRSZ, vsz);
    desc = FIELD_DP32(desc, PREDDESC, ESZ, a->esz);
    desc = FIELD_DP32(desc, PREDDESC, DATA, high_odd);
    t_desc = tcg_const_i32(desc);

    fn(t_d, t_n, t_desc);

    tcg_temp_free_i32(t_desc);
    tcg_temp_free_ptr(t_d);
    tcg_temp_free_ptr(t_n);
    return true;
}

static bool trans_ZIP1_p(DisasContext *s, arg_rrr_esz *a)
{
    return do_perm_pred3(s, a, 0, gen_helper_sve_zip_p);
}

static bool trans_ZIP2_p(DisasContext *s, arg_rrr_esz *a)
{
    return do_perm_pred3(s, a, 1, gen_helper_sve_zip_p);
}

static bool trans_UZP1_p(DisasContext *s, arg_rrr_esz *a)
{
    return do_perm_pred3(s, a, 0, gen_helper_sve_uzp_p);
}

static bool trans_UZP2_p(DisasContext *s, arg_rrr_esz *a)
{
    return do_perm_pred3(s, a, 1, gen_helper_sve_uzp_p);
}

static bool trans_TRN1_p(DisasContext *s, arg_rrr_esz *a)
{
    return do_perm_pred3(s, a, 0, gen_helper_sve_trn_p);
}

static bool trans_TRN2_p(DisasContext *s, arg_rrr_esz *a)
{
    return do_perm_pred3(s, a, 1, gen_helper_sve_trn_p);
}

static bool trans_REV_p(DisasContext *s, arg_rr_esz *a)
{
    return do_perm_pred2(s, a, 0, gen_helper_sve_rev_p);
}

static bool trans_PUNPKLO(DisasContext *s, arg_PUNPKLO *a)
{
    return do_perm_pred2(s, a, 0, gen_helper_sve_punpk_p);
}

static bool trans_PUNPKHI(DisasContext *s, arg_PUNPKHI *a)
{
    return do_perm_pred2(s, a, 1, gen_helper_sve_punpk_p);
}

/*
 *** SVE Permute - Interleaving Group
 */

static bool do_zip(DisasContext *s, arg_rrr_esz *a, bool high)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve_zip_b, gen_helper_sve_zip_h,
        gen_helper_sve_zip_s, gen_helper_sve_zip_d,
    };

    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        unsigned high_ofs = high ? vsz / 2 : 0;
        tcg_gen_gvec_3_ool(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn) + high_ofs,
                           vec_full_reg_offset(s, a->rm) + high_ofs,
                           vsz, vsz, 0, fns[a->esz]);
    }
    return true;
}

static bool do_zzz_data_ool(DisasContext *s, arg_rrr_esz *a, int data,
                            gen_helper_gvec_3 *fn)
{
    if (sve_access_check(s)) {
        gen_gvec_ool_zzz(s, fn, a->rd, a->rn, a->rm, data);
    }
    return true;
}

static bool trans_ZIP1_z(DisasContext *s, arg_rrr_esz *a)
{
    return do_zip(s, a, false);
}

static bool trans_ZIP2_z(DisasContext *s, arg_rrr_esz *a)
{
    return do_zip(s, a, true);
}

static bool do_zip_q(DisasContext *s, arg_rrr_esz *a, bool high)
{
    if (!dc_isar_feature(aa64_sve_f64mm, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        unsigned high_ofs = high ? QEMU_ALIGN_DOWN(vsz, 32) / 2 : 0;
        tcg_gen_gvec_3_ool(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn) + high_ofs,
                           vec_full_reg_offset(s, a->rm) + high_ofs,
                           vsz, vsz, 0, gen_helper_sve2_zip_q);
    }
    return true;
}

static bool trans_ZIP1_q(DisasContext *s, arg_rrr_esz *a)
{
    return do_zip_q(s, a, false);
}

static bool trans_ZIP2_q(DisasContext *s, arg_rrr_esz *a)
{
    return do_zip_q(s, a, true);
}

static gen_helper_gvec_3 * const uzp_fns[4] = {
    gen_helper_sve_uzp_b, gen_helper_sve_uzp_h,
    gen_helper_sve_uzp_s, gen_helper_sve_uzp_d,
};

static bool trans_UZP1_z(DisasContext *s, arg_rrr_esz *a)
{
    return do_zzz_data_ool(s, a, 0, uzp_fns[a->esz]);
}

static bool trans_UZP2_z(DisasContext *s, arg_rrr_esz *a)
{
    return do_zzz_data_ool(s, a, 1 << a->esz, uzp_fns[a->esz]);
}

static bool trans_UZP1_q(DisasContext *s, arg_rrr_esz *a)
{
    if (!dc_isar_feature(aa64_sve_f64mm, s)) {
        return false;
    }
    return do_zzz_data_ool(s, a, 0, gen_helper_sve2_uzp_q);
}

static bool trans_UZP2_q(DisasContext *s, arg_rrr_esz *a)
{
    if (!dc_isar_feature(aa64_sve_f64mm, s)) {
        return false;
    }
    return do_zzz_data_ool(s, a, 16, gen_helper_sve2_uzp_q);
}

static gen_helper_gvec_3 * const trn_fns[4] = {
    gen_helper_sve_trn_b, gen_helper_sve_trn_h,
    gen_helper_sve_trn_s, gen_helper_sve_trn_d,
};

static bool trans_TRN1_z(DisasContext *s, arg_rrr_esz *a)
{
    return do_zzz_data_ool(s, a, 0, trn_fns[a->esz]);
}

static bool trans_TRN2_z(DisasContext *s, arg_rrr_esz *a)
{
    return do_zzz_data_ool(s, a, 1 << a->esz, trn_fns[a->esz]);
}

static bool trans_TRN1_q(DisasContext *s, arg_rrr_esz *a)
{
    if (!dc_isar_feature(aa64_sve_f64mm, s)) {
        return false;
    }
    return do_zzz_data_ool(s, a, 0, gen_helper_sve2_trn_q);
}

static bool trans_TRN2_q(DisasContext *s, arg_rrr_esz *a)
{
    if (!dc_isar_feature(aa64_sve_f64mm, s)) {
        return false;
    }
    return do_zzz_data_ool(s, a, 16, gen_helper_sve2_trn_q);
}

/*
 *** SVE Permute Vector - Predicated Group
 */

static bool trans_COMPACT(DisasContext *s, arg_rpr_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        NULL, NULL, gen_helper_sve_compact_s, gen_helper_sve_compact_d
    };
    return do_zpz_ool(s, a, fns[a->esz]);
}

/* Call the helper that computes the ARM LastActiveElement pseudocode
 * function, scaled by the element size.  This includes the not found
 * indication; e.g. not found for esz=3 is -8.
 */
static void find_last_active(DisasContext *s, TCGv_i32 ret, int esz, int pg)
{
    /* Predicate sizes may be smaller and cannot use simd_desc.  We cannot
     * round up, as we do elsewhere, because we need the exact size.
     */
    TCGv_ptr t_p = tcg_temp_new_ptr();
    TCGv_i32 t_desc;
    unsigned desc = 0;

    desc = FIELD_DP32(desc, PREDDESC, OPRSZ, pred_full_reg_size(s));
    desc = FIELD_DP32(desc, PREDDESC, ESZ, esz);

    tcg_gen_addi_ptr(t_p, cpu_env, pred_full_reg_offset(s, pg));
    t_desc = tcg_const_i32(desc);

    gen_helper_sve_last_active_element(ret, t_p, t_desc);

    tcg_temp_free_i32(t_desc);
    tcg_temp_free_ptr(t_p);
}

/* Increment LAST to the offset of the next element in the vector,
 * wrapping around to 0.
 */
static void incr_last_active(DisasContext *s, TCGv_i32 last, int esz)
{
    unsigned vsz = vec_full_reg_size(s);

    tcg_gen_addi_i32(last, last, 1 << esz);
    if (is_power_of_2(vsz)) {
        tcg_gen_andi_i32(last, last, vsz - 1);
    } else {
        TCGv_i32 max = tcg_const_i32(vsz);
        TCGv_i32 zero = tcg_const_i32(0);
        tcg_gen_movcond_i32(TCG_COND_GEU, last, last, max, zero, last);
        tcg_temp_free_i32(max);
        tcg_temp_free_i32(zero);
    }
}

/* If LAST < 0, set LAST to the offset of the last element in the vector.  */
static void wrap_last_active(DisasContext *s, TCGv_i32 last, int esz)
{
    unsigned vsz = vec_full_reg_size(s);

    if (is_power_of_2(vsz)) {
        tcg_gen_andi_i32(last, last, vsz - 1);
    } else {
        TCGv_i32 max = tcg_const_i32(vsz - (1 << esz));
        TCGv_i32 zero = tcg_const_i32(0);
        tcg_gen_movcond_i32(TCG_COND_LT, last, last, zero, max, last);
        tcg_temp_free_i32(max);
        tcg_temp_free_i32(zero);
    }
}

/* Load an unsigned element of ESZ from BASE+OFS.  */
static TCGv_i64 load_esz(TCGv_ptr base, int ofs, int esz)
{
    TCGv_i64 r = tcg_temp_new_i64();

    switch (esz) {
    case 0:
        tcg_gen_ld8u_i64(r, base, ofs);
        break;
    case 1:
        tcg_gen_ld16u_i64(r, base, ofs);
        break;
    case 2:
        tcg_gen_ld32u_i64(r, base, ofs);
        break;
    case 3:
        tcg_gen_ld_i64(r, base, ofs);
        break;
    default:
        g_assert_not_reached();
    }
    return r;
}

/* Load an unsigned element of ESZ from RM[LAST].  */
static TCGv_i64 load_last_active(DisasContext *s, TCGv_i32 last,
                                 int rm, int esz)
{
    TCGv_ptr p = tcg_temp_new_ptr();
    TCGv_i64 r;

    /* Convert offset into vector into offset into ENV.
     * The final adjustment for the vector register base
     * is added via constant offset to the load.
     */
#ifdef HOST_WORDS_BIGENDIAN
    /* Adjust for element ordering.  See vec_reg_offset.  */
    if (esz < 3) {
        tcg_gen_xori_i32(last, last, 8 - (1 << esz));
    }
#endif
    tcg_gen_ext_i32_ptr(p, last);
    tcg_gen_add_ptr(p, p, cpu_env);

    r = load_esz(p, vec_full_reg_offset(s, rm), esz);
    tcg_temp_free_ptr(p);

    return r;
}

/* Compute CLAST for a Zreg.  */
static bool do_clast_vector(DisasContext *s, arg_rprr_esz *a, bool before)
{
    TCGv_i32 last;
    TCGLabel *over;
    TCGv_i64 ele;
    unsigned vsz, esz = a->esz;

    if (!sve_access_check(s)) {
        return true;
    }

    last = tcg_temp_local_new_i32();
    over = gen_new_label();

    find_last_active(s, last, esz, a->pg);

    /* There is of course no movcond for a 2048-bit vector,
     * so we must branch over the actual store.
     */
    tcg_gen_brcondi_i32(TCG_COND_LT, last, 0, over);

    if (!before) {
        incr_last_active(s, last, esz);
    }

    ele = load_last_active(s, last, a->rm, esz);
    tcg_temp_free_i32(last);

    vsz = vec_full_reg_size(s);
    tcg_gen_gvec_dup_i64(esz, vec_full_reg_offset(s, a->rd), vsz, vsz, ele);
    tcg_temp_free_i64(ele);

    /* If this insn used MOVPRFX, we may need a second move.  */
    if (a->rd != a->rn) {
        TCGLabel *done = gen_new_label();
        tcg_gen_br(done);

        gen_set_label(over);
        do_mov_z(s, a->rd, a->rn);

        gen_set_label(done);
    } else {
        gen_set_label(over);
    }
    return true;
}

static bool trans_CLASTA_z(DisasContext *s, arg_rprr_esz *a)
{
    return do_clast_vector(s, a, false);
}

static bool trans_CLASTB_z(DisasContext *s, arg_rprr_esz *a)
{
    return do_clast_vector(s, a, true);
}

/* Compute CLAST for a scalar.  */
static void do_clast_scalar(DisasContext *s, int esz, int pg, int rm,
                            bool before, TCGv_i64 reg_val)
{
    TCGv_i32 last = tcg_temp_new_i32();
    TCGv_i64 ele, cmp, zero;

    find_last_active(s, last, esz, pg);

    /* Extend the original value of last prior to incrementing.  */
    cmp = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(cmp, last);

    if (!before) {
        incr_last_active(s, last, esz);
    }

    /* The conceit here is that while last < 0 indicates not found, after
     * adjusting for cpu_env->vfp.zregs[rm], it is still a valid address
     * from which we can load garbage.  We then discard the garbage with
     * a conditional move.
     */
    ele = load_last_active(s, last, rm, esz);
    tcg_temp_free_i32(last);

    zero = tcg_const_i64(0);
    tcg_gen_movcond_i64(TCG_COND_GE, reg_val, cmp, zero, ele, reg_val);

    tcg_temp_free_i64(zero);
    tcg_temp_free_i64(cmp);
    tcg_temp_free_i64(ele);
}

/* Compute CLAST for a Vreg.  */
static bool do_clast_fp(DisasContext *s, arg_rpr_esz *a, bool before)
{
    if (sve_access_check(s)) {
        int esz = a->esz;
        int ofs = vec_reg_offset(s, a->rd, 0, esz);
        TCGv_i64 reg = load_esz(cpu_env, ofs, esz);

        do_clast_scalar(s, esz, a->pg, a->rn, before, reg);
        write_fp_dreg(s, a->rd, reg);
        tcg_temp_free_i64(reg);
    }
    return true;
}

static bool trans_CLASTA_v(DisasContext *s, arg_rpr_esz *a)
{
    return do_clast_fp(s, a, false);
}

static bool trans_CLASTB_v(DisasContext *s, arg_rpr_esz *a)
{
    return do_clast_fp(s, a, true);
}

/* Compute CLAST for a Xreg.  */
static bool do_clast_general(DisasContext *s, arg_rpr_esz *a, bool before)
{
    TCGv_i64 reg;

    if (!sve_access_check(s)) {
        return true;
    }

    reg = cpu_reg(s, a->rd);
    switch (a->esz) {
    case 0:
        tcg_gen_ext8u_i64(reg, reg);
        break;
    case 1:
        tcg_gen_ext16u_i64(reg, reg);
        break;
    case 2:
        tcg_gen_ext32u_i64(reg, reg);
        break;
    case 3:
        break;
    default:
        g_assert_not_reached();
    }

    do_clast_scalar(s, a->esz, a->pg, a->rn, before, reg);
    return true;
}

static bool trans_CLASTA_r(DisasContext *s, arg_rpr_esz *a)
{
    return do_clast_general(s, a, false);
}

static bool trans_CLASTB_r(DisasContext *s, arg_rpr_esz *a)
{
    return do_clast_general(s, a, true);
}

/* Compute LAST for a scalar.  */
static TCGv_i64 do_last_scalar(DisasContext *s, int esz,
                               int pg, int rm, bool before)
{
    TCGv_i32 last = tcg_temp_new_i32();
    TCGv_i64 ret;

    find_last_active(s, last, esz, pg);
    if (before) {
        wrap_last_active(s, last, esz);
    } else {
        incr_last_active(s, last, esz);
    }

    ret = load_last_active(s, last, rm, esz);
    tcg_temp_free_i32(last);
    return ret;
}

/* Compute LAST for a Vreg.  */
static bool do_last_fp(DisasContext *s, arg_rpr_esz *a, bool before)
{
    if (sve_access_check(s)) {
        TCGv_i64 val = do_last_scalar(s, a->esz, a->pg, a->rn, before);
        write_fp_dreg(s, a->rd, val);
        tcg_temp_free_i64(val);
    }
    return true;
}

static bool trans_LASTA_v(DisasContext *s, arg_rpr_esz *a)
{
    return do_last_fp(s, a, false);
}

static bool trans_LASTB_v(DisasContext *s, arg_rpr_esz *a)
{
    return do_last_fp(s, a, true);
}

/* Compute LAST for a Xreg.  */
static bool do_last_general(DisasContext *s, arg_rpr_esz *a, bool before)
{
    if (sve_access_check(s)) {
        TCGv_i64 val = do_last_scalar(s, a->esz, a->pg, a->rn, before);
        tcg_gen_mov_i64(cpu_reg(s, a->rd), val);
        tcg_temp_free_i64(val);
    }
    return true;
}

static bool trans_LASTA_r(DisasContext *s, arg_rpr_esz *a)
{
    return do_last_general(s, a, false);
}

static bool trans_LASTB_r(DisasContext *s, arg_rpr_esz *a)
{
    return do_last_general(s, a, true);
}

static bool trans_CPY_m_r(DisasContext *s, arg_rpr_esz *a)
{
    if (sve_access_check(s)) {
        do_cpy_m(s, a->esz, a->rd, a->rd, a->pg, cpu_reg_sp(s, a->rn));
    }
    return true;
}

static bool trans_CPY_m_v(DisasContext *s, arg_rpr_esz *a)
{
    if (sve_access_check(s)) {
        int ofs = vec_reg_offset(s, a->rn, 0, a->esz);
        TCGv_i64 t = load_esz(cpu_env, ofs, a->esz);
        do_cpy_m(s, a->esz, a->rd, a->rd, a->pg, t);
        tcg_temp_free_i64(t);
    }
    return true;
}

static bool trans_REVB(DisasContext *s, arg_rpr_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        NULL,
        gen_helper_sve_revb_h,
        gen_helper_sve_revb_s,
        gen_helper_sve_revb_d,
    };
    return do_zpz_ool(s, a, fns[a->esz]);
}

static bool trans_REVH(DisasContext *s, arg_rpr_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        NULL,
        NULL,
        gen_helper_sve_revh_s,
        gen_helper_sve_revh_d,
    };
    return do_zpz_ool(s, a, fns[a->esz]);
}

static bool trans_REVW(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ool(s, a, a->esz == 3 ? gen_helper_sve_revw_d : NULL);
}

static bool trans_RBIT(DisasContext *s, arg_rpr_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve_rbit_b,
        gen_helper_sve_rbit_h,
        gen_helper_sve_rbit_s,
        gen_helper_sve_rbit_d,
    };
    return do_zpz_ool(s, a, fns[a->esz]);
}

static bool trans_SPLICE(DisasContext *s, arg_rprr_esz *a)
{
    if (sve_access_check(s)) {
        gen_gvec_ool_zzzp(s, gen_helper_sve_splice,
                          a->rd, a->rn, a->rm, a->pg, a->esz);
    }
    return true;
}

static bool trans_SPLICE_sve2(DisasContext *s, arg_rpr_esz *a)
{
    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_gvec_ool_zzzp(s, gen_helper_sve_splice,
                          a->rd, a->rn, (a->rn + 1) % 32, a->pg, a->esz);
    }
    return true;
}

/*
 *** SVE Integer Compare - Vectors Group
 */

static bool do_ppzz_flags(DisasContext *s, arg_rprr_esz *a,
                          gen_helper_gvec_flags_4 *gen_fn)
{
    TCGv_ptr pd, zn, zm, pg;
    unsigned vsz;
    TCGv_i32 t;

    if (gen_fn == NULL) {
        return false;
    }
    if (!sve_access_check(s)) {
        return true;
    }

    vsz = vec_full_reg_size(s);
    t = tcg_const_i32(simd_desc(vsz, vsz, 0));
    pd = tcg_temp_new_ptr();
    zn = tcg_temp_new_ptr();
    zm = tcg_temp_new_ptr();
    pg = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(pd, cpu_env, pred_full_reg_offset(s, a->rd));
    tcg_gen_addi_ptr(zn, cpu_env, vec_full_reg_offset(s, a->rn));
    tcg_gen_addi_ptr(zm, cpu_env, vec_full_reg_offset(s, a->rm));
    tcg_gen_addi_ptr(pg, cpu_env, pred_full_reg_offset(s, a->pg));

    gen_fn(t, pd, zn, zm, pg, t);

    tcg_temp_free_ptr(pd);
    tcg_temp_free_ptr(zn);
    tcg_temp_free_ptr(zm);
    tcg_temp_free_ptr(pg);

    do_pred_flags(t);

    tcg_temp_free_i32(t);
    return true;
}

#define DO_PPZZ(NAME, name) \
static bool trans_##NAME##_ppzz(DisasContext *s, arg_rprr_esz *a)         \
{                                                                         \
    static gen_helper_gvec_flags_4 * const fns[4] = {                     \
        gen_helper_sve_##name##_ppzz_b, gen_helper_sve_##name##_ppzz_h,   \
        gen_helper_sve_##name##_ppzz_s, gen_helper_sve_##name##_ppzz_d,   \
    };                                                                    \
    return do_ppzz_flags(s, a, fns[a->esz]);                              \
}

DO_PPZZ(CMPEQ, cmpeq)
DO_PPZZ(CMPNE, cmpne)
DO_PPZZ(CMPGT, cmpgt)
DO_PPZZ(CMPGE, cmpge)
DO_PPZZ(CMPHI, cmphi)
DO_PPZZ(CMPHS, cmphs)

#undef DO_PPZZ

#define DO_PPZW(NAME, name) \
static bool trans_##NAME##_ppzw(DisasContext *s, arg_rprr_esz *a)         \
{                                                                         \
    static gen_helper_gvec_flags_4 * const fns[4] = {                     \
        gen_helper_sve_##name##_ppzw_b, gen_helper_sve_##name##_ppzw_h,   \
        gen_helper_sve_##name##_ppzw_s, NULL                              \
    };                                                                    \
    return do_ppzz_flags(s, a, fns[a->esz]);                              \
}

DO_PPZW(CMPEQ, cmpeq)
DO_PPZW(CMPNE, cmpne)
DO_PPZW(CMPGT, cmpgt)
DO_PPZW(CMPGE, cmpge)
DO_PPZW(CMPHI, cmphi)
DO_PPZW(CMPHS, cmphs)
DO_PPZW(CMPLT, cmplt)
DO_PPZW(CMPLE, cmple)
DO_PPZW(CMPLO, cmplo)
DO_PPZW(CMPLS, cmpls)

#undef DO_PPZW

/*
 *** SVE Integer Compare - Immediate Groups
 */

static bool do_ppzi_flags(DisasContext *s, arg_rpri_esz *a,
                          gen_helper_gvec_flags_3 *gen_fn)
{
    TCGv_ptr pd, zn, pg;
    unsigned vsz;
    TCGv_i32 t;

    if (gen_fn == NULL) {
        return false;
    }
    if (!sve_access_check(s)) {
        return true;
    }

    vsz = vec_full_reg_size(s);
    t = tcg_const_i32(simd_desc(vsz, vsz, a->imm));
    pd = tcg_temp_new_ptr();
    zn = tcg_temp_new_ptr();
    pg = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(pd, cpu_env, pred_full_reg_offset(s, a->rd));
    tcg_gen_addi_ptr(zn, cpu_env, vec_full_reg_offset(s, a->rn));
    tcg_gen_addi_ptr(pg, cpu_env, pred_full_reg_offset(s, a->pg));

    gen_fn(t, pd, zn, pg, t);

    tcg_temp_free_ptr(pd);
    tcg_temp_free_ptr(zn);
    tcg_temp_free_ptr(pg);

    do_pred_flags(t);

    tcg_temp_free_i32(t);
    return true;
}

#define DO_PPZI(NAME, name) \
static bool trans_##NAME##_ppzi(DisasContext *s, arg_rpri_esz *a)         \
{                                                                         \
    static gen_helper_gvec_flags_3 * const fns[4] = {                     \
        gen_helper_sve_##name##_ppzi_b, gen_helper_sve_##name##_ppzi_h,   \
        gen_helper_sve_##name##_ppzi_s, gen_helper_sve_##name##_ppzi_d,   \
    };                                                                    \
    return do_ppzi_flags(s, a, fns[a->esz]);                              \
}

DO_PPZI(CMPEQ, cmpeq)
DO_PPZI(CMPNE, cmpne)
DO_PPZI(CMPGT, cmpgt)
DO_PPZI(CMPGE, cmpge)
DO_PPZI(CMPHI, cmphi)
DO_PPZI(CMPHS, cmphs)
DO_PPZI(CMPLT, cmplt)
DO_PPZI(CMPLE, cmple)
DO_PPZI(CMPLO, cmplo)
DO_PPZI(CMPLS, cmpls)

#undef DO_PPZI

/*
 *** SVE Partition Break Group
 */

static bool do_brk3(DisasContext *s, arg_rprr_s *a,
                    gen_helper_gvec_4 *fn, gen_helper_gvec_flags_4 *fn_s)
{
    if (!sve_access_check(s)) {
        return true;
    }

    unsigned vsz = pred_full_reg_size(s);

    /* Predicate sizes may be smaller and cannot use simd_desc.  */
    TCGv_ptr d = tcg_temp_new_ptr();
    TCGv_ptr n = tcg_temp_new_ptr();
    TCGv_ptr m = tcg_temp_new_ptr();
    TCGv_ptr g = tcg_temp_new_ptr();
    TCGv_i32 t = tcg_const_i32(FIELD_DP32(0, PREDDESC, OPRSZ, vsz));

    tcg_gen_addi_ptr(d, cpu_env, pred_full_reg_offset(s, a->rd));
    tcg_gen_addi_ptr(n, cpu_env, pred_full_reg_offset(s, a->rn));
    tcg_gen_addi_ptr(m, cpu_env, pred_full_reg_offset(s, a->rm));
    tcg_gen_addi_ptr(g, cpu_env, pred_full_reg_offset(s, a->pg));

    if (a->s) {
        fn_s(t, d, n, m, g, t);
        do_pred_flags(t);
    } else {
        fn(d, n, m, g, t);
    }
    tcg_temp_free_ptr(d);
    tcg_temp_free_ptr(n);
    tcg_temp_free_ptr(m);
    tcg_temp_free_ptr(g);
    tcg_temp_free_i32(t);
    return true;
}

static bool do_brk2(DisasContext *s, arg_rpr_s *a,
                    gen_helper_gvec_3 *fn, gen_helper_gvec_flags_3 *fn_s)
{
    if (!sve_access_check(s)) {
        return true;
    }

    unsigned vsz = pred_full_reg_size(s);

    /* Predicate sizes may be smaller and cannot use simd_desc.  */
    TCGv_ptr d = tcg_temp_new_ptr();
    TCGv_ptr n = tcg_temp_new_ptr();
    TCGv_ptr g = tcg_temp_new_ptr();
    TCGv_i32 t = tcg_const_i32(FIELD_DP32(0, PREDDESC, OPRSZ, vsz));

    tcg_gen_addi_ptr(d, cpu_env, pred_full_reg_offset(s, a->rd));
    tcg_gen_addi_ptr(n, cpu_env, pred_full_reg_offset(s, a->rn));
    tcg_gen_addi_ptr(g, cpu_env, pred_full_reg_offset(s, a->pg));

    if (a->s) {
        fn_s(t, d, n, g, t);
        do_pred_flags(t);
    } else {
        fn(d, n, g, t);
    }
    tcg_temp_free_ptr(d);
    tcg_temp_free_ptr(n);
    tcg_temp_free_ptr(g);
    tcg_temp_free_i32(t);
    return true;
}

static bool trans_BRKPA(DisasContext *s, arg_rprr_s *a)
{
    return do_brk3(s, a, gen_helper_sve_brkpa, gen_helper_sve_brkpas);
}

static bool trans_BRKPB(DisasContext *s, arg_rprr_s *a)
{
    return do_brk3(s, a, gen_helper_sve_brkpb, gen_helper_sve_brkpbs);
}

static bool trans_BRKA_m(DisasContext *s, arg_rpr_s *a)
{
    return do_brk2(s, a, gen_helper_sve_brka_m, gen_helper_sve_brkas_m);
}

static bool trans_BRKB_m(DisasContext *s, arg_rpr_s *a)
{
    return do_brk2(s, a, gen_helper_sve_brkb_m, gen_helper_sve_brkbs_m);
}

static bool trans_BRKA_z(DisasContext *s, arg_rpr_s *a)
{
    return do_brk2(s, a, gen_helper_sve_brka_z, gen_helper_sve_brkas_z);
}

static bool trans_BRKB_z(DisasContext *s, arg_rpr_s *a)
{
    return do_brk2(s, a, gen_helper_sve_brkb_z, gen_helper_sve_brkbs_z);
}

static bool trans_BRKN(DisasContext *s, arg_rpr_s *a)
{
    return do_brk2(s, a, gen_helper_sve_brkn, gen_helper_sve_brkns);
}

/*
 *** SVE Predicate Count Group
 */

static void do_cntp(DisasContext *s, TCGv_i64 val, int esz, int pn, int pg)
{
    unsigned psz = pred_full_reg_size(s);

    if (psz <= 8) {
        uint64_t psz_mask;

        tcg_gen_ld_i64(val, cpu_env, pred_full_reg_offset(s, pn));
        if (pn != pg) {
            TCGv_i64 g = tcg_temp_new_i64();
            tcg_gen_ld_i64(g, cpu_env, pred_full_reg_offset(s, pg));
            tcg_gen_and_i64(val, val, g);
            tcg_temp_free_i64(g);
        }

        /* Reduce the pred_esz_masks value simply to reduce the
         * size of the code generated here.
         */
        psz_mask = MAKE_64BIT_MASK(0, psz * 8);
        tcg_gen_andi_i64(val, val, pred_esz_masks[esz] & psz_mask);

        tcg_gen_ctpop_i64(val, val);
    } else {
        TCGv_ptr t_pn = tcg_temp_new_ptr();
        TCGv_ptr t_pg = tcg_temp_new_ptr();
        unsigned desc = 0;
        TCGv_i32 t_desc;

        desc = FIELD_DP32(desc, PREDDESC, OPRSZ, psz);
        desc = FIELD_DP32(desc, PREDDESC, ESZ, esz);

        tcg_gen_addi_ptr(t_pn, cpu_env, pred_full_reg_offset(s, pn));
        tcg_gen_addi_ptr(t_pg, cpu_env, pred_full_reg_offset(s, pg));
        t_desc = tcg_const_i32(desc);

        gen_helper_sve_cntp(val, t_pn, t_pg, t_desc);
        tcg_temp_free_ptr(t_pn);
        tcg_temp_free_ptr(t_pg);
        tcg_temp_free_i32(t_desc);
    }
}

static bool trans_CNTP(DisasContext *s, arg_CNTP *a)
{
    if (sve_access_check(s)) {
        do_cntp(s, cpu_reg(s, a->rd), a->esz, a->rn, a->pg);
    }
    return true;
}

static bool trans_INCDECP_r(DisasContext *s, arg_incdec_pred *a)
{
    if (sve_access_check(s)) {
        TCGv_i64 reg = cpu_reg(s, a->rd);
        TCGv_i64 val = tcg_temp_new_i64();

        do_cntp(s, val, a->esz, a->pg, a->pg);
        if (a->d) {
            tcg_gen_sub_i64(reg, reg, val);
        } else {
            tcg_gen_add_i64(reg, reg, val);
        }
        tcg_temp_free_i64(val);
    }
    return true;
}

static bool trans_INCDECP_z(DisasContext *s, arg_incdec2_pred *a)
{
    if (a->esz == 0) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_i64 val = tcg_temp_new_i64();
        GVecGen2sFn *gvec_fn = a->d ? tcg_gen_gvec_subs : tcg_gen_gvec_adds;

        do_cntp(s, val, a->esz, a->pg, a->pg);
        gvec_fn(a->esz, vec_full_reg_offset(s, a->rd),
                vec_full_reg_offset(s, a->rn), val, vsz, vsz);
    }
    return true;
}

static bool trans_SINCDECP_r_32(DisasContext *s, arg_incdec_pred *a)
{
    if (sve_access_check(s)) {
        TCGv_i64 reg = cpu_reg(s, a->rd);
        TCGv_i64 val = tcg_temp_new_i64();

        do_cntp(s, val, a->esz, a->pg, a->pg);
        do_sat_addsub_32(reg, val, a->u, a->d);
    }
    return true;
}

static bool trans_SINCDECP_r_64(DisasContext *s, arg_incdec_pred *a)
{
    if (sve_access_check(s)) {
        TCGv_i64 reg = cpu_reg(s, a->rd);
        TCGv_i64 val = tcg_temp_new_i64();

        do_cntp(s, val, a->esz, a->pg, a->pg);
        do_sat_addsub_64(reg, val, a->u, a->d);
    }
    return true;
}

static bool trans_SINCDECP_z(DisasContext *s, arg_incdec2_pred *a)
{
    if (a->esz == 0) {
        return false;
    }
    if (sve_access_check(s)) {
        TCGv_i64 val = tcg_temp_new_i64();
        do_cntp(s, val, a->esz, a->pg, a->pg);
        do_sat_addsub_vec(s, a->esz, a->rd, a->rn, val, a->u, a->d);
    }
    return true;
}

/*
 *** SVE Integer Compare Scalars Group
 */

static bool trans_CTERM(DisasContext *s, arg_CTERM *a)
{
    if (!sve_access_check(s)) {
        return true;
    }

    TCGCond cond = (a->ne ? TCG_COND_NE : TCG_COND_EQ);
    TCGv_i64 rn = read_cpu_reg(s, a->rn, a->sf);
    TCGv_i64 rm = read_cpu_reg(s, a->rm, a->sf);
    TCGv_i64 cmp = tcg_temp_new_i64();

    tcg_gen_setcond_i64(cond, cmp, rn, rm);
    tcg_gen_extrl_i64_i32(cpu_NF, cmp);
    tcg_temp_free_i64(cmp);

    /* VF = !NF & !CF.  */
    tcg_gen_xori_i32(cpu_VF, cpu_NF, 1);
    tcg_gen_andc_i32(cpu_VF, cpu_VF, cpu_CF);

    /* Both NF and VF actually look at bit 31.  */
    tcg_gen_neg_i32(cpu_NF, cpu_NF);
    tcg_gen_neg_i32(cpu_VF, cpu_VF);
    return true;
}

static bool trans_WHILE(DisasContext *s, arg_WHILE *a)
{
    TCGv_i64 op0, op1, t0, t1, tmax;
    TCGv_i32 t2, t3;
    TCGv_ptr ptr;
    unsigned vsz = vec_full_reg_size(s);
    unsigned desc = 0;
    TCGCond cond;
    uint64_t maxval;
    /* Note that GE/HS has a->eq == 0 and GT/HI has a->eq == 1. */
    bool eq = a->eq == a->lt;

    /* The greater-than conditions are all SVE2. */
    if (!a->lt && !dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    if (!sve_access_check(s)) {
        return true;
    }

    op0 = read_cpu_reg(s, a->rn, 1);
    op1 = read_cpu_reg(s, a->rm, 1);

    if (!a->sf) {
        if (a->u) {
            tcg_gen_ext32u_i64(op0, op0);
            tcg_gen_ext32u_i64(op1, op1);
        } else {
            tcg_gen_ext32s_i64(op0, op0);
            tcg_gen_ext32s_i64(op1, op1);
        }
    }

    /* For the helper, compress the different conditions into a computation
     * of how many iterations for which the condition is true.
     */
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();

    if (a->lt) {
        tcg_gen_sub_i64(t0, op1, op0);
        if (a->u) {
            maxval = a->sf ? UINT64_MAX : UINT32_MAX;
            cond = eq ? TCG_COND_LEU : TCG_COND_LTU;
        } else {
            maxval = a->sf ? INT64_MAX : INT32_MAX;
            cond = eq ? TCG_COND_LE : TCG_COND_LT;
        }
    } else {
        tcg_gen_sub_i64(t0, op0, op1);
        if (a->u) {
            maxval = 0;
            cond = eq ? TCG_COND_GEU : TCG_COND_GTU;
        } else {
            maxval = a->sf ? INT64_MIN : INT32_MIN;
            cond = eq ? TCG_COND_GE : TCG_COND_GT;
        }
    }

    tmax = tcg_const_i64(vsz >> a->esz);
    if (eq) {
        /* Equality means one more iteration.  */
        tcg_gen_addi_i64(t0, t0, 1);

        /*
         * For the less-than while, if op1 is maxval (and the only time
         * the addition above could overflow), then we produce an all-true
         * predicate by setting the count to the vector length.  This is
         * because the pseudocode is described as an increment + compare
         * loop, and the maximum integer would always compare true.
         * Similarly, the greater-than while has the same issue with the
         * minimum integer due to the decrement + compare loop.
         */
        tcg_gen_movi_i64(t1, maxval);
        tcg_gen_movcond_i64(TCG_COND_EQ, t0, op1, t1, tmax, t0);
    }

    /* Bound to the maximum.  */
    tcg_gen_umin_i64(t0, t0, tmax);
    tcg_temp_free_i64(tmax);

    /* Set the count to zero if the condition is false.  */
    tcg_gen_movi_i64(t1, 0);
    tcg_gen_movcond_i64(cond, t0, op0, op1, t0, t1);
    tcg_temp_free_i64(t1);

    /* Since we're bounded, pass as a 32-bit type.  */
    t2 = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(t2, t0);
    tcg_temp_free_i64(t0);

    /* Scale elements to bits.  */
    tcg_gen_shli_i32(t2, t2, a->esz);

    desc = FIELD_DP32(desc, PREDDESC, OPRSZ, vsz / 8);
    desc = FIELD_DP32(desc, PREDDESC, ESZ, a->esz);
    t3 = tcg_const_i32(desc);

    ptr = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(ptr, cpu_env, pred_full_reg_offset(s, a->rd));

    if (a->lt) {
        gen_helper_sve_whilel(t2, ptr, t2, t3);
    } else {
        gen_helper_sve_whileg(t2, ptr, t2, t3);
    }
    do_pred_flags(t2);

    tcg_temp_free_ptr(ptr);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t3);
    return true;
}

static bool trans_WHILE_ptr(DisasContext *s, arg_WHILE_ptr *a)
{
    TCGv_i64 op0, op1, diff, t1, tmax;
    TCGv_i32 t2, t3;
    TCGv_ptr ptr;
    unsigned vsz = vec_full_reg_size(s);
    unsigned desc = 0;

    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    if (!sve_access_check(s)) {
        return true;
    }

    op0 = read_cpu_reg(s, a->rn, 1);
    op1 = read_cpu_reg(s, a->rm, 1);

    tmax = tcg_const_i64(vsz);
    diff = tcg_temp_new_i64();

    if (a->rw) {
        /* WHILERW */
        /* diff = abs(op1 - op0), noting that op0/1 are unsigned. */
        t1 = tcg_temp_new_i64();
        tcg_gen_sub_i64(diff, op0, op1);
        tcg_gen_sub_i64(t1, op1, op0);
        tcg_gen_movcond_i64(TCG_COND_GEU, diff, op0, op1, diff, t1);
        tcg_temp_free_i64(t1);
        /* Round down to a multiple of ESIZE.  */
        tcg_gen_andi_i64(diff, diff, -1 << a->esz);
        /* If op1 == op0, diff == 0, and the condition is always true. */
        tcg_gen_movcond_i64(TCG_COND_EQ, diff, op0, op1, tmax, diff);
    } else {
        /* WHILEWR */
        tcg_gen_sub_i64(diff, op1, op0);
        /* Round down to a multiple of ESIZE.  */
        tcg_gen_andi_i64(diff, diff, -1 << a->esz);
        /* If op0 >= op1, diff <= 0, the condition is always true. */
        tcg_gen_movcond_i64(TCG_COND_GEU, diff, op0, op1, tmax, diff);
    }

    /* Bound to the maximum.  */
    tcg_gen_umin_i64(diff, diff, tmax);
    tcg_temp_free_i64(tmax);

    /* Since we're bounded, pass as a 32-bit type.  */
    t2 = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(t2, diff);
    tcg_temp_free_i64(diff);

    desc = FIELD_DP32(desc, PREDDESC, OPRSZ, vsz / 8);
    desc = FIELD_DP32(desc, PREDDESC, ESZ, a->esz);
    t3 = tcg_const_i32(desc);

    ptr = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(ptr, cpu_env, pred_full_reg_offset(s, a->rd));

    gen_helper_sve_whilel(t2, ptr, t2, t3);
    do_pred_flags(t2);

    tcg_temp_free_ptr(ptr);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t3);
    return true;
}

/*
 *** SVE Integer Wide Immediate - Unpredicated Group
 */

static bool trans_FDUP(DisasContext *s, arg_FDUP *a)
{
    if (a->esz == 0) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        int dofs = vec_full_reg_offset(s, a->rd);
        uint64_t imm;

        /* Decode the VFP immediate.  */
        imm = vfp_expand_imm(a->esz, a->imm);
        tcg_gen_gvec_dup_imm(a->esz, dofs, vsz, vsz, imm);
    }
    return true;
}

static bool trans_DUP_i(DisasContext *s, arg_DUP_i *a)
{
    if (a->esz == 0 && extract32(s->insn, 13, 1)) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        int dofs = vec_full_reg_offset(s, a->rd);

        tcg_gen_gvec_dup_imm(a->esz, dofs, vsz, vsz, a->imm);
    }
    return true;
}

static bool trans_ADD_zzi(DisasContext *s, arg_rri_esz *a)
{
    if (a->esz == 0 && extract32(s->insn, 13, 1)) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_addi(a->esz, vec_full_reg_offset(s, a->rd),
                          vec_full_reg_offset(s, a->rn), a->imm, vsz, vsz);
    }
    return true;
}

static bool trans_SUB_zzi(DisasContext *s, arg_rri_esz *a)
{
    a->imm = -a->imm;
    return trans_ADD_zzi(s, a);
}

static bool trans_SUBR_zzi(DisasContext *s, arg_rri_esz *a)
{
    static const TCGOpcode vecop_list[] = { INDEX_op_sub_vec, 0 };
    static const GVecGen2s op[4] = {
        { .fni8 = tcg_gen_vec_sub8_i64,
          .fniv = tcg_gen_sub_vec,
          .fno = gen_helper_sve_subri_b,
          .opt_opc = vecop_list,
          .vece = MO_8,
          .scalar_first = true },
        { .fni8 = tcg_gen_vec_sub16_i64,
          .fniv = tcg_gen_sub_vec,
          .fno = gen_helper_sve_subri_h,
          .opt_opc = vecop_list,
          .vece = MO_16,
          .scalar_first = true },
        { .fni4 = tcg_gen_sub_i32,
          .fniv = tcg_gen_sub_vec,
          .fno = gen_helper_sve_subri_s,
          .opt_opc = vecop_list,
          .vece = MO_32,
          .scalar_first = true },
        { .fni8 = tcg_gen_sub_i64,
          .fniv = tcg_gen_sub_vec,
          .fno = gen_helper_sve_subri_d,
          .opt_opc = vecop_list,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .vece = MO_64,
          .scalar_first = true }
    };

    if (a->esz == 0 && extract32(s->insn, 13, 1)) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_i64 c = tcg_const_i64(a->imm);
        tcg_gen_gvec_2s(vec_full_reg_offset(s, a->rd),
                        vec_full_reg_offset(s, a->rn),
                        vsz, vsz, c, &op[a->esz]);
        tcg_temp_free_i64(c);
    }
    return true;
}

static bool trans_MUL_zzi(DisasContext *s, arg_rri_esz *a)
{
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_muli(a->esz, vec_full_reg_offset(s, a->rd),
                          vec_full_reg_offset(s, a->rn), a->imm, vsz, vsz);
    }
    return true;
}

static bool do_zzi_sat(DisasContext *s, arg_rri_esz *a, bool u, bool d)
{
    if (a->esz == 0 && extract32(s->insn, 13, 1)) {
        return false;
    }
    if (sve_access_check(s)) {
        TCGv_i64 val = tcg_const_i64(a->imm);
        do_sat_addsub_vec(s, a->esz, a->rd, a->rn, val, u, d);
        tcg_temp_free_i64(val);
    }
    return true;
}

static bool trans_SQADD_zzi(DisasContext *s, arg_rri_esz *a)
{
    return do_zzi_sat(s, a, false, false);
}

static bool trans_UQADD_zzi(DisasContext *s, arg_rri_esz *a)
{
    return do_zzi_sat(s, a, true, false);
}

static bool trans_SQSUB_zzi(DisasContext *s, arg_rri_esz *a)
{
    return do_zzi_sat(s, a, false, true);
}

static bool trans_UQSUB_zzi(DisasContext *s, arg_rri_esz *a)
{
    return do_zzi_sat(s, a, true, true);
}

static bool do_zzi_ool(DisasContext *s, arg_rri_esz *a, gen_helper_gvec_2i *fn)
{
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_i64 c = tcg_const_i64(a->imm);

        tcg_gen_gvec_2i_ool(vec_full_reg_offset(s, a->rd),
                            vec_full_reg_offset(s, a->rn),
                            c, vsz, vsz, 0, fn);
        tcg_temp_free_i64(c);
    }
    return true;
}

#define DO_ZZI(NAME, name) \
static bool trans_##NAME##_zzi(DisasContext *s, arg_rri_esz *a)         \
{                                                                       \
    static gen_helper_gvec_2i * const fns[4] = {                        \
        gen_helper_sve_##name##i_b, gen_helper_sve_##name##i_h,         \
        gen_helper_sve_##name##i_s, gen_helper_sve_##name##i_d,         \
    };                                                                  \
    return do_zzi_ool(s, a, fns[a->esz]);                               \
}

DO_ZZI(SMAX, smax)
DO_ZZI(UMAX, umax)
DO_ZZI(SMIN, smin)
DO_ZZI(UMIN, umin)

#undef DO_ZZI

static bool trans_DOT_zzzz(DisasContext *s, arg_DOT_zzzz *a)
{
    static gen_helper_gvec_4 * const fns[2][2] = {
        { gen_helper_gvec_sdot_b, gen_helper_gvec_sdot_h },
        { gen_helper_gvec_udot_b, gen_helper_gvec_udot_h }
    };

    if (sve_access_check(s)) {
        gen_gvec_ool_zzzz(s, fns[a->u][a->sz], a->rd, a->rn, a->rm, a->ra, 0);
    }
    return true;
}

/*
 * SVE Multiply - Indexed
 */

static bool do_zzxz_ool(DisasContext *s, arg_rrxr_esz *a,
                        gen_helper_gvec_4 *fn)
{
    if (fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_gvec_ool_zzzz(s, fn, a->rd, a->rn, a->rm, a->ra, a->index);
    }
    return true;
}

#define DO_RRXR(NAME, FUNC) \
    static bool NAME(DisasContext *s, arg_rrxr_esz *a)  \
    { return do_zzxz_ool(s, a, FUNC); }

DO_RRXR(trans_SDOT_zzxw_s, gen_helper_gvec_sdot_idx_b)
DO_RRXR(trans_SDOT_zzxw_d, gen_helper_gvec_sdot_idx_h)
DO_RRXR(trans_UDOT_zzxw_s, gen_helper_gvec_udot_idx_b)
DO_RRXR(trans_UDOT_zzxw_d, gen_helper_gvec_udot_idx_h)

static bool trans_SUDOT_zzxw_s(DisasContext *s, arg_rrxr_esz *a)
{
    if (!dc_isar_feature(aa64_sve_i8mm, s)) {
        return false;
    }
    return do_zzxz_ool(s, a, gen_helper_gvec_sudot_idx_b);
}

static bool trans_USDOT_zzxw_s(DisasContext *s, arg_rrxr_esz *a)
{
    if (!dc_isar_feature(aa64_sve_i8mm, s)) {
        return false;
    }
    return do_zzxz_ool(s, a, gen_helper_gvec_usdot_idx_b);
}

#undef DO_RRXR

static bool do_sve2_zzz_data(DisasContext *s, int rd, int rn, int rm, int data,
                             gen_helper_gvec_3 *fn)
{
    if (fn == NULL || !dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_3_ool(vec_full_reg_offset(s, rd),
                           vec_full_reg_offset(s, rn),
                           vec_full_reg_offset(s, rm),
                           vsz, vsz, data, fn);
    }
    return true;
}

#define DO_SVE2_RRX(NAME, FUNC) \
    static bool NAME(DisasContext *s, arg_rrx_esz *a)  \
    { return do_sve2_zzz_data(s, a->rd, a->rn, a->rm, a->index, FUNC); }

DO_SVE2_RRX(trans_MUL_zzx_h, gen_helper_gvec_mul_idx_h)
DO_SVE2_RRX(trans_MUL_zzx_s, gen_helper_gvec_mul_idx_s)
DO_SVE2_RRX(trans_MUL_zzx_d, gen_helper_gvec_mul_idx_d)

DO_SVE2_RRX(trans_SQDMULH_zzx_h, gen_helper_sve2_sqdmulh_idx_h)
DO_SVE2_RRX(trans_SQDMULH_zzx_s, gen_helper_sve2_sqdmulh_idx_s)
DO_SVE2_RRX(trans_SQDMULH_zzx_d, gen_helper_sve2_sqdmulh_idx_d)

DO_SVE2_RRX(trans_SQRDMULH_zzx_h, gen_helper_sve2_sqrdmulh_idx_h)
DO_SVE2_RRX(trans_SQRDMULH_zzx_s, gen_helper_sve2_sqrdmulh_idx_s)
DO_SVE2_RRX(trans_SQRDMULH_zzx_d, gen_helper_sve2_sqrdmulh_idx_d)

#undef DO_SVE2_RRX

#define DO_SVE2_RRX_TB(NAME, FUNC, TOP) \
    static bool NAME(DisasContext *s, arg_rrx_esz *a)           \
    {                                                           \
        return do_sve2_zzz_data(s, a->rd, a->rn, a->rm,         \
                                (a->index << 1) | TOP, FUNC);   \
    }

DO_SVE2_RRX_TB(trans_SQDMULLB_zzx_s, gen_helper_sve2_sqdmull_idx_s, false)
DO_SVE2_RRX_TB(trans_SQDMULLB_zzx_d, gen_helper_sve2_sqdmull_idx_d, false)
DO_SVE2_RRX_TB(trans_SQDMULLT_zzx_s, gen_helper_sve2_sqdmull_idx_s, true)
DO_SVE2_RRX_TB(trans_SQDMULLT_zzx_d, gen_helper_sve2_sqdmull_idx_d, true)

DO_SVE2_RRX_TB(trans_SMULLB_zzx_s, gen_helper_sve2_smull_idx_s, false)
DO_SVE2_RRX_TB(trans_SMULLB_zzx_d, gen_helper_sve2_smull_idx_d, false)
DO_SVE2_RRX_TB(trans_SMULLT_zzx_s, gen_helper_sve2_smull_idx_s, true)
DO_SVE2_RRX_TB(trans_SMULLT_zzx_d, gen_helper_sve2_smull_idx_d, true)

DO_SVE2_RRX_TB(trans_UMULLB_zzx_s, gen_helper_sve2_umull_idx_s, false)
DO_SVE2_RRX_TB(trans_UMULLB_zzx_d, gen_helper_sve2_umull_idx_d, false)
DO_SVE2_RRX_TB(trans_UMULLT_zzx_s, gen_helper_sve2_umull_idx_s, true)
DO_SVE2_RRX_TB(trans_UMULLT_zzx_d, gen_helper_sve2_umull_idx_d, true)

#undef DO_SVE2_RRX_TB

static bool do_sve2_zzzz_data(DisasContext *s, int rd, int rn, int rm, int ra,
                              int data, gen_helper_gvec_4 *fn)
{
    if (fn == NULL || !dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_4_ool(vec_full_reg_offset(s, rd),
                           vec_full_reg_offset(s, rn),
                           vec_full_reg_offset(s, rm),
                           vec_full_reg_offset(s, ra),
                           vsz, vsz, data, fn);
    }
    return true;
}

#define DO_SVE2_RRXR(NAME, FUNC) \
    static bool NAME(DisasContext *s, arg_rrxr_esz *a)  \
    { return do_sve2_zzzz_data(s, a->rd, a->rn, a->rm, a->ra, a->index, FUNC); }

DO_SVE2_RRXR(trans_MLA_zzxz_h, gen_helper_gvec_mla_idx_h)
DO_SVE2_RRXR(trans_MLA_zzxz_s, gen_helper_gvec_mla_idx_s)
DO_SVE2_RRXR(trans_MLA_zzxz_d, gen_helper_gvec_mla_idx_d)

DO_SVE2_RRXR(trans_MLS_zzxz_h, gen_helper_gvec_mls_idx_h)
DO_SVE2_RRXR(trans_MLS_zzxz_s, gen_helper_gvec_mls_idx_s)
DO_SVE2_RRXR(trans_MLS_zzxz_d, gen_helper_gvec_mls_idx_d)

DO_SVE2_RRXR(trans_SQRDMLAH_zzxz_h, gen_helper_sve2_sqrdmlah_idx_h)
DO_SVE2_RRXR(trans_SQRDMLAH_zzxz_s, gen_helper_sve2_sqrdmlah_idx_s)
DO_SVE2_RRXR(trans_SQRDMLAH_zzxz_d, gen_helper_sve2_sqrdmlah_idx_d)

DO_SVE2_RRXR(trans_SQRDMLSH_zzxz_h, gen_helper_sve2_sqrdmlsh_idx_h)
DO_SVE2_RRXR(trans_SQRDMLSH_zzxz_s, gen_helper_sve2_sqrdmlsh_idx_s)
DO_SVE2_RRXR(trans_SQRDMLSH_zzxz_d, gen_helper_sve2_sqrdmlsh_idx_d)

#undef DO_SVE2_RRXR

#define DO_SVE2_RRXR_TB(NAME, FUNC, TOP) \
    static bool NAME(DisasContext *s, arg_rrxr_esz *a)          \
    {                                                           \
        return do_sve2_zzzz_data(s, a->rd, a->rn, a->rm, a->rd, \
                                 (a->index << 1) | TOP, FUNC);  \
    }

DO_SVE2_RRXR_TB(trans_SQDMLALB_zzxw_s, gen_helper_sve2_sqdmlal_idx_s, false)
DO_SVE2_RRXR_TB(trans_SQDMLALB_zzxw_d, gen_helper_sve2_sqdmlal_idx_d, false)
DO_SVE2_RRXR_TB(trans_SQDMLALT_zzxw_s, gen_helper_sve2_sqdmlal_idx_s, true)
DO_SVE2_RRXR_TB(trans_SQDMLALT_zzxw_d, gen_helper_sve2_sqdmlal_idx_d, true)

DO_SVE2_RRXR_TB(trans_SQDMLSLB_zzxw_s, gen_helper_sve2_sqdmlsl_idx_s, false)
DO_SVE2_RRXR_TB(trans_SQDMLSLB_zzxw_d, gen_helper_sve2_sqdmlsl_idx_d, false)
DO_SVE2_RRXR_TB(trans_SQDMLSLT_zzxw_s, gen_helper_sve2_sqdmlsl_idx_s, true)
DO_SVE2_RRXR_TB(trans_SQDMLSLT_zzxw_d, gen_helper_sve2_sqdmlsl_idx_d, true)

DO_SVE2_RRXR_TB(trans_SMLALB_zzxw_s, gen_helper_sve2_smlal_idx_s, false)
DO_SVE2_RRXR_TB(trans_SMLALB_zzxw_d, gen_helper_sve2_smlal_idx_d, false)
DO_SVE2_RRXR_TB(trans_SMLALT_zzxw_s, gen_helper_sve2_smlal_idx_s, true)
DO_SVE2_RRXR_TB(trans_SMLALT_zzxw_d, gen_helper_sve2_smlal_idx_d, true)

DO_SVE2_RRXR_TB(trans_UMLALB_zzxw_s, gen_helper_sve2_umlal_idx_s, false)
DO_SVE2_RRXR_TB(trans_UMLALB_zzxw_d, gen_helper_sve2_umlal_idx_d, false)
DO_SVE2_RRXR_TB(trans_UMLALT_zzxw_s, gen_helper_sve2_umlal_idx_s, true)
DO_SVE2_RRXR_TB(trans_UMLALT_zzxw_d, gen_helper_sve2_umlal_idx_d, true)

DO_SVE2_RRXR_TB(trans_SMLSLB_zzxw_s, gen_helper_sve2_smlsl_idx_s, false)
DO_SVE2_RRXR_TB(trans_SMLSLB_zzxw_d, gen_helper_sve2_smlsl_idx_d, false)
DO_SVE2_RRXR_TB(trans_SMLSLT_zzxw_s, gen_helper_sve2_smlsl_idx_s, true)
DO_SVE2_RRXR_TB(trans_SMLSLT_zzxw_d, gen_helper_sve2_smlsl_idx_d, true)

DO_SVE2_RRXR_TB(trans_UMLSLB_zzxw_s, gen_helper_sve2_umlsl_idx_s, false)
DO_SVE2_RRXR_TB(trans_UMLSLB_zzxw_d, gen_helper_sve2_umlsl_idx_d, false)
DO_SVE2_RRXR_TB(trans_UMLSLT_zzxw_s, gen_helper_sve2_umlsl_idx_s, true)
DO_SVE2_RRXR_TB(trans_UMLSLT_zzxw_d, gen_helper_sve2_umlsl_idx_d, true)

#undef DO_SVE2_RRXR_TB

#define DO_SVE2_RRXR_ROT(NAME, FUNC) \
    static bool trans_##NAME(DisasContext *s, arg_##NAME *a)       \
    {                                                              \
        return do_sve2_zzzz_data(s, a->rd, a->rn, a->rm, a->ra,    \
                                 (a->index << 2) | a->rot, FUNC);  \
    }

DO_SVE2_RRXR_ROT(CMLA_zzxz_h, gen_helper_sve2_cmla_idx_h)
DO_SVE2_RRXR_ROT(CMLA_zzxz_s, gen_helper_sve2_cmla_idx_s)

DO_SVE2_RRXR_ROT(SQRDCMLAH_zzxz_h, gen_helper_sve2_sqrdcmlah_idx_h)
DO_SVE2_RRXR_ROT(SQRDCMLAH_zzxz_s, gen_helper_sve2_sqrdcmlah_idx_s)

DO_SVE2_RRXR_ROT(CDOT_zzxw_s, gen_helper_sve2_cdot_idx_s)
DO_SVE2_RRXR_ROT(CDOT_zzxw_d, gen_helper_sve2_cdot_idx_d)

#undef DO_SVE2_RRXR_ROT

/*
 *** SVE Floating Point Multiply-Add Indexed Group
 */

static bool do_FMLA_zzxz(DisasContext *s, arg_rrxr_esz *a, bool sub)
{
    static gen_helper_gvec_4_ptr * const fns[3] = {
        gen_helper_gvec_fmla_idx_h,
        gen_helper_gvec_fmla_idx_s,
        gen_helper_gvec_fmla_idx_d,
    };

    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_ptr status = fpstatus_ptr(a->esz == MO_16 ? FPST_FPCR_F16 : FPST_FPCR);
        tcg_gen_gvec_4_ptr(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           vec_full_reg_offset(s, a->ra),
                           status, vsz, vsz, (a->index << 1) | sub,
                           fns[a->esz - 1]);
        tcg_temp_free_ptr(status);
    }
    return true;
}

static bool trans_FMLA_zzxz(DisasContext *s, arg_FMLA_zzxz *a)
{
    return do_FMLA_zzxz(s, a, false);
}

static bool trans_FMLS_zzxz(DisasContext *s, arg_FMLA_zzxz *a)
{
    return do_FMLA_zzxz(s, a, true);
}

/*
 *** SVE Floating Point Multiply Indexed Group
 */

static bool trans_FMUL_zzx(DisasContext *s, arg_FMUL_zzx *a)
{
    static gen_helper_gvec_3_ptr * const fns[3] = {
        gen_helper_gvec_fmul_idx_h,
        gen_helper_gvec_fmul_idx_s,
        gen_helper_gvec_fmul_idx_d,
    };

    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_ptr status = fpstatus_ptr(a->esz == MO_16 ? FPST_FPCR_F16 : FPST_FPCR);
        tcg_gen_gvec_3_ptr(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           status, vsz, vsz, a->index, fns[a->esz - 1]);
        tcg_temp_free_ptr(status);
    }
    return true;
}

/*
 *** SVE Floating Point Fast Reduction Group
 */

typedef void gen_helper_fp_reduce(TCGv_i64, TCGv_ptr, TCGv_ptr,
                                  TCGv_ptr, TCGv_i32);

static void do_reduce(DisasContext *s, arg_rpr_esz *a,
                      gen_helper_fp_reduce *fn)
{
    unsigned vsz = vec_full_reg_size(s);
    unsigned p2vsz = pow2ceil(vsz);
    TCGv_i32 t_desc = tcg_const_i32(simd_desc(vsz, vsz, p2vsz));
    TCGv_ptr t_zn, t_pg, status;
    TCGv_i64 temp;

    temp = tcg_temp_new_i64();
    t_zn = tcg_temp_new_ptr();
    t_pg = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(t_zn, cpu_env, vec_full_reg_offset(s, a->rn));
    tcg_gen_addi_ptr(t_pg, cpu_env, pred_full_reg_offset(s, a->pg));
    status = fpstatus_ptr(a->esz == MO_16 ? FPST_FPCR_F16 : FPST_FPCR);

    fn(temp, t_zn, t_pg, status, t_desc);
    tcg_temp_free_ptr(t_zn);
    tcg_temp_free_ptr(t_pg);
    tcg_temp_free_ptr(status);
    tcg_temp_free_i32(t_desc);

    write_fp_dreg(s, a->rd, temp);
    tcg_temp_free_i64(temp);
}

#define DO_VPZ(NAME, name) \
static bool trans_##NAME(DisasContext *s, arg_rpr_esz *a)                \
{                                                                        \
    static gen_helper_fp_reduce * const fns[3] = {                       \
        gen_helper_sve_##name##_h,                                       \
        gen_helper_sve_##name##_s,                                       \
        gen_helper_sve_##name##_d,                                       \
    };                                                                   \
    if (a->esz == 0) {                                                   \
        return false;                                                    \
    }                                                                    \
    if (sve_access_check(s)) {                                           \
        do_reduce(s, a, fns[a->esz - 1]);                                \
    }                                                                    \
    return true;                                                         \
}

DO_VPZ(FADDV, faddv)
DO_VPZ(FMINNMV, fminnmv)
DO_VPZ(FMAXNMV, fmaxnmv)
DO_VPZ(FMINV, fminv)
DO_VPZ(FMAXV, fmaxv)

/*
 *** SVE Floating Point Unary Operations - Unpredicated Group
 */

static void do_zz_fp(DisasContext *s, arg_rr_esz *a, gen_helper_gvec_2_ptr *fn)
{
    unsigned vsz = vec_full_reg_size(s);
    TCGv_ptr status = fpstatus_ptr(a->esz == MO_16 ? FPST_FPCR_F16 : FPST_FPCR);

    tcg_gen_gvec_2_ptr(vec_full_reg_offset(s, a->rd),
                       vec_full_reg_offset(s, a->rn),
                       status, vsz, vsz, 0, fn);
    tcg_temp_free_ptr(status);
}

static bool trans_FRECPE(DisasContext *s, arg_rr_esz *a)
{
    static gen_helper_gvec_2_ptr * const fns[3] = {
        gen_helper_gvec_frecpe_h,
        gen_helper_gvec_frecpe_s,
        gen_helper_gvec_frecpe_d,
    };
    if (a->esz == 0) {
        return false;
    }
    if (sve_access_check(s)) {
        do_zz_fp(s, a, fns[a->esz - 1]);
    }
    return true;
}

static bool trans_FRSQRTE(DisasContext *s, arg_rr_esz *a)
{
    static gen_helper_gvec_2_ptr * const fns[3] = {
        gen_helper_gvec_frsqrte_h,
        gen_helper_gvec_frsqrte_s,
        gen_helper_gvec_frsqrte_d,
    };
    if (a->esz == 0) {
        return false;
    }
    if (sve_access_check(s)) {
        do_zz_fp(s, a, fns[a->esz - 1]);
    }
    return true;
}

/*
 *** SVE Floating Point Compare with Zero Group
 */

static void do_ppz_fp(DisasContext *s, arg_rpr_esz *a,
                      gen_helper_gvec_3_ptr *fn)
{
    unsigned vsz = vec_full_reg_size(s);
    TCGv_ptr status = fpstatus_ptr(a->esz == MO_16 ? FPST_FPCR_F16 : FPST_FPCR);

    tcg_gen_gvec_3_ptr(pred_full_reg_offset(s, a->rd),
                       vec_full_reg_offset(s, a->rn),
                       pred_full_reg_offset(s, a->pg),
                       status, vsz, vsz, 0, fn);
    tcg_temp_free_ptr(status);
}

#define DO_PPZ(NAME, name) \
static bool trans_##NAME(DisasContext *s, arg_rpr_esz *a)         \
{                                                                 \
    static gen_helper_gvec_3_ptr * const fns[3] = {               \
        gen_helper_sve_##name##_h,                                \
        gen_helper_sve_##name##_s,                                \
        gen_helper_sve_##name##_d,                                \
    };                                                            \
    if (a->esz == 0) {                                            \
        return false;                                             \
    }                                                             \
    if (sve_access_check(s)) {                                    \
        do_ppz_fp(s, a, fns[a->esz - 1]);                         \
    }                                                             \
    return true;                                                  \
}

DO_PPZ(FCMGE_ppz0, fcmge0)
DO_PPZ(FCMGT_ppz0, fcmgt0)
DO_PPZ(FCMLE_ppz0, fcmle0)
DO_PPZ(FCMLT_ppz0, fcmlt0)
DO_PPZ(FCMEQ_ppz0, fcmeq0)
DO_PPZ(FCMNE_ppz0, fcmne0)

#undef DO_PPZ

/*
 *** SVE floating-point trig multiply-add coefficient
 */

static bool trans_FTMAD(DisasContext *s, arg_FTMAD *a)
{
    static gen_helper_gvec_3_ptr * const fns[3] = {
        gen_helper_sve_ftmad_h,
        gen_helper_sve_ftmad_s,
        gen_helper_sve_ftmad_d,
    };

    if (a->esz == 0) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_ptr status = fpstatus_ptr(a->esz == MO_16 ? FPST_FPCR_F16 : FPST_FPCR);
        tcg_gen_gvec_3_ptr(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           status, vsz, vsz, a->imm, fns[a->esz - 1]);
        tcg_temp_free_ptr(status);
    }
    return true;
}

/*
 *** SVE Floating Point Accumulating Reduction Group
 */

static bool trans_FADDA(DisasContext *s, arg_rprr_esz *a)
{
    typedef void fadda_fn(TCGv_i64, TCGv_i64, TCGv_ptr,
                          TCGv_ptr, TCGv_ptr, TCGv_i32);
    static fadda_fn * const fns[3] = {
        gen_helper_sve_fadda_h,
        gen_helper_sve_fadda_s,
        gen_helper_sve_fadda_d,
    };
    unsigned vsz = vec_full_reg_size(s);
    TCGv_ptr t_rm, t_pg, t_fpst;
    TCGv_i64 t_val;
    TCGv_i32 t_desc;

    if (a->esz == 0) {
        return false;
    }
    if (!sve_access_check(s)) {
        return true;
    }

    t_val = load_esz(cpu_env, vec_reg_offset(s, a->rn, 0, a->esz), a->esz);
    t_rm = tcg_temp_new_ptr();
    t_pg = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(t_rm, cpu_env, vec_full_reg_offset(s, a->rm));
    tcg_gen_addi_ptr(t_pg, cpu_env, pred_full_reg_offset(s, a->pg));
    t_fpst = fpstatus_ptr(a->esz == MO_16 ? FPST_FPCR_F16 : FPST_FPCR);
    t_desc = tcg_const_i32(simd_desc(vsz, vsz, 0));

    fns[a->esz - 1](t_val, t_val, t_rm, t_pg, t_fpst, t_desc);

    tcg_temp_free_i32(t_desc);
    tcg_temp_free_ptr(t_fpst);
    tcg_temp_free_ptr(t_pg);
    tcg_temp_free_ptr(t_rm);

    write_fp_dreg(s, a->rd, t_val);
    tcg_temp_free_i64(t_val);
    return true;
}

/*
 *** SVE Floating Point Arithmetic - Unpredicated Group
 */

static bool do_zzz_fp(DisasContext *s, arg_rrr_esz *a,
                      gen_helper_gvec_3_ptr *fn)
{
    if (fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_ptr status = fpstatus_ptr(a->esz == MO_16 ? FPST_FPCR_F16 : FPST_FPCR);
        tcg_gen_gvec_3_ptr(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           status, vsz, vsz, 0, fn);
        tcg_temp_free_ptr(status);
    }
    return true;
}


#define DO_FP3(NAME, name) \
static bool trans_##NAME(DisasContext *s, arg_rrr_esz *a)           \
{                                                                   \
    static gen_helper_gvec_3_ptr * const fns[4] = {                 \
        NULL, gen_helper_gvec_##name##_h,                           \
        gen_helper_gvec_##name##_s, gen_helper_gvec_##name##_d      \
    };                                                              \
    return do_zzz_fp(s, a, fns[a->esz]);                            \
}

DO_FP3(FADD_zzz, fadd)
DO_FP3(FSUB_zzz, fsub)
DO_FP3(FMUL_zzz, fmul)
DO_FP3(FTSMUL, ftsmul)
DO_FP3(FRECPS, recps)
DO_FP3(FRSQRTS, rsqrts)

#undef DO_FP3

/*
 *** SVE Floating Point Arithmetic - Predicated Group
 */

static bool do_zpzz_fp(DisasContext *s, arg_rprr_esz *a,
                       gen_helper_gvec_4_ptr *fn)
{
    if (fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_ptr status = fpstatus_ptr(a->esz == MO_16 ? FPST_FPCR_F16 : FPST_FPCR);
        tcg_gen_gvec_4_ptr(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           pred_full_reg_offset(s, a->pg),
                           status, vsz, vsz, 0, fn);
        tcg_temp_free_ptr(status);
    }
    return true;
}

#define DO_FP3(NAME, name) \
static bool trans_##NAME(DisasContext *s, arg_rprr_esz *a)          \
{                                                                   \
    static gen_helper_gvec_4_ptr * const fns[4] = {                 \
        NULL, gen_helper_sve_##name##_h,                            \
        gen_helper_sve_##name##_s, gen_helper_sve_##name##_d        \
    };                                                              \
    return do_zpzz_fp(s, a, fns[a->esz]);                           \
}

DO_FP3(FADD_zpzz, fadd)
DO_FP3(FSUB_zpzz, fsub)
DO_FP3(FMUL_zpzz, fmul)
DO_FP3(FMIN_zpzz, fmin)
DO_FP3(FMAX_zpzz, fmax)
DO_FP3(FMINNM_zpzz, fminnum)
DO_FP3(FMAXNM_zpzz, fmaxnum)
DO_FP3(FABD, fabd)
DO_FP3(FSCALE, fscalbn)
DO_FP3(FDIV, fdiv)
DO_FP3(FMULX, fmulx)

#undef DO_FP3

typedef void gen_helper_sve_fp2scalar(TCGv_ptr, TCGv_ptr, TCGv_ptr,
                                      TCGv_i64, TCGv_ptr, TCGv_i32);

static void do_fp_scalar(DisasContext *s, int zd, int zn, int pg, bool is_fp16,
                         TCGv_i64 scalar, gen_helper_sve_fp2scalar *fn)
{
    unsigned vsz = vec_full_reg_size(s);
    TCGv_ptr t_zd, t_zn, t_pg, status;
    TCGv_i32 desc;

    t_zd = tcg_temp_new_ptr();
    t_zn = tcg_temp_new_ptr();
    t_pg = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(t_zd, cpu_env, vec_full_reg_offset(s, zd));
    tcg_gen_addi_ptr(t_zn, cpu_env, vec_full_reg_offset(s, zn));
    tcg_gen_addi_ptr(t_pg, cpu_env, pred_full_reg_offset(s, pg));

    status = fpstatus_ptr(is_fp16 ? FPST_FPCR_F16 : FPST_FPCR);
    desc = tcg_const_i32(simd_desc(vsz, vsz, 0));
    fn(t_zd, t_zn, t_pg, scalar, status, desc);

    tcg_temp_free_i32(desc);
    tcg_temp_free_ptr(status);
    tcg_temp_free_ptr(t_pg);
    tcg_temp_free_ptr(t_zn);
    tcg_temp_free_ptr(t_zd);
}

static void do_fp_imm(DisasContext *s, arg_rpri_esz *a, uint64_t imm,
                      gen_helper_sve_fp2scalar *fn)
{
    TCGv_i64 temp = tcg_const_i64(imm);
    do_fp_scalar(s, a->rd, a->rn, a->pg, a->esz == MO_16, temp, fn);
    tcg_temp_free_i64(temp);
}

#define DO_FP_IMM(NAME, name, const0, const1) \
static bool trans_##NAME##_zpzi(DisasContext *s, arg_rpri_esz *a)         \
{                                                                         \
    static gen_helper_sve_fp2scalar * const fns[3] = {                    \
        gen_helper_sve_##name##_h,                                        \
        gen_helper_sve_##name##_s,                                        \
        gen_helper_sve_##name##_d                                         \
    };                                                                    \
    static uint64_t const val[3][2] = {                                   \
        { float16_##const0, float16_##const1 },                           \
        { float32_##const0, float32_##const1 },                           \
        { float64_##const0, float64_##const1 },                           \
    };                                                                    \
    if (a->esz == 0) {                                                    \
        return false;                                                     \
    }                                                                     \
    if (sve_access_check(s)) {                                            \
        do_fp_imm(s, a, val[a->esz - 1][a->imm], fns[a->esz - 1]);        \
    }                                                                     \
    return true;                                                          \
}

DO_FP_IMM(FADD, fadds, half, one)
DO_FP_IMM(FSUB, fsubs, half, one)
DO_FP_IMM(FMUL, fmuls, half, two)
DO_FP_IMM(FSUBR, fsubrs, half, one)
DO_FP_IMM(FMAXNM, fmaxnms, zero, one)
DO_FP_IMM(FMINNM, fminnms, zero, one)
DO_FP_IMM(FMAX, fmaxs, zero, one)
DO_FP_IMM(FMIN, fmins, zero, one)

#undef DO_FP_IMM

static bool do_fp_cmp(DisasContext *s, arg_rprr_esz *a,
                      gen_helper_gvec_4_ptr *fn)
{
    if (fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_ptr status = fpstatus_ptr(a->esz == MO_16 ? FPST_FPCR_F16 : FPST_FPCR);
        tcg_gen_gvec_4_ptr(pred_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           pred_full_reg_offset(s, a->pg),
                           status, vsz, vsz, 0, fn);
        tcg_temp_free_ptr(status);
    }
    return true;
}

#define DO_FPCMP(NAME, name) \
static bool trans_##NAME##_ppzz(DisasContext *s, arg_rprr_esz *a)     \
{                                                                     \
    static gen_helper_gvec_4_ptr * const fns[4] = {                   \
        NULL, gen_helper_sve_##name##_h,                              \
        gen_helper_sve_##name##_s, gen_helper_sve_##name##_d          \
    };                                                                \
    return do_fp_cmp(s, a, fns[a->esz]);                              \
}

DO_FPCMP(FCMGE, fcmge)
DO_FPCMP(FCMGT, fcmgt)
DO_FPCMP(FCMEQ, fcmeq)
DO_FPCMP(FCMNE, fcmne)
DO_FPCMP(FCMUO, fcmuo)
DO_FPCMP(FACGE, facge)
DO_FPCMP(FACGT, facgt)

#undef DO_FPCMP

static bool trans_FCADD(DisasContext *s, arg_FCADD *a)
{
    static gen_helper_gvec_4_ptr * const fns[3] = {
        gen_helper_sve_fcadd_h,
        gen_helper_sve_fcadd_s,
        gen_helper_sve_fcadd_d
    };

    if (a->esz == 0) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_ptr status = fpstatus_ptr(a->esz == MO_16 ? FPST_FPCR_F16 : FPST_FPCR);
        tcg_gen_gvec_4_ptr(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           pred_full_reg_offset(s, a->pg),
                           status, vsz, vsz, a->rot, fns[a->esz - 1]);
        tcg_temp_free_ptr(status);
    }
    return true;
}

static bool do_fmla(DisasContext *s, arg_rprrr_esz *a,
                    gen_helper_gvec_5_ptr *fn)
{
    if (a->esz == 0) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_ptr status = fpstatus_ptr(a->esz == MO_16 ? FPST_FPCR_F16 : FPST_FPCR);
        tcg_gen_gvec_5_ptr(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           vec_full_reg_offset(s, a->ra),
                           pred_full_reg_offset(s, a->pg),
                           status, vsz, vsz, 0, fn);
        tcg_temp_free_ptr(status);
    }
    return true;
}

#define DO_FMLA(NAME, name) \
static bool trans_##NAME(DisasContext *s, arg_rprrr_esz *a)          \
{                                                                    \
    static gen_helper_gvec_5_ptr * const fns[4] = {                  \
        NULL, gen_helper_sve_##name##_h,                             \
        gen_helper_sve_##name##_s, gen_helper_sve_##name##_d         \
    };                                                               \
    return do_fmla(s, a, fns[a->esz]);                               \
}

DO_FMLA(FMLA_zpzzz, fmla_zpzzz)
DO_FMLA(FMLS_zpzzz, fmls_zpzzz)
DO_FMLA(FNMLA_zpzzz, fnmla_zpzzz)
DO_FMLA(FNMLS_zpzzz, fnmls_zpzzz)

#undef DO_FMLA

static bool trans_FCMLA_zpzzz(DisasContext *s, arg_FCMLA_zpzzz *a)
{
    static gen_helper_gvec_5_ptr * const fns[4] = {
        NULL,
        gen_helper_sve_fcmla_zpzzz_h,
        gen_helper_sve_fcmla_zpzzz_s,
        gen_helper_sve_fcmla_zpzzz_d,
    };

    if (a->esz == 0) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_ptr status = fpstatus_ptr(a->esz == MO_16 ? FPST_FPCR_F16 : FPST_FPCR);
        tcg_gen_gvec_5_ptr(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           vec_full_reg_offset(s, a->ra),
                           pred_full_reg_offset(s, a->pg),
                           status, vsz, vsz, a->rot, fns[a->esz]);
        tcg_temp_free_ptr(status);
    }
    return true;
}

static bool trans_FCMLA_zzxz(DisasContext *s, arg_FCMLA_zzxz *a)
{
    static gen_helper_gvec_4_ptr * const fns[2] = {
        gen_helper_gvec_fcmlah_idx,
        gen_helper_gvec_fcmlas_idx,
    };

    tcg_debug_assert(a->esz == 1 || a->esz == 2);
    tcg_debug_assert(a->rd == a->ra);
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_ptr status = fpstatus_ptr(a->esz == MO_16 ? FPST_FPCR_F16 : FPST_FPCR);
        tcg_gen_gvec_4_ptr(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           vec_full_reg_offset(s, a->ra),
                           status, vsz, vsz,
                           a->index * 4 + a->rot,
                           fns[a->esz - 1]);
        tcg_temp_free_ptr(status);
    }
    return true;
}

/*
 *** SVE Floating Point Unary Operations Predicated Group
 */

static bool do_zpz_ptr(DisasContext *s, int rd, int rn, int pg,
                       bool is_fp16, gen_helper_gvec_3_ptr *fn)
{
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_ptr status = fpstatus_ptr(is_fp16 ? FPST_FPCR_F16 : FPST_FPCR);
        tcg_gen_gvec_3_ptr(vec_full_reg_offset(s, rd),
                           vec_full_reg_offset(s, rn),
                           pred_full_reg_offset(s, pg),
                           status, vsz, vsz, 0, fn);
        tcg_temp_free_ptr(status);
    }
    return true;
}

static bool trans_FCVT_sh(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_fcvt_sh);
}

static bool trans_FCVT_hs(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_fcvt_hs);
}

static bool trans_BFCVT(DisasContext *s, arg_rpr_esz *a)
{
    if (!dc_isar_feature(aa64_sve_bf16, s)) {
        return false;
    }
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_bfcvt);
}

static bool trans_FCVT_dh(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_fcvt_dh);
}

static bool trans_FCVT_hd(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_fcvt_hd);
}

static bool trans_FCVT_ds(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_fcvt_ds);
}

static bool trans_FCVT_sd(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_fcvt_sd);
}

static bool trans_FCVTZS_hh(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, true, gen_helper_sve_fcvtzs_hh);
}

static bool trans_FCVTZU_hh(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, true, gen_helper_sve_fcvtzu_hh);
}

static bool trans_FCVTZS_hs(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, true, gen_helper_sve_fcvtzs_hs);
}

static bool trans_FCVTZU_hs(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, true, gen_helper_sve_fcvtzu_hs);
}

static bool trans_FCVTZS_hd(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, true, gen_helper_sve_fcvtzs_hd);
}

static bool trans_FCVTZU_hd(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, true, gen_helper_sve_fcvtzu_hd);
}

static bool trans_FCVTZS_ss(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_fcvtzs_ss);
}

static bool trans_FCVTZU_ss(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_fcvtzu_ss);
}

static bool trans_FCVTZS_sd(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_fcvtzs_sd);
}

static bool trans_FCVTZU_sd(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_fcvtzu_sd);
}

static bool trans_FCVTZS_ds(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_fcvtzs_ds);
}

static bool trans_FCVTZU_ds(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_fcvtzu_ds);
}

static bool trans_FCVTZS_dd(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_fcvtzs_dd);
}

static bool trans_FCVTZU_dd(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_fcvtzu_dd);
}

static gen_helper_gvec_3_ptr * const frint_fns[3] = {
    gen_helper_sve_frint_h,
    gen_helper_sve_frint_s,
    gen_helper_sve_frint_d
};

static bool trans_FRINTI(DisasContext *s, arg_rpr_esz *a)
{
    if (a->esz == 0) {
        return false;
    }
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, a->esz == MO_16,
                      frint_fns[a->esz - 1]);
}

static bool trans_FRINTX(DisasContext *s, arg_rpr_esz *a)
{
    static gen_helper_gvec_3_ptr * const fns[3] = {
        gen_helper_sve_frintx_h,
        gen_helper_sve_frintx_s,
        gen_helper_sve_frintx_d
    };
    if (a->esz == 0) {
        return false;
    }
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, a->esz == MO_16, fns[a->esz - 1]);
}

static bool do_frint_mode(DisasContext *s, arg_rpr_esz *a,
                          int mode, gen_helper_gvec_3_ptr *fn)
{
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_i32 tmode = tcg_const_i32(mode);
        TCGv_ptr status = fpstatus_ptr(a->esz == MO_16 ? FPST_FPCR_F16 : FPST_FPCR);

        gen_helper_set_rmode(tmode, tmode, status);

        tcg_gen_gvec_3_ptr(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           pred_full_reg_offset(s, a->pg),
                           status, vsz, vsz, 0, fn);

        gen_helper_set_rmode(tmode, tmode, status);
        tcg_temp_free_i32(tmode);
        tcg_temp_free_ptr(status);
    }
    return true;
}

static bool trans_FRINTN(DisasContext *s, arg_rpr_esz *a)
{
    if (a->esz == 0) {
        return false;
    }
    return do_frint_mode(s, a, float_round_nearest_even, frint_fns[a->esz - 1]);
}

static bool trans_FRINTP(DisasContext *s, arg_rpr_esz *a)
{
    if (a->esz == 0) {
        return false;
    }
    return do_frint_mode(s, a, float_round_up, frint_fns[a->esz - 1]);
}

static bool trans_FRINTM(DisasContext *s, arg_rpr_esz *a)
{
    if (a->esz == 0) {
        return false;
    }
    return do_frint_mode(s, a, float_round_down, frint_fns[a->esz - 1]);
}

static bool trans_FRINTZ(DisasContext *s, arg_rpr_esz *a)
{
    if (a->esz == 0) {
        return false;
    }
    return do_frint_mode(s, a, float_round_to_zero, frint_fns[a->esz - 1]);
}

static bool trans_FRINTA(DisasContext *s, arg_rpr_esz *a)
{
    if (a->esz == 0) {
        return false;
    }
    return do_frint_mode(s, a, float_round_ties_away, frint_fns[a->esz - 1]);
}

static bool trans_FRECPX(DisasContext *s, arg_rpr_esz *a)
{
    static gen_helper_gvec_3_ptr * const fns[3] = {
        gen_helper_sve_frecpx_h,
        gen_helper_sve_frecpx_s,
        gen_helper_sve_frecpx_d
    };
    if (a->esz == 0) {
        return false;
    }
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, a->esz == MO_16, fns[a->esz - 1]);
}

static bool trans_FSQRT(DisasContext *s, arg_rpr_esz *a)
{
    static gen_helper_gvec_3_ptr * const fns[3] = {
        gen_helper_sve_fsqrt_h,
        gen_helper_sve_fsqrt_s,
        gen_helper_sve_fsqrt_d
    };
    if (a->esz == 0) {
        return false;
    }
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, a->esz == MO_16, fns[a->esz - 1]);
}

static bool trans_SCVTF_hh(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, true, gen_helper_sve_scvt_hh);
}

static bool trans_SCVTF_sh(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, true, gen_helper_sve_scvt_sh);
}

static bool trans_SCVTF_dh(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, true, gen_helper_sve_scvt_dh);
}

static bool trans_SCVTF_ss(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_scvt_ss);
}

static bool trans_SCVTF_ds(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_scvt_ds);
}

static bool trans_SCVTF_sd(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_scvt_sd);
}

static bool trans_SCVTF_dd(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_scvt_dd);
}

static bool trans_UCVTF_hh(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, true, gen_helper_sve_ucvt_hh);
}

static bool trans_UCVTF_sh(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, true, gen_helper_sve_ucvt_sh);
}

static bool trans_UCVTF_dh(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, true, gen_helper_sve_ucvt_dh);
}

static bool trans_UCVTF_ss(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_ucvt_ss);
}

static bool trans_UCVTF_ds(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_ucvt_ds);
}

static bool trans_UCVTF_sd(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_ucvt_sd);
}

static bool trans_UCVTF_dd(DisasContext *s, arg_rpr_esz *a)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_ucvt_dd);
}

/*
 *** SVE Memory - 32-bit Gather and Unsized Contiguous Group
 */

/* Subroutine loading a vector register at VOFS of LEN bytes.
 * The load should begin at the address Rn + IMM.
 */

static void do_ldr(DisasContext *s, uint32_t vofs, int len, int rn, int imm)
{
    int len_align = QEMU_ALIGN_DOWN(len, 8);
    int len_remain = len % 8;
    int nparts = len / 8 + ctpop8(len_remain);
    int midx = get_mem_index(s);
    TCGv_i64 dirty_addr, clean_addr, t0, t1;

    dirty_addr = tcg_temp_new_i64();
    tcg_gen_addi_i64(dirty_addr, cpu_reg_sp(s, rn), imm);
    clean_addr = gen_mte_checkN(s, dirty_addr, false, rn != 31, len);
    tcg_temp_free_i64(dirty_addr);

    /*
     * Note that unpredicated load/store of vector/predicate registers
     * are defined as a stream of bytes, which equates to little-endian
     * operations on larger quantities.
     * Attempt to keep code expansion to a minimum by limiting the
     * amount of unrolling done.
     */
    if (nparts <= 4) {
        int i;

        t0 = tcg_temp_new_i64();
        for (i = 0; i < len_align; i += 8) {
            tcg_gen_qemu_ld_i64(t0, clean_addr, midx, MO_LEQ);
            tcg_gen_st_i64(t0, cpu_env, vofs + i);
            tcg_gen_addi_i64(clean_addr, clean_addr, 8);
        }
        tcg_temp_free_i64(t0);
    } else {
        TCGLabel *loop = gen_new_label();
        TCGv_ptr tp, i = tcg_const_local_ptr(0);

        /* Copy the clean address into a local temp, live across the loop. */
        t0 = clean_addr;
        clean_addr = new_tmp_a64_local(s);
        tcg_gen_mov_i64(clean_addr, t0);

        gen_set_label(loop);

        t0 = tcg_temp_new_i64();
        tcg_gen_qemu_ld_i64(t0, clean_addr, midx, MO_LEQ);
        tcg_gen_addi_i64(clean_addr, clean_addr, 8);

        tp = tcg_temp_new_ptr();
        tcg_gen_add_ptr(tp, cpu_env, i);
        tcg_gen_addi_ptr(i, i, 8);
        tcg_gen_st_i64(t0, tp, vofs);
        tcg_temp_free_ptr(tp);
        tcg_temp_free_i64(t0);

        tcg_gen_brcondi_ptr(TCG_COND_LTU, i, len_align, loop);
        tcg_temp_free_ptr(i);
    }

    /*
     * Predicate register loads can be any multiple of 2.
     * Note that we still store the entire 64-bit unit into cpu_env.
     */
    if (len_remain) {
        t0 = tcg_temp_new_i64();
        switch (len_remain) {
        case 2:
        case 4:
        case 8:
            tcg_gen_qemu_ld_i64(t0, clean_addr, midx,
                                MO_LE | ctz32(len_remain));
            break;

        case 6:
            t1 = tcg_temp_new_i64();
            tcg_gen_qemu_ld_i64(t0, clean_addr, midx, MO_LEUL);
            tcg_gen_addi_i64(clean_addr, clean_addr, 4);
            tcg_gen_qemu_ld_i64(t1, clean_addr, midx, MO_LEUW);
            tcg_gen_deposit_i64(t0, t0, t1, 32, 32);
            tcg_temp_free_i64(t1);
            break;

        default:
            g_assert_not_reached();
        }
        tcg_gen_st_i64(t0, cpu_env, vofs + len_align);
        tcg_temp_free_i64(t0);
    }
}

/* Similarly for stores.  */
static void do_str(DisasContext *s, uint32_t vofs, int len, int rn, int imm)
{
    int len_align = QEMU_ALIGN_DOWN(len, 8);
    int len_remain = len % 8;
    int nparts = len / 8 + ctpop8(len_remain);
    int midx = get_mem_index(s);
    TCGv_i64 dirty_addr, clean_addr, t0;

    dirty_addr = tcg_temp_new_i64();
    tcg_gen_addi_i64(dirty_addr, cpu_reg_sp(s, rn), imm);
    clean_addr = gen_mte_checkN(s, dirty_addr, false, rn != 31, len);
    tcg_temp_free_i64(dirty_addr);

    /* Note that unpredicated load/store of vector/predicate registers
     * are defined as a stream of bytes, which equates to little-endian
     * operations on larger quantities.  There is no nice way to force
     * a little-endian store for aarch64_be-linux-user out of line.
     *
     * Attempt to keep code expansion to a minimum by limiting the
     * amount of unrolling done.
     */
    if (nparts <= 4) {
        int i;

        t0 = tcg_temp_new_i64();
        for (i = 0; i < len_align; i += 8) {
            tcg_gen_ld_i64(t0, cpu_env, vofs + i);
            tcg_gen_qemu_st_i64(t0, clean_addr, midx, MO_LEQ);
            tcg_gen_addi_i64(clean_addr, clean_addr, 8);
        }
        tcg_temp_free_i64(t0);
    } else {
        TCGLabel *loop = gen_new_label();
        TCGv_ptr tp, i = tcg_const_local_ptr(0);

        /* Copy the clean address into a local temp, live across the loop. */
        t0 = clean_addr;
        clean_addr = new_tmp_a64_local(s);
        tcg_gen_mov_i64(clean_addr, t0);

        gen_set_label(loop);

        t0 = tcg_temp_new_i64();
        tp = tcg_temp_new_ptr();
        tcg_gen_add_ptr(tp, cpu_env, i);
        tcg_gen_ld_i64(t0, tp, vofs);
        tcg_gen_addi_ptr(i, i, 8);
        tcg_temp_free_ptr(tp);

        tcg_gen_qemu_st_i64(t0, clean_addr, midx, MO_LEQ);
        tcg_gen_addi_i64(clean_addr, clean_addr, 8);
        tcg_temp_free_i64(t0);

        tcg_gen_brcondi_ptr(TCG_COND_LTU, i, len_align, loop);
        tcg_temp_free_ptr(i);
    }

    /* Predicate register stores can be any multiple of 2.  */
    if (len_remain) {
        t0 = tcg_temp_new_i64();
        tcg_gen_ld_i64(t0, cpu_env, vofs + len_align);

        switch (len_remain) {
        case 2:
        case 4:
        case 8:
            tcg_gen_qemu_st_i64(t0, clean_addr, midx,
                                MO_LE | ctz32(len_remain));
            break;

        case 6:
            tcg_gen_qemu_st_i64(t0, clean_addr, midx, MO_LEUL);
            tcg_gen_addi_i64(clean_addr, clean_addr, 4);
            tcg_gen_shri_i64(t0, t0, 32);
            tcg_gen_qemu_st_i64(t0, clean_addr, midx, MO_LEUW);
            break;

        default:
            g_assert_not_reached();
        }
        tcg_temp_free_i64(t0);
    }
}

static bool trans_LDR_zri(DisasContext *s, arg_rri *a)
{
    if (sve_access_check(s)) {
        int size = vec_full_reg_size(s);
        int off = vec_full_reg_offset(s, a->rd);
        do_ldr(s, off, size, a->rn, a->imm * size);
    }
    return true;
}

static bool trans_LDR_pri(DisasContext *s, arg_rri *a)
{
    if (sve_access_check(s)) {
        int size = pred_full_reg_size(s);
        int off = pred_full_reg_offset(s, a->rd);
        do_ldr(s, off, size, a->rn, a->imm * size);
    }
    return true;
}

static bool trans_STR_zri(DisasContext *s, arg_rri *a)
{
    if (sve_access_check(s)) {
        int size = vec_full_reg_size(s);
        int off = vec_full_reg_offset(s, a->rd);
        do_str(s, off, size, a->rn, a->imm * size);
    }
    return true;
}

static bool trans_STR_pri(DisasContext *s, arg_rri *a)
{
    if (sve_access_check(s)) {
        int size = pred_full_reg_size(s);
        int off = pred_full_reg_offset(s, a->rd);
        do_str(s, off, size, a->rn, a->imm * size);
    }
    return true;
}

/*
 *** SVE Memory - Contiguous Load Group
 */

/* The memory mode of the dtype.  */
static const MemOp dtype_mop[16] = {
    MO_UB, MO_UB, MO_UB, MO_UB,
    MO_SL, MO_UW, MO_UW, MO_UW,
    MO_SW, MO_SW, MO_UL, MO_UL,
    MO_SB, MO_SB, MO_SB, MO_Q
};

#define dtype_msz(x)  (dtype_mop[x] & MO_SIZE)

/* The vector element size of dtype.  */
static const uint8_t dtype_esz[16] = {
    0, 1, 2, 3,
    3, 1, 2, 3,
    3, 2, 2, 3,
    3, 2, 1, 3
};

static void do_mem_zpa(DisasContext *s, int zt, int pg, TCGv_i64 addr,
                       int dtype, uint32_t mte_n, bool is_write,
                       gen_helper_gvec_mem *fn)
{
    unsigned vsz = vec_full_reg_size(s);
    TCGv_ptr t_pg;
    TCGv_i32 t_desc;
    int desc = 0;

    /*
     * For e.g. LD4, there are not enough arguments to pass all 4
     * registers as pointers, so encode the regno into the data field.
     * For consistency, do this even for LD1.
     */
    if (s->mte_active[0]) {
        int msz = dtype_msz(dtype);

        desc = FIELD_DP32(desc, MTEDESC, MIDX, get_mem_index(s));
        desc = FIELD_DP32(desc, MTEDESC, TBI, s->tbid);
        desc = FIELD_DP32(desc, MTEDESC, TCMA, s->tcma);
        desc = FIELD_DP32(desc, MTEDESC, WRITE, is_write);
        desc = FIELD_DP32(desc, MTEDESC, SIZEM1, (mte_n << msz) - 1);
        desc <<= SVE_MTEDESC_SHIFT;
    } else {
        addr = clean_data_tbi(s, addr);
    }

    desc = simd_desc(vsz, vsz, zt | desc);
    t_desc = tcg_const_i32(desc);
    t_pg = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(t_pg, cpu_env, pred_full_reg_offset(s, pg));
    fn(cpu_env, t_pg, addr, t_desc);

    tcg_temp_free_ptr(t_pg);
    tcg_temp_free_i32(t_desc);
}

/* Indexed by [mte][be][dtype][nreg] */
static gen_helper_gvec_mem * const ldr_fns[2][2][16][4] = {
    { /* mte inactive, little-endian */
      { { gen_helper_sve_ld1bb_r, gen_helper_sve_ld2bb_r,
          gen_helper_sve_ld3bb_r, gen_helper_sve_ld4bb_r },
        { gen_helper_sve_ld1bhu_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1bsu_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1bdu_r, NULL, NULL, NULL },

        { gen_helper_sve_ld1sds_le_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1hh_le_r, gen_helper_sve_ld2hh_le_r,
          gen_helper_sve_ld3hh_le_r, gen_helper_sve_ld4hh_le_r },
        { gen_helper_sve_ld1hsu_le_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1hdu_le_r, NULL, NULL, NULL },

        { gen_helper_sve_ld1hds_le_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1hss_le_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1ss_le_r, gen_helper_sve_ld2ss_le_r,
          gen_helper_sve_ld3ss_le_r, gen_helper_sve_ld4ss_le_r },
        { gen_helper_sve_ld1sdu_le_r, NULL, NULL, NULL },

        { gen_helper_sve_ld1bds_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1bss_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1bhs_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1dd_le_r, gen_helper_sve_ld2dd_le_r,
          gen_helper_sve_ld3dd_le_r, gen_helper_sve_ld4dd_le_r } },

      /* mte inactive, big-endian */
      { { gen_helper_sve_ld1bb_r, gen_helper_sve_ld2bb_r,
          gen_helper_sve_ld3bb_r, gen_helper_sve_ld4bb_r },
        { gen_helper_sve_ld1bhu_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1bsu_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1bdu_r, NULL, NULL, NULL },

        { gen_helper_sve_ld1sds_be_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1hh_be_r, gen_helper_sve_ld2hh_be_r,
          gen_helper_sve_ld3hh_be_r, gen_helper_sve_ld4hh_be_r },
        { gen_helper_sve_ld1hsu_be_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1hdu_be_r, NULL, NULL, NULL },

        { gen_helper_sve_ld1hds_be_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1hss_be_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1ss_be_r, gen_helper_sve_ld2ss_be_r,
          gen_helper_sve_ld3ss_be_r, gen_helper_sve_ld4ss_be_r },
        { gen_helper_sve_ld1sdu_be_r, NULL, NULL, NULL },

        { gen_helper_sve_ld1bds_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1bss_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1bhs_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1dd_be_r, gen_helper_sve_ld2dd_be_r,
          gen_helper_sve_ld3dd_be_r, gen_helper_sve_ld4dd_be_r } } },

    { /* mte active, little-endian */
      { { gen_helper_sve_ld1bb_r_mte,
          gen_helper_sve_ld2bb_r_mte,
          gen_helper_sve_ld3bb_r_mte,
          gen_helper_sve_ld4bb_r_mte },
        { gen_helper_sve_ld1bhu_r_mte, NULL, NULL, NULL },
        { gen_helper_sve_ld1bsu_r_mte, NULL, NULL, NULL },
        { gen_helper_sve_ld1bdu_r_mte, NULL, NULL, NULL },

        { gen_helper_sve_ld1sds_le_r_mte, NULL, NULL, NULL },
        { gen_helper_sve_ld1hh_le_r_mte,
          gen_helper_sve_ld2hh_le_r_mte,
          gen_helper_sve_ld3hh_le_r_mte,
          gen_helper_sve_ld4hh_le_r_mte },
        { gen_helper_sve_ld1hsu_le_r_mte, NULL, NULL, NULL },
        { gen_helper_sve_ld1hdu_le_r_mte, NULL, NULL, NULL },

        { gen_helper_sve_ld1hds_le_r_mte, NULL, NULL, NULL },
        { gen_helper_sve_ld1hss_le_r_mte, NULL, NULL, NULL },
        { gen_helper_sve_ld1ss_le_r_mte,
          gen_helper_sve_ld2ss_le_r_mte,
          gen_helper_sve_ld3ss_le_r_mte,
          gen_helper_sve_ld4ss_le_r_mte },
        { gen_helper_sve_ld1sdu_le_r_mte, NULL, NULL, NULL },

        { gen_helper_sve_ld1bds_r_mte, NULL, NULL, NULL },
        { gen_helper_sve_ld1bss_r_mte, NULL, NULL, NULL },
        { gen_helper_sve_ld1bhs_r_mte, NULL, NULL, NULL },
        { gen_helper_sve_ld1dd_le_r_mte,
          gen_helper_sve_ld2dd_le_r_mte,
          gen_helper_sve_ld3dd_le_r_mte,
          gen_helper_sve_ld4dd_le_r_mte } },

      /* mte active, big-endian */
      { { gen_helper_sve_ld1bb_r_mte,
          gen_helper_sve_ld2bb_r_mte,
          gen_helper_sve_ld3bb_r_mte,
          gen_helper_sve_ld4bb_r_mte },
        { gen_helper_sve_ld1bhu_r_mte, NULL, NULL, NULL },
        { gen_helper_sve_ld1bsu_r_mte, NULL, NULL, NULL },
        { gen_helper_sve_ld1bdu_r_mte, NULL, NULL, NULL },

        { gen_helper_sve_ld1sds_be_r_mte, NULL, NULL, NULL },
        { gen_helper_sve_ld1hh_be_r_mte,
          gen_helper_sve_ld2hh_be_r_mte,
          gen_helper_sve_ld3hh_be_r_mte,
          gen_helper_sve_ld4hh_be_r_mte },
        { gen_helper_sve_ld1hsu_be_r_mte, NULL, NULL, NULL },
        { gen_helper_sve_ld1hdu_be_r_mte, NULL, NULL, NULL },

        { gen_helper_sve_ld1hds_be_r_mte, NULL, NULL, NULL },
        { gen_helper_sve_ld1hss_be_r_mte, NULL, NULL, NULL },
        { gen_helper_sve_ld1ss_be_r_mte,
          gen_helper_sve_ld2ss_be_r_mte,
          gen_helper_sve_ld3ss_be_r_mte,
          gen_helper_sve_ld4ss_be_r_mte },
        { gen_helper_sve_ld1sdu_be_r_mte, NULL, NULL, NULL },

        { gen_helper_sve_ld1bds_r_mte, NULL, NULL, NULL },
        { gen_helper_sve_ld1bss_r_mte, NULL, NULL, NULL },
        { gen_helper_sve_ld1bhs_r_mte, NULL, NULL, NULL },
        { gen_helper_sve_ld1dd_be_r_mte,
          gen_helper_sve_ld2dd_be_r_mte,
          gen_helper_sve_ld3dd_be_r_mte,
          gen_helper_sve_ld4dd_be_r_mte } } },
};

static void do_ld_zpa(DisasContext *s, int zt, int pg,
                      TCGv_i64 addr, int dtype, int nreg)
{
    gen_helper_gvec_mem *fn
        = ldr_fns[s->mte_active[0]][s->be_data == MO_BE][dtype][nreg];

    /*
     * While there are holes in the table, they are not
     * accessible via the instruction encoding.
     */
    assert(fn != NULL);
    do_mem_zpa(s, zt, pg, addr, dtype, nreg, false, fn);
}

static bool trans_LD_zprr(DisasContext *s, arg_rprr_load *a)
{
    if (a->rm == 31) {
        return false;
    }
    if (sve_access_check(s)) {
        TCGv_i64 addr = new_tmp_a64(s);
        tcg_gen_shli_i64(addr, cpu_reg(s, a->rm), dtype_msz(a->dtype));
        tcg_gen_add_i64(addr, addr, cpu_reg_sp(s, a->rn));
        do_ld_zpa(s, a->rd, a->pg, addr, a->dtype, a->nreg);
    }
    return true;
}

static bool trans_LD_zpri(DisasContext *s, arg_rpri_load *a)
{
    if (sve_access_check(s)) {
        int vsz = vec_full_reg_size(s);
        int elements = vsz >> dtype_esz[a->dtype];
        TCGv_i64 addr = new_tmp_a64(s);

        tcg_gen_addi_i64(addr, cpu_reg_sp(s, a->rn),
                         (a->imm * elements * (a->nreg + 1))
                         << dtype_msz(a->dtype));
        do_ld_zpa(s, a->rd, a->pg, addr, a->dtype, a->nreg);
    }
    return true;
}

static bool trans_LDFF1_zprr(DisasContext *s, arg_rprr_load *a)
{
    static gen_helper_gvec_mem * const fns[2][2][16] = {
        { /* mte inactive, little-endian */
          { gen_helper_sve_ldff1bb_r,
            gen_helper_sve_ldff1bhu_r,
            gen_helper_sve_ldff1bsu_r,
            gen_helper_sve_ldff1bdu_r,

            gen_helper_sve_ldff1sds_le_r,
            gen_helper_sve_ldff1hh_le_r,
            gen_helper_sve_ldff1hsu_le_r,
            gen_helper_sve_ldff1hdu_le_r,

            gen_helper_sve_ldff1hds_le_r,
            gen_helper_sve_ldff1hss_le_r,
            gen_helper_sve_ldff1ss_le_r,
            gen_helper_sve_ldff1sdu_le_r,

            gen_helper_sve_ldff1bds_r,
            gen_helper_sve_ldff1bss_r,
            gen_helper_sve_ldff1bhs_r,
            gen_helper_sve_ldff1dd_le_r },

          /* mte inactive, big-endian */
          { gen_helper_sve_ldff1bb_r,
            gen_helper_sve_ldff1bhu_r,
            gen_helper_sve_ldff1bsu_r,
            gen_helper_sve_ldff1bdu_r,

            gen_helper_sve_ldff1sds_be_r,
            gen_helper_sve_ldff1hh_be_r,
            gen_helper_sve_ldff1hsu_be_r,
            gen_helper_sve_ldff1hdu_be_r,

            gen_helper_sve_ldff1hds_be_r,
            gen_helper_sve_ldff1hss_be_r,
            gen_helper_sve_ldff1ss_be_r,
            gen_helper_sve_ldff1sdu_be_r,

            gen_helper_sve_ldff1bds_r,
            gen_helper_sve_ldff1bss_r,
            gen_helper_sve_ldff1bhs_r,
            gen_helper_sve_ldff1dd_be_r } },

        { /* mte active, little-endian */
          { gen_helper_sve_ldff1bb_r_mte,
            gen_helper_sve_ldff1bhu_r_mte,
            gen_helper_sve_ldff1bsu_r_mte,
            gen_helper_sve_ldff1bdu_r_mte,

            gen_helper_sve_ldff1sds_le_r_mte,
            gen_helper_sve_ldff1hh_le_r_mte,
            gen_helper_sve_ldff1hsu_le_r_mte,
            gen_helper_sve_ldff1hdu_le_r_mte,

            gen_helper_sve_ldff1hds_le_r_mte,
            gen_helper_sve_ldff1hss_le_r_mte,
            gen_helper_sve_ldff1ss_le_r_mte,
            gen_helper_sve_ldff1sdu_le_r_mte,

            gen_helper_sve_ldff1bds_r_mte,
            gen_helper_sve_ldff1bss_r_mte,
            gen_helper_sve_ldff1bhs_r_mte,
            gen_helper_sve_ldff1dd_le_r_mte },

          /* mte active, big-endian */
          { gen_helper_sve_ldff1bb_r_mte,
            gen_helper_sve_ldff1bhu_r_mte,
            gen_helper_sve_ldff1bsu_r_mte,
            gen_helper_sve_ldff1bdu_r_mte,

            gen_helper_sve_ldff1sds_be_r_mte,
            gen_helper_sve_ldff1hh_be_r_mte,
            gen_helper_sve_ldff1hsu_be_r_mte,
            gen_helper_sve_ldff1hdu_be_r_mte,

            gen_helper_sve_ldff1hds_be_r_mte,
            gen_helper_sve_ldff1hss_be_r_mte,
            gen_helper_sve_ldff1ss_be_r_mte,
            gen_helper_sve_ldff1sdu_be_r_mte,

            gen_helper_sve_ldff1bds_r_mte,
            gen_helper_sve_ldff1bss_r_mte,
            gen_helper_sve_ldff1bhs_r_mte,
            gen_helper_sve_ldff1dd_be_r_mte } },
    };

    if (sve_access_check(s)) {
        TCGv_i64 addr = new_tmp_a64(s);
        tcg_gen_shli_i64(addr, cpu_reg(s, a->rm), dtype_msz(a->dtype));
        tcg_gen_add_i64(addr, addr, cpu_reg_sp(s, a->rn));
        do_mem_zpa(s, a->rd, a->pg, addr, a->dtype, 1, false,
                   fns[s->mte_active[0]][s->be_data == MO_BE][a->dtype]);
    }
    return true;
}

static bool trans_LDNF1_zpri(DisasContext *s, arg_rpri_load *a)
{
    static gen_helper_gvec_mem * const fns[2][2][16] = {
        { /* mte inactive, little-endian */
          { gen_helper_sve_ldnf1bb_r,
            gen_helper_sve_ldnf1bhu_r,
            gen_helper_sve_ldnf1bsu_r,
            gen_helper_sve_ldnf1bdu_r,

            gen_helper_sve_ldnf1sds_le_r,
            gen_helper_sve_ldnf1hh_le_r,
            gen_helper_sve_ldnf1hsu_le_r,
            gen_helper_sve_ldnf1hdu_le_r,

            gen_helper_sve_ldnf1hds_le_r,
            gen_helper_sve_ldnf1hss_le_r,
            gen_helper_sve_ldnf1ss_le_r,
            gen_helper_sve_ldnf1sdu_le_r,

            gen_helper_sve_ldnf1bds_r,
            gen_helper_sve_ldnf1bss_r,
            gen_helper_sve_ldnf1bhs_r,
            gen_helper_sve_ldnf1dd_le_r },

          /* mte inactive, big-endian */
          { gen_helper_sve_ldnf1bb_r,
            gen_helper_sve_ldnf1bhu_r,
            gen_helper_sve_ldnf1bsu_r,
            gen_helper_sve_ldnf1bdu_r,

            gen_helper_sve_ldnf1sds_be_r,
            gen_helper_sve_ldnf1hh_be_r,
            gen_helper_sve_ldnf1hsu_be_r,
            gen_helper_sve_ldnf1hdu_be_r,

            gen_helper_sve_ldnf1hds_be_r,
            gen_helper_sve_ldnf1hss_be_r,
            gen_helper_sve_ldnf1ss_be_r,
            gen_helper_sve_ldnf1sdu_be_r,

            gen_helper_sve_ldnf1bds_r,
            gen_helper_sve_ldnf1bss_r,
            gen_helper_sve_ldnf1bhs_r,
            gen_helper_sve_ldnf1dd_be_r } },

        { /* mte inactive, little-endian */
          { gen_helper_sve_ldnf1bb_r_mte,
            gen_helper_sve_ldnf1bhu_r_mte,
            gen_helper_sve_ldnf1bsu_r_mte,
            gen_helper_sve_ldnf1bdu_r_mte,

            gen_helper_sve_ldnf1sds_le_r_mte,
            gen_helper_sve_ldnf1hh_le_r_mte,
            gen_helper_sve_ldnf1hsu_le_r_mte,
            gen_helper_sve_ldnf1hdu_le_r_mte,

            gen_helper_sve_ldnf1hds_le_r_mte,
            gen_helper_sve_ldnf1hss_le_r_mte,
            gen_helper_sve_ldnf1ss_le_r_mte,
            gen_helper_sve_ldnf1sdu_le_r_mte,

            gen_helper_sve_ldnf1bds_r_mte,
            gen_helper_sve_ldnf1bss_r_mte,
            gen_helper_sve_ldnf1bhs_r_mte,
            gen_helper_sve_ldnf1dd_le_r_mte },

          /* mte inactive, big-endian */
          { gen_helper_sve_ldnf1bb_r_mte,
            gen_helper_sve_ldnf1bhu_r_mte,
            gen_helper_sve_ldnf1bsu_r_mte,
            gen_helper_sve_ldnf1bdu_r_mte,

            gen_helper_sve_ldnf1sds_be_r_mte,
            gen_helper_sve_ldnf1hh_be_r_mte,
            gen_helper_sve_ldnf1hsu_be_r_mte,
            gen_helper_sve_ldnf1hdu_be_r_mte,

            gen_helper_sve_ldnf1hds_be_r_mte,
            gen_helper_sve_ldnf1hss_be_r_mte,
            gen_helper_sve_ldnf1ss_be_r_mte,
            gen_helper_sve_ldnf1sdu_be_r_mte,

            gen_helper_sve_ldnf1bds_r_mte,
            gen_helper_sve_ldnf1bss_r_mte,
            gen_helper_sve_ldnf1bhs_r_mte,
            gen_helper_sve_ldnf1dd_be_r_mte } },
    };

    if (sve_access_check(s)) {
        int vsz = vec_full_reg_size(s);
        int elements = vsz >> dtype_esz[a->dtype];
        int off = (a->imm * elements) << dtype_msz(a->dtype);
        TCGv_i64 addr = new_tmp_a64(s);

        tcg_gen_addi_i64(addr, cpu_reg_sp(s, a->rn), off);
        do_mem_zpa(s, a->rd, a->pg, addr, a->dtype, 1, false,
                   fns[s->mte_active[0]][s->be_data == MO_BE][a->dtype]);
    }
    return true;
}

static void do_ldrq(DisasContext *s, int zt, int pg, TCGv_i64 addr, int dtype)
{
    unsigned vsz = vec_full_reg_size(s);
    TCGv_ptr t_pg;
    int poff;

    /* Load the first quadword using the normal predicated load helpers.  */
    poff = pred_full_reg_offset(s, pg);
    if (vsz > 16) {
        /*
         * Zero-extend the first 16 bits of the predicate into a temporary.
         * This avoids triggering an assert making sure we don't have bits
         * set within a predicate beyond VQ, but we have lowered VQ to 1
         * for this load operation.
         */
        TCGv_i64 tmp = tcg_temp_new_i64();
#ifdef HOST_WORDS_BIGENDIAN
        poff += 6;
#endif
        tcg_gen_ld16u_i64(tmp, cpu_env, poff);

        poff = offsetof(CPUARMState, vfp.preg_tmp);
        tcg_gen_st_i64(tmp, cpu_env, poff);
        tcg_temp_free_i64(tmp);
    }

    t_pg = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(t_pg, cpu_env, poff);

    gen_helper_gvec_mem *fn
        = ldr_fns[s->mte_active[0]][s->be_data == MO_BE][dtype][0];
    fn(cpu_env, t_pg, addr, tcg_constant_i32(simd_desc(16, 16, zt)));

    tcg_temp_free_ptr(t_pg);

    /* Replicate that first quadword.  */
    if (vsz > 16) {
        int doff = vec_full_reg_offset(s, zt);
        tcg_gen_gvec_dup_mem(4, doff + 16, doff, vsz - 16, vsz - 16);
    }
}

static bool trans_LD1RQ_zprr(DisasContext *s, arg_rprr_load *a)
{
    if (a->rm == 31) {
        return false;
    }
    if (sve_access_check(s)) {
        int msz = dtype_msz(a->dtype);
        TCGv_i64 addr = new_tmp_a64(s);
        tcg_gen_shli_i64(addr, cpu_reg(s, a->rm), msz);
        tcg_gen_add_i64(addr, addr, cpu_reg_sp(s, a->rn));
        do_ldrq(s, a->rd, a->pg, addr, a->dtype);
    }
    return true;
}

static bool trans_LD1RQ_zpri(DisasContext *s, arg_rpri_load *a)
{
    if (sve_access_check(s)) {
        TCGv_i64 addr = new_tmp_a64(s);
        tcg_gen_addi_i64(addr, cpu_reg_sp(s, a->rn), a->imm * 16);
        do_ldrq(s, a->rd, a->pg, addr, a->dtype);
    }
    return true;
}

static void do_ldro(DisasContext *s, int zt, int pg, TCGv_i64 addr, int dtype)
{
    unsigned vsz = vec_full_reg_size(s);
    unsigned vsz_r32;
    TCGv_ptr t_pg;
    int poff, doff;

    if (vsz < 32) {
        /*
         * Note that this UNDEFINED check comes after CheckSVEEnabled()
         * in the ARM pseudocode, which is the sve_access_check() done
         * in our caller.  We should not now return false from the caller.
         */
        unallocated_encoding(s);
        return;
    }

    /* Load the first octaword using the normal predicated load helpers.  */

    poff = pred_full_reg_offset(s, pg);
    if (vsz > 32) {
        /*
         * Zero-extend the first 32 bits of the predicate into a temporary.
         * This avoids triggering an assert making sure we don't have bits
         * set within a predicate beyond VQ, but we have lowered VQ to 2
         * for this load operation.
         */
        TCGv_i64 tmp = tcg_temp_new_i64();
#ifdef HOST_WORDS_BIGENDIAN
        poff += 4;
#endif
        tcg_gen_ld32u_i64(tmp, cpu_env, poff);

        poff = offsetof(CPUARMState, vfp.preg_tmp);
        tcg_gen_st_i64(tmp, cpu_env, poff);
        tcg_temp_free_i64(tmp);
    }

    t_pg = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(t_pg, cpu_env, poff);

    gen_helper_gvec_mem *fn
        = ldr_fns[s->mte_active[0]][s->be_data == MO_BE][dtype][0];
    fn(cpu_env, t_pg, addr, tcg_constant_i32(simd_desc(32, 32, zt)));

    tcg_temp_free_ptr(t_pg);

    /*
     * Replicate that first octaword.
     * The replication happens in units of 32; if the full vector size
     * is not a multiple of 32, the final bits are zeroed.
     */
    doff = vec_full_reg_offset(s, zt);
    vsz_r32 = QEMU_ALIGN_DOWN(vsz, 32);
    if (vsz >= 64) {
        tcg_gen_gvec_dup_mem(5, doff + 32, doff, vsz_r32 - 32, vsz_r32 - 32);
    }
    vsz -= vsz_r32;
    if (vsz) {
        tcg_gen_gvec_dup_imm(MO_64, doff + vsz_r32, vsz, vsz, 0);
    }
}

static bool trans_LD1RO_zprr(DisasContext *s, arg_rprr_load *a)
{
    if (!dc_isar_feature(aa64_sve_f64mm, s)) {
        return false;
    }
    if (a->rm == 31) {
        return false;
    }
    if (sve_access_check(s)) {
        TCGv_i64 addr = new_tmp_a64(s);
        tcg_gen_shli_i64(addr, cpu_reg(s, a->rm), dtype_msz(a->dtype));
        tcg_gen_add_i64(addr, addr, cpu_reg_sp(s, a->rn));
        do_ldro(s, a->rd, a->pg, addr, a->dtype);
    }
    return true;
}

static bool trans_LD1RO_zpri(DisasContext *s, arg_rpri_load *a)
{
    if (!dc_isar_feature(aa64_sve_f64mm, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        TCGv_i64 addr = new_tmp_a64(s);
        tcg_gen_addi_i64(addr, cpu_reg_sp(s, a->rn), a->imm * 32);
        do_ldro(s, a->rd, a->pg, addr, a->dtype);
    }
    return true;
}

/* Load and broadcast element.  */
static bool trans_LD1R_zpri(DisasContext *s, arg_rpri_load *a)
{
    unsigned vsz = vec_full_reg_size(s);
    unsigned psz = pred_full_reg_size(s);
    unsigned esz = dtype_esz[a->dtype];
    unsigned msz = dtype_msz(a->dtype);
    TCGLabel *over;
    TCGv_i64 temp, clean_addr;

    if (!sve_access_check(s)) {
        return true;
    }

    over = gen_new_label();

    /* If the guarding predicate has no bits set, no load occurs.  */
    if (psz <= 8) {
        /* Reduce the pred_esz_masks value simply to reduce the
         * size of the code generated here.
         */
        uint64_t psz_mask = MAKE_64BIT_MASK(0, psz * 8);
        temp = tcg_temp_new_i64();
        tcg_gen_ld_i64(temp, cpu_env, pred_full_reg_offset(s, a->pg));
        tcg_gen_andi_i64(temp, temp, pred_esz_masks[esz] & psz_mask);
        tcg_gen_brcondi_i64(TCG_COND_EQ, temp, 0, over);
        tcg_temp_free_i64(temp);
    } else {
        TCGv_i32 t32 = tcg_temp_new_i32();
        find_last_active(s, t32, esz, a->pg);
        tcg_gen_brcondi_i32(TCG_COND_LT, t32, 0, over);
        tcg_temp_free_i32(t32);
    }

    /* Load the data.  */
    temp = tcg_temp_new_i64();
    tcg_gen_addi_i64(temp, cpu_reg_sp(s, a->rn), a->imm << msz);
    clean_addr = gen_mte_check1(s, temp, false, true, msz);

    tcg_gen_qemu_ld_i64(temp, clean_addr, get_mem_index(s),
                        finalize_memop(s, dtype_mop[a->dtype]));

    /* Broadcast to *all* elements.  */
    tcg_gen_gvec_dup_i64(esz, vec_full_reg_offset(s, a->rd),
                         vsz, vsz, temp);
    tcg_temp_free_i64(temp);

    /* Zero the inactive elements.  */
    gen_set_label(over);
    return do_movz_zpz(s, a->rd, a->rd, a->pg, esz, false);
}

static void do_st_zpa(DisasContext *s, int zt, int pg, TCGv_i64 addr,
                      int msz, int esz, int nreg)
{
    static gen_helper_gvec_mem * const fn_single[2][2][4][4] = {
        { { { gen_helper_sve_st1bb_r,
              gen_helper_sve_st1bh_r,
              gen_helper_sve_st1bs_r,
              gen_helper_sve_st1bd_r },
            { NULL,
              gen_helper_sve_st1hh_le_r,
              gen_helper_sve_st1hs_le_r,
              gen_helper_sve_st1hd_le_r },
            { NULL, NULL,
              gen_helper_sve_st1ss_le_r,
              gen_helper_sve_st1sd_le_r },
            { NULL, NULL, NULL,
              gen_helper_sve_st1dd_le_r } },
          { { gen_helper_sve_st1bb_r,
              gen_helper_sve_st1bh_r,
              gen_helper_sve_st1bs_r,
              gen_helper_sve_st1bd_r },
            { NULL,
              gen_helper_sve_st1hh_be_r,
              gen_helper_sve_st1hs_be_r,
              gen_helper_sve_st1hd_be_r },
            { NULL, NULL,
              gen_helper_sve_st1ss_be_r,
              gen_helper_sve_st1sd_be_r },
            { NULL, NULL, NULL,
              gen_helper_sve_st1dd_be_r } } },

        { { { gen_helper_sve_st1bb_r_mte,
              gen_helper_sve_st1bh_r_mte,
              gen_helper_sve_st1bs_r_mte,
              gen_helper_sve_st1bd_r_mte },
            { NULL,
              gen_helper_sve_st1hh_le_r_mte,
              gen_helper_sve_st1hs_le_r_mte,
              gen_helper_sve_st1hd_le_r_mte },
            { NULL, NULL,
              gen_helper_sve_st1ss_le_r_mte,
              gen_helper_sve_st1sd_le_r_mte },
            { NULL, NULL, NULL,
              gen_helper_sve_st1dd_le_r_mte } },
          { { gen_helper_sve_st1bb_r_mte,
              gen_helper_sve_st1bh_r_mte,
              gen_helper_sve_st1bs_r_mte,
              gen_helper_sve_st1bd_r_mte },
            { NULL,
              gen_helper_sve_st1hh_be_r_mte,
              gen_helper_sve_st1hs_be_r_mte,
              gen_helper_sve_st1hd_be_r_mte },
            { NULL, NULL,
              gen_helper_sve_st1ss_be_r_mte,
              gen_helper_sve_st1sd_be_r_mte },
            { NULL, NULL, NULL,
              gen_helper_sve_st1dd_be_r_mte } } },
    };
    static gen_helper_gvec_mem * const fn_multiple[2][2][3][4] = {
        { { { gen_helper_sve_st2bb_r,
              gen_helper_sve_st2hh_le_r,
              gen_helper_sve_st2ss_le_r,
              gen_helper_sve_st2dd_le_r },
            { gen_helper_sve_st3bb_r,
              gen_helper_sve_st3hh_le_r,
              gen_helper_sve_st3ss_le_r,
              gen_helper_sve_st3dd_le_r },
            { gen_helper_sve_st4bb_r,
              gen_helper_sve_st4hh_le_r,
              gen_helper_sve_st4ss_le_r,
              gen_helper_sve_st4dd_le_r } },
          { { gen_helper_sve_st2bb_r,
              gen_helper_sve_st2hh_be_r,
              gen_helper_sve_st2ss_be_r,
              gen_helper_sve_st2dd_be_r },
            { gen_helper_sve_st3bb_r,
              gen_helper_sve_st3hh_be_r,
              gen_helper_sve_st3ss_be_r,
              gen_helper_sve_st3dd_be_r },
            { gen_helper_sve_st4bb_r,
              gen_helper_sve_st4hh_be_r,
              gen_helper_sve_st4ss_be_r,
              gen_helper_sve_st4dd_be_r } } },
        { { { gen_helper_sve_st2bb_r_mte,
              gen_helper_sve_st2hh_le_r_mte,
              gen_helper_sve_st2ss_le_r_mte,
              gen_helper_sve_st2dd_le_r_mte },
            { gen_helper_sve_st3bb_r_mte,
              gen_helper_sve_st3hh_le_r_mte,
              gen_helper_sve_st3ss_le_r_mte,
              gen_helper_sve_st3dd_le_r_mte },
            { gen_helper_sve_st4bb_r_mte,
              gen_helper_sve_st4hh_le_r_mte,
              gen_helper_sve_st4ss_le_r_mte,
              gen_helper_sve_st4dd_le_r_mte } },
          { { gen_helper_sve_st2bb_r_mte,
              gen_helper_sve_st2hh_be_r_mte,
              gen_helper_sve_st2ss_be_r_mte,
              gen_helper_sve_st2dd_be_r_mte },
            { gen_helper_sve_st3bb_r_mte,
              gen_helper_sve_st3hh_be_r_mte,
              gen_helper_sve_st3ss_be_r_mte,
              gen_helper_sve_st3dd_be_r_mte },
            { gen_helper_sve_st4bb_r_mte,
              gen_helper_sve_st4hh_be_r_mte,
              gen_helper_sve_st4ss_be_r_mte,
              gen_helper_sve_st4dd_be_r_mte } } },
    };
    gen_helper_gvec_mem *fn;
    int be = s->be_data == MO_BE;

    if (nreg == 0) {
        /* ST1 */
        fn = fn_single[s->mte_active[0]][be][msz][esz];
        nreg = 1;
    } else {
        /* ST2, ST3, ST4 -- msz == esz, enforced by encoding */
        assert(msz == esz);
        fn = fn_multiple[s->mte_active[0]][be][nreg - 1][msz];
    }
    assert(fn != NULL);
    do_mem_zpa(s, zt, pg, addr, msz_dtype(s, msz), nreg, true, fn);
}

static bool trans_ST_zprr(DisasContext *s, arg_rprr_store *a)
{
    if (a->rm == 31 || a->msz > a->esz) {
        return false;
    }
    if (sve_access_check(s)) {
        TCGv_i64 addr = new_tmp_a64(s);
        tcg_gen_shli_i64(addr, cpu_reg(s, a->rm), a->msz);
        tcg_gen_add_i64(addr, addr, cpu_reg_sp(s, a->rn));
        do_st_zpa(s, a->rd, a->pg, addr, a->msz, a->esz, a->nreg);
    }
    return true;
}

static bool trans_ST_zpri(DisasContext *s, arg_rpri_store *a)
{
    if (a->msz > a->esz) {
        return false;
    }
    if (sve_access_check(s)) {
        int vsz = vec_full_reg_size(s);
        int elements = vsz >> a->esz;
        TCGv_i64 addr = new_tmp_a64(s);

        tcg_gen_addi_i64(addr, cpu_reg_sp(s, a->rn),
                         (a->imm * elements * (a->nreg + 1)) << a->msz);
        do_st_zpa(s, a->rd, a->pg, addr, a->msz, a->esz, a->nreg);
    }
    return true;
}

/*
 *** SVE gather loads / scatter stores
 */

static void do_mem_zpz(DisasContext *s, int zt, int pg, int zm,
                       int scale, TCGv_i64 scalar, int msz, bool is_write,
                       gen_helper_gvec_mem_scatter *fn)
{
    unsigned vsz = vec_full_reg_size(s);
    TCGv_ptr t_zm = tcg_temp_new_ptr();
    TCGv_ptr t_pg = tcg_temp_new_ptr();
    TCGv_ptr t_zt = tcg_temp_new_ptr();
    TCGv_i32 t_desc;
    int desc = 0;

    if (s->mte_active[0]) {
        desc = FIELD_DP32(desc, MTEDESC, MIDX, get_mem_index(s));
        desc = FIELD_DP32(desc, MTEDESC, TBI, s->tbid);
        desc = FIELD_DP32(desc, MTEDESC, TCMA, s->tcma);
        desc = FIELD_DP32(desc, MTEDESC, WRITE, is_write);
        desc = FIELD_DP32(desc, MTEDESC, SIZEM1, (1 << msz) - 1);
        desc <<= SVE_MTEDESC_SHIFT;
    }
    desc = simd_desc(vsz, vsz, desc | scale);
    t_desc = tcg_const_i32(desc);

    tcg_gen_addi_ptr(t_pg, cpu_env, pred_full_reg_offset(s, pg));
    tcg_gen_addi_ptr(t_zm, cpu_env, vec_full_reg_offset(s, zm));
    tcg_gen_addi_ptr(t_zt, cpu_env, vec_full_reg_offset(s, zt));
    fn(cpu_env, t_zt, t_pg, t_zm, scalar, t_desc);

    tcg_temp_free_ptr(t_zt);
    tcg_temp_free_ptr(t_zm);
    tcg_temp_free_ptr(t_pg);
    tcg_temp_free_i32(t_desc);
}

/* Indexed by [mte][be][ff][xs][u][msz].  */
static gen_helper_gvec_mem_scatter * const
gather_load_fn32[2][2][2][2][2][3] = {
    { /* MTE Inactive */
        { /* Little-endian */
            { { { gen_helper_sve_ldbss_zsu,
                  gen_helper_sve_ldhss_le_zsu,
                  NULL, },
                { gen_helper_sve_ldbsu_zsu,
                  gen_helper_sve_ldhsu_le_zsu,
                  gen_helper_sve_ldss_le_zsu, } },
              { { gen_helper_sve_ldbss_zss,
                  gen_helper_sve_ldhss_le_zss,
                  NULL, },
                { gen_helper_sve_ldbsu_zss,
                  gen_helper_sve_ldhsu_le_zss,
                  gen_helper_sve_ldss_le_zss, } } },

            /* First-fault */
            { { { gen_helper_sve_ldffbss_zsu,
                  gen_helper_sve_ldffhss_le_zsu,
                  NULL, },
                { gen_helper_sve_ldffbsu_zsu,
                  gen_helper_sve_ldffhsu_le_zsu,
                  gen_helper_sve_ldffss_le_zsu, } },
              { { gen_helper_sve_ldffbss_zss,
                  gen_helper_sve_ldffhss_le_zss,
                  NULL, },
                { gen_helper_sve_ldffbsu_zss,
                  gen_helper_sve_ldffhsu_le_zss,
                  gen_helper_sve_ldffss_le_zss, } } } },

        { /* Big-endian */
            { { { gen_helper_sve_ldbss_zsu,
                  gen_helper_sve_ldhss_be_zsu,
                  NULL, },
                { gen_helper_sve_ldbsu_zsu,
                  gen_helper_sve_ldhsu_be_zsu,
                  gen_helper_sve_ldss_be_zsu, } },
              { { gen_helper_sve_ldbss_zss,
                  gen_helper_sve_ldhss_be_zss,
                  NULL, },
                { gen_helper_sve_ldbsu_zss,
                  gen_helper_sve_ldhsu_be_zss,
                  gen_helper_sve_ldss_be_zss, } } },

            /* First-fault */
            { { { gen_helper_sve_ldffbss_zsu,
                  gen_helper_sve_ldffhss_be_zsu,
                  NULL, },
                { gen_helper_sve_ldffbsu_zsu,
                  gen_helper_sve_ldffhsu_be_zsu,
                  gen_helper_sve_ldffss_be_zsu, } },
              { { gen_helper_sve_ldffbss_zss,
                  gen_helper_sve_ldffhss_be_zss,
                  NULL, },
                { gen_helper_sve_ldffbsu_zss,
                  gen_helper_sve_ldffhsu_be_zss,
                  gen_helper_sve_ldffss_be_zss, } } } } },
    { /* MTE Active */
        { /* Little-endian */
            { { { gen_helper_sve_ldbss_zsu_mte,
                  gen_helper_sve_ldhss_le_zsu_mte,
                  NULL, },
                { gen_helper_sve_ldbsu_zsu_mte,
                  gen_helper_sve_ldhsu_le_zsu_mte,
                  gen_helper_sve_ldss_le_zsu_mte, } },
              { { gen_helper_sve_ldbss_zss_mte,
                  gen_helper_sve_ldhss_le_zss_mte,
                  NULL, },
                { gen_helper_sve_ldbsu_zss_mte,
                  gen_helper_sve_ldhsu_le_zss_mte,
                  gen_helper_sve_ldss_le_zss_mte, } } },

            /* First-fault */
            { { { gen_helper_sve_ldffbss_zsu_mte,
                  gen_helper_sve_ldffhss_le_zsu_mte,
                  NULL, },
                { gen_helper_sve_ldffbsu_zsu_mte,
                  gen_helper_sve_ldffhsu_le_zsu_mte,
                  gen_helper_sve_ldffss_le_zsu_mte, } },
              { { gen_helper_sve_ldffbss_zss_mte,
                  gen_helper_sve_ldffhss_le_zss_mte,
                  NULL, },
                { gen_helper_sve_ldffbsu_zss_mte,
                  gen_helper_sve_ldffhsu_le_zss_mte,
                  gen_helper_sve_ldffss_le_zss_mte, } } } },

        { /* Big-endian */
            { { { gen_helper_sve_ldbss_zsu_mte,
                  gen_helper_sve_ldhss_be_zsu_mte,
                  NULL, },
                { gen_helper_sve_ldbsu_zsu_mte,
                  gen_helper_sve_ldhsu_be_zsu_mte,
                  gen_helper_sve_ldss_be_zsu_mte, } },
              { { gen_helper_sve_ldbss_zss_mte,
                  gen_helper_sve_ldhss_be_zss_mte,
                  NULL, },
                { gen_helper_sve_ldbsu_zss_mte,
                  gen_helper_sve_ldhsu_be_zss_mte,
                  gen_helper_sve_ldss_be_zss_mte, } } },

            /* First-fault */
            { { { gen_helper_sve_ldffbss_zsu_mte,
                  gen_helper_sve_ldffhss_be_zsu_mte,
                  NULL, },
                { gen_helper_sve_ldffbsu_zsu_mte,
                  gen_helper_sve_ldffhsu_be_zsu_mte,
                  gen_helper_sve_ldffss_be_zsu_mte, } },
              { { gen_helper_sve_ldffbss_zss_mte,
                  gen_helper_sve_ldffhss_be_zss_mte,
                  NULL, },
                { gen_helper_sve_ldffbsu_zss_mte,
                  gen_helper_sve_ldffhsu_be_zss_mte,
                  gen_helper_sve_ldffss_be_zss_mte, } } } } },
};

/* Note that we overload xs=2 to indicate 64-bit offset.  */
static gen_helper_gvec_mem_scatter * const
gather_load_fn64[2][2][2][3][2][4] = {
    { /* MTE Inactive */
        { /* Little-endian */
            { { { gen_helper_sve_ldbds_zsu,
                  gen_helper_sve_ldhds_le_zsu,
                  gen_helper_sve_ldsds_le_zsu,
                  NULL, },
                { gen_helper_sve_ldbdu_zsu,
                  gen_helper_sve_ldhdu_le_zsu,
                  gen_helper_sve_ldsdu_le_zsu,
                  gen_helper_sve_lddd_le_zsu, } },
              { { gen_helper_sve_ldbds_zss,
                  gen_helper_sve_ldhds_le_zss,
                  gen_helper_sve_ldsds_le_zss,
                  NULL, },
                { gen_helper_sve_ldbdu_zss,
                  gen_helper_sve_ldhdu_le_zss,
                  gen_helper_sve_ldsdu_le_zss,
                  gen_helper_sve_lddd_le_zss, } },
              { { gen_helper_sve_ldbds_zd,
                  gen_helper_sve_ldhds_le_zd,
                  gen_helper_sve_ldsds_le_zd,
                  NULL, },
                { gen_helper_sve_ldbdu_zd,
                  gen_helper_sve_ldhdu_le_zd,
                  gen_helper_sve_ldsdu_le_zd,
                  gen_helper_sve_lddd_le_zd, } } },

            /* First-fault */
            { { { gen_helper_sve_ldffbds_zsu,
                  gen_helper_sve_ldffhds_le_zsu,
                  gen_helper_sve_ldffsds_le_zsu,
                  NULL, },
                { gen_helper_sve_ldffbdu_zsu,
                  gen_helper_sve_ldffhdu_le_zsu,
                  gen_helper_sve_ldffsdu_le_zsu,
                  gen_helper_sve_ldffdd_le_zsu, } },
              { { gen_helper_sve_ldffbds_zss,
                  gen_helper_sve_ldffhds_le_zss,
                  gen_helper_sve_ldffsds_le_zss,
                  NULL, },
                { gen_helper_sve_ldffbdu_zss,
                  gen_helper_sve_ldffhdu_le_zss,
                  gen_helper_sve_ldffsdu_le_zss,
                  gen_helper_sve_ldffdd_le_zss, } },
              { { gen_helper_sve_ldffbds_zd,
                  gen_helper_sve_ldffhds_le_zd,
                  gen_helper_sve_ldffsds_le_zd,
                  NULL, },
                { gen_helper_sve_ldffbdu_zd,
                  gen_helper_sve_ldffhdu_le_zd,
                  gen_helper_sve_ldffsdu_le_zd,
                  gen_helper_sve_ldffdd_le_zd, } } } },
        { /* Big-endian */
            { { { gen_helper_sve_ldbds_zsu,
                  gen_helper_sve_ldhds_be_zsu,
                  gen_helper_sve_ldsds_be_zsu,
                  NULL, },
                { gen_helper_sve_ldbdu_zsu,
                  gen_helper_sve_ldhdu_be_zsu,
                  gen_helper_sve_ldsdu_be_zsu,
                  gen_helper_sve_lddd_be_zsu, } },
              { { gen_helper_sve_ldbds_zss,
                  gen_helper_sve_ldhds_be_zss,
                  gen_helper_sve_ldsds_be_zss,
                  NULL, },
                { gen_helper_sve_ldbdu_zss,
                  gen_helper_sve_ldhdu_be_zss,
                  gen_helper_sve_ldsdu_be_zss,
                  gen_helper_sve_lddd_be_zss, } },
              { { gen_helper_sve_ldbds_zd,
                  gen_helper_sve_ldhds_be_zd,
                  gen_helper_sve_ldsds_be_zd,
                  NULL, },
                { gen_helper_sve_ldbdu_zd,
                  gen_helper_sve_ldhdu_be_zd,
                  gen_helper_sve_ldsdu_be_zd,
                  gen_helper_sve_lddd_be_zd, } } },

            /* First-fault */
            { { { gen_helper_sve_ldffbds_zsu,
                  gen_helper_sve_ldffhds_be_zsu,
                  gen_helper_sve_ldffsds_be_zsu,
                  NULL, },
                { gen_helper_sve_ldffbdu_zsu,
                  gen_helper_sve_ldffhdu_be_zsu,
                  gen_helper_sve_ldffsdu_be_zsu,
                  gen_helper_sve_ldffdd_be_zsu, } },
              { { gen_helper_sve_ldffbds_zss,
                  gen_helper_sve_ldffhds_be_zss,
                  gen_helper_sve_ldffsds_be_zss,
                  NULL, },
                { gen_helper_sve_ldffbdu_zss,
                  gen_helper_sve_ldffhdu_be_zss,
                  gen_helper_sve_ldffsdu_be_zss,
                  gen_helper_sve_ldffdd_be_zss, } },
              { { gen_helper_sve_ldffbds_zd,
                  gen_helper_sve_ldffhds_be_zd,
                  gen_helper_sve_ldffsds_be_zd,
                  NULL, },
                { gen_helper_sve_ldffbdu_zd,
                  gen_helper_sve_ldffhdu_be_zd,
                  gen_helper_sve_ldffsdu_be_zd,
                  gen_helper_sve_ldffdd_be_zd, } } } } },
    { /* MTE Active */
        { /* Little-endian */
            { { { gen_helper_sve_ldbds_zsu_mte,
                  gen_helper_sve_ldhds_le_zsu_mte,
                  gen_helper_sve_ldsds_le_zsu_mte,
                  NULL, },
                { gen_helper_sve_ldbdu_zsu_mte,
                  gen_helper_sve_ldhdu_le_zsu_mte,
                  gen_helper_sve_ldsdu_le_zsu_mte,
                  gen_helper_sve_lddd_le_zsu_mte, } },
              { { gen_helper_sve_ldbds_zss_mte,
                  gen_helper_sve_ldhds_le_zss_mte,
                  gen_helper_sve_ldsds_le_zss_mte,
                  NULL, },
                { gen_helper_sve_ldbdu_zss_mte,
                  gen_helper_sve_ldhdu_le_zss_mte,
                  gen_helper_sve_ldsdu_le_zss_mte,
                  gen_helper_sve_lddd_le_zss_mte, } },
              { { gen_helper_sve_ldbds_zd_mte,
                  gen_helper_sve_ldhds_le_zd_mte,
                  gen_helper_sve_ldsds_le_zd_mte,
                  NULL, },
                { gen_helper_sve_ldbdu_zd_mte,
                  gen_helper_sve_ldhdu_le_zd_mte,
                  gen_helper_sve_ldsdu_le_zd_mte,
                  gen_helper_sve_lddd_le_zd_mte, } } },

            /* First-fault */
            { { { gen_helper_sve_ldffbds_zsu_mte,
                  gen_helper_sve_ldffhds_le_zsu_mte,
                  gen_helper_sve_ldffsds_le_zsu_mte,
                  NULL, },
                { gen_helper_sve_ldffbdu_zsu_mte,
                  gen_helper_sve_ldffhdu_le_zsu_mte,
                  gen_helper_sve_ldffsdu_le_zsu_mte,
                  gen_helper_sve_ldffdd_le_zsu_mte, } },
              { { gen_helper_sve_ldffbds_zss_mte,
                  gen_helper_sve_ldffhds_le_zss_mte,
                  gen_helper_sve_ldffsds_le_zss_mte,
                  NULL, },
                { gen_helper_sve_ldffbdu_zss_mte,
                  gen_helper_sve_ldffhdu_le_zss_mte,
                  gen_helper_sve_ldffsdu_le_zss_mte,
                  gen_helper_sve_ldffdd_le_zss_mte, } },
              { { gen_helper_sve_ldffbds_zd_mte,
                  gen_helper_sve_ldffhds_le_zd_mte,
                  gen_helper_sve_ldffsds_le_zd_mte,
                  NULL, },
                { gen_helper_sve_ldffbdu_zd_mte,
                  gen_helper_sve_ldffhdu_le_zd_mte,
                  gen_helper_sve_ldffsdu_le_zd_mte,
                  gen_helper_sve_ldffdd_le_zd_mte, } } } },
        { /* Big-endian */
            { { { gen_helper_sve_ldbds_zsu_mte,
                  gen_helper_sve_ldhds_be_zsu_mte,
                  gen_helper_sve_ldsds_be_zsu_mte,
                  NULL, },
                { gen_helper_sve_ldbdu_zsu_mte,
                  gen_helper_sve_ldhdu_be_zsu_mte,
                  gen_helper_sve_ldsdu_be_zsu_mte,
                  gen_helper_sve_lddd_be_zsu_mte, } },
              { { gen_helper_sve_ldbds_zss_mte,
                  gen_helper_sve_ldhds_be_zss_mte,
                  gen_helper_sve_ldsds_be_zss_mte,
                  NULL, },
                { gen_helper_sve_ldbdu_zss_mte,
                  gen_helper_sve_ldhdu_be_zss_mte,
                  gen_helper_sve_ldsdu_be_zss_mte,
                  gen_helper_sve_lddd_be_zss_mte, } },
              { { gen_helper_sve_ldbds_zd_mte,
                  gen_helper_sve_ldhds_be_zd_mte,
                  gen_helper_sve_ldsds_be_zd_mte,
                  NULL, },
                { gen_helper_sve_ldbdu_zd_mte,
                  gen_helper_sve_ldhdu_be_zd_mte,
                  gen_helper_sve_ldsdu_be_zd_mte,
                  gen_helper_sve_lddd_be_zd_mte, } } },

            /* First-fault */
            { { { gen_helper_sve_ldffbds_zsu_mte,
                  gen_helper_sve_ldffhds_be_zsu_mte,
                  gen_helper_sve_ldffsds_be_zsu_mte,
                  NULL, },
                { gen_helper_sve_ldffbdu_zsu_mte,
                  gen_helper_sve_ldffhdu_be_zsu_mte,
                  gen_helper_sve_ldffsdu_be_zsu_mte,
                  gen_helper_sve_ldffdd_be_zsu_mte, } },
              { { gen_helper_sve_ldffbds_zss_mte,
                  gen_helper_sve_ldffhds_be_zss_mte,
                  gen_helper_sve_ldffsds_be_zss_mte,
                  NULL, },
                { gen_helper_sve_ldffbdu_zss_mte,
                  gen_helper_sve_ldffhdu_be_zss_mte,
                  gen_helper_sve_ldffsdu_be_zss_mte,
                  gen_helper_sve_ldffdd_be_zss_mte, } },
              { { gen_helper_sve_ldffbds_zd_mte,
                  gen_helper_sve_ldffhds_be_zd_mte,
                  gen_helper_sve_ldffsds_be_zd_mte,
                  NULL, },
                { gen_helper_sve_ldffbdu_zd_mte,
                  gen_helper_sve_ldffhdu_be_zd_mte,
                  gen_helper_sve_ldffsdu_be_zd_mte,
                  gen_helper_sve_ldffdd_be_zd_mte, } } } } },
};

static bool trans_LD1_zprz(DisasContext *s, arg_LD1_zprz *a)
{
    gen_helper_gvec_mem_scatter *fn = NULL;
    bool be = s->be_data == MO_BE;
    bool mte = s->mte_active[0];

    if (!sve_access_check(s)) {
        return true;
    }

    switch (a->esz) {
    case MO_32:
        fn = gather_load_fn32[mte][be][a->ff][a->xs][a->u][a->msz];
        break;
    case MO_64:
        fn = gather_load_fn64[mte][be][a->ff][a->xs][a->u][a->msz];
        break;
    }
    assert(fn != NULL);

    do_mem_zpz(s, a->rd, a->pg, a->rm, a->scale * a->msz,
               cpu_reg_sp(s, a->rn), a->msz, false, fn);
    return true;
}

static bool trans_LD1_zpiz(DisasContext *s, arg_LD1_zpiz *a)
{
    gen_helper_gvec_mem_scatter *fn = NULL;
    bool be = s->be_data == MO_BE;
    bool mte = s->mte_active[0];
    TCGv_i64 imm;

    if (a->esz < a->msz || (a->esz == a->msz && !a->u)) {
        return false;
    }
    if (!sve_access_check(s)) {
        return true;
    }

    switch (a->esz) {
    case MO_32:
        fn = gather_load_fn32[mte][be][a->ff][0][a->u][a->msz];
        break;
    case MO_64:
        fn = gather_load_fn64[mte][be][a->ff][2][a->u][a->msz];
        break;
    }
    assert(fn != NULL);

    /* Treat LD1_zpiz (zn[x] + imm) the same way as LD1_zprz (rn + zm[x])
     * by loading the immediate into the scalar parameter.
     */
    imm = tcg_const_i64(a->imm << a->msz);
    do_mem_zpz(s, a->rd, a->pg, a->rn, 0, imm, a->msz, false, fn);
    tcg_temp_free_i64(imm);
    return true;
}

static bool trans_LDNT1_zprz(DisasContext *s, arg_LD1_zprz *a)
{
    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    return trans_LD1_zprz(s, a);
}

/* Indexed by [mte][be][xs][msz].  */
static gen_helper_gvec_mem_scatter * const scatter_store_fn32[2][2][2][3] = {
    { /* MTE Inactive */
        { /* Little-endian */
            { gen_helper_sve_stbs_zsu,
              gen_helper_sve_sths_le_zsu,
              gen_helper_sve_stss_le_zsu, },
            { gen_helper_sve_stbs_zss,
              gen_helper_sve_sths_le_zss,
              gen_helper_sve_stss_le_zss, } },
        { /* Big-endian */
            { gen_helper_sve_stbs_zsu,
              gen_helper_sve_sths_be_zsu,
              gen_helper_sve_stss_be_zsu, },
            { gen_helper_sve_stbs_zss,
              gen_helper_sve_sths_be_zss,
              gen_helper_sve_stss_be_zss, } } },
    { /* MTE Active */
        { /* Little-endian */
            { gen_helper_sve_stbs_zsu_mte,
              gen_helper_sve_sths_le_zsu_mte,
              gen_helper_sve_stss_le_zsu_mte, },
            { gen_helper_sve_stbs_zss_mte,
              gen_helper_sve_sths_le_zss_mte,
              gen_helper_sve_stss_le_zss_mte, } },
        { /* Big-endian */
            { gen_helper_sve_stbs_zsu_mte,
              gen_helper_sve_sths_be_zsu_mte,
              gen_helper_sve_stss_be_zsu_mte, },
            { gen_helper_sve_stbs_zss_mte,
              gen_helper_sve_sths_be_zss_mte,
              gen_helper_sve_stss_be_zss_mte, } } },
};

/* Note that we overload xs=2 to indicate 64-bit offset.  */
static gen_helper_gvec_mem_scatter * const scatter_store_fn64[2][2][3][4] = {
    { /* MTE Inactive */
         { /* Little-endian */
             { gen_helper_sve_stbd_zsu,
               gen_helper_sve_sthd_le_zsu,
               gen_helper_sve_stsd_le_zsu,
               gen_helper_sve_stdd_le_zsu, },
             { gen_helper_sve_stbd_zss,
               gen_helper_sve_sthd_le_zss,
               gen_helper_sve_stsd_le_zss,
               gen_helper_sve_stdd_le_zss, },
             { gen_helper_sve_stbd_zd,
               gen_helper_sve_sthd_le_zd,
               gen_helper_sve_stsd_le_zd,
               gen_helper_sve_stdd_le_zd, } },
         { /* Big-endian */
             { gen_helper_sve_stbd_zsu,
               gen_helper_sve_sthd_be_zsu,
               gen_helper_sve_stsd_be_zsu,
               gen_helper_sve_stdd_be_zsu, },
             { gen_helper_sve_stbd_zss,
               gen_helper_sve_sthd_be_zss,
               gen_helper_sve_stsd_be_zss,
               gen_helper_sve_stdd_be_zss, },
             { gen_helper_sve_stbd_zd,
               gen_helper_sve_sthd_be_zd,
               gen_helper_sve_stsd_be_zd,
               gen_helper_sve_stdd_be_zd, } } },
    { /* MTE Inactive */
         { /* Little-endian */
             { gen_helper_sve_stbd_zsu_mte,
               gen_helper_sve_sthd_le_zsu_mte,
               gen_helper_sve_stsd_le_zsu_mte,
               gen_helper_sve_stdd_le_zsu_mte, },
             { gen_helper_sve_stbd_zss_mte,
               gen_helper_sve_sthd_le_zss_mte,
               gen_helper_sve_stsd_le_zss_mte,
               gen_helper_sve_stdd_le_zss_mte, },
             { gen_helper_sve_stbd_zd_mte,
               gen_helper_sve_sthd_le_zd_mte,
               gen_helper_sve_stsd_le_zd_mte,
               gen_helper_sve_stdd_le_zd_mte, } },
         { /* Big-endian */
             { gen_helper_sve_stbd_zsu_mte,
               gen_helper_sve_sthd_be_zsu_mte,
               gen_helper_sve_stsd_be_zsu_mte,
               gen_helper_sve_stdd_be_zsu_mte, },
             { gen_helper_sve_stbd_zss_mte,
               gen_helper_sve_sthd_be_zss_mte,
               gen_helper_sve_stsd_be_zss_mte,
               gen_helper_sve_stdd_be_zss_mte, },
             { gen_helper_sve_stbd_zd_mte,
               gen_helper_sve_sthd_be_zd_mte,
               gen_helper_sve_stsd_be_zd_mte,
               gen_helper_sve_stdd_be_zd_mte, } } },
};

static bool trans_ST1_zprz(DisasContext *s, arg_ST1_zprz *a)
{
    gen_helper_gvec_mem_scatter *fn;
    bool be = s->be_data == MO_BE;
    bool mte = s->mte_active[0];

    if (a->esz < a->msz || (a->msz == 0 && a->scale)) {
        return false;
    }
    if (!sve_access_check(s)) {
        return true;
    }
    switch (a->esz) {
    case MO_32:
        fn = scatter_store_fn32[mte][be][a->xs][a->msz];
        break;
    case MO_64:
        fn = scatter_store_fn64[mte][be][a->xs][a->msz];
        break;
    default:
        g_assert_not_reached();
    }
    do_mem_zpz(s, a->rd, a->pg, a->rm, a->scale * a->msz,
               cpu_reg_sp(s, a->rn), a->msz, true, fn);
    return true;
}

static bool trans_ST1_zpiz(DisasContext *s, arg_ST1_zpiz *a)
{
    gen_helper_gvec_mem_scatter *fn = NULL;
    bool be = s->be_data == MO_BE;
    bool mte = s->mte_active[0];
    TCGv_i64 imm;

    if (a->esz < a->msz) {
        return false;
    }
    if (!sve_access_check(s)) {
        return true;
    }

    switch (a->esz) {
    case MO_32:
        fn = scatter_store_fn32[mte][be][0][a->msz];
        break;
    case MO_64:
        fn = scatter_store_fn64[mte][be][2][a->msz];
        break;
    }
    assert(fn != NULL);

    /* Treat ST1_zpiz (zn[x] + imm) the same way as ST1_zprz (rn + zm[x])
     * by loading the immediate into the scalar parameter.
     */
    imm = tcg_const_i64(a->imm << a->msz);
    do_mem_zpz(s, a->rd, a->pg, a->rn, 0, imm, a->msz, true, fn);
    tcg_temp_free_i64(imm);
    return true;
}

static bool trans_STNT1_zprz(DisasContext *s, arg_ST1_zprz *a)
{
    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    return trans_ST1_zprz(s, a);
}

/*
 * Prefetches
 */

static bool trans_PRF(DisasContext *s, arg_PRF *a)
{
    /* Prefetch is a nop within QEMU.  */
    (void)sve_access_check(s);
    return true;
}

static bool trans_PRF_rr(DisasContext *s, arg_PRF_rr *a)
{
    if (a->rm == 31) {
        return false;
    }
    /* Prefetch is a nop within QEMU.  */
    (void)sve_access_check(s);
    return true;
}

/*
 * Move Prefix
 *
 * TODO: The implementation so far could handle predicated merging movprfx.
 * The helper functions as written take an extra source register to
 * use in the operation, but the result is only written when predication
 * succeeds.  For unpredicated movprfx, we need to rearrange the helpers
 * to allow the final write back to the destination to be unconditional.
 * For predicated zeroing movprfx, we need to rearrange the helpers to
 * allow the final write back to zero inactives.
 *
 * In the meantime, just emit the moves.
 */

static bool trans_MOVPRFX(DisasContext *s, arg_MOVPRFX *a)
{
    return do_mov_z(s, a->rd, a->rn);
}

static bool trans_MOVPRFX_m(DisasContext *s, arg_rpr_esz *a)
{
    if (sve_access_check(s)) {
        do_sel_z(s, a->rd, a->rn, a->rd, a->pg, a->esz);
    }
    return true;
}

static bool trans_MOVPRFX_z(DisasContext *s, arg_rpr_esz *a)
{
    return do_movz_zpz(s, a->rd, a->rn, a->pg, a->esz, false);
}

/*
 * SVE2 Integer Multiply - Unpredicated
 */

static bool trans_MUL_zzz(DisasContext *s, arg_rrr_esz *a)
{
    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_gvec_fn_zzz(s, tcg_gen_gvec_mul, a->esz, a->rd, a->rn, a->rm);
    }
    return true;
}

static bool do_sve2_zzz_ool(DisasContext *s, arg_rrr_esz *a,
                            gen_helper_gvec_3 *fn)
{
    if (fn == NULL || !dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_gvec_ool_zzz(s, fn, a->rd, a->rn, a->rm, 0);
    }
    return true;
}

static bool trans_SMULH_zzz(DisasContext *s, arg_rrr_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_gvec_smulh_b, gen_helper_gvec_smulh_h,
        gen_helper_gvec_smulh_s, gen_helper_gvec_smulh_d,
    };
    return do_sve2_zzz_ool(s, a, fns[a->esz]);
}

static bool trans_UMULH_zzz(DisasContext *s, arg_rrr_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_gvec_umulh_b, gen_helper_gvec_umulh_h,
        gen_helper_gvec_umulh_s, gen_helper_gvec_umulh_d,
    };
    return do_sve2_zzz_ool(s, a, fns[a->esz]);
}

static bool trans_PMUL_zzz(DisasContext *s, arg_rrr_esz *a)
{
    return do_sve2_zzz_ool(s, a, gen_helper_gvec_pmul_b);
}

static bool trans_SQDMULH_zzz(DisasContext *s, arg_rrr_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve2_sqdmulh_b, gen_helper_sve2_sqdmulh_h,
        gen_helper_sve2_sqdmulh_s, gen_helper_sve2_sqdmulh_d,
    };
    return do_sve2_zzz_ool(s, a, fns[a->esz]);
}

static bool trans_SQRDMULH_zzz(DisasContext *s, arg_rrr_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve2_sqrdmulh_b, gen_helper_sve2_sqrdmulh_h,
        gen_helper_sve2_sqrdmulh_s, gen_helper_sve2_sqrdmulh_d,
    };
    return do_sve2_zzz_ool(s, a, fns[a->esz]);
}

/*
 * SVE2 Integer - Predicated
 */

static bool do_sve2_zpzz_ool(DisasContext *s, arg_rprr_esz *a,
                             gen_helper_gvec_4 *fn)
{
    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    return do_zpzz_ool(s, a, fn);
}

static bool trans_SADALP_zpzz(DisasContext *s, arg_rprr_esz *a)
{
    static gen_helper_gvec_4 * const fns[3] = {
        gen_helper_sve2_sadalp_zpzz_h,
        gen_helper_sve2_sadalp_zpzz_s,
        gen_helper_sve2_sadalp_zpzz_d,
    };
    if (a->esz == 0) {
        return false;
    }
    return do_sve2_zpzz_ool(s, a, fns[a->esz - 1]);
}

static bool trans_UADALP_zpzz(DisasContext *s, arg_rprr_esz *a)
{
    static gen_helper_gvec_4 * const fns[3] = {
        gen_helper_sve2_uadalp_zpzz_h,
        gen_helper_sve2_uadalp_zpzz_s,
        gen_helper_sve2_uadalp_zpzz_d,
    };
    if (a->esz == 0) {
        return false;
    }
    return do_sve2_zpzz_ool(s, a, fns[a->esz - 1]);
}

/*
 * SVE2 integer unary operations (predicated)
 */

static bool do_sve2_zpz_ool(DisasContext *s, arg_rpr_esz *a,
                            gen_helper_gvec_3 *fn)
{
    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    return do_zpz_ool(s, a, fn);
}

static bool trans_URECPE(DisasContext *s, arg_rpr_esz *a)
{
    if (a->esz != 2) {
        return false;
    }
    return do_sve2_zpz_ool(s, a, gen_helper_sve2_urecpe_s);
}

static bool trans_URSQRTE(DisasContext *s, arg_rpr_esz *a)
{
    if (a->esz != 2) {
        return false;
    }
    return do_sve2_zpz_ool(s, a, gen_helper_sve2_ursqrte_s);
}

static bool trans_SQABS(DisasContext *s, arg_rpr_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve2_sqabs_b, gen_helper_sve2_sqabs_h,
        gen_helper_sve2_sqabs_s, gen_helper_sve2_sqabs_d,
    };
    return do_sve2_zpz_ool(s, a, fns[a->esz]);
}

static bool trans_SQNEG(DisasContext *s, arg_rpr_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve2_sqneg_b, gen_helper_sve2_sqneg_h,
        gen_helper_sve2_sqneg_s, gen_helper_sve2_sqneg_d,
    };
    return do_sve2_zpz_ool(s, a, fns[a->esz]);
}

#define DO_SVE2_ZPZZ(NAME, name) \
static bool trans_##NAME(DisasContext *s, arg_rprr_esz *a)                \
{                                                                         \
    static gen_helper_gvec_4 * const fns[4] = {                           \
        gen_helper_sve2_##name##_zpzz_b, gen_helper_sve2_##name##_zpzz_h, \
        gen_helper_sve2_##name##_zpzz_s, gen_helper_sve2_##name##_zpzz_d, \
    };                                                                    \
    return do_sve2_zpzz_ool(s, a, fns[a->esz]);                           \
}

DO_SVE2_ZPZZ(SQSHL, sqshl)
DO_SVE2_ZPZZ(SQRSHL, sqrshl)
DO_SVE2_ZPZZ(SRSHL, srshl)

DO_SVE2_ZPZZ(UQSHL, uqshl)
DO_SVE2_ZPZZ(UQRSHL, uqrshl)
DO_SVE2_ZPZZ(URSHL, urshl)

DO_SVE2_ZPZZ(SHADD, shadd)
DO_SVE2_ZPZZ(SRHADD, srhadd)
DO_SVE2_ZPZZ(SHSUB, shsub)

DO_SVE2_ZPZZ(UHADD, uhadd)
DO_SVE2_ZPZZ(URHADD, urhadd)
DO_SVE2_ZPZZ(UHSUB, uhsub)

DO_SVE2_ZPZZ(ADDP, addp)
DO_SVE2_ZPZZ(SMAXP, smaxp)
DO_SVE2_ZPZZ(UMAXP, umaxp)
DO_SVE2_ZPZZ(SMINP, sminp)
DO_SVE2_ZPZZ(UMINP, uminp)

DO_SVE2_ZPZZ(SQADD_zpzz, sqadd)
DO_SVE2_ZPZZ(UQADD_zpzz, uqadd)
DO_SVE2_ZPZZ(SQSUB_zpzz, sqsub)
DO_SVE2_ZPZZ(UQSUB_zpzz, uqsub)
DO_SVE2_ZPZZ(SUQADD, suqadd)
DO_SVE2_ZPZZ(USQADD, usqadd)

/*
 * SVE2 Widening Integer Arithmetic
 */

static bool do_sve2_zzw_ool(DisasContext *s, arg_rrr_esz *a,
                            gen_helper_gvec_3 *fn, int data)
{
    if (fn == NULL || !dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_3_ool(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           vsz, vsz, data, fn);
    }
    return true;
}

#define DO_SVE2_ZZZ_TB(NAME, name, SEL1, SEL2) \
static bool trans_##NAME(DisasContext *s, arg_rrr_esz *a)               \
{                                                                       \
    static gen_helper_gvec_3 * const fns[4] = {                         \
        NULL,                       gen_helper_sve2_##name##_h,         \
        gen_helper_sve2_##name##_s, gen_helper_sve2_##name##_d,         \
    };                                                                  \
    return do_sve2_zzw_ool(s, a, fns[a->esz], (SEL2 << 1) | SEL1);      \
}

DO_SVE2_ZZZ_TB(SADDLB, saddl, false, false)
DO_SVE2_ZZZ_TB(SSUBLB, ssubl, false, false)
DO_SVE2_ZZZ_TB(SABDLB, sabdl, false, false)

DO_SVE2_ZZZ_TB(UADDLB, uaddl, false, false)
DO_SVE2_ZZZ_TB(USUBLB, usubl, false, false)
DO_SVE2_ZZZ_TB(UABDLB, uabdl, false, false)

DO_SVE2_ZZZ_TB(SADDLT, saddl, true, true)
DO_SVE2_ZZZ_TB(SSUBLT, ssubl, true, true)
DO_SVE2_ZZZ_TB(SABDLT, sabdl, true, true)

DO_SVE2_ZZZ_TB(UADDLT, uaddl, true, true)
DO_SVE2_ZZZ_TB(USUBLT, usubl, true, true)
DO_SVE2_ZZZ_TB(UABDLT, uabdl, true, true)

DO_SVE2_ZZZ_TB(SADDLBT, saddl, false, true)
DO_SVE2_ZZZ_TB(SSUBLBT, ssubl, false, true)
DO_SVE2_ZZZ_TB(SSUBLTB, ssubl, true, false)

DO_SVE2_ZZZ_TB(SQDMULLB_zzz, sqdmull_zzz, false, false)
DO_SVE2_ZZZ_TB(SQDMULLT_zzz, sqdmull_zzz, true, true)

DO_SVE2_ZZZ_TB(SMULLB_zzz, smull_zzz, false, false)
DO_SVE2_ZZZ_TB(SMULLT_zzz, smull_zzz, true, true)

DO_SVE2_ZZZ_TB(UMULLB_zzz, umull_zzz, false, false)
DO_SVE2_ZZZ_TB(UMULLT_zzz, umull_zzz, true, true)

static bool do_eor_tb(DisasContext *s, arg_rrr_esz *a, bool sel1)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve2_eoril_b, gen_helper_sve2_eoril_h,
        gen_helper_sve2_eoril_s, gen_helper_sve2_eoril_d,
    };
    return do_sve2_zzw_ool(s, a, fns[a->esz], (!sel1 << 1) | sel1);
}

static bool trans_EORBT(DisasContext *s, arg_rrr_esz *a)
{
    return do_eor_tb(s, a, false);
}

static bool trans_EORTB(DisasContext *s, arg_rrr_esz *a)
{
    return do_eor_tb(s, a, true);
}

static bool do_trans_pmull(DisasContext *s, arg_rrr_esz *a, bool sel)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_gvec_pmull_q, gen_helper_sve2_pmull_h,
        NULL,                    gen_helper_sve2_pmull_d,
    };
    if (a->esz == 0 && !dc_isar_feature(aa64_sve2_pmull128, s)) {
        return false;
    }
    return do_sve2_zzw_ool(s, a, fns[a->esz], sel);
}

static bool trans_PMULLB(DisasContext *s, arg_rrr_esz *a)
{
    return do_trans_pmull(s, a, false);
}

static bool trans_PMULLT(DisasContext *s, arg_rrr_esz *a)
{
    return do_trans_pmull(s, a, true);
}

#define DO_SVE2_ZZZ_WTB(NAME, name, SEL2) \
static bool trans_##NAME(DisasContext *s, arg_rrr_esz *a)       \
{                                                               \
    static gen_helper_gvec_3 * const fns[4] = {                 \
        NULL,                       gen_helper_sve2_##name##_h, \
        gen_helper_sve2_##name##_s, gen_helper_sve2_##name##_d, \
    };                                                          \
    return do_sve2_zzw_ool(s, a, fns[a->esz], SEL2);            \
}

DO_SVE2_ZZZ_WTB(SADDWB, saddw, false)
DO_SVE2_ZZZ_WTB(SADDWT, saddw, true)
DO_SVE2_ZZZ_WTB(SSUBWB, ssubw, false)
DO_SVE2_ZZZ_WTB(SSUBWT, ssubw, true)

DO_SVE2_ZZZ_WTB(UADDWB, uaddw, false)
DO_SVE2_ZZZ_WTB(UADDWT, uaddw, true)
DO_SVE2_ZZZ_WTB(USUBWB, usubw, false)
DO_SVE2_ZZZ_WTB(USUBWT, usubw, true)

static void gen_sshll_vec(unsigned vece, TCGv_vec d, TCGv_vec n, int64_t imm)
{
    int top = imm & 1;
    int shl = imm >> 1;
    int halfbits = 4 << vece;

    if (top) {
        if (shl == halfbits) {
            TCGv_vec t = tcg_temp_new_vec_matching(d);
            tcg_gen_dupi_vec(vece, t, MAKE_64BIT_MASK(halfbits, halfbits));
            tcg_gen_and_vec(vece, d, n, t);
            tcg_temp_free_vec(t);
        } else {
            tcg_gen_sari_vec(vece, d, n, halfbits);
            tcg_gen_shli_vec(vece, d, d, shl);
        }
    } else {
        tcg_gen_shli_vec(vece, d, n, halfbits);
        tcg_gen_sari_vec(vece, d, d, halfbits - shl);
    }
}

static void gen_ushll_i64(unsigned vece, TCGv_i64 d, TCGv_i64 n, int imm)
{
    int halfbits = 4 << vece;
    int top = imm & 1;
    int shl = (imm >> 1);
    int shift;
    uint64_t mask;

    mask = MAKE_64BIT_MASK(0, halfbits);
    mask <<= shl;
    mask = dup_const(vece, mask);

    shift = shl - top * halfbits;
    if (shift < 0) {
        tcg_gen_shri_i64(d, n, -shift);
    } else {
        tcg_gen_shli_i64(d, n, shift);
    }
    tcg_gen_andi_i64(d, d, mask);
}

static void gen_ushll16_i64(TCGv_i64 d, TCGv_i64 n, int64_t imm)
{
    gen_ushll_i64(MO_16, d, n, imm);
}

static void gen_ushll32_i64(TCGv_i64 d, TCGv_i64 n, int64_t imm)
{
    gen_ushll_i64(MO_32, d, n, imm);
}

static void gen_ushll64_i64(TCGv_i64 d, TCGv_i64 n, int64_t imm)
{
    gen_ushll_i64(MO_64, d, n, imm);
}

static void gen_ushll_vec(unsigned vece, TCGv_vec d, TCGv_vec n, int64_t imm)
{
    int halfbits = 4 << vece;
    int top = imm & 1;
    int shl = imm >> 1;

    if (top) {
        if (shl == halfbits) {
            TCGv_vec t = tcg_temp_new_vec_matching(d);
            tcg_gen_dupi_vec(vece, t, MAKE_64BIT_MASK(halfbits, halfbits));
            tcg_gen_and_vec(vece, d, n, t);
            tcg_temp_free_vec(t);
        } else {
            tcg_gen_shri_vec(vece, d, n, halfbits);
            tcg_gen_shli_vec(vece, d, d, shl);
        }
    } else {
        if (shl == 0) {
            TCGv_vec t = tcg_temp_new_vec_matching(d);
            tcg_gen_dupi_vec(vece, t, MAKE_64BIT_MASK(0, halfbits));
            tcg_gen_and_vec(vece, d, n, t);
            tcg_temp_free_vec(t);
        } else {
            tcg_gen_shli_vec(vece, d, n, halfbits);
            tcg_gen_shri_vec(vece, d, d, halfbits - shl);
        }
    }
}

static bool do_sve2_shll_tb(DisasContext *s, arg_rri_esz *a,
                            bool sel, bool uns)
{
    static const TCGOpcode sshll_list[] = {
        INDEX_op_shli_vec, INDEX_op_sari_vec, 0
    };
    static const TCGOpcode ushll_list[] = {
        INDEX_op_shli_vec, INDEX_op_shri_vec, 0
    };
    static const GVecGen2i ops[2][3] = {
        { { .fniv = gen_sshll_vec,
            .opt_opc = sshll_list,
            .fno = gen_helper_sve2_sshll_h,
            .vece = MO_16 },
          { .fniv = gen_sshll_vec,
            .opt_opc = sshll_list,
            .fno = gen_helper_sve2_sshll_s,
            .vece = MO_32 },
          { .fniv = gen_sshll_vec,
            .opt_opc = sshll_list,
            .fno = gen_helper_sve2_sshll_d,
            .vece = MO_64 } },
        { { .fni8 = gen_ushll16_i64,
            .fniv = gen_ushll_vec,
            .opt_opc = ushll_list,
            .fno = gen_helper_sve2_ushll_h,
            .vece = MO_16 },
          { .fni8 = gen_ushll32_i64,
            .fniv = gen_ushll_vec,
            .opt_opc = ushll_list,
            .fno = gen_helper_sve2_ushll_s,
            .vece = MO_32 },
          { .fni8 = gen_ushll64_i64,
            .fniv = gen_ushll_vec,
            .opt_opc = ushll_list,
            .fno = gen_helper_sve2_ushll_d,
            .vece = MO_64 } },
    };

    if (a->esz < 0 || a->esz > 2 || !dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_2i(vec_full_reg_offset(s, a->rd),
                        vec_full_reg_offset(s, a->rn),
                        vsz, vsz, (a->imm << 1) | sel,
                        &ops[uns][a->esz]);
    }
    return true;
}

static bool trans_SSHLLB(DisasContext *s, arg_rri_esz *a)
{
    return do_sve2_shll_tb(s, a, false, false);
}

static bool trans_SSHLLT(DisasContext *s, arg_rri_esz *a)
{
    return do_sve2_shll_tb(s, a, true, false);
}

static bool trans_USHLLB(DisasContext *s, arg_rri_esz *a)
{
    return do_sve2_shll_tb(s, a, false, true);
}

static bool trans_USHLLT(DisasContext *s, arg_rri_esz *a)
{
    return do_sve2_shll_tb(s, a, true, true);
}

static bool trans_BEXT(DisasContext *s, arg_rrr_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve2_bext_b, gen_helper_sve2_bext_h,
        gen_helper_sve2_bext_s, gen_helper_sve2_bext_d,
    };
    if (!dc_isar_feature(aa64_sve2_bitperm, s)) {
        return false;
    }
    return do_sve2_zzw_ool(s, a, fns[a->esz], 0);
}

static bool trans_BDEP(DisasContext *s, arg_rrr_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve2_bdep_b, gen_helper_sve2_bdep_h,
        gen_helper_sve2_bdep_s, gen_helper_sve2_bdep_d,
    };
    if (!dc_isar_feature(aa64_sve2_bitperm, s)) {
        return false;
    }
    return do_sve2_zzw_ool(s, a, fns[a->esz], 0);
}

static bool trans_BGRP(DisasContext *s, arg_rrr_esz *a)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve2_bgrp_b, gen_helper_sve2_bgrp_h,
        gen_helper_sve2_bgrp_s, gen_helper_sve2_bgrp_d,
    };
    if (!dc_isar_feature(aa64_sve2_bitperm, s)) {
        return false;
    }
    return do_sve2_zzw_ool(s, a, fns[a->esz], 0);
}

static bool do_cadd(DisasContext *s, arg_rrr_esz *a, bool sq, bool rot)
{
    static gen_helper_gvec_3 * const fns[2][4] = {
        { gen_helper_sve2_cadd_b, gen_helper_sve2_cadd_h,
          gen_helper_sve2_cadd_s, gen_helper_sve2_cadd_d },
        { gen_helper_sve2_sqcadd_b, gen_helper_sve2_sqcadd_h,
          gen_helper_sve2_sqcadd_s, gen_helper_sve2_sqcadd_d },
    };
    return do_sve2_zzw_ool(s, a, fns[sq][a->esz], rot);
}

static bool trans_CADD_rot90(DisasContext *s, arg_rrr_esz *a)
{
    return do_cadd(s, a, false, false);
}

static bool trans_CADD_rot270(DisasContext *s, arg_rrr_esz *a)
{
    return do_cadd(s, a, false, true);
}

static bool trans_SQCADD_rot90(DisasContext *s, arg_rrr_esz *a)
{
    return do_cadd(s, a, true, false);
}

static bool trans_SQCADD_rot270(DisasContext *s, arg_rrr_esz *a)
{
    return do_cadd(s, a, true, true);
}

static bool do_sve2_zzzz_ool(DisasContext *s, arg_rrrr_esz *a,
                             gen_helper_gvec_4 *fn, int data)
{
    if (fn == NULL || !dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_gvec_ool_zzzz(s, fn, a->rd, a->rn, a->rm, a->ra, data);
    }
    return true;
}

static bool do_abal(DisasContext *s, arg_rrrr_esz *a, bool uns, bool sel)
{
    static gen_helper_gvec_4 * const fns[2][4] = {
        { NULL,                    gen_helper_sve2_sabal_h,
          gen_helper_sve2_sabal_s, gen_helper_sve2_sabal_d },
        { NULL,                    gen_helper_sve2_uabal_h,
          gen_helper_sve2_uabal_s, gen_helper_sve2_uabal_d },
    };
    return do_sve2_zzzz_ool(s, a, fns[uns][a->esz], sel);
}

static bool trans_SABALB(DisasContext *s, arg_rrrr_esz *a)
{
    return do_abal(s, a, false, false);
}

static bool trans_SABALT(DisasContext *s, arg_rrrr_esz *a)
{
    return do_abal(s, a, false, true);
}

static bool trans_UABALB(DisasContext *s, arg_rrrr_esz *a)
{
    return do_abal(s, a, true, false);
}

static bool trans_UABALT(DisasContext *s, arg_rrrr_esz *a)
{
    return do_abal(s, a, true, true);
}

static bool do_adcl(DisasContext *s, arg_rrrr_esz *a, bool sel)
{
    static gen_helper_gvec_4 * const fns[2] = {
        gen_helper_sve2_adcl_s,
        gen_helper_sve2_adcl_d,
    };
    /*
     * Note that in this case the ESZ field encodes both size and sign.
     * Split out 'subtract' into bit 1 of the data field for the helper.
     */
    return do_sve2_zzzz_ool(s, a, fns[a->esz & 1], (a->esz & 2) | sel);
}

static bool trans_ADCLB(DisasContext *s, arg_rrrr_esz *a)
{
    return do_adcl(s, a, false);
}

static bool trans_ADCLT(DisasContext *s, arg_rrrr_esz *a)
{
    return do_adcl(s, a, true);
}

static bool do_sve2_fn2i(DisasContext *s, arg_rri_esz *a, GVecGen2iFn *fn)
{
    if (a->esz < 0 || !dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        unsigned rd_ofs = vec_full_reg_offset(s, a->rd);
        unsigned rn_ofs = vec_full_reg_offset(s, a->rn);
        fn(a->esz, rd_ofs, rn_ofs, a->imm, vsz, vsz);
    }
    return true;
}

static bool trans_SSRA(DisasContext *s, arg_rri_esz *a)
{
    return do_sve2_fn2i(s, a, gen_gvec_ssra);
}

static bool trans_USRA(DisasContext *s, arg_rri_esz *a)
{
    return do_sve2_fn2i(s, a, gen_gvec_usra);
}

static bool trans_SRSRA(DisasContext *s, arg_rri_esz *a)
{
    return do_sve2_fn2i(s, a, gen_gvec_srsra);
}

static bool trans_URSRA(DisasContext *s, arg_rri_esz *a)
{
    return do_sve2_fn2i(s, a, gen_gvec_ursra);
}

static bool trans_SRI(DisasContext *s, arg_rri_esz *a)
{
    return do_sve2_fn2i(s, a, gen_gvec_sri);
}

static bool trans_SLI(DisasContext *s, arg_rri_esz *a)
{
    return do_sve2_fn2i(s, a, gen_gvec_sli);
}

static bool do_sve2_fn_zzz(DisasContext *s, arg_rrr_esz *a, GVecGen3Fn *fn)
{
    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_gvec_fn_zzz(s, fn, a->esz, a->rd, a->rn, a->rm);
    }
    return true;
}

static bool trans_SABA(DisasContext *s, arg_rrr_esz *a)
{
    return do_sve2_fn_zzz(s, a, gen_gvec_saba);
}

static bool trans_UABA(DisasContext *s, arg_rrr_esz *a)
{
    return do_sve2_fn_zzz(s, a, gen_gvec_uaba);
}

static bool do_sve2_narrow_extract(DisasContext *s, arg_rri_esz *a,
                                   const GVecGen2 ops[3])
{
    if (a->esz < 0 || a->esz > MO_32 || a->imm != 0 ||
        !dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_2(vec_full_reg_offset(s, a->rd),
                        vec_full_reg_offset(s, a->rn),
                        vsz, vsz, &ops[a->esz]);
    }
    return true;
}

static const TCGOpcode sqxtn_list[] = {
    INDEX_op_shli_vec, INDEX_op_smin_vec, INDEX_op_smax_vec, 0
};

static void gen_sqxtnb_vec(unsigned vece, TCGv_vec d, TCGv_vec n)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    int halfbits = 4 << vece;
    int64_t mask = (1ull << halfbits) - 1;
    int64_t min = -1ull << (halfbits - 1);
    int64_t max = -min - 1;

    tcg_gen_dupi_vec(vece, t, min);
    tcg_gen_smax_vec(vece, d, n, t);
    tcg_gen_dupi_vec(vece, t, max);
    tcg_gen_smin_vec(vece, d, d, t);
    tcg_gen_dupi_vec(vece, t, mask);
    tcg_gen_and_vec(vece, d, d, t);
    tcg_temp_free_vec(t);
}

static bool trans_SQXTNB(DisasContext *s, arg_rri_esz *a)
{
    static const GVecGen2 ops[3] = {
        { .fniv = gen_sqxtnb_vec,
          .opt_opc = sqxtn_list,
          .fno = gen_helper_sve2_sqxtnb_h,
          .vece = MO_16 },
        { .fniv = gen_sqxtnb_vec,
          .opt_opc = sqxtn_list,
          .fno = gen_helper_sve2_sqxtnb_s,
          .vece = MO_32 },
        { .fniv = gen_sqxtnb_vec,
          .opt_opc = sqxtn_list,
          .fno = gen_helper_sve2_sqxtnb_d,
          .vece = MO_64 },
    };
    return do_sve2_narrow_extract(s, a, ops);
}

static void gen_sqxtnt_vec(unsigned vece, TCGv_vec d, TCGv_vec n)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    int halfbits = 4 << vece;
    int64_t mask = (1ull << halfbits) - 1;
    int64_t min = -1ull << (halfbits - 1);
    int64_t max = -min - 1;

    tcg_gen_dupi_vec(vece, t, min);
    tcg_gen_smax_vec(vece, n, n, t);
    tcg_gen_dupi_vec(vece, t, max);
    tcg_gen_smin_vec(vece, n, n, t);
    tcg_gen_shli_vec(vece, n, n, halfbits);
    tcg_gen_dupi_vec(vece, t, mask);
    tcg_gen_bitsel_vec(vece, d, t, d, n);
    tcg_temp_free_vec(t);
}

static bool trans_SQXTNT(DisasContext *s, arg_rri_esz *a)
{
    static const GVecGen2 ops[3] = {
        { .fniv = gen_sqxtnt_vec,
          .opt_opc = sqxtn_list,
          .load_dest = true,
          .fno = gen_helper_sve2_sqxtnt_h,
          .vece = MO_16 },
        { .fniv = gen_sqxtnt_vec,
          .opt_opc = sqxtn_list,
          .load_dest = true,
          .fno = gen_helper_sve2_sqxtnt_s,
          .vece = MO_32 },
        { .fniv = gen_sqxtnt_vec,
          .opt_opc = sqxtn_list,
          .load_dest = true,
          .fno = gen_helper_sve2_sqxtnt_d,
          .vece = MO_64 },
    };
    return do_sve2_narrow_extract(s, a, ops);
}

static const TCGOpcode uqxtn_list[] = {
    INDEX_op_shli_vec, INDEX_op_umin_vec, 0
};

static void gen_uqxtnb_vec(unsigned vece, TCGv_vec d, TCGv_vec n)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    int halfbits = 4 << vece;
    int64_t max = (1ull << halfbits) - 1;

    tcg_gen_dupi_vec(vece, t, max);
    tcg_gen_umin_vec(vece, d, n, t);
    tcg_temp_free_vec(t);
}

static bool trans_UQXTNB(DisasContext *s, arg_rri_esz *a)
{
    static const GVecGen2 ops[3] = {
        { .fniv = gen_uqxtnb_vec,
          .opt_opc = uqxtn_list,
          .fno = gen_helper_sve2_uqxtnb_h,
          .vece = MO_16 },
        { .fniv = gen_uqxtnb_vec,
          .opt_opc = uqxtn_list,
          .fno = gen_helper_sve2_uqxtnb_s,
          .vece = MO_32 },
        { .fniv = gen_uqxtnb_vec,
          .opt_opc = uqxtn_list,
          .fno = gen_helper_sve2_uqxtnb_d,
          .vece = MO_64 },
    };
    return do_sve2_narrow_extract(s, a, ops);
}

static void gen_uqxtnt_vec(unsigned vece, TCGv_vec d, TCGv_vec n)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    int halfbits = 4 << vece;
    int64_t max = (1ull << halfbits) - 1;

    tcg_gen_dupi_vec(vece, t, max);
    tcg_gen_umin_vec(vece, n, n, t);
    tcg_gen_shli_vec(vece, n, n, halfbits);
    tcg_gen_bitsel_vec(vece, d, t, d, n);
    tcg_temp_free_vec(t);
}

static bool trans_UQXTNT(DisasContext *s, arg_rri_esz *a)
{
    static const GVecGen2 ops[3] = {
        { .fniv = gen_uqxtnt_vec,
          .opt_opc = uqxtn_list,
          .load_dest = true,
          .fno = gen_helper_sve2_uqxtnt_h,
          .vece = MO_16 },
        { .fniv = gen_uqxtnt_vec,
          .opt_opc = uqxtn_list,
          .load_dest = true,
          .fno = gen_helper_sve2_uqxtnt_s,
          .vece = MO_32 },
        { .fniv = gen_uqxtnt_vec,
          .opt_opc = uqxtn_list,
          .load_dest = true,
          .fno = gen_helper_sve2_uqxtnt_d,
          .vece = MO_64 },
    };
    return do_sve2_narrow_extract(s, a, ops);
}

static const TCGOpcode sqxtun_list[] = {
    INDEX_op_shli_vec, INDEX_op_umin_vec, INDEX_op_smax_vec, 0
};

static void gen_sqxtunb_vec(unsigned vece, TCGv_vec d, TCGv_vec n)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    int halfbits = 4 << vece;
    int64_t max = (1ull << halfbits) - 1;

    tcg_gen_dupi_vec(vece, t, 0);
    tcg_gen_smax_vec(vece, d, n, t);
    tcg_gen_dupi_vec(vece, t, max);
    tcg_gen_umin_vec(vece, d, d, t);
    tcg_temp_free_vec(t);
}

static bool trans_SQXTUNB(DisasContext *s, arg_rri_esz *a)
{
    static const GVecGen2 ops[3] = {
        { .fniv = gen_sqxtunb_vec,
          .opt_opc = sqxtun_list,
          .fno = gen_helper_sve2_sqxtunb_h,
          .vece = MO_16 },
        { .fniv = gen_sqxtunb_vec,
          .opt_opc = sqxtun_list,
          .fno = gen_helper_sve2_sqxtunb_s,
          .vece = MO_32 },
        { .fniv = gen_sqxtunb_vec,
          .opt_opc = sqxtun_list,
          .fno = gen_helper_sve2_sqxtunb_d,
          .vece = MO_64 },
    };
    return do_sve2_narrow_extract(s, a, ops);
}

static void gen_sqxtunt_vec(unsigned vece, TCGv_vec d, TCGv_vec n)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    int halfbits = 4 << vece;
    int64_t max = (1ull << halfbits) - 1;

    tcg_gen_dupi_vec(vece, t, 0);
    tcg_gen_smax_vec(vece, n, n, t);
    tcg_gen_dupi_vec(vece, t, max);
    tcg_gen_umin_vec(vece, n, n, t);
    tcg_gen_shli_vec(vece, n, n, halfbits);
    tcg_gen_bitsel_vec(vece, d, t, d, n);
    tcg_temp_free_vec(t);
}

static bool trans_SQXTUNT(DisasContext *s, arg_rri_esz *a)
{
    static const GVecGen2 ops[3] = {
        { .fniv = gen_sqxtunt_vec,
          .opt_opc = sqxtun_list,
          .load_dest = true,
          .fno = gen_helper_sve2_sqxtunt_h,
          .vece = MO_16 },
        { .fniv = gen_sqxtunt_vec,
          .opt_opc = sqxtun_list,
          .load_dest = true,
          .fno = gen_helper_sve2_sqxtunt_s,
          .vece = MO_32 },
        { .fniv = gen_sqxtunt_vec,
          .opt_opc = sqxtun_list,
          .load_dest = true,
          .fno = gen_helper_sve2_sqxtunt_d,
          .vece = MO_64 },
    };
    return do_sve2_narrow_extract(s, a, ops);
}

static bool do_sve2_shr_narrow(DisasContext *s, arg_rri_esz *a,
                               const GVecGen2i ops[3])
{
    if (a->esz < 0 || a->esz > MO_32 || !dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    assert(a->imm > 0 && a->imm <= (8 << a->esz));
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_2i(vec_full_reg_offset(s, a->rd),
                        vec_full_reg_offset(s, a->rn),
                        vsz, vsz, a->imm, &ops[a->esz]);
    }
    return true;
}

static void gen_shrnb_i64(unsigned vece, TCGv_i64 d, TCGv_i64 n, int shr)
{
    int halfbits = 4 << vece;
    uint64_t mask = dup_const(vece, MAKE_64BIT_MASK(0, halfbits));

    tcg_gen_shri_i64(d, n, shr);
    tcg_gen_andi_i64(d, d, mask);
}

static void gen_shrnb16_i64(TCGv_i64 d, TCGv_i64 n, int64_t shr)
{
    gen_shrnb_i64(MO_16, d, n, shr);
}

static void gen_shrnb32_i64(TCGv_i64 d, TCGv_i64 n, int64_t shr)
{
    gen_shrnb_i64(MO_32, d, n, shr);
}

static void gen_shrnb64_i64(TCGv_i64 d, TCGv_i64 n, int64_t shr)
{
    gen_shrnb_i64(MO_64, d, n, shr);
}

static void gen_shrnb_vec(unsigned vece, TCGv_vec d, TCGv_vec n, int64_t shr)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    int halfbits = 4 << vece;
    uint64_t mask = MAKE_64BIT_MASK(0, halfbits);

    tcg_gen_shri_vec(vece, n, n, shr);
    tcg_gen_dupi_vec(vece, t, mask);
    tcg_gen_and_vec(vece, d, n, t);
    tcg_temp_free_vec(t);
}

static bool trans_SHRNB(DisasContext *s, arg_rri_esz *a)
{
    static const TCGOpcode vec_list[] = { INDEX_op_shri_vec, 0 };
    static const GVecGen2i ops[3] = {
        { .fni8 = gen_shrnb16_i64,
          .fniv = gen_shrnb_vec,
          .opt_opc = vec_list,
          .fno = gen_helper_sve2_shrnb_h,
          .vece = MO_16 },
        { .fni8 = gen_shrnb32_i64,
          .fniv = gen_shrnb_vec,
          .opt_opc = vec_list,
          .fno = gen_helper_sve2_shrnb_s,
          .vece = MO_32 },
        { .fni8 = gen_shrnb64_i64,
          .fniv = gen_shrnb_vec,
          .opt_opc = vec_list,
          .fno = gen_helper_sve2_shrnb_d,
          .vece = MO_64 },
    };
    return do_sve2_shr_narrow(s, a, ops);
}

static void gen_shrnt_i64(unsigned vece, TCGv_i64 d, TCGv_i64 n, int shr)
{
    int halfbits = 4 << vece;
    uint64_t mask = dup_const(vece, MAKE_64BIT_MASK(0, halfbits));

    tcg_gen_shli_i64(n, n, halfbits - shr);
    tcg_gen_andi_i64(n, n, ~mask);
    tcg_gen_andi_i64(d, d, mask);
    tcg_gen_or_i64(d, d, n);
}

static void gen_shrnt16_i64(TCGv_i64 d, TCGv_i64 n, int64_t shr)
{
    gen_shrnt_i64(MO_16, d, n, shr);
}

static void gen_shrnt32_i64(TCGv_i64 d, TCGv_i64 n, int64_t shr)
{
    gen_shrnt_i64(MO_32, d, n, shr);
}

static void gen_shrnt64_i64(TCGv_i64 d, TCGv_i64 n, int64_t shr)
{
    tcg_gen_shri_i64(n, n, shr);
    tcg_gen_deposit_i64(d, d, n, 32, 32);
}

static void gen_shrnt_vec(unsigned vece, TCGv_vec d, TCGv_vec n, int64_t shr)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    int halfbits = 4 << vece;
    uint64_t mask = MAKE_64BIT_MASK(0, halfbits);

    tcg_gen_shli_vec(vece, n, n, halfbits - shr);
    tcg_gen_dupi_vec(vece, t, mask);
    tcg_gen_bitsel_vec(vece, d, t, d, n);
    tcg_temp_free_vec(t);
}

static bool trans_SHRNT(DisasContext *s, arg_rri_esz *a)
{
    static const TCGOpcode vec_list[] = { INDEX_op_shli_vec, 0 };
    static const GVecGen2i ops[3] = {
        { .fni8 = gen_shrnt16_i64,
          .fniv = gen_shrnt_vec,
          .opt_opc = vec_list,
          .load_dest = true,
          .fno = gen_helper_sve2_shrnt_h,
          .vece = MO_16 },
        { .fni8 = gen_shrnt32_i64,
          .fniv = gen_shrnt_vec,
          .opt_opc = vec_list,
          .load_dest = true,
          .fno = gen_helper_sve2_shrnt_s,
          .vece = MO_32 },
        { .fni8 = gen_shrnt64_i64,
          .fniv = gen_shrnt_vec,
          .opt_opc = vec_list,
          .load_dest = true,
          .fno = gen_helper_sve2_shrnt_d,
          .vece = MO_64 },
    };
    return do_sve2_shr_narrow(s, a, ops);
}

static bool trans_RSHRNB(DisasContext *s, arg_rri_esz *a)
{
    static const GVecGen2i ops[3] = {
        { .fno = gen_helper_sve2_rshrnb_h },
        { .fno = gen_helper_sve2_rshrnb_s },
        { .fno = gen_helper_sve2_rshrnb_d },
    };
    return do_sve2_shr_narrow(s, a, ops);
}

static bool trans_RSHRNT(DisasContext *s, arg_rri_esz *a)
{
    static const GVecGen2i ops[3] = {
        { .fno = gen_helper_sve2_rshrnt_h },
        { .fno = gen_helper_sve2_rshrnt_s },
        { .fno = gen_helper_sve2_rshrnt_d },
    };
    return do_sve2_shr_narrow(s, a, ops);
}

static void gen_sqshrunb_vec(unsigned vece, TCGv_vec d,
                             TCGv_vec n, int64_t shr)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    int halfbits = 4 << vece;

    tcg_gen_sari_vec(vece, n, n, shr);
    tcg_gen_dupi_vec(vece, t, 0);
    tcg_gen_smax_vec(vece, n, n, t);
    tcg_gen_dupi_vec(vece, t, MAKE_64BIT_MASK(0, halfbits));
    tcg_gen_umin_vec(vece, d, n, t);
    tcg_temp_free_vec(t);
}

static bool trans_SQSHRUNB(DisasContext *s, arg_rri_esz *a)
{
    static const TCGOpcode vec_list[] = {
        INDEX_op_sari_vec, INDEX_op_smax_vec, INDEX_op_umin_vec, 0
    };
    static const GVecGen2i ops[3] = {
        { .fniv = gen_sqshrunb_vec,
          .opt_opc = vec_list,
          .fno = gen_helper_sve2_sqshrunb_h,
          .vece = MO_16 },
        { .fniv = gen_sqshrunb_vec,
          .opt_opc = vec_list,
          .fno = gen_helper_sve2_sqshrunb_s,
          .vece = MO_32 },
        { .fniv = gen_sqshrunb_vec,
          .opt_opc = vec_list,
          .fno = gen_helper_sve2_sqshrunb_d,
          .vece = MO_64 },
    };
    return do_sve2_shr_narrow(s, a, ops);
}

static void gen_sqshrunt_vec(unsigned vece, TCGv_vec d,
                             TCGv_vec n, int64_t shr)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    int halfbits = 4 << vece;

    tcg_gen_sari_vec(vece, n, n, shr);
    tcg_gen_dupi_vec(vece, t, 0);
    tcg_gen_smax_vec(vece, n, n, t);
    tcg_gen_dupi_vec(vece, t, MAKE_64BIT_MASK(0, halfbits));
    tcg_gen_umin_vec(vece, n, n, t);
    tcg_gen_shli_vec(vece, n, n, halfbits);
    tcg_gen_bitsel_vec(vece, d, t, d, n);
    tcg_temp_free_vec(t);
}

static bool trans_SQSHRUNT(DisasContext *s, arg_rri_esz *a)
{
    static const TCGOpcode vec_list[] = {
        INDEX_op_shli_vec, INDEX_op_sari_vec,
        INDEX_op_smax_vec, INDEX_op_umin_vec, 0
    };
    static const GVecGen2i ops[3] = {
        { .fniv = gen_sqshrunt_vec,
          .opt_opc = vec_list,
          .load_dest = true,
          .fno = gen_helper_sve2_sqshrunt_h,
          .vece = MO_16 },
        { .fniv = gen_sqshrunt_vec,
          .opt_opc = vec_list,
          .load_dest = true,
          .fno = gen_helper_sve2_sqshrunt_s,
          .vece = MO_32 },
        { .fniv = gen_sqshrunt_vec,
          .opt_opc = vec_list,
          .load_dest = true,
          .fno = gen_helper_sve2_sqshrunt_d,
          .vece = MO_64 },
    };
    return do_sve2_shr_narrow(s, a, ops);
}

static bool trans_SQRSHRUNB(DisasContext *s, arg_rri_esz *a)
{
    static const GVecGen2i ops[3] = {
        { .fno = gen_helper_sve2_sqrshrunb_h },
        { .fno = gen_helper_sve2_sqrshrunb_s },
        { .fno = gen_helper_sve2_sqrshrunb_d },
    };
    return do_sve2_shr_narrow(s, a, ops);
}

static bool trans_SQRSHRUNT(DisasContext *s, arg_rri_esz *a)
{
    static const GVecGen2i ops[3] = {
        { .fno = gen_helper_sve2_sqrshrunt_h },
        { .fno = gen_helper_sve2_sqrshrunt_s },
        { .fno = gen_helper_sve2_sqrshrunt_d },
    };
    return do_sve2_shr_narrow(s, a, ops);
}

static void gen_sqshrnb_vec(unsigned vece, TCGv_vec d,
                            TCGv_vec n, int64_t shr)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    int halfbits = 4 << vece;
    int64_t max = MAKE_64BIT_MASK(0, halfbits - 1);
    int64_t min = -max - 1;

    tcg_gen_sari_vec(vece, n, n, shr);
    tcg_gen_dupi_vec(vece, t, min);
    tcg_gen_smax_vec(vece, n, n, t);
    tcg_gen_dupi_vec(vece, t, max);
    tcg_gen_smin_vec(vece, n, n, t);
    tcg_gen_dupi_vec(vece, t, MAKE_64BIT_MASK(0, halfbits));
    tcg_gen_and_vec(vece, d, n, t);
    tcg_temp_free_vec(t);
}

static bool trans_SQSHRNB(DisasContext *s, arg_rri_esz *a)
{
    static const TCGOpcode vec_list[] = {
        INDEX_op_sari_vec, INDEX_op_smax_vec, INDEX_op_smin_vec, 0
    };
    static const GVecGen2i ops[3] = {
        { .fniv = gen_sqshrnb_vec,
          .opt_opc = vec_list,
          .fno = gen_helper_sve2_sqshrnb_h,
          .vece = MO_16 },
        { .fniv = gen_sqshrnb_vec,
          .opt_opc = vec_list,
          .fno = gen_helper_sve2_sqshrnb_s,
          .vece = MO_32 },
        { .fniv = gen_sqshrnb_vec,
          .opt_opc = vec_list,
          .fno = gen_helper_sve2_sqshrnb_d,
          .vece = MO_64 },
    };
    return do_sve2_shr_narrow(s, a, ops);
}

static void gen_sqshrnt_vec(unsigned vece, TCGv_vec d,
                             TCGv_vec n, int64_t shr)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    int halfbits = 4 << vece;
    int64_t max = MAKE_64BIT_MASK(0, halfbits - 1);
    int64_t min = -max - 1;

    tcg_gen_sari_vec(vece, n, n, shr);
    tcg_gen_dupi_vec(vece, t, min);
    tcg_gen_smax_vec(vece, n, n, t);
    tcg_gen_dupi_vec(vece, t, max);
    tcg_gen_smin_vec(vece, n, n, t);
    tcg_gen_shli_vec(vece, n, n, halfbits);
    tcg_gen_dupi_vec(vece, t, MAKE_64BIT_MASK(0, halfbits));
    tcg_gen_bitsel_vec(vece, d, t, d, n);
    tcg_temp_free_vec(t);
}

static bool trans_SQSHRNT(DisasContext *s, arg_rri_esz *a)
{
    static const TCGOpcode vec_list[] = {
        INDEX_op_shli_vec, INDEX_op_sari_vec,
        INDEX_op_smax_vec, INDEX_op_smin_vec, 0
    };
    static const GVecGen2i ops[3] = {
        { .fniv = gen_sqshrnt_vec,
          .opt_opc = vec_list,
          .load_dest = true,
          .fno = gen_helper_sve2_sqshrnt_h,
          .vece = MO_16 },
        { .fniv = gen_sqshrnt_vec,
          .opt_opc = vec_list,
          .load_dest = true,
          .fno = gen_helper_sve2_sqshrnt_s,
          .vece = MO_32 },
        { .fniv = gen_sqshrnt_vec,
          .opt_opc = vec_list,
          .load_dest = true,
          .fno = gen_helper_sve2_sqshrnt_d,
          .vece = MO_64 },
    };
    return do_sve2_shr_narrow(s, a, ops);
}

static bool trans_SQRSHRNB(DisasContext *s, arg_rri_esz *a)
{
    static const GVecGen2i ops[3] = {
        { .fno = gen_helper_sve2_sqrshrnb_h },
        { .fno = gen_helper_sve2_sqrshrnb_s },
        { .fno = gen_helper_sve2_sqrshrnb_d },
    };
    return do_sve2_shr_narrow(s, a, ops);
}

static bool trans_SQRSHRNT(DisasContext *s, arg_rri_esz *a)
{
    static const GVecGen2i ops[3] = {
        { .fno = gen_helper_sve2_sqrshrnt_h },
        { .fno = gen_helper_sve2_sqrshrnt_s },
        { .fno = gen_helper_sve2_sqrshrnt_d },
    };
    return do_sve2_shr_narrow(s, a, ops);
}

static void gen_uqshrnb_vec(unsigned vece, TCGv_vec d,
                            TCGv_vec n, int64_t shr)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    int halfbits = 4 << vece;

    tcg_gen_shri_vec(vece, n, n, shr);
    tcg_gen_dupi_vec(vece, t, MAKE_64BIT_MASK(0, halfbits));
    tcg_gen_umin_vec(vece, d, n, t);
    tcg_temp_free_vec(t);
}

static bool trans_UQSHRNB(DisasContext *s, arg_rri_esz *a)
{
    static const TCGOpcode vec_list[] = {
        INDEX_op_shri_vec, INDEX_op_umin_vec, 0
    };
    static const GVecGen2i ops[3] = {
        { .fniv = gen_uqshrnb_vec,
          .opt_opc = vec_list,
          .fno = gen_helper_sve2_uqshrnb_h,
          .vece = MO_16 },
        { .fniv = gen_uqshrnb_vec,
          .opt_opc = vec_list,
          .fno = gen_helper_sve2_uqshrnb_s,
          .vece = MO_32 },
        { .fniv = gen_uqshrnb_vec,
          .opt_opc = vec_list,
          .fno = gen_helper_sve2_uqshrnb_d,
          .vece = MO_64 },
    };
    return do_sve2_shr_narrow(s, a, ops);
}

static void gen_uqshrnt_vec(unsigned vece, TCGv_vec d,
                            TCGv_vec n, int64_t shr)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    int halfbits = 4 << vece;

    tcg_gen_shri_vec(vece, n, n, shr);
    tcg_gen_dupi_vec(vece, t, MAKE_64BIT_MASK(0, halfbits));
    tcg_gen_umin_vec(vece, n, n, t);
    tcg_gen_shli_vec(vece, n, n, halfbits);
    tcg_gen_bitsel_vec(vece, d, t, d, n);
    tcg_temp_free_vec(t);
}

static bool trans_UQSHRNT(DisasContext *s, arg_rri_esz *a)
{
    static const TCGOpcode vec_list[] = {
        INDEX_op_shli_vec, INDEX_op_shri_vec, INDEX_op_umin_vec, 0
    };
    static const GVecGen2i ops[3] = {
        { .fniv = gen_uqshrnt_vec,
          .opt_opc = vec_list,
          .load_dest = true,
          .fno = gen_helper_sve2_uqshrnt_h,
          .vece = MO_16 },
        { .fniv = gen_uqshrnt_vec,
          .opt_opc = vec_list,
          .load_dest = true,
          .fno = gen_helper_sve2_uqshrnt_s,
          .vece = MO_32 },
        { .fniv = gen_uqshrnt_vec,
          .opt_opc = vec_list,
          .load_dest = true,
          .fno = gen_helper_sve2_uqshrnt_d,
          .vece = MO_64 },
    };
    return do_sve2_shr_narrow(s, a, ops);
}

static bool trans_UQRSHRNB(DisasContext *s, arg_rri_esz *a)
{
    static const GVecGen2i ops[3] = {
        { .fno = gen_helper_sve2_uqrshrnb_h },
        { .fno = gen_helper_sve2_uqrshrnb_s },
        { .fno = gen_helper_sve2_uqrshrnb_d },
    };
    return do_sve2_shr_narrow(s, a, ops);
}

static bool trans_UQRSHRNT(DisasContext *s, arg_rri_esz *a)
{
    static const GVecGen2i ops[3] = {
        { .fno = gen_helper_sve2_uqrshrnt_h },
        { .fno = gen_helper_sve2_uqrshrnt_s },
        { .fno = gen_helper_sve2_uqrshrnt_d },
    };
    return do_sve2_shr_narrow(s, a, ops);
}

#define DO_SVE2_ZZZ_NARROW(NAME, name)                                    \
static bool trans_##NAME(DisasContext *s, arg_rrr_esz *a)                 \
{                                                                         \
    static gen_helper_gvec_3 * const fns[4] = {                           \
        NULL,                       gen_helper_sve2_##name##_h,           \
        gen_helper_sve2_##name##_s, gen_helper_sve2_##name##_d,           \
    };                                                                    \
    return do_sve2_zzz_ool(s, a, fns[a->esz]);                            \
}

DO_SVE2_ZZZ_NARROW(ADDHNB, addhnb)
DO_SVE2_ZZZ_NARROW(ADDHNT, addhnt)
DO_SVE2_ZZZ_NARROW(RADDHNB, raddhnb)
DO_SVE2_ZZZ_NARROW(RADDHNT, raddhnt)

DO_SVE2_ZZZ_NARROW(SUBHNB, subhnb)
DO_SVE2_ZZZ_NARROW(SUBHNT, subhnt)
DO_SVE2_ZZZ_NARROW(RSUBHNB, rsubhnb)
DO_SVE2_ZZZ_NARROW(RSUBHNT, rsubhnt)

static bool do_sve2_ppzz_flags(DisasContext *s, arg_rprr_esz *a,
                               gen_helper_gvec_flags_4 *fn)
{
    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    return do_ppzz_flags(s, a, fn);
}

#define DO_SVE2_PPZZ_MATCH(NAME, name)                                      \
static bool trans_##NAME(DisasContext *s, arg_rprr_esz *a)                  \
{                                                                           \
    static gen_helper_gvec_flags_4 * const fns[4] = {                       \
        gen_helper_sve2_##name##_ppzz_b, gen_helper_sve2_##name##_ppzz_h,   \
        NULL,                            NULL                               \
    };                                                                      \
    return do_sve2_ppzz_flags(s, a, fns[a->esz]);                           \
}

DO_SVE2_PPZZ_MATCH(MATCH, match)
DO_SVE2_PPZZ_MATCH(NMATCH, nmatch)

static bool trans_HISTCNT(DisasContext *s, arg_rprr_esz *a)
{
    static gen_helper_gvec_4 * const fns[2] = {
        gen_helper_sve2_histcnt_s, gen_helper_sve2_histcnt_d
    };
    if (a->esz < 2) {
        return false;
    }
    return do_sve2_zpzz_ool(s, a, fns[a->esz - 2]);
}

static bool trans_HISTSEG(DisasContext *s, arg_rrr_esz *a)
{
    if (a->esz != 0) {
        return false;
    }
    return do_sve2_zzz_ool(s, a, gen_helper_sve2_histseg);
}

static bool do_sve2_zpzz_fp(DisasContext *s, arg_rprr_esz *a,
                            gen_helper_gvec_4_ptr *fn)
{
    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    return do_zpzz_fp(s, a, fn);
}

#define DO_SVE2_ZPZZ_FP(NAME, name)                                         \
static bool trans_##NAME(DisasContext *s, arg_rprr_esz *a)                  \
{                                                                           \
    static gen_helper_gvec_4_ptr * const fns[4] = {                         \
        NULL,                            gen_helper_sve2_##name##_zpzz_h,   \
        gen_helper_sve2_##name##_zpzz_s, gen_helper_sve2_##name##_zpzz_d    \
    };                                                                      \
    return do_sve2_zpzz_fp(s, a, fns[a->esz]);                              \
}

DO_SVE2_ZPZZ_FP(FADDP, faddp)
DO_SVE2_ZPZZ_FP(FMAXNMP, fmaxnmp)
DO_SVE2_ZPZZ_FP(FMINNMP, fminnmp)
DO_SVE2_ZPZZ_FP(FMAXP, fmaxp)
DO_SVE2_ZPZZ_FP(FMINP, fminp)

/*
 * SVE Integer Multiply-Add (unpredicated)
 */

static bool trans_FMMLA(DisasContext *s, arg_rrrr_esz *a)
{
    gen_helper_gvec_4_ptr *fn;

    switch (a->esz) {
    case MO_32:
        if (!dc_isar_feature(aa64_sve_f32mm, s)) {
            return false;
        }
        fn = gen_helper_fmmla_s;
        break;
    case MO_64:
        if (!dc_isar_feature(aa64_sve_f64mm, s)) {
            return false;
        }
        fn = gen_helper_fmmla_d;
        break;
    default:
        return false;
    }

    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_ptr status = fpstatus_ptr(FPST_FPCR);
        tcg_gen_gvec_4_ptr(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           vec_full_reg_offset(s, a->ra),
                           status, vsz, vsz, 0, fn);
        tcg_temp_free_ptr(status);
    }
    return true;
}

static bool do_sqdmlal_zzzw(DisasContext *s, arg_rrrr_esz *a,
                            bool sel1, bool sel2)
{
    static gen_helper_gvec_4 * const fns[] = {
        NULL,                           gen_helper_sve2_sqdmlal_zzzw_h,
        gen_helper_sve2_sqdmlal_zzzw_s, gen_helper_sve2_sqdmlal_zzzw_d,
    };
    return do_sve2_zzzz_ool(s, a, fns[a->esz], (sel2 << 1) | sel1);
}

static bool do_sqdmlsl_zzzw(DisasContext *s, arg_rrrr_esz *a,
                            bool sel1, bool sel2)
{
    static gen_helper_gvec_4 * const fns[] = {
        NULL,                           gen_helper_sve2_sqdmlsl_zzzw_h,
        gen_helper_sve2_sqdmlsl_zzzw_s, gen_helper_sve2_sqdmlsl_zzzw_d,
    };
    return do_sve2_zzzz_ool(s, a, fns[a->esz], (sel2 << 1) | sel1);
}

static bool trans_SQDMLALB_zzzw(DisasContext *s, arg_rrrr_esz *a)
{
    return do_sqdmlal_zzzw(s, a, false, false);
}

static bool trans_SQDMLALT_zzzw(DisasContext *s, arg_rrrr_esz *a)
{
    return do_sqdmlal_zzzw(s, a, true, true);
}

static bool trans_SQDMLALBT(DisasContext *s, arg_rrrr_esz *a)
{
    return do_sqdmlal_zzzw(s, a, false, true);
}

static bool trans_SQDMLSLB_zzzw(DisasContext *s, arg_rrrr_esz *a)
{
    return do_sqdmlsl_zzzw(s, a, false, false);
}

static bool trans_SQDMLSLT_zzzw(DisasContext *s, arg_rrrr_esz *a)
{
    return do_sqdmlsl_zzzw(s, a, true, true);
}

static bool trans_SQDMLSLBT(DisasContext *s, arg_rrrr_esz *a)
{
    return do_sqdmlsl_zzzw(s, a, false, true);
}

static bool trans_SQRDMLAH_zzzz(DisasContext *s, arg_rrrr_esz *a)
{
    static gen_helper_gvec_4 * const fns[] = {
        gen_helper_sve2_sqrdmlah_b, gen_helper_sve2_sqrdmlah_h,
        gen_helper_sve2_sqrdmlah_s, gen_helper_sve2_sqrdmlah_d,
    };
    return do_sve2_zzzz_ool(s, a, fns[a->esz], 0);
}

static bool trans_SQRDMLSH_zzzz(DisasContext *s, arg_rrrr_esz *a)
{
    static gen_helper_gvec_4 * const fns[] = {
        gen_helper_sve2_sqrdmlsh_b, gen_helper_sve2_sqrdmlsh_h,
        gen_helper_sve2_sqrdmlsh_s, gen_helper_sve2_sqrdmlsh_d,
    };
    return do_sve2_zzzz_ool(s, a, fns[a->esz], 0);
}

static bool do_smlal_zzzw(DisasContext *s, arg_rrrr_esz *a, bool sel)
{
    static gen_helper_gvec_4 * const fns[] = {
        NULL,                         gen_helper_sve2_smlal_zzzw_h,
        gen_helper_sve2_smlal_zzzw_s, gen_helper_sve2_smlal_zzzw_d,
    };
    return do_sve2_zzzz_ool(s, a, fns[a->esz], sel);
}

static bool trans_SMLALB_zzzw(DisasContext *s, arg_rrrr_esz *a)
{
    return do_smlal_zzzw(s, a, false);
}

static bool trans_SMLALT_zzzw(DisasContext *s, arg_rrrr_esz *a)
{
    return do_smlal_zzzw(s, a, true);
}

static bool do_umlal_zzzw(DisasContext *s, arg_rrrr_esz *a, bool sel)
{
    static gen_helper_gvec_4 * const fns[] = {
        NULL,                         gen_helper_sve2_umlal_zzzw_h,
        gen_helper_sve2_umlal_zzzw_s, gen_helper_sve2_umlal_zzzw_d,
    };
    return do_sve2_zzzz_ool(s, a, fns[a->esz], sel);
}

static bool trans_UMLALB_zzzw(DisasContext *s, arg_rrrr_esz *a)
{
    return do_umlal_zzzw(s, a, false);
}

static bool trans_UMLALT_zzzw(DisasContext *s, arg_rrrr_esz *a)
{
    return do_umlal_zzzw(s, a, true);
}

static bool do_smlsl_zzzw(DisasContext *s, arg_rrrr_esz *a, bool sel)
{
    static gen_helper_gvec_4 * const fns[] = {
        NULL,                         gen_helper_sve2_smlsl_zzzw_h,
        gen_helper_sve2_smlsl_zzzw_s, gen_helper_sve2_smlsl_zzzw_d,
    };
    return do_sve2_zzzz_ool(s, a, fns[a->esz], sel);
}

static bool trans_SMLSLB_zzzw(DisasContext *s, arg_rrrr_esz *a)
{
    return do_smlsl_zzzw(s, a, false);
}

static bool trans_SMLSLT_zzzw(DisasContext *s, arg_rrrr_esz *a)
{
    return do_smlsl_zzzw(s, a, true);
}

static bool do_umlsl_zzzw(DisasContext *s, arg_rrrr_esz *a, bool sel)
{
    static gen_helper_gvec_4 * const fns[] = {
        NULL,                         gen_helper_sve2_umlsl_zzzw_h,
        gen_helper_sve2_umlsl_zzzw_s, gen_helper_sve2_umlsl_zzzw_d,
    };
    return do_sve2_zzzz_ool(s, a, fns[a->esz], sel);
}

static bool trans_UMLSLB_zzzw(DisasContext *s, arg_rrrr_esz *a)
{
    return do_umlsl_zzzw(s, a, false);
}

static bool trans_UMLSLT_zzzw(DisasContext *s, arg_rrrr_esz *a)
{
    return do_umlsl_zzzw(s, a, true);
}

static bool trans_CMLA_zzzz(DisasContext *s, arg_CMLA_zzzz *a)
{
    static gen_helper_gvec_4 * const fns[] = {
        gen_helper_sve2_cmla_zzzz_b, gen_helper_sve2_cmla_zzzz_h,
        gen_helper_sve2_cmla_zzzz_s, gen_helper_sve2_cmla_zzzz_d,
    };

    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_gvec_ool_zzzz(s, fns[a->esz], a->rd, a->rn, a->rm, a->ra, a->rot);
    }
    return true;
}

static bool trans_CDOT_zzzz(DisasContext *s, arg_CMLA_zzzz *a)
{
    if (!dc_isar_feature(aa64_sve2, s) || a->esz < MO_32) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_helper_gvec_4 *fn = (a->esz == MO_32
                                 ? gen_helper_sve2_cdot_zzzz_s
                                 : gen_helper_sve2_cdot_zzzz_d);
        gen_gvec_ool_zzzz(s, fn, a->rd, a->rn, a->rm, a->ra, a->rot);
    }
    return true;
}

static bool trans_SQRDCMLAH_zzzz(DisasContext *s, arg_SQRDCMLAH_zzzz *a)
{
    static gen_helper_gvec_4 * const fns[] = {
        gen_helper_sve2_sqrdcmlah_zzzz_b, gen_helper_sve2_sqrdcmlah_zzzz_h,
        gen_helper_sve2_sqrdcmlah_zzzz_s, gen_helper_sve2_sqrdcmlah_zzzz_d,
    };

    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_gvec_ool_zzzz(s, fns[a->esz], a->rd, a->rn, a->rm, a->ra, a->rot);
    }
    return true;
}

static bool trans_USDOT_zzzz(DisasContext *s, arg_USDOT_zzzz *a)
{
    if (a->esz != 2 || !dc_isar_feature(aa64_sve_i8mm, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_4_ool(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           vec_full_reg_offset(s, a->ra),
                           vsz, vsz, 0, gen_helper_gvec_usdot_b);
    }
    return true;
}

static bool trans_AESMC(DisasContext *s, arg_AESMC *a)
{
    if (!dc_isar_feature(aa64_sve2_aes, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_gvec_ool_zz(s, gen_helper_crypto_aesmc, a->rd, a->rd, a->decrypt);
    }
    return true;
}

static bool do_aese(DisasContext *s, arg_rrr_esz *a, bool decrypt)
{
    if (!dc_isar_feature(aa64_sve2_aes, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_gvec_ool_zzz(s, gen_helper_crypto_aese,
                         a->rd, a->rn, a->rm, decrypt);
    }
    return true;
}

static bool trans_AESE(DisasContext *s, arg_rrr_esz *a)
{
    return do_aese(s, a, false);
}

static bool trans_AESD(DisasContext *s, arg_rrr_esz *a)
{
    return do_aese(s, a, true);
}

static bool do_sm4(DisasContext *s, arg_rrr_esz *a, gen_helper_gvec_3 *fn)
{
    if (!dc_isar_feature(aa64_sve2_sm4, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_gvec_ool_zzz(s, fn, a->rd, a->rn, a->rm, 0);
    }
    return true;
}

static bool trans_SM4E(DisasContext *s, arg_rrr_esz *a)
{
    return do_sm4(s, a, gen_helper_crypto_sm4e);
}

static bool trans_SM4EKEY(DisasContext *s, arg_rrr_esz *a)
{
    return do_sm4(s, a, gen_helper_crypto_sm4ekey);
}

static bool trans_RAX1(DisasContext *s, arg_rrr_esz *a)
{
    if (!dc_isar_feature(aa64_sve2_sha3, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_gvec_fn_zzz(s, gen_gvec_rax1, MO_64, a->rd, a->rn, a->rm);
    }
    return true;
}

static bool trans_FCVTNT_sh(DisasContext *s, arg_rpr_esz *a)
{
    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve2_fcvtnt_sh);
}

static bool trans_BFCVTNT(DisasContext *s, arg_rpr_esz *a)
{
    if (!dc_isar_feature(aa64_sve_bf16, s)) {
        return false;
    }
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_bfcvtnt);
}

static bool trans_FCVTNT_ds(DisasContext *s, arg_rpr_esz *a)
{
    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve2_fcvtnt_ds);
}

static bool trans_FCVTLT_hs(DisasContext *s, arg_rpr_esz *a)
{
    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve2_fcvtlt_hs);
}

static bool trans_FCVTLT_sd(DisasContext *s, arg_rpr_esz *a)
{
    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve2_fcvtlt_sd);
}

static bool trans_FCVTX_ds(DisasContext *s, arg_rpr_esz *a)
{
    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    return do_frint_mode(s, a, float_round_to_odd, gen_helper_sve_fcvt_ds);
}

static bool trans_FCVTXNT_ds(DisasContext *s, arg_rpr_esz *a)
{
    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    return do_frint_mode(s, a, float_round_to_odd, gen_helper_sve2_fcvtnt_ds);
}

static bool trans_FLOGB(DisasContext *s, arg_rpr_esz *a)
{
    static gen_helper_gvec_3_ptr * const fns[] = {
        NULL,               gen_helper_flogb_h,
        gen_helper_flogb_s, gen_helper_flogb_d
    };

    if (!dc_isar_feature(aa64_sve2, s) || fns[a->esz] == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        TCGv_ptr status =
            fpstatus_ptr(a->esz == MO_16 ? FPST_FPCR_F16 : FPST_FPCR);
        unsigned vsz = vec_full_reg_size(s);

        tcg_gen_gvec_3_ptr(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           pred_full_reg_offset(s, a->pg),
                           status, vsz, vsz, 0, fns[a->esz]);
        tcg_temp_free_ptr(status);
    }
    return true;
}

static bool do_FMLAL_zzzw(DisasContext *s, arg_rrrr_esz *a, bool sub, bool sel)
{
    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_4_ptr(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           vec_full_reg_offset(s, a->ra),
                           cpu_env, vsz, vsz, (sel << 1) | sub,
                           gen_helper_sve2_fmlal_zzzw_s);
    }
    return true;
}

static bool trans_FMLALB_zzzw(DisasContext *s, arg_rrrr_esz *a)
{
    return do_FMLAL_zzzw(s, a, false, false);
}

static bool trans_FMLALT_zzzw(DisasContext *s, arg_rrrr_esz *a)
{
    return do_FMLAL_zzzw(s, a, false, true);
}

static bool trans_FMLSLB_zzzw(DisasContext *s, arg_rrrr_esz *a)
{
    return do_FMLAL_zzzw(s, a, true, false);
}

static bool trans_FMLSLT_zzzw(DisasContext *s, arg_rrrr_esz *a)
{
    return do_FMLAL_zzzw(s, a, true, true);
}

static bool do_FMLAL_zzxw(DisasContext *s, arg_rrxr_esz *a, bool sub, bool sel)
{
    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_4_ptr(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           vec_full_reg_offset(s, a->ra),
                           cpu_env, vsz, vsz,
                           (a->index << 2) | (sel << 1) | sub,
                           gen_helper_sve2_fmlal_zzxw_s);
    }
    return true;
}

static bool trans_FMLALB_zzxw(DisasContext *s, arg_rrxr_esz *a)
{
    return do_FMLAL_zzxw(s, a, false, false);
}

static bool trans_FMLALT_zzxw(DisasContext *s, arg_rrxr_esz *a)
{
    return do_FMLAL_zzxw(s, a, false, true);
}

static bool trans_FMLSLB_zzxw(DisasContext *s, arg_rrxr_esz *a)
{
    return do_FMLAL_zzxw(s, a, true, false);
}

static bool trans_FMLSLT_zzxw(DisasContext *s, arg_rrxr_esz *a)
{
    return do_FMLAL_zzxw(s, a, true, true);
}

static bool do_i8mm_zzzz_ool(DisasContext *s, arg_rrrr_esz *a,
                             gen_helper_gvec_4 *fn, int data)
{
    if (!dc_isar_feature(aa64_sve_i8mm, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_gvec_ool_zzzz(s, fn, a->rd, a->rn, a->rm, a->ra, data);
    }
    return true;
}

static bool trans_SMMLA(DisasContext *s, arg_rrrr_esz *a)
{
    return do_i8mm_zzzz_ool(s, a, gen_helper_gvec_smmla_b, 0);
}

static bool trans_USMMLA(DisasContext *s, arg_rrrr_esz *a)
{
    return do_i8mm_zzzz_ool(s, a, gen_helper_gvec_usmmla_b, 0);
}

static bool trans_UMMLA(DisasContext *s, arg_rrrr_esz *a)
{
    return do_i8mm_zzzz_ool(s, a, gen_helper_gvec_ummla_b, 0);
}

static bool trans_BFDOT_zzzz(DisasContext *s, arg_rrrr_esz *a)
{
    if (!dc_isar_feature(aa64_sve_bf16, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_gvec_ool_zzzz(s, gen_helper_gvec_bfdot,
                          a->rd, a->rn, a->rm, a->ra, 0);
    }
    return true;
}

static bool trans_BFDOT_zzxz(DisasContext *s, arg_rrxr_esz *a)
{
    if (!dc_isar_feature(aa64_sve_bf16, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_gvec_ool_zzzz(s, gen_helper_gvec_bfdot_idx,
                          a->rd, a->rn, a->rm, a->ra, a->index);
    }
    return true;
}

static bool trans_BFMMLA(DisasContext *s, arg_rrrr_esz *a)
{
    if (!dc_isar_feature(aa64_sve_bf16, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        gen_gvec_ool_zzzz(s, gen_helper_gvec_bfmmla,
                          a->rd, a->rn, a->rm, a->ra, 0);
    }
    return true;
}

static bool do_BFMLAL_zzzw(DisasContext *s, arg_rrrr_esz *a, bool sel)
{
    if (!dc_isar_feature(aa64_sve_bf16, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        TCGv_ptr status = fpstatus_ptr(FPST_FPCR);
        unsigned vsz = vec_full_reg_size(s);

        tcg_gen_gvec_4_ptr(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           vec_full_reg_offset(s, a->ra),
                           status, vsz, vsz, sel,
                           gen_helper_gvec_bfmlal);
        tcg_temp_free_ptr(status);
    }
    return true;
}

static bool trans_BFMLALB_zzzw(DisasContext *s, arg_rrrr_esz *a)
{
    return do_BFMLAL_zzzw(s, a, false);
}

static bool trans_BFMLALT_zzzw(DisasContext *s, arg_rrrr_esz *a)
{
    return do_BFMLAL_zzzw(s, a, true);
}

static bool do_BFMLAL_zzxw(DisasContext *s, arg_rrxr_esz *a, bool sel)
{
    if (!dc_isar_feature(aa64_sve_bf16, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        TCGv_ptr status = fpstatus_ptr(FPST_FPCR);
        unsigned vsz = vec_full_reg_size(s);

        tcg_gen_gvec_4_ptr(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           vec_full_reg_offset(s, a->ra),
                           status, vsz, vsz, (a->index << 1) | sel,
                           gen_helper_gvec_bfmlal_idx);
        tcg_temp_free_ptr(status);
    }
    return true;
}

static bool trans_BFMLALB_zzxw(DisasContext *s, arg_rrxr_esz *a)
{
    return do_BFMLAL_zzxw(s, a, false);
}

static bool trans_BFMLALT_zzxw(DisasContext *s, arg_rrxr_esz *a)
{
    return do_BFMLAL_zzxw(s, a, true);
}
