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
#include "translate.h"
#include "translate-a64.h"
#include "fpu/softfloat.h"


typedef void GVecGen2sFn(unsigned, uint32_t, uint32_t,
                         TCGv_i64, uint32_t, uint32_t);

typedef void gen_helper_gvec_flags_3(TCGv_i32, TCGv_ptr, TCGv_ptr,
                                     TCGv_ptr, TCGv_i32);
typedef void gen_helper_gvec_flags_4(TCGv_i32, TCGv_ptr, TCGv_ptr,
                                     TCGv_ptr, TCGv_ptr, TCGv_i32);

typedef void gen_helper_gvec_mem(TCGv_env, TCGv_ptr, TCGv_i64, TCGv_i64);
typedef void gen_helper_gvec_mem_scatter(TCGv_env, TCGv_ptr, TCGv_ptr,
                                         TCGv_ptr, TCGv_i64, TCGv_i64);

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
    /*
     * We won't use the tszimm_shr() value if tszimm_esz() returns -1 (the
     * trans function will check for esz < 0), so we can return any
     * value we like from here in that case as long as we avoid UB.
     */
    int esz = tszimm_esz(s, x);
    if (esz < 0) {
        return esz;
    }
    return (16 << esz) - x;
}

/* See e.g. LSL (immediate, predicated).  */
static int tszimm_shl(DisasContext *s, int x)
{
    /* As with tszimm_shr(), value will be unused if esz < 0 */
    int esz = tszimm_esz(s, x);
    if (esz < 0) {
        return esz;
    }
    return x - (8 << esz);
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
    static const uint8_t dtype[5] = { 0, 5, 10, 15, 18 };
    return dtype[msz];
}

/*
 * Include the generated decoder.
 */

#include "decode-sve.c.inc"

/*
 * Implement all of the translator functions referenced by the decoder.
 */

/* Invoke an out-of-line helper on 2 Zregs. */
static bool gen_gvec_ool_zz(DisasContext *s, gen_helper_gvec_2 *fn,
                            int rd, int rn, int data)
{
    if (fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_2_ool(vec_full_reg_offset(s, rd),
                           vec_full_reg_offset(s, rn),
                           vsz, vsz, data, fn);
    }
    return true;
}

static bool gen_gvec_fpst_zz(DisasContext *s, gen_helper_gvec_2_ptr *fn,
                             int rd, int rn, int data,
                             ARMFPStatusFlavour flavour)
{
    if (fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_ptr status = fpstatus_ptr(flavour);

        tcg_gen_gvec_2_ptr(vec_full_reg_offset(s, rd),
                           vec_full_reg_offset(s, rn),
                           status, vsz, vsz, data, fn);
    }
    return true;
}

static bool gen_gvec_fpst_ah_arg_zz(DisasContext *s, gen_helper_gvec_2_ptr *fn,
                                    arg_rr_esz *a, int data)
{
    return gen_gvec_fpst_zz(s, fn, a->rd, a->rn, data,
                            select_ah_fpst(s, a->esz));
}

/* Invoke an out-of-line helper on 3 Zregs. */
static bool gen_gvec_ool_zzz(DisasContext *s, gen_helper_gvec_3 *fn,
                             int rd, int rn, int rm, int data)
{
    if (fn == NULL) {
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

static bool gen_gvec_ool_arg_zzz(DisasContext *s, gen_helper_gvec_3 *fn,
                                 arg_rrr_esz *a, int data)
{
    return gen_gvec_ool_zzz(s, fn, a->rd, a->rn, a->rm, data);
}

/* Invoke an out-of-line helper on 3 Zregs, plus float_status. */
static bool gen_gvec_fpst_zzz(DisasContext *s, gen_helper_gvec_3_ptr *fn,
                              int rd, int rn, int rm,
                              int data, ARMFPStatusFlavour flavour)
{
    if (fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_ptr status = fpstatus_ptr(flavour);

        tcg_gen_gvec_3_ptr(vec_full_reg_offset(s, rd),
                           vec_full_reg_offset(s, rn),
                           vec_full_reg_offset(s, rm),
                           status, vsz, vsz, data, fn);
    }
    return true;
}

static bool gen_gvec_fpst_arg_zzz(DisasContext *s, gen_helper_gvec_3_ptr *fn,
                                  arg_rrr_esz *a, int data)
{
    /* These insns use MO_8 to encode BFloat16 */
    if (a->esz == MO_8 && !dc_isar_feature(aa64_sve_b16b16, s)) {
        return false;
    }
    return gen_gvec_fpst_zzz(s, fn, a->rd, a->rn, a->rm, data,
                             a->esz == MO_16 ? FPST_A64_F16 : FPST_A64);
}

static bool gen_gvec_fpst_ah_arg_zzz(DisasContext *s, gen_helper_gvec_3_ptr *fn,
                                     arg_rrr_esz *a, int data)
{
    return gen_gvec_fpst_zzz(s, fn, a->rd, a->rn, a->rm, data,
                             select_ah_fpst(s, a->esz));
}

/* Invoke an out-of-line helper on 4 Zregs. */
static bool gen_gvec_ool_zzzz(DisasContext *s, gen_helper_gvec_4 *fn,
                              int rd, int rn, int rm, int ra, int data)
{
    if (fn == NULL) {
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

static bool gen_gvec_ool_arg_zzzz(DisasContext *s, gen_helper_gvec_4 *fn,
                                  arg_rrrr_esz *a, int data)
{
    return gen_gvec_ool_zzzz(s, fn, a->rd, a->rn, a->rm, a->ra, data);
}

static bool gen_gvec_ool_arg_zzxz(DisasContext *s, gen_helper_gvec_4 *fn,
                                  arg_rrxr_esz *a)
{
    return gen_gvec_ool_zzzz(s, fn, a->rd, a->rn, a->rm, a->ra, a->index);
}

/* Invoke an out-of-line helper on 4 Zregs, plus a pointer. */
static bool gen_gvec_ptr_zzzz(DisasContext *s, gen_helper_gvec_4_ptr *fn,
                              int rd, int rn, int rm, int ra,
                              int data, TCGv_ptr ptr)
{
    if (fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_4_ptr(vec_full_reg_offset(s, rd),
                           vec_full_reg_offset(s, rn),
                           vec_full_reg_offset(s, rm),
                           vec_full_reg_offset(s, ra),
                           ptr, vsz, vsz, data, fn);
    }
    return true;
}

static bool gen_gvec_fpst_zzzz(DisasContext *s, gen_helper_gvec_4_ptr *fn,
                               int rd, int rn, int rm, int ra,
                               int data, ARMFPStatusFlavour flavour)
{
    TCGv_ptr status = fpstatus_ptr(flavour);
    bool ret = gen_gvec_ptr_zzzz(s, fn, rd, rn, rm, ra, data, status);
    return ret;
}

static bool gen_gvec_env_zzzz(DisasContext *s, gen_helper_gvec_4_ptr *fn,
                              int rd, int rn, int rm, int ra,
                              int data)
{
    return gen_gvec_ptr_zzzz(s, fn, rd, rn, rm, ra, data, tcg_env);
}

static bool gen_gvec_env_arg_zzzz(DisasContext *s, gen_helper_gvec_4_ptr *fn,
                                  arg_rrrr_esz *a, int data)
{
    return gen_gvec_env_zzzz(s, fn, a->rd, a->rn, a->rm, a->ra, data);
}

static bool gen_gvec_env_arg_zzxz(DisasContext *s, gen_helper_gvec_4_ptr *fn,
                                  arg_rrxr_esz *a)
{
    return gen_gvec_env_zzzz(s, fn, a->rd, a->rn, a->rm, a->ra, a->index);
}

/* Invoke an out-of-line helper on 4 Zregs, 1 Preg, plus fpst. */
static bool gen_gvec_fpst_zzzzp(DisasContext *s, gen_helper_gvec_5_ptr *fn,
                                int rd, int rn, int rm, int ra, int pg,
                                int data, ARMFPStatusFlavour flavour)
{
    if (fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_ptr status = fpstatus_ptr(flavour);

        tcg_gen_gvec_5_ptr(vec_full_reg_offset(s, rd),
                           vec_full_reg_offset(s, rn),
                           vec_full_reg_offset(s, rm),
                           vec_full_reg_offset(s, ra),
                           pred_full_reg_offset(s, pg),
                           status, vsz, vsz, data, fn);
    }
    return true;
}

/* Invoke an out-of-line helper on 2 Zregs and a predicate. */
static bool gen_gvec_ool_zzp(DisasContext *s, gen_helper_gvec_3 *fn,
                             int rd, int rn, int pg, int data)
{
    if (fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_3_ool(vec_full_reg_offset(s, rd),
                           vec_full_reg_offset(s, rn),
                           pred_full_reg_offset(s, pg),
                           vsz, vsz, data, fn);
    }
    return true;
}

static bool gen_gvec_ool_arg_zpz(DisasContext *s, gen_helper_gvec_3 *fn,
                                 arg_rpr_esz *a, int data)
{
    return gen_gvec_ool_zzp(s, fn, a->rd, a->rn, a->pg, data);
}

static bool gen_gvec_ool_arg_zpzi(DisasContext *s, gen_helper_gvec_3 *fn,
                                  arg_rpri_esz *a)
{
    return gen_gvec_ool_zzp(s, fn, a->rd, a->rn, a->pg, a->imm);
}

static bool gen_gvec_fpst_zzp(DisasContext *s, gen_helper_gvec_3_ptr *fn,
                              int rd, int rn, int pg, int data,
                              ARMFPStatusFlavour flavour)
{
    if (fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_ptr status = fpstatus_ptr(flavour);

        tcg_gen_gvec_3_ptr(vec_full_reg_offset(s, rd),
                           vec_full_reg_offset(s, rn),
                           pred_full_reg_offset(s, pg),
                           status, vsz, vsz, data, fn);
    }
    return true;
}

static bool gen_gvec_fpst_arg_zpz(DisasContext *s, gen_helper_gvec_3_ptr *fn,
                                  arg_rpr_esz *a, int data,
                                  ARMFPStatusFlavour flavour)
{
    return gen_gvec_fpst_zzp(s, fn, a->rd, a->rn, a->pg, data, flavour);
}

/* Invoke an out-of-line helper on 3 Zregs and a predicate. */
static bool gen_gvec_ool_zzzp(DisasContext *s, gen_helper_gvec_4 *fn,
                              int rd, int rn, int rm, int pg, int data)
{
    if (fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_4_ool(vec_full_reg_offset(s, rd),
                           vec_full_reg_offset(s, rn),
                           vec_full_reg_offset(s, rm),
                           pred_full_reg_offset(s, pg),
                           vsz, vsz, data, fn);
    }
    return true;
}

static bool gen_gvec_ool_arg_zpzz(DisasContext *s, gen_helper_gvec_4 *fn,
                                  arg_rprr_esz *a, int data)
{
    return gen_gvec_ool_zzzp(s, fn, a->rd, a->rn, a->rm, a->pg, data);
}

/* Invoke an out-of-line helper on 3 Zregs and a predicate. */
static bool gen_gvec_fpst_zzzp(DisasContext *s, gen_helper_gvec_4_ptr *fn,
                               int rd, int rn, int rm, int pg, int data,
                               ARMFPStatusFlavour flavour)
{
    if (fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_ptr status = fpstatus_ptr(flavour);

        tcg_gen_gvec_4_ptr(vec_full_reg_offset(s, rd),
                           vec_full_reg_offset(s, rn),
                           vec_full_reg_offset(s, rm),
                           pred_full_reg_offset(s, pg),
                           status, vsz, vsz, data, fn);
    }
    return true;
}

static bool gen_gvec_fpst_arg_zpzz(DisasContext *s, gen_helper_gvec_4_ptr *fn,
                                   arg_rprr_esz *a)
{
    /* These insns use MO_8 to encode BFloat16. */
    if (a->esz == MO_8 && !dc_isar_feature(aa64_sve_b16b16, s)) {
        return false;
    }
    return gen_gvec_fpst_zzzp(s, fn, a->rd, a->rn, a->rm, a->pg, 0,
                              a->esz == MO_16 ? FPST_A64_F16 : FPST_A64);
}

/* Invoke a vector expander on two Zregs and an immediate.  */
static bool gen_gvec_fn_zzi(DisasContext *s, GVecGen2iFn *gvec_fn,
                            int esz, int rd, int rn, uint64_t imm)
{
    if (gvec_fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        gvec_fn(esz, vec_full_reg_offset(s, rd),
                vec_full_reg_offset(s, rn), imm, vsz, vsz);
    }
    return true;
}

static bool gen_gvec_fn_arg_zzi(DisasContext *s, GVecGen2iFn *gvec_fn,
                                arg_rri_esz *a)
{
    if (a->esz < 0) {
        /* Invalid tsz encoding -- see tszimm_esz. */
        return false;
    }
    return gen_gvec_fn_zzi(s, gvec_fn, a->esz, a->rd, a->rn, a->imm);
}

/* Invoke a vector expander on three Zregs.  */
static bool gen_gvec_fn_zzz(DisasContext *s, GVecGen3Fn *gvec_fn,
                            int esz, int rd, int rn, int rm)
{
    if (gvec_fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        gvec_fn(esz, vec_full_reg_offset(s, rd),
                vec_full_reg_offset(s, rn),
                vec_full_reg_offset(s, rm), vsz, vsz);
    }
    return true;
}

static bool gen_gvec_fn_arg_zzz(DisasContext *s, GVecGen3Fn *fn,
                                arg_rrr_esz *a)
{
    return gen_gvec_fn_zzz(s, fn, a->esz, a->rd, a->rn, a->rm);
}

/* Invoke a vector expander on four Zregs.  */
static bool gen_gvec_fn_arg_zzzz(DisasContext *s, GVecGen4Fn *gvec_fn,
                                 arg_rrrr_esz *a)
{
    if (gvec_fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        gvec_fn(a->esz, vec_full_reg_offset(s, a->rd),
                vec_full_reg_offset(s, a->rn),
                vec_full_reg_offset(s, a->rm),
                vec_full_reg_offset(s, a->ra), vsz, vsz);
    }
    return true;
}

/* Invoke a vector move on two Zregs.  */
static bool do_mov_z(DisasContext *s, int rd, int rn)
{
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_mov(MO_8, vec_full_reg_offset(s, rd),
                         vec_full_reg_offset(s, rn), vsz, vsz);
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
static bool gen_gvec_fn_ppp(DisasContext *s, GVecGen3Fn *gvec_fn,
                            int rd, int rn, int rm)
{
    if (sve_access_check(s)) {
        unsigned psz = pred_gvec_reg_size(s);
        gvec_fn(MO_64, pred_full_reg_offset(s, rd),
                pred_full_reg_offset(s, rn),
                pred_full_reg_offset(s, rm), psz, psz);
    }
    return true;
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
}

static void do_predtest(DisasContext *s, int dofs, int gofs, int words)
{
    TCGv_ptr dptr = tcg_temp_new_ptr();
    TCGv_ptr gptr = tcg_temp_new_ptr();
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_addi_ptr(dptr, tcg_env, dofs);
    tcg_gen_addi_ptr(gptr, tcg_env, gofs);

    gen_helper_sve_predtest(t, dptr, gptr, tcg_constant_i32(words));

    do_pred_flags(t);
}

/* For each element size, the bits within a predicate word that are active.  */
const uint64_t pred_esz_masks[5] = {
    0xffffffffffffffffull, 0x5555555555555555ull,
    0x1111111111111111ull, 0x0101010101010101ull,
    0x0001000100010001ull,
};

static bool trans_INVALID(DisasContext *s, arg_INVALID *a)
{
    unallocated_encoding(s);
    return true;
}

/*
 *** SVE Logical - Unpredicated Group
 */

TRANS_FEAT(AND_zzz, aa64_sve, gen_gvec_fn_arg_zzz, tcg_gen_gvec_and, a)
TRANS_FEAT(ORR_zzz, aa64_sve, gen_gvec_fn_arg_zzz, tcg_gen_gvec_or, a)
TRANS_FEAT(EOR_zzz, aa64_sve, gen_gvec_fn_arg_zzz, tcg_gen_gvec_xor, a)
TRANS_FEAT(BIC_zzz, aa64_sve, gen_gvec_fn_arg_zzz, tcg_gen_gvec_andc, a)

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

TRANS_FEAT(EOR3, aa64_sve2, gen_gvec_fn_arg_zzzz, gen_gvec_eor3, a)
TRANS_FEAT(BCAX, aa64_sve2, gen_gvec_fn_arg_zzzz, gen_gvec_bcax, a)

static void gen_bsl(unsigned vece, uint32_t d, uint32_t n, uint32_t m,
                    uint32_t a, uint32_t oprsz, uint32_t maxsz)
{
    /* BSL differs from the generic bitsel in argument ordering. */
    tcg_gen_gvec_bitsel(vece, d, a, n, m, oprsz, maxsz);
}

TRANS_FEAT(BSL, aa64_sve2, gen_gvec_fn_arg_zzzz, gen_bsl, a)

static void gen_bsl1n_i64(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m, TCGv_i64 k)
{
    tcg_gen_andc_i64(n, k, n);
    tcg_gen_andc_i64(m, m, k);
    tcg_gen_or_i64(d, n, m);
}

static void gen_bsl1n_vec(unsigned vece, TCGv_vec d, TCGv_vec n,
                          TCGv_vec m, TCGv_vec k)
{
    tcg_gen_not_vec(vece, n, n);
    tcg_gen_bitsel_vec(vece, d, k, n, m);
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

TRANS_FEAT(BSL1N, aa64_sve2, gen_gvec_fn_arg_zzzz, gen_bsl1n, a)

static void gen_bsl2n_i64(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m, TCGv_i64 k)
{
    /*
     * Z[dn] = (n & k) | (~m & ~k)
     *       =         | ~(m | k)
     */
    tcg_gen_and_i64(n, n, k);
    if (tcg_op_supported(INDEX_op_orc, TCG_TYPE_I64, 0)) {
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
    tcg_gen_not_vec(vece, m, m);
    tcg_gen_bitsel_vec(vece, d, k, n, m);
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

TRANS_FEAT(BSL2N, aa64_sve2, gen_gvec_fn_arg_zzzz, gen_bsl2n, a)

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

TRANS_FEAT(NBSL, aa64_sve2, gen_gvec_fn_arg_zzzz, gen_nbsl, a)

/*
 *** SVE Integer Arithmetic - Unpredicated Group
 */

TRANS_FEAT(ADD_zzz, aa64_sve, gen_gvec_fn_arg_zzz, tcg_gen_gvec_add, a)
TRANS_FEAT(SUB_zzz, aa64_sve, gen_gvec_fn_arg_zzz, tcg_gen_gvec_sub, a)
TRANS_FEAT(SQADD_zzz, aa64_sve, gen_gvec_fn_arg_zzz, tcg_gen_gvec_ssadd, a)
TRANS_FEAT(SQSUB_zzz, aa64_sve, gen_gvec_fn_arg_zzz, tcg_gen_gvec_sssub, a)
TRANS_FEAT(UQADD_zzz, aa64_sve, gen_gvec_fn_arg_zzz, tcg_gen_gvec_usadd, a)
TRANS_FEAT(UQSUB_zzz, aa64_sve, gen_gvec_fn_arg_zzz, tcg_gen_gvec_ussub, a)

/*
 *** SVE Integer Arithmetic - Binary Predicated Group
 */

/* Select active elememnts from Zn and inactive elements from Zm,
 * storing the result in Zd.
 */
static bool do_sel_z(DisasContext *s, int rd, int rn, int rm, int pg, int esz)
{
    static gen_helper_gvec_4 * const fns[4] = {
        gen_helper_sve_sel_zpzz_b, gen_helper_sve_sel_zpzz_h,
        gen_helper_sve_sel_zpzz_s, gen_helper_sve_sel_zpzz_d
    };
    return gen_gvec_ool_zzzp(s, fns[esz], rd, rn, rm, pg, 0);
}

#define DO_ZPZZ(NAME, FEAT, name) \
    static gen_helper_gvec_4 * const name##_zpzz_fns[4] = {               \
        gen_helper_##name##_zpzz_b, gen_helper_##name##_zpzz_h,           \
        gen_helper_##name##_zpzz_s, gen_helper_##name##_zpzz_d,           \
    };                                                                    \
    TRANS_FEAT(NAME, FEAT, gen_gvec_ool_arg_zpzz,                         \
               name##_zpzz_fns[a->esz], a, 0)

DO_ZPZZ(AND_zpzz, aa64_sve, sve_and)
DO_ZPZZ(EOR_zpzz, aa64_sve, sve_eor)
DO_ZPZZ(ORR_zpzz, aa64_sve, sve_orr)
DO_ZPZZ(BIC_zpzz, aa64_sve, sve_bic)

DO_ZPZZ(ADD_zpzz, aa64_sve, sve_add)
DO_ZPZZ(SUB_zpzz, aa64_sve, sve_sub)

DO_ZPZZ(SMAX_zpzz, aa64_sve, sve_smax)
DO_ZPZZ(UMAX_zpzz, aa64_sve, sve_umax)
DO_ZPZZ(SMIN_zpzz, aa64_sve, sve_smin)
DO_ZPZZ(UMIN_zpzz, aa64_sve, sve_umin)
DO_ZPZZ(SABD_zpzz, aa64_sve, sve_sabd)
DO_ZPZZ(UABD_zpzz, aa64_sve, sve_uabd)

DO_ZPZZ(MUL_zpzz, aa64_sve, sve_mul)
DO_ZPZZ(SMULH_zpzz, aa64_sve, sve_smulh)
DO_ZPZZ(UMULH_zpzz, aa64_sve, sve_umulh)

DO_ZPZZ(ASR_zpzz, aa64_sve, sve_asr)
DO_ZPZZ(LSR_zpzz, aa64_sve, sve_lsr)
DO_ZPZZ(LSL_zpzz, aa64_sve, sve_lsl)

static gen_helper_gvec_4 * const sdiv_fns[4] = {
    NULL, NULL, gen_helper_sve_sdiv_zpzz_s, gen_helper_sve_sdiv_zpzz_d
};
TRANS_FEAT(SDIV_zpzz, aa64_sve, gen_gvec_ool_arg_zpzz, sdiv_fns[a->esz], a, 0)

static gen_helper_gvec_4 * const udiv_fns[4] = {
    NULL, NULL, gen_helper_sve_udiv_zpzz_s, gen_helper_sve_udiv_zpzz_d
};
TRANS_FEAT(UDIV_zpzz, aa64_sve, gen_gvec_ool_arg_zpzz, udiv_fns[a->esz], a, 0)

TRANS_FEAT(SEL_zpzz, aa64_sve, do_sel_z, a->rd, a->rn, a->rm, a->pg, a->esz)

/*
 *** SVE Integer Arithmetic - Unary Predicated Group
 */

#define DO_ZPZ(NAME, FEAT, name) \
    static gen_helper_gvec_3 * const name##_fns[4] = {              \
        gen_helper_##name##_b, gen_helper_##name##_h,               \
        gen_helper_##name##_s, gen_helper_##name##_d,               \
    };                                                              \
    TRANS_FEAT(NAME, FEAT, gen_gvec_ool_arg_zpz, name##_fns[a->esz], a, 0)

DO_ZPZ(CLS, aa64_sve, sve_cls)
DO_ZPZ(CLZ, aa64_sve, sve_clz)
DO_ZPZ(CNT_zpz, aa64_sve, sve_cnt_zpz)
DO_ZPZ(CNOT, aa64_sve, sve_cnot)
DO_ZPZ(NOT_zpz, aa64_sve, sve_not_zpz)
DO_ZPZ(ABS, aa64_sve, sve_abs)
DO_ZPZ(NEG, aa64_sve, sve_neg)
DO_ZPZ(RBIT, aa64_sve, sve_rbit)
DO_ZPZ(ORQV, aa64_sme2p1_or_sve2p1, sve2p1_orqv)
DO_ZPZ(EORQV, aa64_sme2p1_or_sve2p1, sve2p1_eorqv)
DO_ZPZ(ANDQV, aa64_sme2p1_or_sve2p1, sve2p1_andqv)

static gen_helper_gvec_3 * const fabs_fns[4] = {
    NULL,                  gen_helper_sve_fabs_h,
    gen_helper_sve_fabs_s, gen_helper_sve_fabs_d,
};
static gen_helper_gvec_3 * const fabs_ah_fns[4] = {
    NULL,                  gen_helper_sve_ah_fabs_h,
    gen_helper_sve_ah_fabs_s, gen_helper_sve_ah_fabs_d,
};
TRANS_FEAT(FABS, aa64_sve, gen_gvec_ool_arg_zpz,
           s->fpcr_ah ? fabs_ah_fns[a->esz] : fabs_fns[a->esz], a, 0)

static gen_helper_gvec_3 * const fneg_fns[4] = {
    NULL,                  gen_helper_sve_fneg_h,
    gen_helper_sve_fneg_s, gen_helper_sve_fneg_d,
};
static gen_helper_gvec_3 * const fneg_ah_fns[4] = {
    NULL,                  gen_helper_sve_ah_fneg_h,
    gen_helper_sve_ah_fneg_s, gen_helper_sve_ah_fneg_d,
};
TRANS_FEAT(FNEG, aa64_sve, gen_gvec_ool_arg_zpz,
           s->fpcr_ah ? fneg_ah_fns[a->esz] : fneg_fns[a->esz], a, 0)

static gen_helper_gvec_3 * const sxtb_fns[4] = {
    NULL,                  gen_helper_sve_sxtb_h,
    gen_helper_sve_sxtb_s, gen_helper_sve_sxtb_d,
};
TRANS_FEAT(SXTB, aa64_sve, gen_gvec_ool_arg_zpz, sxtb_fns[a->esz], a, 0)

static gen_helper_gvec_3 * const uxtb_fns[4] = {
    NULL,                  gen_helper_sve_uxtb_h,
    gen_helper_sve_uxtb_s, gen_helper_sve_uxtb_d,
};
TRANS_FEAT(UXTB, aa64_sve, gen_gvec_ool_arg_zpz, uxtb_fns[a->esz], a, 0)

static gen_helper_gvec_3 * const sxth_fns[4] = {
    NULL, NULL, gen_helper_sve_sxth_s, gen_helper_sve_sxth_d
};
TRANS_FEAT(SXTH, aa64_sve, gen_gvec_ool_arg_zpz, sxth_fns[a->esz], a, 0)

static gen_helper_gvec_3 * const uxth_fns[4] = {
    NULL, NULL, gen_helper_sve_uxth_s, gen_helper_sve_uxth_d
};
TRANS_FEAT(UXTH, aa64_sve, gen_gvec_ool_arg_zpz, uxth_fns[a->esz], a, 0)

TRANS_FEAT(SXTW, aa64_sve, gen_gvec_ool_arg_zpz,
           a->esz == 3 ? gen_helper_sve_sxtw_d : NULL, a, 0)
TRANS_FEAT(UXTW, aa64_sve, gen_gvec_ool_arg_zpz,
           a->esz == 3 ? gen_helper_sve_uxtw_d : NULL, a, 0)

static gen_helper_gvec_3 * const addqv_fns[4] = {
    gen_helper_sve2p1_addqv_b, gen_helper_sve2p1_addqv_h,
    gen_helper_sve2p1_addqv_s, gen_helper_sve2p1_addqv_d,
};
TRANS_FEAT(ADDQV, aa64_sme2p1_or_sve2p1,
           gen_gvec_ool_arg_zpz, addqv_fns[a->esz], a, 0)

static gen_helper_gvec_3 * const smaxqv_fns[4] = {
    gen_helper_sve2p1_smaxqv_b, gen_helper_sve2p1_smaxqv_h,
    gen_helper_sve2p1_smaxqv_s, gen_helper_sve2p1_smaxqv_d,
};
TRANS_FEAT(SMAXQV, aa64_sme2p1_or_sve2p1,
           gen_gvec_ool_arg_zpz, smaxqv_fns[a->esz], a, 0)

static gen_helper_gvec_3 * const sminqv_fns[4] = {
    gen_helper_sve2p1_sminqv_b, gen_helper_sve2p1_sminqv_h,
    gen_helper_sve2p1_sminqv_s, gen_helper_sve2p1_sminqv_d,
};
TRANS_FEAT(SMINQV, aa64_sme2p1_or_sve2p1,
           gen_gvec_ool_arg_zpz, sminqv_fns[a->esz], a, 0)

static gen_helper_gvec_3 * const umaxqv_fns[4] = {
    gen_helper_sve2p1_umaxqv_b, gen_helper_sve2p1_umaxqv_h,
    gen_helper_sve2p1_umaxqv_s, gen_helper_sve2p1_umaxqv_d,
};
TRANS_FEAT(UMAXQV, aa64_sme2p1_or_sve2p1,
           gen_gvec_ool_arg_zpz, umaxqv_fns[a->esz], a, 0)

static gen_helper_gvec_3 * const uminqv_fns[4] = {
    gen_helper_sve2p1_uminqv_b, gen_helper_sve2p1_uminqv_h,
    gen_helper_sve2p1_uminqv_s, gen_helper_sve2p1_uminqv_d,
};
TRANS_FEAT(UMINQV, aa64_sme2p1_or_sve2p1,
           gen_gvec_ool_arg_zpz, uminqv_fns[a->esz], a, 0)

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

    desc = tcg_constant_i32(simd_desc(vsz, vsz, 0));
    temp = tcg_temp_new_i64();
    t_zn = tcg_temp_new_ptr();
    t_pg = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(t_zn, tcg_env, vec_full_reg_offset(s, a->rn));
    tcg_gen_addi_ptr(t_pg, tcg_env, pred_full_reg_offset(s, a->pg));
    fn(temp, t_zn, t_pg, desc);

    write_fp_dreg(s, a->rd, temp);
    return true;
}

#define DO_VPZ(NAME, name) \
    static gen_helper_gvec_reduc * const name##_fns[4] = {               \
        gen_helper_sve_##name##_b, gen_helper_sve_##name##_h,            \
        gen_helper_sve_##name##_s, gen_helper_sve_##name##_d,            \
    };                                                                   \
    TRANS_FEAT(NAME, aa64_sve, do_vpz_ool, a, name##_fns[a->esz])

DO_VPZ(ORV, orv)
DO_VPZ(ANDV, andv)
DO_VPZ(EORV, eorv)

DO_VPZ(UADDV, uaddv)
DO_VPZ(SMAXV, smaxv)
DO_VPZ(UMAXV, umaxv)
DO_VPZ(SMINV, sminv)
DO_VPZ(UMINV, uminv)

static gen_helper_gvec_reduc * const saddv_fns[4] = {
    gen_helper_sve_saddv_b, gen_helper_sve_saddv_h,
    gen_helper_sve_saddv_s, NULL
};
TRANS_FEAT(SADDV, aa64_sve, do_vpz_ool, a, saddv_fns[a->esz])

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
    return gen_gvec_ool_zzp(s, fns[esz], rd, rn, pg, invert);
}

static bool do_shift_zpzi(DisasContext *s, arg_rpri_esz *a, bool asr,
                          gen_helper_gvec_3 * const fns[4])
{
    int max;

    if (a->esz < 0) {
        /* Invalid tsz encoding -- see tszimm_esz. */
        return false;
    }

    /*
     * Shift by element size is architecturally valid.
     * For arithmetic right-shift, it's the same as by one less.
     * For logical shifts and ASRD, it is a zeroing operation.
     */
    max = 8 << a->esz;
    if (a->imm >= max) {
        if (asr) {
            a->imm = max - 1;
        } else {
            return do_movz_zpz(s, a->rd, a->rd, a->pg, a->esz, true);
        }
    }
    return gen_gvec_ool_arg_zpzi(s, fns[a->esz], a);
}

static gen_helper_gvec_3 * const asr_zpzi_fns[4] = {
    gen_helper_sve_asr_zpzi_b, gen_helper_sve_asr_zpzi_h,
    gen_helper_sve_asr_zpzi_s, gen_helper_sve_asr_zpzi_d,
};
TRANS_FEAT(ASR_zpzi, aa64_sve, do_shift_zpzi, a, true, asr_zpzi_fns)

static gen_helper_gvec_3 * const lsr_zpzi_fns[4] = {
    gen_helper_sve_lsr_zpzi_b, gen_helper_sve_lsr_zpzi_h,
    gen_helper_sve_lsr_zpzi_s, gen_helper_sve_lsr_zpzi_d,
};
TRANS_FEAT(LSR_zpzi, aa64_sve, do_shift_zpzi, a, false, lsr_zpzi_fns)

static gen_helper_gvec_3 * const lsl_zpzi_fns[4] = {
    gen_helper_sve_lsl_zpzi_b, gen_helper_sve_lsl_zpzi_h,
    gen_helper_sve_lsl_zpzi_s, gen_helper_sve_lsl_zpzi_d,
};
TRANS_FEAT(LSL_zpzi, aa64_sve, do_shift_zpzi, a, false, lsl_zpzi_fns)

static gen_helper_gvec_3 * const asrd_fns[4] = {
    gen_helper_sve_asrd_b, gen_helper_sve_asrd_h,
    gen_helper_sve_asrd_s, gen_helper_sve_asrd_d,
};
TRANS_FEAT(ASRD, aa64_sve, do_shift_zpzi, a, false, asrd_fns)

static gen_helper_gvec_3 * const sqshl_zpzi_fns[4] = {
    gen_helper_sve2_sqshl_zpzi_b, gen_helper_sve2_sqshl_zpzi_h,
    gen_helper_sve2_sqshl_zpzi_s, gen_helper_sve2_sqshl_zpzi_d,
};
TRANS_FEAT(SQSHL_zpzi, aa64_sve2, gen_gvec_ool_arg_zpzi,
           a->esz < 0 ? NULL : sqshl_zpzi_fns[a->esz], a)

static gen_helper_gvec_3 * const uqshl_zpzi_fns[4] = {
    gen_helper_sve2_uqshl_zpzi_b, gen_helper_sve2_uqshl_zpzi_h,
    gen_helper_sve2_uqshl_zpzi_s, gen_helper_sve2_uqshl_zpzi_d,
};
TRANS_FEAT(UQSHL_zpzi, aa64_sve2, gen_gvec_ool_arg_zpzi,
           a->esz < 0 ? NULL : uqshl_zpzi_fns[a->esz], a)

static gen_helper_gvec_3 * const srshr_fns[4] = {
    gen_helper_sve2_srshr_b, gen_helper_sve2_srshr_h,
    gen_helper_sve2_srshr_s, gen_helper_sve2_srshr_d,
};
TRANS_FEAT(SRSHR, aa64_sve2, gen_gvec_ool_arg_zpzi,
           a->esz < 0 ? NULL : srshr_fns[a->esz], a)

static gen_helper_gvec_3 * const urshr_fns[4] = {
    gen_helper_sve2_urshr_b, gen_helper_sve2_urshr_h,
    gen_helper_sve2_urshr_s, gen_helper_sve2_urshr_d,
};
TRANS_FEAT(URSHR, aa64_sve2, gen_gvec_ool_arg_zpzi,
           a->esz < 0 ? NULL : urshr_fns[a->esz], a)

static gen_helper_gvec_3 * const sqshlu_fns[4] = {
    gen_helper_sve2_sqshlu_b, gen_helper_sve2_sqshlu_h,
    gen_helper_sve2_sqshlu_s, gen_helper_sve2_sqshlu_d,
};
TRANS_FEAT(SQSHLU, aa64_sve2, gen_gvec_ool_arg_zpzi,
           a->esz < 0 ? NULL : sqshlu_fns[a->esz], a)

/*
 *** SVE Bitwise Shift - Predicated Group
 */

#define DO_ZPZW(NAME, name) \
    static gen_helper_gvec_4 * const name##_zpzw_fns[4] = {               \
        gen_helper_sve_##name##_zpzw_b, gen_helper_sve_##name##_zpzw_h,   \
        gen_helper_sve_##name##_zpzw_s, NULL                              \
    };                                                                    \
    TRANS_FEAT(NAME##_zpzw, aa64_sve, gen_gvec_ool_arg_zpzz,              \
               a->esz < 0 ? NULL : name##_zpzw_fns[a->esz], a, 0)

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

TRANS_FEAT(ASR_zzi, aa64_sve, do_shift_imm, a, true, tcg_gen_gvec_sari)
TRANS_FEAT(LSR_zzi, aa64_sve, do_shift_imm, a, false, tcg_gen_gvec_shri)
TRANS_FEAT(LSL_zzi, aa64_sve, do_shift_imm, a, false, tcg_gen_gvec_shli)

#define DO_ZZW(NAME, name) \
    static gen_helper_gvec_3 * const name##_zzw_fns[4] = {                \
        gen_helper_sve_##name##_zzw_b, gen_helper_sve_##name##_zzw_h,     \
        gen_helper_sve_##name##_zzw_s, NULL                               \
    };                                                                    \
    TRANS_FEAT(NAME, aa64_sve, gen_gvec_ool_arg_zzz,                      \
               name##_zzw_fns[a->esz], a, 0)

DO_ZZW(ASR_zzw, asr)
DO_ZZW(LSR_zzw, lsr)
DO_ZZW(LSL_zzw, lsl)

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

static gen_helper_gvec_5 * const mla_fns[4] = {
    gen_helper_sve_mla_b, gen_helper_sve_mla_h,
    gen_helper_sve_mla_s, gen_helper_sve_mla_d,
};
TRANS_FEAT(MLA, aa64_sve, do_zpzzz_ool, a, mla_fns[a->esz])

static gen_helper_gvec_5 * const mls_fns[4] = {
    gen_helper_sve_mls_b, gen_helper_sve_mls_h,
    gen_helper_sve_mls_s, gen_helper_sve_mls_d,
};
TRANS_FEAT(MLS, aa64_sve, do_zpzzz_ool, a, mls_fns[a->esz])

/*
 *** SVE Index Generation Group
 */

static bool do_index(DisasContext *s, int esz, int rd,
                     TCGv_i64 start, TCGv_i64 incr)
{
    unsigned vsz;
    TCGv_i32 desc;
    TCGv_ptr t_zd;

    if (!sve_access_check(s)) {
        return true;
    }

    vsz = vec_full_reg_size(s);
    desc = tcg_constant_i32(simd_desc(vsz, vsz, 0));
    t_zd = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(t_zd, tcg_env, vec_full_reg_offset(s, rd));
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
    }
    return true;
}

TRANS_FEAT(INDEX_ii, aa64_sve, do_index, a->esz, a->rd,
           tcg_constant_i64(a->imm1), tcg_constant_i64(a->imm2))
TRANS_FEAT(INDEX_ir, aa64_sve, do_index, a->esz, a->rd,
           tcg_constant_i64(a->imm), cpu_reg(s, a->rm))
TRANS_FEAT(INDEX_ri, aa64_sve, do_index, a->esz, a->rd,
           cpu_reg(s, a->rn), tcg_constant_i64(a->imm))
TRANS_FEAT(INDEX_rr, aa64_sve, do_index, a->esz, a->rd,
           cpu_reg(s, a->rn), cpu_reg(s, a->rm))

/*
 *** SVE Stack Allocation Group
 */

static bool trans_ADDVL(DisasContext *s, arg_ADDVL *a)
{
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        TCGv_i64 rd = cpu_reg_sp(s, a->rd);
        TCGv_i64 rn = cpu_reg_sp(s, a->rn);
        tcg_gen_addi_i64(rd, rn, a->imm * vec_full_reg_size(s));
    }
    return true;
}

static bool trans_ADDSVL(DisasContext *s, arg_ADDSVL *a)
{
    if (!dc_isar_feature(aa64_sme, s)) {
        return false;
    }
    if (sme_enabled_check(s)) {
        TCGv_i64 rd = cpu_reg_sp(s, a->rd);
        TCGv_i64 rn = cpu_reg_sp(s, a->rn);
        tcg_gen_addi_i64(rd, rn, a->imm * streaming_vec_reg_size(s));
    }
    return true;
}

static bool trans_ADDPL(DisasContext *s, arg_ADDPL *a)
{
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        TCGv_i64 rd = cpu_reg_sp(s, a->rd);
        TCGv_i64 rn = cpu_reg_sp(s, a->rn);
        tcg_gen_addi_i64(rd, rn, a->imm * pred_full_reg_size(s));
    }
    return true;
}

static bool trans_ADDSPL(DisasContext *s, arg_ADDSPL *a)
{
    if (!dc_isar_feature(aa64_sme, s)) {
        return false;
    }
    if (sme_enabled_check(s)) {
        TCGv_i64 rd = cpu_reg_sp(s, a->rd);
        TCGv_i64 rn = cpu_reg_sp(s, a->rn);
        tcg_gen_addi_i64(rd, rn, a->imm * streaming_pred_reg_size(s));
    }
    return true;
}

static bool trans_RDVL(DisasContext *s, arg_RDVL *a)
{
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        TCGv_i64 reg = cpu_reg(s, a->rd);
        tcg_gen_movi_i64(reg, a->imm * vec_full_reg_size(s));
    }
    return true;
}

static bool trans_RDSVL(DisasContext *s, arg_RDSVL *a)
{
    if (!dc_isar_feature(aa64_sme, s)) {
        return false;
    }
    if (sme_enabled_check(s)) {
        TCGv_i64 reg = cpu_reg(s, a->rd);
        tcg_gen_movi_i64(reg, a->imm * streaming_vec_reg_size(s));
    }
    return true;
}

/*
 *** SVE Compute Vector Address Group
 */

static bool do_adr(DisasContext *s, arg_rrri *a, gen_helper_gvec_3 *fn)
{
    return gen_gvec_ool_zzz(s, fn, a->rd, a->rn, a->rm, a->imm);
}

TRANS_FEAT_NONSTREAMING(ADR_p32, aa64_sve, do_adr, a, gen_helper_sve_adr_p32)
TRANS_FEAT_NONSTREAMING(ADR_p64, aa64_sve, do_adr, a, gen_helper_sve_adr_p64)
TRANS_FEAT_NONSTREAMING(ADR_s32, aa64_sve, do_adr, a, gen_helper_sve_adr_s32)
TRANS_FEAT_NONSTREAMING(ADR_u32, aa64_sve, do_adr, a, gen_helper_sve_adr_u32)

/*
 *** SVE Integer Misc - Unpredicated Group
 */

static gen_helper_gvec_2 * const fexpa_fns[4] = {
    NULL,                   gen_helper_sve_fexpa_h,
    gen_helper_sve_fexpa_s, gen_helper_sve_fexpa_d,
};
TRANS_FEAT_NONSTREAMING(FEXPA, aa64_sve, gen_gvec_ool_zz,
                        fexpa_fns[a->esz], a->rd, a->rn, s->fpcr_ah)

static gen_helper_gvec_3 * const ftssel_fns[4] = {
    NULL,                    gen_helper_sve_ftssel_h,
    gen_helper_sve_ftssel_s, gen_helper_sve_ftssel_d,
};
TRANS_FEAT_NONSTREAMING(FTSSEL, aa64_sve, gen_gvec_ool_arg_zzz,
                        ftssel_fns[a->esz], a, s->fpcr_ah)

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

        tcg_gen_ld_i64(pn, tcg_env, nofs);
        tcg_gen_ld_i64(pm, tcg_env, mofs);
        tcg_gen_ld_i64(pg, tcg_env, gofs);

        gvec_op->fni8(pd, pn, pm, pg);
        tcg_gen_st_i64(pd, tcg_env, dofs);

        do_predtest1(pd, pg);
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

    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (!a->s) {
        if (a->rn == a->rm) {
            if (a->pg == a->rn) {
                return do_mov_p(s, a->rd, a->rn);
            }
            return gen_gvec_fn_ppp(s, tcg_gen_gvec_and, a->rd, a->rn, a->pg);
        } else if (a->pg == a->rn || a->pg == a->rm) {
            return gen_gvec_fn_ppp(s, tcg_gen_gvec_and, a->rd, a->rn, a->rm);
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

    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (!a->s && a->pg == a->rn) {
        return gen_gvec_fn_ppp(s, tcg_gen_gvec_andc, a->rd, a->rn, a->rm);
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

    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    /* Alias NOT (predicate) is EOR Pd.B, Pg/Z, Pn.B, Pg.B */
    if (!a->s && a->pg == a->rm) {
        return gen_gvec_fn_ppp(s, tcg_gen_gvec_andc, a->rd, a->pg, a->rn);
    }
    return do_pppp_flags(s, a, &op);
}

static bool trans_SEL_pppp(DisasContext *s, arg_rprr_s *a)
{
    if (a->s || !dc_isar_feature(aa64_sve, s)) {
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

    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
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

    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
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

    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
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

    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    return do_pppp_flags(s, a, &op);
}

/*
 *** SVE Predicate Misc Group
 */

static bool trans_PTEST(DisasContext *s, arg_PTEST *a)
{
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        int nofs = pred_full_reg_offset(s, a->rn);
        int gofs = pred_full_reg_offset(s, a->pg);
        int words = DIV_ROUND_UP(pred_full_reg_size(s), 8);

        if (words == 1) {
            TCGv_i64 pn = tcg_temp_new_i64();
            TCGv_i64 pg = tcg_temp_new_i64();

            tcg_gen_ld_i64(pn, tcg_env, nofs);
            tcg_gen_ld_i64(pg, tcg_env, gofs);
            do_predtest1(pn, pg);
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
        tcg_gen_st_i64(t, tcg_env, ofs);
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
        tcg_gen_st_i64(t, tcg_env, ofs + i);
    }
    if (lastword != word) {
        tcg_gen_movi_i64(t, lastword);
        tcg_gen_st_i64(t, tcg_env, ofs + i);
        i += 8;
    }
    if (i < fullsz) {
        tcg_gen_movi_i64(t, 0);
        for (; i < fullsz; i += 8) {
            tcg_gen_st_i64(t, tcg_env, ofs + i);
        }
    }

 done:
    /* PTRUES */
    if (setflag) {
        tcg_gen_movi_i32(cpu_NF, -(word != 0));
        tcg_gen_movi_i32(cpu_CF, word == 0);
        tcg_gen_movi_i32(cpu_VF, 0);
        tcg_gen_mov_i32(cpu_ZF, cpu_NF);
    }
    return true;
}

TRANS_FEAT(PTRUE, aa64_sve, do_predset, a->esz, a->rd, a->pat, a->s)

static bool trans_PTRUE_cnt(DisasContext *s, arg_PTRUE_cnt *a)
{
    if (!dc_isar_feature(aa64_sme2_or_sve2p1, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        /* Canonical TRUE is 0 count, invert bit, plus element size. */
        int val = (1 << 15) | (1 << a->esz);

        /* Write val to the first uint64_t; clear all of the rest. */
        tcg_gen_gvec_dup_imm(MO_64, pred_full_reg_offset(s, a->rd),
                             8, size_for_gvec(pred_full_reg_size(s)), val);
    }
    return true;
}

/* Note pat == 31 is #all, to set all elements.  */
TRANS_FEAT_NONSTREAMING(SETFFR, aa64_sve,
                        do_predset, 0, FFR_PRED_NUM, 31, false)

/* Note pat == 32 is #unimp, to set no elements.  */
TRANS_FEAT(PFALSE, aa64_sve, do_predset, 0, a->rd, 32, false)

static bool trans_RDFFR_p(DisasContext *s, arg_RDFFR_p *a)
{
    /* The path through do_pppp_flags is complicated enough to want to avoid
     * duplication.  Frob the arguments into the form of a predicated AND.
     */
    arg_rprr_s alt_a = {
        .rd = a->rd, .pg = a->pg, .s = a->s,
        .rn = FFR_PRED_NUM, .rm = FFR_PRED_NUM,
    };

    s->is_nonstreaming = true;
    return trans_AND_pppp(s, &alt_a);
}

TRANS_FEAT_NONSTREAMING(RDFFR, aa64_sve, do_mov_p, a->rd, FFR_PRED_NUM)
TRANS_FEAT_NONSTREAMING(WRFFR, aa64_sve, do_mov_p, FFR_PRED_NUM, a->rn)

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

    tcg_gen_addi_ptr(t_pd, tcg_env, pred_full_reg_offset(s, a->rd));
    tcg_gen_addi_ptr(t_pg, tcg_env, pred_full_reg_offset(s, a->rn));
    t = tcg_temp_new_i32();

    gen_fn(t, t_pd, t_pg, tcg_constant_i32(desc));

    do_pred_flags(t);
    return true;
}

TRANS_FEAT(PFIRST, aa64_sve, do_pfirst_pnext, a, gen_helper_sve_pfirst)
TRANS_FEAT(PNEXT, aa64_sve, do_pfirst_pnext, a, gen_helper_sve_pnext)

/*
 *** SVE Element Count Group
 */

/* Perform an inline saturating addition of a 32-bit value within
 * a 64-bit register.  The second operand is known to be positive,
 * which halves the comparisons we must perform to bound the result.
 */
static void do_sat_addsub_32(TCGv_i64 reg, TCGv_i64 val, bool u, bool d)
{
    int64_t ibound;

    /* Use normal 64-bit arithmetic to detect 32-bit overflow.  */
    if (u) {
        tcg_gen_ext32u_i64(reg, reg);
    } else {
        tcg_gen_ext32s_i64(reg, reg);
    }
    if (d) {
        tcg_gen_sub_i64(reg, reg, val);
        ibound = (u ? 0 : INT32_MIN);
        tcg_gen_smax_i64(reg, reg, tcg_constant_i64(ibound));
    } else {
        tcg_gen_add_i64(reg, reg, val);
        ibound = (u ? UINT32_MAX : INT32_MAX);
        tcg_gen_smin_i64(reg, reg, tcg_constant_i64(ibound));
    }
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
    }
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
    tcg_gen_addi_ptr(dptr, tcg_env, vec_full_reg_offset(s, rd));
    tcg_gen_addi_ptr(nptr, tcg_env, vec_full_reg_offset(s, rn));
    desc = tcg_constant_i32(simd_desc(vsz, vsz, 0));

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
        } else {
            gen_helper_sve_sqaddi_d(dptr, nptr, val, desc);
        }
        break;

    default:
        g_assert_not_reached();
    }
}

static bool trans_CNT_r(DisasContext *s, arg_CNT_r *a)
{
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned fullsz = vec_full_reg_size(s);
        unsigned numelem = decode_pred_count(fullsz, a->pat, a->esz);
        tcg_gen_movi_i64(cpu_reg(s, a->rd), numelem * a->imm);
    }
    return true;
}

static bool trans_INCDEC_r(DisasContext *s, arg_incdec_cnt *a)
{
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
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
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
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
        do_sat_addsub_32(reg, tcg_constant_i64(inc), a->u, a->d);
    }
    return true;
}

static bool trans_SINCDEC_r_64(DisasContext *s, arg_incdec_cnt *a)
{
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (!sve_access_check(s)) {
        return true;
    }

    unsigned fullsz = vec_full_reg_size(s);
    unsigned numelem = decode_pred_count(fullsz, a->pat, a->esz);
    int inc = numelem * a->imm;
    TCGv_i64 reg = cpu_reg(s, a->rd);

    if (inc != 0) {
        do_sat_addsub_64(reg, tcg_constant_i64(inc), a->u, a->d);
    }
    return true;
}

static bool trans_INCDEC_v(DisasContext *s, arg_incdec2_cnt *a)
{
    if (a->esz == 0 || !dc_isar_feature(aa64_sve, s)) {
        return false;
    }

    unsigned fullsz = vec_full_reg_size(s);
    unsigned numelem = decode_pred_count(fullsz, a->pat, a->esz);
    int inc = numelem * a->imm;

    if (inc != 0) {
        if (sve_access_check(s)) {
            tcg_gen_gvec_adds(a->esz, vec_full_reg_offset(s, a->rd),
                              vec_full_reg_offset(s, a->rn),
                              tcg_constant_i64(a->d ? -inc : inc),
                              fullsz, fullsz);
        }
    } else {
        do_mov_z(s, a->rd, a->rn);
    }
    return true;
}

static bool trans_SINCDEC_v(DisasContext *s, arg_incdec2_cnt *a)
{
    if (a->esz == 0 || !dc_isar_feature(aa64_sve, s)) {
        return false;
    }

    unsigned fullsz = vec_full_reg_size(s);
    unsigned numelem = decode_pred_count(fullsz, a->pat, a->esz);
    int inc = numelem * a->imm;

    if (inc != 0) {
        if (sve_access_check(s)) {
            do_sat_addsub_vec(s, a->esz, a->rd, a->rn,
                              tcg_constant_i64(inc), a->u, a->d);
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
    return gen_gvec_fn_zzi(s, gvec_fn, MO_64, a->rd, a->rn, imm);
}

TRANS_FEAT(AND_zzi, aa64_sve, do_zz_dbm, a, tcg_gen_gvec_andi)
TRANS_FEAT(ORR_zzi, aa64_sve, do_zz_dbm, a, tcg_gen_gvec_ori)
TRANS_FEAT(EOR_zzi, aa64_sve, do_zz_dbm, a, tcg_gen_gvec_xori)

static bool trans_DUPM(DisasContext *s, arg_DUPM *a)
{
    uint64_t imm;

    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
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
    TCGv_i32 desc = tcg_constant_i32(simd_desc(vsz, vsz, 0));
    TCGv_ptr t_zd = tcg_temp_new_ptr();
    TCGv_ptr t_zn = tcg_temp_new_ptr();
    TCGv_ptr t_pg = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(t_zd, tcg_env, vec_full_reg_offset(s, rd));
    tcg_gen_addi_ptr(t_zn, tcg_env, vec_full_reg_offset(s, rn));
    tcg_gen_addi_ptr(t_pg, tcg_env, pred_full_reg_offset(s, pg));

    fns[esz](t_zd, t_zn, t_pg, val, desc);
}

static bool trans_FCPY(DisasContext *s, arg_FCPY *a)
{
    if (a->esz == 0 || !dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        /* Decode the VFP immediate.  */
        uint64_t imm = vfp_expand_imm(a->esz, a->imm);
        do_cpy_m(s, a->esz, a->rd, a->rn, a->pg, tcg_constant_i64(imm));
    }
    return true;
}

static bool trans_CPY_m_i(DisasContext *s, arg_rpri_esz *a)
{
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        do_cpy_m(s, a->esz, a->rd, a->rn, a->pg, tcg_constant_i64(a->imm));
    }
    return true;
}

static bool trans_CPY_z_i(DisasContext *s, arg_CPY_z_i *a)
{
    static gen_helper_gvec_2i * const fns[4] = {
        gen_helper_sve_cpy_z_b, gen_helper_sve_cpy_z_h,
        gen_helper_sve_cpy_z_s, gen_helper_sve_cpy_z_d,
    };

    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_2i_ool(vec_full_reg_offset(s, a->rd),
                            pred_full_reg_offset(s, a->pg),
                            tcg_constant_i64(a->imm),
                            vsz, vsz, 0, fns[a->esz]);
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

TRANS_FEAT(EXT, aa64_sve, do_EXT, a->rd, a->rn, a->rm, a->imm)
TRANS_FEAT(EXT_sve2, aa64_sve2, do_EXT, a->rd, a->rn, (a->rn + 1) % 32, a->imm)

static bool trans_EXTQ(DisasContext *s, arg_EXTQ *a)
{
    unsigned vl, dofs, sofs0, sofs1, sofs2, imm;

    if (!dc_isar_feature(aa64_sme2p1_or_sve2p1, s)) {
        return false;
    }
    if (!sve_access_check(s)) {
        return true;
    }

    imm = a->imm;
    if (imm == 0) {
        /* So far we never optimize Zdn with MOVPRFX, so zd = zn is a nop. */
        return true;
    }

    vl = vec_full_reg_size(s);
    dofs = vec_full_reg_offset(s, a->rd);
    sofs2 = vec_full_reg_offset(s, a->rn);

    if (imm & 8) {
        sofs0 = dofs + 8;
        sofs1 = sofs2;
        sofs2 += 8;
    } else {
        sofs0 = dofs;
        sofs1 = dofs + 8;
    }
    imm = (imm & 7) << 3;

    for (unsigned i = 0; i < vl; i += 16) {
        TCGv_i64 s0 = tcg_temp_new_i64();
        TCGv_i64 s1 = tcg_temp_new_i64();
        TCGv_i64 s2 = tcg_temp_new_i64();

        tcg_gen_ld_i64(s0, tcg_env, sofs0 + i);
        tcg_gen_ld_i64(s1, tcg_env, sofs1 + i);
        tcg_gen_ld_i64(s2, tcg_env, sofs2 + i);

        tcg_gen_extract2_i64(s0, s0, s1, imm);
        tcg_gen_extract2_i64(s1, s1, s2, imm);

        tcg_gen_st_i64(s0, tcg_env, dofs + i);
        tcg_gen_st_i64(s1, tcg_env, dofs + i + 8);
    }
    return true;
}

/*
 *** SVE Permute - Unpredicated Group
 */

static bool trans_DUP_s(DisasContext *s, arg_DUP_s *a)
{
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_dup_i64(a->esz, vec_full_reg_offset(s, a->rd),
                             vsz, vsz, cpu_reg_sp(s, a->rn));
    }
    return true;
}

static bool trans_DUP_x(DisasContext *s, arg_DUP_x *a)
{
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
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

static bool trans_DUPQ(DisasContext *s, arg_DUPQ *a)
{
    unsigned vl, dofs, nofs;

    if (!dc_isar_feature(aa64_sme2p1_or_sve2p1, s)) {
        return false;
    }
    if (!sve_access_check(s)) {
        return true;
    }

    vl = vec_full_reg_size(s);
    dofs = vec_full_reg_offset(s, a->rd);
    nofs = vec_reg_offset(s, a->rn, a->imm, a->esz);

    for (unsigned i = 0; i < vl; i += 16) {
        tcg_gen_gvec_dup_mem(a->esz, dofs + i, nofs + i, 16, 16);
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
    TCGv_i32 desc = tcg_constant_i32(simd_desc(vsz, vsz, 0));
    TCGv_ptr t_zd = tcg_temp_new_ptr();
    TCGv_ptr t_zn = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(t_zd, tcg_env, vec_full_reg_offset(s, a->rd));
    tcg_gen_addi_ptr(t_zn, tcg_env, vec_full_reg_offset(s, a->rn));

    fns[a->esz](t_zd, t_zn, val, desc);
}

static bool trans_INSR_f(DisasContext *s, arg_rrr_esz *a)
{
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        TCGv_i64 t = tcg_temp_new_i64();
        tcg_gen_ld_i64(t, tcg_env, vec_reg_offset(s, a->rm, 0, MO_64));
        do_insr_i64(s, a, t);
    }
    return true;
}

static bool trans_INSR_r(DisasContext *s, arg_rrr_esz *a)
{
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        do_insr_i64(s, a, cpu_reg(s, a->rm));
    }
    return true;
}

static gen_helper_gvec_2 * const rev_fns[4] = {
    gen_helper_sve_rev_b, gen_helper_sve_rev_h,
    gen_helper_sve_rev_s, gen_helper_sve_rev_d
};
TRANS_FEAT(REV_v, aa64_sve, gen_gvec_ool_zz, rev_fns[a->esz], a->rd, a->rn, 0)

static gen_helper_gvec_3 * const sve_tbl_fns[4] = {
    gen_helper_sve_tbl_b, gen_helper_sve_tbl_h,
    gen_helper_sve_tbl_s, gen_helper_sve_tbl_d
};
TRANS_FEAT(TBL, aa64_sve, gen_gvec_ool_arg_zzz, sve_tbl_fns[a->esz], a, 0)

static gen_helper_gvec_4 * const sve2_tbl_fns[4] = {
    gen_helper_sve2_tbl_b, gen_helper_sve2_tbl_h,
    gen_helper_sve2_tbl_s, gen_helper_sve2_tbl_d
};
TRANS_FEAT(TBL_sve2, aa64_sve2, gen_gvec_ool_zzzz, sve2_tbl_fns[a->esz],
           a->rd, a->rn, (a->rn + 1) % 32, a->rm, 0)

static gen_helper_gvec_3 * const tblq_fns[4] = {
    gen_helper_sve2p1_tblq_b, gen_helper_sve2p1_tblq_h,
    gen_helper_sve2p1_tblq_s, gen_helper_sve2p1_tblq_d
};
TRANS_FEAT(TBLQ, aa64_sme2p1_or_sve2p1, gen_gvec_ool_arg_zzz,
           tblq_fns[a->esz], a, 0)

static gen_helper_gvec_3 * const tbx_fns[4] = {
    gen_helper_sve2_tbx_b, gen_helper_sve2_tbx_h,
    gen_helper_sve2_tbx_s, gen_helper_sve2_tbx_d
};
TRANS_FEAT(TBX, aa64_sve2, gen_gvec_ool_arg_zzz, tbx_fns[a->esz], a, 0)

static gen_helper_gvec_3 * const tbxq_fns[4] = {
    gen_helper_sve2p1_tbxq_b, gen_helper_sve2p1_tbxq_h,
    gen_helper_sve2p1_tbxq_s, gen_helper_sve2p1_tbxq_d
};
TRANS_FEAT(TBXQ, aa64_sme2p1_or_sve2p1, gen_gvec_ool_arg_zzz,
           tbxq_fns[a->esz], a, 0)

static bool trans_PMOV_pv(DisasContext *s, arg_PMOV_pv *a)
{
    static gen_helper_gvec_2 * const fns[4] = {
        NULL,                 gen_helper_pmov_pv_h,
        gen_helper_pmov_pv_s, gen_helper_pmov_pv_d
    };
    unsigned vl, pl, vofs, pofs;
    TCGv_i64 tmp;

    if (!dc_isar_feature(aa64_sme2p1_or_sve2p1, s)) {
        return false;
    }
    if (!sve_access_check(s)) {
        return true;
    }

    vl = vec_full_reg_size(s);
    if (a->esz != MO_8) {
        tcg_gen_gvec_2_ool(pred_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vl, vl, a->imm, fns[a->esz]);
        return true;
    }

    /*
     * Copy the low PL bytes from vector Zn, zero-extending to a
     * multiple of 8 bytes, so that Pd is properly cleared.
     */

    pl = vl / 8;
    pofs = pred_full_reg_offset(s, a->rd);
    vofs = vec_full_reg_offset(s, a->rn);

    QEMU_BUILD_BUG_ON(sizeof(ARMPredicateReg) != 32);
    for (unsigned i = 32; i >= 8; i >>= 1) {
        if (pl & i) {
            tcg_gen_gvec_mov(MO_64, pofs, vofs, i, i);
            pofs += i;
            vofs += i;
        }
    }
    switch (pl & 7) {
    case 0:
        return true;
    case 2:
        tmp = tcg_temp_new_i64();
        tcg_gen_ld16u_i64(tmp, tcg_env, vofs + (HOST_BIG_ENDIAN ? 6 : 0));
        break;
    case 4:
        tmp = tcg_temp_new_i64();
        tcg_gen_ld32u_i64(tmp, tcg_env, vofs + (HOST_BIG_ENDIAN ? 4 : 0));
        break;
    case 6:
        tmp = tcg_temp_new_i64();
        tcg_gen_ld_i64(tmp, tcg_env, vofs);
        tcg_gen_extract_i64(tmp, tmp, 0, 48);
        break;
    default:
        g_assert_not_reached();
    }
    tcg_gen_st_i64(tmp, tcg_env, pofs);
    return true;
}

static bool trans_PMOV_vp(DisasContext *s, arg_PMOV_pv *a)
{
    static gen_helper_gvec_2 * const fns[4] = {
        NULL,                 gen_helper_pmov_vp_h,
        gen_helper_pmov_vp_s, gen_helper_pmov_vp_d
    };
    unsigned vl;

    if (!dc_isar_feature(aa64_sme2p1_or_sve2p1, s)) {
        return false;
    }
    if (!sve_access_check(s)) {
        return true;
    }

    vl = vec_full_reg_size(s);

    if (a->esz == MO_8) {
        /*
         * The low PL bytes are copied from Pn to Zd unchanged.
         * We know that the unused portion of Pn is zero, and
         * that imm == 0, so the balance of Zd must be zeroed.
         */
        tcg_gen_gvec_mov(MO_64, vec_full_reg_offset(s, a->rd),
                         pred_full_reg_offset(s, a->rn),
                         size_for_gvec(vl / 8), vl);
    } else {
        tcg_gen_gvec_2_ool(vec_full_reg_offset(s, a->rd),
                           pred_full_reg_offset(s, a->rn),
                           vl, vl, a->imm, fns[a->esz]);
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

    if (a->esz == 0 || !dc_isar_feature(aa64_sve, s)) {
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
    uint32_t desc = 0;

    desc = FIELD_DP32(desc, PREDDESC, OPRSZ, vsz);
    desc = FIELD_DP32(desc, PREDDESC, ESZ, a->esz);
    desc = FIELD_DP32(desc, PREDDESC, DATA, high_odd);

    tcg_gen_addi_ptr(t_d, tcg_env, pred_full_reg_offset(s, a->rd));
    tcg_gen_addi_ptr(t_n, tcg_env, pred_full_reg_offset(s, a->rn));
    tcg_gen_addi_ptr(t_m, tcg_env, pred_full_reg_offset(s, a->rm));

    fn(t_d, t_n, t_m, tcg_constant_i32(desc));
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
    uint32_t desc = 0;

    tcg_gen_addi_ptr(t_d, tcg_env, pred_full_reg_offset(s, a->rd));
    tcg_gen_addi_ptr(t_n, tcg_env, pred_full_reg_offset(s, a->rn));

    desc = FIELD_DP32(desc, PREDDESC, OPRSZ, vsz);
    desc = FIELD_DP32(desc, PREDDESC, ESZ, a->esz);
    desc = FIELD_DP32(desc, PREDDESC, DATA, high_odd);

    fn(t_d, t_n, tcg_constant_i32(desc));
    return true;
}

TRANS_FEAT(ZIP1_p, aa64_sve, do_perm_pred3, a, 0, gen_helper_sve_zip_p)
TRANS_FEAT(ZIP2_p, aa64_sve, do_perm_pred3, a, 1, gen_helper_sve_zip_p)
TRANS_FEAT(UZP1_p, aa64_sve, do_perm_pred3, a, 0, gen_helper_sve_uzp_p)
TRANS_FEAT(UZP2_p, aa64_sve, do_perm_pred3, a, 1, gen_helper_sve_uzp_p)
TRANS_FEAT(TRN1_p, aa64_sve, do_perm_pred3, a, 0, gen_helper_sve_trn_p)
TRANS_FEAT(TRN2_p, aa64_sve, do_perm_pred3, a, 1, gen_helper_sve_trn_p)

TRANS_FEAT(REV_p, aa64_sve, do_perm_pred2, a, 0, gen_helper_sve_rev_p)
TRANS_FEAT(PUNPKLO, aa64_sve, do_perm_pred2, a, 0, gen_helper_sve_punpk_p)
TRANS_FEAT(PUNPKHI, aa64_sve, do_perm_pred2, a, 1, gen_helper_sve_punpk_p)

/*
 *** SVE Permute - Interleaving Group
 */

static bool do_interleave_q(DisasContext *s, gen_helper_gvec_3 *fn,
                            arg_rrr_esz *a, int data)
{
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        if (vsz < 32) {
            unallocated_encoding(s);
        } else {
            tcg_gen_gvec_3_ool(vec_full_reg_offset(s, a->rd),
                               vec_full_reg_offset(s, a->rn),
                               vec_full_reg_offset(s, a->rm),
                               vsz, vsz, data, fn);
        }
    }
    return true;
}

static gen_helper_gvec_3 * const zip_fns[4] = {
    gen_helper_sve_zip_b, gen_helper_sve_zip_h,
    gen_helper_sve_zip_s, gen_helper_sve_zip_d,
};
TRANS_FEAT(ZIP1_z, aa64_sve, gen_gvec_ool_arg_zzz,
           zip_fns[a->esz], a, 0)
TRANS_FEAT(ZIP2_z, aa64_sve, gen_gvec_ool_arg_zzz,
           zip_fns[a->esz], a, vec_full_reg_size(s) / 2)

TRANS_FEAT_NONSTREAMING(ZIP1_q, aa64_sve_f64mm, do_interleave_q,
                        gen_helper_sve2_zip_q, a, 0)
TRANS_FEAT_NONSTREAMING(ZIP2_q, aa64_sve_f64mm, do_interleave_q,
                        gen_helper_sve2_zip_q, a,
                        QEMU_ALIGN_DOWN(vec_full_reg_size(s), 32) / 2)

static gen_helper_gvec_3 * const zipq_fns[4] = {
    gen_helper_sve2p1_zipq_b, gen_helper_sve2p1_zipq_h,
    gen_helper_sve2p1_zipq_s, gen_helper_sve2p1_zipq_d,
};
TRANS_FEAT(ZIPQ1, aa64_sme2p1_or_sve2p1, gen_gvec_ool_arg_zzz,
           zipq_fns[a->esz], a, 0)
TRANS_FEAT(ZIPQ2, aa64_sme2p1_or_sve2p1, gen_gvec_ool_arg_zzz,
           zipq_fns[a->esz], a, 16 / 2)

static gen_helper_gvec_3 * const uzp_fns[4] = {
    gen_helper_sve_uzp_b, gen_helper_sve_uzp_h,
    gen_helper_sve_uzp_s, gen_helper_sve_uzp_d,
};
TRANS_FEAT(UZP1_z, aa64_sve, gen_gvec_ool_arg_zzz,
           uzp_fns[a->esz], a, 0)
TRANS_FEAT(UZP2_z, aa64_sve, gen_gvec_ool_arg_zzz,
           uzp_fns[a->esz], a, 1 << a->esz)

TRANS_FEAT_NONSTREAMING(UZP1_q, aa64_sve_f64mm, do_interleave_q,
                        gen_helper_sve2_uzp_q, a, 0)
TRANS_FEAT_NONSTREAMING(UZP2_q, aa64_sve_f64mm, do_interleave_q,
                        gen_helper_sve2_uzp_q, a, 16)

static gen_helper_gvec_3 * const uzpq_fns[4] = {
    gen_helper_sve2p1_uzpq_b, gen_helper_sve2p1_uzpq_h,
    gen_helper_sve2p1_uzpq_s, gen_helper_sve2p1_uzpq_d,
};
TRANS_FEAT(UZPQ1, aa64_sme2p1_or_sve2p1, gen_gvec_ool_arg_zzz,
           uzpq_fns[a->esz], a, 0)
TRANS_FEAT(UZPQ2, aa64_sme2p1_or_sve2p1, gen_gvec_ool_arg_zzz,
           uzpq_fns[a->esz], a, 1 << a->esz)

static gen_helper_gvec_3 * const trn_fns[4] = {
    gen_helper_sve_trn_b, gen_helper_sve_trn_h,
    gen_helper_sve_trn_s, gen_helper_sve_trn_d,
};

TRANS_FEAT(TRN1_z, aa64_sve, gen_gvec_ool_arg_zzz,
           trn_fns[a->esz], a, 0)
TRANS_FEAT(TRN2_z, aa64_sve, gen_gvec_ool_arg_zzz,
           trn_fns[a->esz], a, 1 << a->esz)

TRANS_FEAT_NONSTREAMING(TRN1_q, aa64_sve_f64mm, do_interleave_q,
                        gen_helper_sve2_trn_q, a, 0)
TRANS_FEAT_NONSTREAMING(TRN2_q, aa64_sve_f64mm, do_interleave_q,
                        gen_helper_sve2_trn_q, a, 16)

/*
 *** SVE Permute Vector - Predicated Group
 */

static gen_helper_gvec_3 * const compact_fns[4] = {
    NULL, NULL, gen_helper_sve_compact_s, gen_helper_sve_compact_d
};
TRANS_FEAT_NONSTREAMING(COMPACT, aa64_sve, gen_gvec_ool_arg_zpz,
                        compact_fns[a->esz], a, 0)

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
    unsigned desc = 0;

    desc = FIELD_DP32(desc, PREDDESC, OPRSZ, pred_full_reg_size(s));
    desc = FIELD_DP32(desc, PREDDESC, ESZ, esz);

    tcg_gen_addi_ptr(t_p, tcg_env, pred_full_reg_offset(s, pg));

    gen_helper_sve_last_active_element(ret, t_p, tcg_constant_i32(desc));
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
        TCGv_i32 max = tcg_constant_i32(vsz);
        TCGv_i32 zero = tcg_constant_i32(0);
        tcg_gen_movcond_i32(TCG_COND_GEU, last, last, max, zero, last);
    }
}

/* If LAST < 0, set LAST to the offset of the last element in the vector.  */
static void wrap_last_active(DisasContext *s, TCGv_i32 last, int esz)
{
    unsigned vsz = vec_full_reg_size(s);

    if (is_power_of_2(vsz)) {
        tcg_gen_andi_i32(last, last, vsz - 1);
    } else {
        TCGv_i32 max = tcg_constant_i32(vsz - (1 << esz));
        TCGv_i32 zero = tcg_constant_i32(0);
        tcg_gen_movcond_i32(TCG_COND_LT, last, last, zero, max, last);
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

    /* Convert offset into vector into offset into ENV.
     * The final adjustment for the vector register base
     * is added via constant offset to the load.
     */
#if HOST_BIG_ENDIAN
    /* Adjust for element ordering.  See vec_reg_offset.  */
    if (esz < 3) {
        tcg_gen_xori_i32(last, last, 8 - (1 << esz));
    }
#endif
    tcg_gen_ext_i32_ptr(p, last);
    tcg_gen_add_ptr(p, p, tcg_env);

    return load_esz(p, vec_full_reg_offset(s, rm), esz);
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

    last = tcg_temp_new_i32();
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

    vsz = vec_full_reg_size(s);
    tcg_gen_gvec_dup_i64(esz, vec_full_reg_offset(s, a->rd), vsz, vsz, ele);

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

TRANS_FEAT(CLASTA_z, aa64_sve, do_clast_vector, a, false)
TRANS_FEAT(CLASTB_z, aa64_sve, do_clast_vector, a, true)

/* Compute CLAST for a scalar.  */
static void do_clast_scalar(DisasContext *s, int esz, int pg, int rm,
                            bool before, TCGv_i64 reg_val)
{
    TCGv_i32 last = tcg_temp_new_i32();
    TCGv_i64 ele, cmp;

    find_last_active(s, last, esz, pg);

    /* Extend the original value of last prior to incrementing.  */
    cmp = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(cmp, last);

    if (!before) {
        incr_last_active(s, last, esz);
    }

    /* The conceit here is that while last < 0 indicates not found, after
     * adjusting for tcg_env->vfp.zregs[rm], it is still a valid address
     * from which we can load garbage.  We then discard the garbage with
     * a conditional move.
     */
    ele = load_last_active(s, last, rm, esz);

    tcg_gen_movcond_i64(TCG_COND_GE, reg_val, cmp, tcg_constant_i64(0),
                        ele, reg_val);
}

/* Compute CLAST for a Vreg.  */
static bool do_clast_fp(DisasContext *s, arg_rpr_esz *a, bool before)
{
    if (sve_access_check(s)) {
        int esz = a->esz;
        int ofs = vec_reg_offset(s, a->rd, 0, esz);
        TCGv_i64 reg = load_esz(tcg_env, ofs, esz);

        do_clast_scalar(s, esz, a->pg, a->rn, before, reg);
        write_fp_dreg(s, a->rd, reg);
    }
    return true;
}

TRANS_FEAT(CLASTA_v, aa64_sve, do_clast_fp, a, false)
TRANS_FEAT(CLASTB_v, aa64_sve, do_clast_fp, a, true)

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

TRANS_FEAT(CLASTA_r, aa64_sve, do_clast_general, a, false)
TRANS_FEAT(CLASTB_r, aa64_sve, do_clast_general, a, true)

/* Compute LAST for a scalar.  */
static TCGv_i64 do_last_scalar(DisasContext *s, int esz,
                               int pg, int rm, bool before)
{
    TCGv_i32 last = tcg_temp_new_i32();

    find_last_active(s, last, esz, pg);
    if (before) {
        wrap_last_active(s, last, esz);
    } else {
        incr_last_active(s, last, esz);
    }

    return load_last_active(s, last, rm, esz);
}

/* Compute LAST for a Vreg.  */
static bool do_last_fp(DisasContext *s, arg_rpr_esz *a, bool before)
{
    if (sve_access_check(s)) {
        TCGv_i64 val = do_last_scalar(s, a->esz, a->pg, a->rn, before);
        write_fp_dreg(s, a->rd, val);
    }
    return true;
}

TRANS_FEAT(LASTA_v, aa64_sve, do_last_fp, a, false)
TRANS_FEAT(LASTB_v, aa64_sve, do_last_fp, a, true)

/* Compute LAST for a Xreg.  */
static bool do_last_general(DisasContext *s, arg_rpr_esz *a, bool before)
{
    if (sve_access_check(s)) {
        TCGv_i64 val = do_last_scalar(s, a->esz, a->pg, a->rn, before);
        tcg_gen_mov_i64(cpu_reg(s, a->rd), val);
    }
    return true;
}

TRANS_FEAT(LASTA_r, aa64_sve, do_last_general, a, false)
TRANS_FEAT(LASTB_r, aa64_sve, do_last_general, a, true)

static bool trans_CPY_m_r(DisasContext *s, arg_rpr_esz *a)
{
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        do_cpy_m(s, a->esz, a->rd, a->rd, a->pg, cpu_reg_sp(s, a->rn));
    }
    return true;
}

static bool trans_CPY_m_v(DisasContext *s, arg_rpr_esz *a)
{
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        int ofs = vec_reg_offset(s, a->rn, 0, a->esz);
        TCGv_i64 t = load_esz(tcg_env, ofs, a->esz);
        do_cpy_m(s, a->esz, a->rd, a->rd, a->pg, t);
    }
    return true;
}

static gen_helper_gvec_3 * const revb_fns[4] = {
    NULL,                  gen_helper_sve_revb_h,
    gen_helper_sve_revb_s, gen_helper_sve_revb_d,
};
TRANS_FEAT(REVB, aa64_sve, gen_gvec_ool_arg_zpz, revb_fns[a->esz], a, 0)

static gen_helper_gvec_3 * const revh_fns[4] = {
    NULL, NULL, gen_helper_sve_revh_s, gen_helper_sve_revh_d,
};
TRANS_FEAT(REVH, aa64_sve, gen_gvec_ool_arg_zpz, revh_fns[a->esz], a, 0)

TRANS_FEAT(REVW, aa64_sve, gen_gvec_ool_arg_zpz,
           a->esz == 3 ? gen_helper_sve_revw_d : NULL, a, 0)

TRANS_FEAT(REVD, aa64_sme, gen_gvec_ool_arg_zpz, gen_helper_sme_revd_q, a, 0)

TRANS_FEAT(SPLICE, aa64_sve, gen_gvec_ool_arg_zpzz,
           gen_helper_sve_splice, a, a->esz)

TRANS_FEAT(SPLICE_sve2, aa64_sve2, gen_gvec_ool_zzzp, gen_helper_sve_splice,
           a->rd, a->rn, (a->rn + 1) % 32, a->pg, a->esz)

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
    t = tcg_temp_new_i32();
    pd = tcg_temp_new_ptr();
    zn = tcg_temp_new_ptr();
    zm = tcg_temp_new_ptr();
    pg = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(pd, tcg_env, pred_full_reg_offset(s, a->rd));
    tcg_gen_addi_ptr(zn, tcg_env, vec_full_reg_offset(s, a->rn));
    tcg_gen_addi_ptr(zm, tcg_env, vec_full_reg_offset(s, a->rm));
    tcg_gen_addi_ptr(pg, tcg_env, pred_full_reg_offset(s, a->pg));

    gen_fn(t, pd, zn, zm, pg, tcg_constant_i32(simd_desc(vsz, vsz, 0)));

    do_pred_flags(t);
    return true;
}

#define DO_PPZZ(NAME, name) \
    static gen_helper_gvec_flags_4 * const name##_ppzz_fns[4] = {       \
        gen_helper_sve_##name##_ppzz_b, gen_helper_sve_##name##_ppzz_h, \
        gen_helper_sve_##name##_ppzz_s, gen_helper_sve_##name##_ppzz_d, \
    };                                                                  \
    TRANS_FEAT(NAME##_ppzz, aa64_sve, do_ppzz_flags,                    \
               a, name##_ppzz_fns[a->esz])

DO_PPZZ(CMPEQ, cmpeq)
DO_PPZZ(CMPNE, cmpne)
DO_PPZZ(CMPGT, cmpgt)
DO_PPZZ(CMPGE, cmpge)
DO_PPZZ(CMPHI, cmphi)
DO_PPZZ(CMPHS, cmphs)

#undef DO_PPZZ

#define DO_PPZW(NAME, name) \
    static gen_helper_gvec_flags_4 * const name##_ppzw_fns[4] = {       \
        gen_helper_sve_##name##_ppzw_b, gen_helper_sve_##name##_ppzw_h, \
        gen_helper_sve_##name##_ppzw_s, NULL                            \
    };                                                                  \
    TRANS_FEAT(NAME##_ppzw, aa64_sve, do_ppzz_flags,                    \
               a, name##_ppzw_fns[a->esz])

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
    t = tcg_temp_new_i32();
    pd = tcg_temp_new_ptr();
    zn = tcg_temp_new_ptr();
    pg = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(pd, tcg_env, pred_full_reg_offset(s, a->rd));
    tcg_gen_addi_ptr(zn, tcg_env, vec_full_reg_offset(s, a->rn));
    tcg_gen_addi_ptr(pg, tcg_env, pred_full_reg_offset(s, a->pg));

    gen_fn(t, pd, zn, pg, tcg_constant_i32(simd_desc(vsz, vsz, a->imm)));

    do_pred_flags(t);
    return true;
}

#define DO_PPZI(NAME, name) \
    static gen_helper_gvec_flags_3 * const name##_ppzi_fns[4] = {         \
        gen_helper_sve_##name##_ppzi_b, gen_helper_sve_##name##_ppzi_h,   \
        gen_helper_sve_##name##_ppzi_s, gen_helper_sve_##name##_ppzi_d,   \
    };                                                                    \
    TRANS_FEAT(NAME##_ppzi, aa64_sve, do_ppzi_flags, a,                   \
               name##_ppzi_fns[a->esz])

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
    TCGv_i32 desc = tcg_constant_i32(FIELD_DP32(0, PREDDESC, OPRSZ, vsz));

    tcg_gen_addi_ptr(d, tcg_env, pred_full_reg_offset(s, a->rd));
    tcg_gen_addi_ptr(n, tcg_env, pred_full_reg_offset(s, a->rn));
    tcg_gen_addi_ptr(m, tcg_env, pred_full_reg_offset(s, a->rm));
    tcg_gen_addi_ptr(g, tcg_env, pred_full_reg_offset(s, a->pg));

    if (a->s) {
        TCGv_i32 t = tcg_temp_new_i32();
        fn_s(t, d, n, m, g, desc);
        do_pred_flags(t);
    } else {
        fn(d, n, m, g, desc);
    }
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
    TCGv_i32 desc = tcg_constant_i32(FIELD_DP32(0, PREDDESC, OPRSZ, vsz));

    tcg_gen_addi_ptr(d, tcg_env, pred_full_reg_offset(s, a->rd));
    tcg_gen_addi_ptr(n, tcg_env, pred_full_reg_offset(s, a->rn));
    tcg_gen_addi_ptr(g, tcg_env, pred_full_reg_offset(s, a->pg));

    if (a->s) {
        TCGv_i32 t = tcg_temp_new_i32();
        fn_s(t, d, n, g, desc);
        do_pred_flags(t);
    } else {
        fn(d, n, g, desc);
    }
    return true;
}

TRANS_FEAT(BRKPA, aa64_sve, do_brk3, a,
           gen_helper_sve_brkpa, gen_helper_sve_brkpas)
TRANS_FEAT(BRKPB, aa64_sve, do_brk3, a,
           gen_helper_sve_brkpb, gen_helper_sve_brkpbs)

TRANS_FEAT(BRKA_m, aa64_sve, do_brk2, a,
           gen_helper_sve_brka_m, gen_helper_sve_brkas_m)
TRANS_FEAT(BRKB_m, aa64_sve, do_brk2, a,
           gen_helper_sve_brkb_m, gen_helper_sve_brkbs_m)

TRANS_FEAT(BRKA_z, aa64_sve, do_brk2, a,
           gen_helper_sve_brka_z, gen_helper_sve_brkas_z)
TRANS_FEAT(BRKB_z, aa64_sve, do_brk2, a,
           gen_helper_sve_brkb_z, gen_helper_sve_brkbs_z)

TRANS_FEAT(BRKN, aa64_sve, do_brk2, a,
           gen_helper_sve_brkn, gen_helper_sve_brkns)

/*
 *** SVE Predicate Count Group
 */

static void do_cntp(DisasContext *s, TCGv_i64 val, int esz, int pn, int pg)
{
    unsigned psz = pred_full_reg_size(s);

    if (psz <= 8) {
        uint64_t psz_mask;

        tcg_gen_ld_i64(val, tcg_env, pred_full_reg_offset(s, pn));
        if (pn != pg) {
            TCGv_i64 g = tcg_temp_new_i64();
            tcg_gen_ld_i64(g, tcg_env, pred_full_reg_offset(s, pg));
            tcg_gen_and_i64(val, val, g);
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

        desc = FIELD_DP32(desc, PREDDESC, OPRSZ, psz);
        desc = FIELD_DP32(desc, PREDDESC, ESZ, esz);

        tcg_gen_addi_ptr(t_pn, tcg_env, pred_full_reg_offset(s, pn));
        tcg_gen_addi_ptr(t_pg, tcg_env, pred_full_reg_offset(s, pg));

        gen_helper_sve_cntp(val, t_pn, t_pg, tcg_constant_i32(desc));
    }
}

static bool trans_CNTP(DisasContext *s, arg_CNTP *a)
{
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        do_cntp(s, cpu_reg(s, a->rd), a->esz, a->rn, a->pg);
    }
    return true;
}

static bool trans_CNTP_c(DisasContext *s, arg_CNTP_c *a)
{
    TCGv_i32 t_png;
    uint32_t desc = 0;

    if (dc_isar_feature(aa64_sve2p1, s)) {
        if (!sve_access_check(s)) {
            return true;
        }
    } else if (dc_isar_feature(aa64_sme2, s)) {
        if (!sme_sm_enabled_check(s)) {
            return true;
        }
    } else {
        return false;
    }

    t_png = tcg_temp_new_i32();
    tcg_gen_ld16u_i32(t_png, tcg_env,
                      pred_full_reg_offset(s, a->rn) ^
                      (HOST_BIG_ENDIAN ? 6 : 0));

    desc = FIELD_DP32(desc, PREDDESC, OPRSZ, pred_full_reg_size(s));
    desc = FIELD_DP32(desc, PREDDESC, ESZ, a->esz);
    desc = FIELD_DP32(desc, PREDDESC, DATA, a->vl);

    gen_helper_sve2p1_cntp_c(cpu_reg(s, a->rd), t_png, tcg_constant_i32(desc));
    return true;
}

static bool trans_INCDECP_r(DisasContext *s, arg_incdec_pred *a)
{
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        TCGv_i64 reg = cpu_reg(s, a->rd);
        TCGv_i64 val = tcg_temp_new_i64();

        do_cntp(s, val, a->esz, a->pg, a->pg);
        if (a->d) {
            tcg_gen_sub_i64(reg, reg, val);
        } else {
            tcg_gen_add_i64(reg, reg, val);
        }
    }
    return true;
}

static bool trans_INCDECP_z(DisasContext *s, arg_incdec2_pred *a)
{
    if (a->esz == 0 || !dc_isar_feature(aa64_sve, s)) {
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
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
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
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
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
    if (a->esz == 0 || !dc_isar_feature(aa64_sve, s)) {
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
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (!sve_access_check(s)) {
        return true;
    }

    TCGCond cond = (a->ne ? TCG_COND_NE : TCG_COND_EQ);
    TCGv_i64 rn = read_cpu_reg(s, a->rn, a->sf);
    TCGv_i64 rm = read_cpu_reg(s, a->rm, a->sf);
    TCGv_i64 cmp = tcg_temp_new_i64();

    tcg_gen_setcond_i64(cond, cmp, rn, rm);
    tcg_gen_extrl_i64_i32(cpu_NF, cmp);

    /* VF = !NF & !CF.  */
    tcg_gen_xori_i32(cpu_VF, cpu_NF, 1);
    tcg_gen_andc_i32(cpu_VF, cpu_VF, cpu_CF);

    /* Both NF and VF actually look at bit 31.  */
    tcg_gen_neg_i32(cpu_NF, cpu_NF);
    tcg_gen_neg_i32(cpu_VF, cpu_VF);
    return true;
}

typedef void gen_while_fn(TCGv_i32, TCGv_ptr, TCGv_i32, TCGv_i32);
static bool do_WHILE(DisasContext *s, arg_while *a,
                     bool lt, int scale, int data, gen_while_fn *fn)
{
    TCGv_i64 op0, op1, t0, t1, tmax;
    TCGv_i32 t2;
    TCGv_ptr ptr;
    unsigned vsz = vec_full_reg_size(s);
    unsigned desc = 0;
    TCGCond cond;
    uint64_t maxval;
    /* Note that GE/HS has a->eq == 0 and GT/HI has a->eq == 1. */
    bool eq = a->eq == lt;

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

    if (lt) {
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

    tmax = tcg_constant_i64((vsz << scale) >> a->esz);
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

    /* Set the count to zero if the condition is false.  */
    tcg_gen_movi_i64(t1, 0);
    tcg_gen_movcond_i64(cond, t0, op0, op1, t0, t1);

    /* Since we're bounded, pass as a 32-bit type.  */
    t2 = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(t2, t0);

    desc = FIELD_DP32(desc, PREDDESC, OPRSZ, vsz / 8);
    desc = FIELD_DP32(desc, PREDDESC, ESZ, a->esz);
    desc = FIELD_DP32(desc, PREDDESC, DATA, data);

    ptr = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(ptr, tcg_env, pred_full_reg_offset(s, a->rd));

    fn(t2, ptr, t2, tcg_constant_i32(desc));

    do_pred_flags(t2);
    return true;
}

TRANS_FEAT(WHILE_lt, aa64_sve, do_WHILE,
           a, true, 0, 0, gen_helper_sve_whilel)
TRANS_FEAT(WHILE_gt, aa64_sve2, do_WHILE,
           a, false, 0, 0, gen_helper_sve_whileg)

TRANS_FEAT(WHILE_lt_pair, aa64_sme2_or_sve2p1, do_WHILE,
           a, true, 1, 0, gen_helper_sve_while2l)
TRANS_FEAT(WHILE_gt_pair, aa64_sme2_or_sve2p1, do_WHILE,
           a, false, 1, 0, gen_helper_sve_while2g)

TRANS_FEAT(WHILE_lt_cnt2, aa64_sme2_or_sve2p1, do_WHILE,
           a, true, 1, 1, gen_helper_sve_whilecl)
TRANS_FEAT(WHILE_lt_cnt4, aa64_sme2_or_sve2p1, do_WHILE,
           a, true, 2, 2, gen_helper_sve_whilecl)
TRANS_FEAT(WHILE_gt_cnt2, aa64_sme2_or_sve2p1, do_WHILE,
           a, false, 1, 1, gen_helper_sve_whilecg)
TRANS_FEAT(WHILE_gt_cnt4, aa64_sme2_or_sve2p1, do_WHILE,
           a, false, 2, 2, gen_helper_sve_whilecg)

static bool trans_WHILE_ptr(DisasContext *s, arg_WHILE_ptr *a)
{
    TCGv_i64 op0, op1, diff, t1, tmax;
    TCGv_i32 t2;
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

    tmax = tcg_constant_i64(vsz >> a->esz);
    diff = tcg_temp_new_i64();

    if (a->rw) {
        /* WHILERW */
        /* diff = abs(op1 - op0), noting that op0/1 are unsigned. */
        t1 = tcg_temp_new_i64();
        tcg_gen_sub_i64(diff, op0, op1);
        tcg_gen_sub_i64(t1, op1, op0);
        tcg_gen_movcond_i64(TCG_COND_GEU, diff, op0, op1, diff, t1);
        /* Divide, rounding down, by ESIZE.  */
        tcg_gen_shri_i64(diff, diff, a->esz);
        /* If op1 == op0, diff == 0, and the condition is always true. */
        tcg_gen_movcond_i64(TCG_COND_EQ, diff, op0, op1, tmax, diff);
    } else {
        /* WHILEWR */
        tcg_gen_sub_i64(diff, op1, op0);
        /* Divide, rounding down, by ESIZE.  */
        tcg_gen_shri_i64(diff, diff, a->esz);
        /* If op0 >= op1, diff <= 0, the condition is always true. */
        tcg_gen_movcond_i64(TCG_COND_GEU, diff, op0, op1, tmax, diff);
    }

    /* Bound to the maximum.  */
    tcg_gen_umin_i64(diff, diff, tmax);

    /* Since we're bounded, pass as a 32-bit type.  */
    t2 = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(t2, diff);

    desc = FIELD_DP32(desc, PREDDESC, OPRSZ, vsz / 8);
    desc = FIELD_DP32(desc, PREDDESC, ESZ, a->esz);

    ptr = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(ptr, tcg_env, pred_full_reg_offset(s, a->rd));

    gen_helper_sve_whilel(t2, ptr, t2, tcg_constant_i32(desc));
    do_pred_flags(t2);
    return true;
}

static bool do_pext(DisasContext *s, arg_pext *a, int n)
{
    TCGv_i32 t_png;
    TCGv_ptr t_pd;
    int pl;

    if (!sve_access_check(s)) {
        return true;
    }

    t_png = tcg_temp_new_i32();
    tcg_gen_ld16u_i32(t_png, tcg_env,
                      pred_full_reg_offset(s, a->rn) ^
                      (HOST_BIG_ENDIAN ? 6 : 0));

    t_pd = tcg_temp_new_ptr();
    pl = pred_full_reg_size(s);

    for (int i = 0; i < n; ++i) {
        int rd = (a->rd + i) % 16;
        int part = a->imm * n + i;
        unsigned desc = 0;

        desc = FIELD_DP32(desc, PREDDESC, OPRSZ, pl);
        desc = FIELD_DP32(desc, PREDDESC, ESZ, a->esz);
        desc = FIELD_DP32(desc, PREDDESC, DATA, part);

        tcg_gen_addi_ptr(t_pd, tcg_env, pred_full_reg_offset(s, rd));
        gen_helper_pext(t_pd, t_png, tcg_constant_i32(desc));
    }
    return true;
}

TRANS_FEAT(PEXT_1, aa64_sme2_or_sve2p1, do_pext, a, 1)
TRANS_FEAT(PEXT_2, aa64_sme2_or_sve2p1, do_pext, a, 2)

/*
 *** SVE Integer Wide Immediate - Unpredicated Group
 */

static bool trans_FDUP(DisasContext *s, arg_FDUP *a)
{
    if (a->esz == 0 || !dc_isar_feature(aa64_sve, s)) {
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
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        int dofs = vec_full_reg_offset(s, a->rd);
        tcg_gen_gvec_dup_imm(a->esz, dofs, vsz, vsz, a->imm);
    }
    return true;
}

TRANS_FEAT(ADD_zzi, aa64_sve, gen_gvec_fn_arg_zzi, tcg_gen_gvec_addi, a)

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

    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_2s(vec_full_reg_offset(s, a->rd),
                        vec_full_reg_offset(s, a->rn),
                        vsz, vsz, tcg_constant_i64(a->imm), &op[a->esz]);
    }
    return true;
}

TRANS_FEAT(MUL_zzi, aa64_sve, gen_gvec_fn_arg_zzi, tcg_gen_gvec_muli, a)

static bool do_zzi_sat(DisasContext *s, arg_rri_esz *a, bool u, bool d)
{
    if (sve_access_check(s)) {
        do_sat_addsub_vec(s, a->esz, a->rd, a->rn,
                          tcg_constant_i64(a->imm), u, d);
    }
    return true;
}

TRANS_FEAT(SQADD_zzi, aa64_sve, do_zzi_sat, a, false, false)
TRANS_FEAT(UQADD_zzi, aa64_sve, do_zzi_sat, a, true, false)
TRANS_FEAT(SQSUB_zzi, aa64_sve, do_zzi_sat, a, false, true)
TRANS_FEAT(UQSUB_zzi, aa64_sve, do_zzi_sat, a, true, true)

static bool do_zzi_ool(DisasContext *s, arg_rri_esz *a, gen_helper_gvec_2i *fn)
{
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_2i_ool(vec_full_reg_offset(s, a->rd),
                            vec_full_reg_offset(s, a->rn),
                            tcg_constant_i64(a->imm), vsz, vsz, 0, fn);
    }
    return true;
}

#define DO_ZZI(NAME, name) \
    static gen_helper_gvec_2i * const name##i_fns[4] = {                \
        gen_helper_sve_##name##i_b, gen_helper_sve_##name##i_h,         \
        gen_helper_sve_##name##i_s, gen_helper_sve_##name##i_d,         \
    };                                                                  \
    TRANS_FEAT(NAME##_zzi, aa64_sve, do_zzi_ool, a, name##i_fns[a->esz])

DO_ZZI(SMAX, smax)
DO_ZZI(UMAX, umax)
DO_ZZI(SMIN, smin)
DO_ZZI(UMIN, umin)

#undef DO_ZZI

static gen_helper_gvec_4 * const dot_fns[2][2] = {
    { gen_helper_gvec_sdot_4b, gen_helper_gvec_sdot_4h },
    { gen_helper_gvec_udot_4b, gen_helper_gvec_udot_4h }
};
TRANS_FEAT(DOT_zzzz, aa64_sve, gen_gvec_ool_zzzz,
           dot_fns[a->u][a->sz], a->rd, a->rn, a->rm, a->ra, 0)

/*
 * SVE Multiply - Indexed
 */

TRANS_FEAT(SDOT_zzxw_4s, aa64_sve, gen_gvec_ool_arg_zzxz,
           gen_helper_gvec_sdot_idx_4b, a)
TRANS_FEAT(SDOT_zzxw_4d, aa64_sve, gen_gvec_ool_arg_zzxz,
           gen_helper_gvec_sdot_idx_4h, a)
TRANS_FEAT(UDOT_zzxw_4s, aa64_sve, gen_gvec_ool_arg_zzxz,
           gen_helper_gvec_udot_idx_4b, a)
TRANS_FEAT(UDOT_zzxw_4d, aa64_sve, gen_gvec_ool_arg_zzxz,
           gen_helper_gvec_udot_idx_4h, a)

TRANS_FEAT(SUDOT_zzxw_4s, aa64_sve_i8mm, gen_gvec_ool_arg_zzxz,
           gen_helper_gvec_sudot_idx_4b, a)
TRANS_FEAT(USDOT_zzxw_4s, aa64_sve_i8mm, gen_gvec_ool_arg_zzxz,
           gen_helper_gvec_usdot_idx_4b, a)

TRANS_FEAT(SDOT_zzxw_2s, aa64_sme2_or_sve2p1, gen_gvec_ool_arg_zzxz,
           gen_helper_gvec_sdot_idx_2h, a)
TRANS_FEAT(UDOT_zzxw_2s, aa64_sme2_or_sve2p1, gen_gvec_ool_arg_zzxz,
           gen_helper_gvec_udot_idx_2h, a)

#define DO_SVE2_RRX(NAME, FUNC) \
    TRANS_FEAT(NAME, aa64_sve, gen_gvec_ool_zzz, FUNC,          \
               a->rd, a->rn, a->rm, a->index)

DO_SVE2_RRX(MUL_zzx_h, gen_helper_gvec_mul_idx_h)
DO_SVE2_RRX(MUL_zzx_s, gen_helper_gvec_mul_idx_s)
DO_SVE2_RRX(MUL_zzx_d, gen_helper_gvec_mul_idx_d)

DO_SVE2_RRX(SQDMULH_zzx_h, gen_helper_sve2_sqdmulh_idx_h)
DO_SVE2_RRX(SQDMULH_zzx_s, gen_helper_sve2_sqdmulh_idx_s)
DO_SVE2_RRX(SQDMULH_zzx_d, gen_helper_sve2_sqdmulh_idx_d)

DO_SVE2_RRX(SQRDMULH_zzx_h, gen_helper_sve2_sqrdmulh_idx_h)
DO_SVE2_RRX(SQRDMULH_zzx_s, gen_helper_sve2_sqrdmulh_idx_s)
DO_SVE2_RRX(SQRDMULH_zzx_d, gen_helper_sve2_sqrdmulh_idx_d)

#undef DO_SVE2_RRX

#define DO_SVE2_RRX_TB(NAME, FUNC, TOP) \
    TRANS_FEAT(NAME, aa64_sve, gen_gvec_ool_zzz, FUNC,          \
               a->rd, a->rn, a->rm, (a->index << 1) | TOP)

DO_SVE2_RRX_TB(SQDMULLB_zzx_s, gen_helper_sve2_sqdmull_idx_s, false)
DO_SVE2_RRX_TB(SQDMULLB_zzx_d, gen_helper_sve2_sqdmull_idx_d, false)
DO_SVE2_RRX_TB(SQDMULLT_zzx_s, gen_helper_sve2_sqdmull_idx_s, true)
DO_SVE2_RRX_TB(SQDMULLT_zzx_d, gen_helper_sve2_sqdmull_idx_d, true)

DO_SVE2_RRX_TB(SMULLB_zzx_s, gen_helper_sve2_smull_idx_s, false)
DO_SVE2_RRX_TB(SMULLB_zzx_d, gen_helper_sve2_smull_idx_d, false)
DO_SVE2_RRX_TB(SMULLT_zzx_s, gen_helper_sve2_smull_idx_s, true)
DO_SVE2_RRX_TB(SMULLT_zzx_d, gen_helper_sve2_smull_idx_d, true)

DO_SVE2_RRX_TB(UMULLB_zzx_s, gen_helper_sve2_umull_idx_s, false)
DO_SVE2_RRX_TB(UMULLB_zzx_d, gen_helper_sve2_umull_idx_d, false)
DO_SVE2_RRX_TB(UMULLT_zzx_s, gen_helper_sve2_umull_idx_s, true)
DO_SVE2_RRX_TB(UMULLT_zzx_d, gen_helper_sve2_umull_idx_d, true)

#undef DO_SVE2_RRX_TB

#define DO_SVE2_RRXR(NAME, FUNC) \
    TRANS_FEAT(NAME, aa64_sve2, gen_gvec_ool_arg_zzxz, FUNC, a)

DO_SVE2_RRXR(MLA_zzxz_h, gen_helper_gvec_mla_idx_h)
DO_SVE2_RRXR(MLA_zzxz_s, gen_helper_gvec_mla_idx_s)
DO_SVE2_RRXR(MLA_zzxz_d, gen_helper_gvec_mla_idx_d)

DO_SVE2_RRXR(MLS_zzxz_h, gen_helper_gvec_mls_idx_h)
DO_SVE2_RRXR(MLS_zzxz_s, gen_helper_gvec_mls_idx_s)
DO_SVE2_RRXR(MLS_zzxz_d, gen_helper_gvec_mls_idx_d)

DO_SVE2_RRXR(SQRDMLAH_zzxz_h, gen_helper_sve2_sqrdmlah_idx_h)
DO_SVE2_RRXR(SQRDMLAH_zzxz_s, gen_helper_sve2_sqrdmlah_idx_s)
DO_SVE2_RRXR(SQRDMLAH_zzxz_d, gen_helper_sve2_sqrdmlah_idx_d)

DO_SVE2_RRXR(SQRDMLSH_zzxz_h, gen_helper_sve2_sqrdmlsh_idx_h)
DO_SVE2_RRXR(SQRDMLSH_zzxz_s, gen_helper_sve2_sqrdmlsh_idx_s)
DO_SVE2_RRXR(SQRDMLSH_zzxz_d, gen_helper_sve2_sqrdmlsh_idx_d)

#undef DO_SVE2_RRXR

#define DO_SVE2_RRXR_TB(NAME, FUNC, TOP) \
    TRANS_FEAT(NAME, aa64_sve2, gen_gvec_ool_zzzz, FUNC,        \
               a->rd, a->rn, a->rm, a->ra, (a->index << 1) | TOP)

DO_SVE2_RRXR_TB(SQDMLALB_zzxw_s, gen_helper_sve2_sqdmlal_idx_s, false)
DO_SVE2_RRXR_TB(SQDMLALB_zzxw_d, gen_helper_sve2_sqdmlal_idx_d, false)
DO_SVE2_RRXR_TB(SQDMLALT_zzxw_s, gen_helper_sve2_sqdmlal_idx_s, true)
DO_SVE2_RRXR_TB(SQDMLALT_zzxw_d, gen_helper_sve2_sqdmlal_idx_d, true)

DO_SVE2_RRXR_TB(SQDMLSLB_zzxw_s, gen_helper_sve2_sqdmlsl_idx_s, false)
DO_SVE2_RRXR_TB(SQDMLSLB_zzxw_d, gen_helper_sve2_sqdmlsl_idx_d, false)
DO_SVE2_RRXR_TB(SQDMLSLT_zzxw_s, gen_helper_sve2_sqdmlsl_idx_s, true)
DO_SVE2_RRXR_TB(SQDMLSLT_zzxw_d, gen_helper_sve2_sqdmlsl_idx_d, true)

DO_SVE2_RRXR_TB(SMLALB_zzxw_s, gen_helper_sve2_smlal_idx_s, false)
DO_SVE2_RRXR_TB(SMLALB_zzxw_d, gen_helper_sve2_smlal_idx_d, false)
DO_SVE2_RRXR_TB(SMLALT_zzxw_s, gen_helper_sve2_smlal_idx_s, true)
DO_SVE2_RRXR_TB(SMLALT_zzxw_d, gen_helper_sve2_smlal_idx_d, true)

DO_SVE2_RRXR_TB(UMLALB_zzxw_s, gen_helper_sve2_umlal_idx_s, false)
DO_SVE2_RRXR_TB(UMLALB_zzxw_d, gen_helper_sve2_umlal_idx_d, false)
DO_SVE2_RRXR_TB(UMLALT_zzxw_s, gen_helper_sve2_umlal_idx_s, true)
DO_SVE2_RRXR_TB(UMLALT_zzxw_d, gen_helper_sve2_umlal_idx_d, true)

DO_SVE2_RRXR_TB(SMLSLB_zzxw_s, gen_helper_sve2_smlsl_idx_s, false)
DO_SVE2_RRXR_TB(SMLSLB_zzxw_d, gen_helper_sve2_smlsl_idx_d, false)
DO_SVE2_RRXR_TB(SMLSLT_zzxw_s, gen_helper_sve2_smlsl_idx_s, true)
DO_SVE2_RRXR_TB(SMLSLT_zzxw_d, gen_helper_sve2_smlsl_idx_d, true)

DO_SVE2_RRXR_TB(UMLSLB_zzxw_s, gen_helper_sve2_umlsl_idx_s, false)
DO_SVE2_RRXR_TB(UMLSLB_zzxw_d, gen_helper_sve2_umlsl_idx_d, false)
DO_SVE2_RRXR_TB(UMLSLT_zzxw_s, gen_helper_sve2_umlsl_idx_s, true)
DO_SVE2_RRXR_TB(UMLSLT_zzxw_d, gen_helper_sve2_umlsl_idx_d, true)

#undef DO_SVE2_RRXR_TB

#define DO_SVE2_RRXR_ROT(NAME, FUNC) \
    TRANS_FEAT(NAME, aa64_sve2, gen_gvec_ool_zzzz, FUNC,           \
               a->rd, a->rn, a->rm, a->ra, (a->index << 2) | a->rot)

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

static bool do_fmla_zzxz(DisasContext *s, arg_rrxr_esz *a,
                         gen_helper_gvec_4_ptr *fn)
{
    /* These insns use MO_8 to encode BFloat16 */
    if (a->esz == MO_8 && !dc_isar_feature(aa64_sve_b16b16, s)) {
        return false;
    }
    return gen_gvec_fpst_zzzz(s, fn, a->rd, a->rn, a->rm, a->ra, a->index,
                              a->esz == MO_16 ? FPST_A64_F16 : FPST_A64);
}

static gen_helper_gvec_4_ptr * const fmla_idx_fns[4] = {
    gen_helper_gvec_bfmla_idx, gen_helper_gvec_fmla_idx_h,
    gen_helper_gvec_fmla_idx_s, gen_helper_gvec_fmla_idx_d
};
TRANS_FEAT(FMLA_zzxz, aa64_sve, do_fmla_zzxz, a, fmla_idx_fns[a->esz])

static gen_helper_gvec_4_ptr * const fmls_idx_fns[4][2] = {
    { gen_helper_gvec_bfmls_idx, gen_helper_gvec_ah_bfmls_idx },
    { gen_helper_gvec_fmls_idx_h, gen_helper_gvec_ah_fmls_idx_h },
    { gen_helper_gvec_fmls_idx_s, gen_helper_gvec_ah_fmls_idx_s },
    { gen_helper_gvec_fmls_idx_d, gen_helper_gvec_ah_fmls_idx_d },
};
TRANS_FEAT(FMLS_zzxz, aa64_sve, do_fmla_zzxz, a,
           fmls_idx_fns[a->esz][s->fpcr_ah])

/*
 *** SVE Floating Point Multiply Indexed Group
 */

static gen_helper_gvec_3_ptr * const fmul_idx_fns[4] = {
    gen_helper_gvec_fmul_idx_b16, gen_helper_gvec_fmul_idx_h,
    gen_helper_gvec_fmul_idx_s, gen_helper_gvec_fmul_idx_d,
};
TRANS_FEAT(FMUL_zzx, aa64_sve, gen_gvec_fpst_zzz,
           fmul_idx_fns[a->esz], a->rd, a->rn, a->rm, a->index,
           a->esz == MO_16 ? FPST_A64_F16 : FPST_A64)

/*
 *** SVE Floating Point Fast Reduction Group
 */

typedef void gen_helper_fp_reduce(TCGv_i64, TCGv_ptr, TCGv_ptr,
                                  TCGv_ptr, TCGv_i32);

static bool do_reduce(DisasContext *s, arg_rpr_esz *a,
                      gen_helper_fp_reduce *fn)
{
    unsigned vsz, p2vsz;
    TCGv_i32 t_desc;
    TCGv_ptr t_zn, t_pg, status;
    TCGv_i64 temp;

    if (fn == NULL) {
        return false;
    }
    if (!sve_access_check(s)) {
        return true;
    }

    vsz = vec_full_reg_size(s);
    p2vsz = pow2ceil(vsz);
    t_desc = tcg_constant_i32(simd_desc(vsz, vsz, p2vsz));
    temp = tcg_temp_new_i64();
    t_zn = tcg_temp_new_ptr();
    t_pg = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(t_zn, tcg_env, vec_full_reg_offset(s, a->rn));
    tcg_gen_addi_ptr(t_pg, tcg_env, pred_full_reg_offset(s, a->pg));
    status = fpstatus_ptr(a->esz == MO_16 ? FPST_A64_F16 : FPST_A64);

    fn(temp, t_zn, t_pg, status, t_desc);

    write_fp_dreg(s, a->rd, temp);
    return true;
}

#define DO_VPZ(NAME, name) \
    static gen_helper_fp_reduce * const name##_fns[4] = {                \
        NULL,                      gen_helper_sve_##name##_h,            \
        gen_helper_sve_##name##_s, gen_helper_sve_##name##_d,            \
    };                                                                   \
    TRANS_FEAT(NAME, aa64_sve, do_reduce, a, name##_fns[a->esz])

#define DO_VPZ_AH(NAME, name)                                            \
    static gen_helper_fp_reduce * const name##_fns[4] = {                \
        NULL,                      gen_helper_sve_##name##_h,            \
        gen_helper_sve_##name##_s, gen_helper_sve_##name##_d,            \
    };                                                                   \
    static gen_helper_fp_reduce * const name##_ah_fns[4] = {             \
        NULL,                      gen_helper_sve_ah_##name##_h,         \
        gen_helper_sve_ah_##name##_s, gen_helper_sve_ah_##name##_d,      \
    };                                                                   \
    TRANS_FEAT(NAME, aa64_sve, do_reduce, a,                             \
               s->fpcr_ah ? name##_ah_fns[a->esz] : name##_fns[a->esz])

DO_VPZ(FADDV, faddv)
DO_VPZ(FMINNMV, fminnmv)
DO_VPZ(FMAXNMV, fmaxnmv)
DO_VPZ_AH(FMINV, fminv)
DO_VPZ_AH(FMAXV, fmaxv)

#undef DO_VPZ

static gen_helper_gvec_3_ptr * const faddqv_fns[4] = {
    NULL,                       gen_helper_sve2p1_faddqv_h,
    gen_helper_sve2p1_faddqv_s, gen_helper_sve2p1_faddqv_d,
};
TRANS_FEAT(FADDQV, aa64_sme2p1_or_sve2p1, gen_gvec_fpst_arg_zpz,
           faddqv_fns[a->esz], a, 0,
           a->esz == MO_16 ? FPST_A64_F16 : FPST_A64)

static gen_helper_gvec_3_ptr * const fmaxnmqv_fns[4] = {
    NULL,                         gen_helper_sve2p1_fmaxnmqv_h,
    gen_helper_sve2p1_fmaxnmqv_s, gen_helper_sve2p1_fmaxnmqv_d,
};
TRANS_FEAT(FMAXNMQV, aa64_sme2p1_or_sve2p1, gen_gvec_fpst_arg_zpz,
           fmaxnmqv_fns[a->esz], a, 0,
           a->esz == MO_16 ? FPST_A64_F16 : FPST_A64)

static gen_helper_gvec_3_ptr * const fminnmqv_fns[4] = {
    NULL,                         gen_helper_sve2p1_fminnmqv_h,
    gen_helper_sve2p1_fminnmqv_s, gen_helper_sve2p1_fminnmqv_d,
};
TRANS_FEAT(FMINNMQV, aa64_sme2p1_or_sve2p1, gen_gvec_fpst_arg_zpz,
           fminnmqv_fns[a->esz], a, 0,
           a->esz == MO_16 ? FPST_A64_F16 : FPST_A64)

static gen_helper_gvec_3_ptr * const fmaxqv_fns[4] = {
    NULL,                       gen_helper_sve2p1_fmaxqv_h,
    gen_helper_sve2p1_fmaxqv_s, gen_helper_sve2p1_fmaxqv_d,
};
static gen_helper_gvec_3_ptr * const fmaxqv_ah_fns[4] = {
    NULL,                          gen_helper_sve2p1_ah_fmaxqv_h,
    gen_helper_sve2p1_ah_fmaxqv_s, gen_helper_sve2p1_ah_fmaxqv_d,
};
TRANS_FEAT(FMAXQV, aa64_sme2p1_or_sve2p1, gen_gvec_fpst_arg_zpz,
           (s->fpcr_ah ? fmaxqv_ah_fns : fmaxqv_fns)[a->esz], a, 0,
           a->esz == MO_16 ? FPST_A64_F16 : FPST_A64)

static gen_helper_gvec_3_ptr * const fminqv_fns[4] = {
    NULL,                       gen_helper_sve2p1_fminqv_h,
    gen_helper_sve2p1_fminqv_s, gen_helper_sve2p1_fminqv_d,
};
static gen_helper_gvec_3_ptr * const fminqv_ah_fns[4] = {
    NULL,                          gen_helper_sve2p1_ah_fminqv_h,
    gen_helper_sve2p1_ah_fminqv_s, gen_helper_sve2p1_ah_fminqv_d,
};
TRANS_FEAT(FMINQV, aa64_sme2p1_or_sve2p1, gen_gvec_fpst_arg_zpz,
           (s->fpcr_ah ? fminqv_ah_fns : fminqv_fns)[a->esz], a, 0,
           a->esz == MO_16 ? FPST_A64_F16 : FPST_A64)

/*
 *** SVE Floating Point Unary Operations - Unpredicated Group
 */

static gen_helper_gvec_2_ptr * const frecpe_fns[] = {
    NULL,                     gen_helper_gvec_frecpe_h,
    gen_helper_gvec_frecpe_s, gen_helper_gvec_frecpe_d,
};
static gen_helper_gvec_2_ptr * const frecpe_rpres_fns[] = {
    NULL,                           gen_helper_gvec_frecpe_h,
    gen_helper_gvec_frecpe_rpres_s, gen_helper_gvec_frecpe_d,
};
TRANS_FEAT(FRECPE, aa64_sve, gen_gvec_fpst_ah_arg_zz,
           s->fpcr_ah && dc_isar_feature(aa64_rpres, s) ?
           frecpe_rpres_fns[a->esz] : frecpe_fns[a->esz], a, 0)

static gen_helper_gvec_2_ptr * const frsqrte_fns[] = {
    NULL,                      gen_helper_gvec_frsqrte_h,
    gen_helper_gvec_frsqrte_s, gen_helper_gvec_frsqrte_d,
};
static gen_helper_gvec_2_ptr * const frsqrte_rpres_fns[] = {
    NULL,                            gen_helper_gvec_frsqrte_h,
    gen_helper_gvec_frsqrte_rpres_s, gen_helper_gvec_frsqrte_d,
};
TRANS_FEAT(FRSQRTE, aa64_sve, gen_gvec_fpst_ah_arg_zz,
           s->fpcr_ah && dc_isar_feature(aa64_rpres, s) ?
           frsqrte_rpres_fns[a->esz] : frsqrte_fns[a->esz], a, 0)

/*
 *** SVE Floating Point Compare with Zero Group
 */

static bool do_ppz_fp(DisasContext *s, arg_rpr_esz *a,
                      gen_helper_gvec_3_ptr *fn)
{
    if (fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_ptr status =
            fpstatus_ptr(a->esz == MO_16 ? FPST_A64_F16 : FPST_A64);

        tcg_gen_gvec_3_ptr(pred_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           pred_full_reg_offset(s, a->pg),
                           status, vsz, vsz, 0, fn);
    }
    return true;
}

#define DO_PPZ(NAME, name) \
    static gen_helper_gvec_3_ptr * const name##_fns[] = {         \
        NULL,                      gen_helper_sve_##name##_h,     \
        gen_helper_sve_##name##_s, gen_helper_sve_##name##_d,     \
    };                                                            \
    TRANS_FEAT(NAME, aa64_sve, do_ppz_fp, a, name##_fns[a->esz])

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

static gen_helper_gvec_3_ptr * const ftmad_fns[4] = {
    NULL,                   gen_helper_sve_ftmad_h,
    gen_helper_sve_ftmad_s, gen_helper_sve_ftmad_d,
};
TRANS_FEAT_NONSTREAMING(FTMAD, aa64_sve, gen_gvec_fpst_zzz,
                        ftmad_fns[a->esz], a->rd, a->rn, a->rm,
                        a->imm | (s->fpcr_ah << 3),
                        a->esz == MO_16 ? FPST_A64_F16 : FPST_A64)

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

    if (a->esz == 0 || !dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    s->is_nonstreaming = true;
    if (!sve_access_check(s)) {
        return true;
    }

    t_val = load_esz(tcg_env, vec_reg_offset(s, a->rn, 0, a->esz), a->esz);
    t_rm = tcg_temp_new_ptr();
    t_pg = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(t_rm, tcg_env, vec_full_reg_offset(s, a->rm));
    tcg_gen_addi_ptr(t_pg, tcg_env, pred_full_reg_offset(s, a->pg));
    t_fpst = fpstatus_ptr(a->esz == MO_16 ? FPST_A64_F16 : FPST_A64);
    t_desc = tcg_constant_i32(simd_desc(vsz, vsz, 0));

    fns[a->esz - 1](t_val, t_val, t_rm, t_pg, t_fpst, t_desc);

    write_fp_dreg(s, a->rd, t_val);
    return true;
}

/*
 *** SVE Floating Point Arithmetic - Unpredicated Group
 */

#define DO_FP3(NAME, name) \
    static gen_helper_gvec_3_ptr * const name##_fns[4] = {          \
        gen_helper_gvec_##name##_b16, gen_helper_gvec_##name##_h,   \
        gen_helper_gvec_##name##_s, gen_helper_gvec_##name##_d      \
    };                                                              \
    TRANS_FEAT(NAME, aa64_sve, gen_gvec_fpst_arg_zzz, name##_fns[a->esz], a, 0)

#define DO_FP3_AH(NAME, name) \
    static gen_helper_gvec_3_ptr * const name##_fns[4] = {          \
        NULL, gen_helper_gvec_##name##_h,                           \
        gen_helper_gvec_##name##_s, gen_helper_gvec_##name##_d      \
    };                                                              \
    static gen_helper_gvec_3_ptr * const name##_ah_fns[4] = {       \
        NULL, gen_helper_gvec_ah_##name##_h,                        \
        gen_helper_gvec_ah_##name##_s, gen_helper_gvec_ah_##name##_d    \
    };                                                              \
    TRANS_FEAT(NAME, aa64_sve, gen_gvec_fpst_ah_arg_zzz,            \
               s->fpcr_ah ? name##_ah_fns[a->esz] : name##_fns[a->esz], a, 0)

DO_FP3(FADD_zzz, fadd)
DO_FP3(FSUB_zzz, fsub)
DO_FP3(FMUL_zzz, fmul)
DO_FP3_AH(FRECPS, recps)
DO_FP3_AH(FRSQRTS, rsqrts)

#undef DO_FP3

static gen_helper_gvec_3_ptr * const ftsmul_fns[4] = {
    NULL,                     gen_helper_gvec_ftsmul_h,
    gen_helper_gvec_ftsmul_s, gen_helper_gvec_ftsmul_d
};
TRANS_FEAT_NONSTREAMING(FTSMUL, aa64_sve, gen_gvec_fpst_arg_zzz,
                        ftsmul_fns[a->esz], a, 0)

/*
 *** SVE Floating Point Arithmetic - Predicated Group
 */

#define DO_ZPZZ_FP(NAME, FEAT, name) \
    static gen_helper_gvec_4_ptr * const name##_zpzz_fns[4] = { \
        NULL,                  gen_helper_##name##_h,           \
        gen_helper_##name##_s, gen_helper_##name##_d            \
    };                                                          \
    TRANS_FEAT(NAME, FEAT, gen_gvec_fpst_arg_zpzz, name##_zpzz_fns[a->esz], a)

#define DO_ZPZZ_AH_FP(NAME, FEAT, name, ah_name)                        \
    static gen_helper_gvec_4_ptr * const name##_zpzz_fns[4] = {         \
        NULL,                  gen_helper_##name##_h,                   \
        gen_helper_##name##_s, gen_helper_##name##_d                    \
    };                                                                  \
    static gen_helper_gvec_4_ptr * const name##_ah_zpzz_fns[4] = {      \
        NULL,                  gen_helper_##ah_name##_h,                \
        gen_helper_##ah_name##_s, gen_helper_##ah_name##_d              \
    };                                                                  \
    TRANS_FEAT(NAME, FEAT, gen_gvec_fpst_arg_zpzz,                      \
               s->fpcr_ah ? name##_ah_zpzz_fns[a->esz] :                \
               name##_zpzz_fns[a->esz], a)

/* Similar, but for insns where sz == 0 encodes bfloat16 */
#define DO_ZPZZ_FP_B16(NAME, FEAT, name) \
    static gen_helper_gvec_4_ptr * const name##_zpzz_fns[4] = { \
        gen_helper_##name##_b16, gen_helper_##name##_h,         \
        gen_helper_##name##_s, gen_helper_##name##_d            \
    };                                                          \
    TRANS_FEAT(NAME, FEAT, gen_gvec_fpst_arg_zpzz, name##_zpzz_fns[a->esz], a)

#define DO_ZPZZ_AH_FP_B16(NAME, FEAT, name, ah_name)                    \
    static gen_helper_gvec_4_ptr * const name##_zpzz_fns[4] = {         \
        gen_helper_##name##_b16, gen_helper_##name##_h,                 \
        gen_helper_##name##_s, gen_helper_##name##_d                    \
    };                                                                  \
    static gen_helper_gvec_4_ptr * const name##_ah_zpzz_fns[4] = {      \
        gen_helper_##ah_name##_b16, gen_helper_##ah_name##_h,           \
        gen_helper_##ah_name##_s, gen_helper_##ah_name##_d              \
    };                                                                  \
    TRANS_FEAT(NAME, FEAT, gen_gvec_fpst_arg_zpzz,                      \
               s->fpcr_ah ? name##_ah_zpzz_fns[a->esz] :                \
               name##_zpzz_fns[a->esz], a)

DO_ZPZZ_FP_B16(FADD_zpzz, aa64_sve, sve_fadd)
DO_ZPZZ_FP_B16(FSUB_zpzz, aa64_sve, sve_fsub)
DO_ZPZZ_FP_B16(FMUL_zpzz, aa64_sve, sve_fmul)
DO_ZPZZ_AH_FP_B16(FMIN_zpzz, aa64_sve, sve_fmin, sve_ah_fmin)
DO_ZPZZ_AH_FP_B16(FMAX_zpzz, aa64_sve, sve_fmax, sve_ah_fmax)
DO_ZPZZ_FP_B16(FMINNM_zpzz, aa64_sve, sve_fminnum)
DO_ZPZZ_FP_B16(FMAXNM_zpzz, aa64_sve, sve_fmaxnum)
DO_ZPZZ_AH_FP(FABD, aa64_sve, sve_fabd, sve_ah_fabd)
DO_ZPZZ_FP(FSCALE, aa64_sve, sve_fscalbn)
DO_ZPZZ_FP(FDIV, aa64_sve, sve_fdiv)
DO_ZPZZ_FP(FMULX, aa64_sve, sve_fmulx)

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
    tcg_gen_addi_ptr(t_zd, tcg_env, vec_full_reg_offset(s, zd));
    tcg_gen_addi_ptr(t_zn, tcg_env, vec_full_reg_offset(s, zn));
    tcg_gen_addi_ptr(t_pg, tcg_env, pred_full_reg_offset(s, pg));

    status = fpstatus_ptr(is_fp16 ? FPST_A64_F16 : FPST_A64);
    desc = tcg_constant_i32(simd_desc(vsz, vsz, 0));
    fn(t_zd, t_zn, t_pg, scalar, status, desc);
}

static bool do_fp_imm(DisasContext *s, arg_rpri_esz *a, uint64_t imm,
                      gen_helper_sve_fp2scalar *fn)
{
    if (fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        do_fp_scalar(s, a->rd, a->rn, a->pg, a->esz == MO_16,
                     tcg_constant_i64(imm), fn);
    }
    return true;
}

#define DO_FP_IMM(NAME, name, const0, const1)                           \
    static gen_helper_sve_fp2scalar * const name##_fns[4] = {           \
        NULL, gen_helper_sve_##name##_h,                                \
        gen_helper_sve_##name##_s,                                      \
        gen_helper_sve_##name##_d                                       \
    };                                                                  \
    static uint64_t const name##_const[4][2] = {                        \
        { -1, -1 },                                                     \
        { float16_##const0, float16_##const1 },                         \
        { float32_##const0, float32_##const1 },                         \
        { float64_##const0, float64_##const1 },                         \
    };                                                                  \
    TRANS_FEAT(NAME##_zpzi, aa64_sve, do_fp_imm, a,                     \
               name##_const[a->esz][a->imm], name##_fns[a->esz])

#define DO_FP_AH_IMM(NAME, name, const0, const1)                        \
    static gen_helper_sve_fp2scalar * const name##_fns[4] = {           \
        NULL, gen_helper_sve_##name##_h,                                \
        gen_helper_sve_##name##_s,                                      \
        gen_helper_sve_##name##_d                                       \
    };                                                                  \
    static gen_helper_sve_fp2scalar * const name##_ah_fns[4] = {        \
        NULL, gen_helper_sve_ah_##name##_h,                             \
        gen_helper_sve_ah_##name##_s,                                   \
        gen_helper_sve_ah_##name##_d                                    \
    };                                                                  \
    static uint64_t const name##_const[4][2] = {                        \
        { -1, -1 },                                                     \
        { float16_##const0, float16_##const1 },                         \
        { float32_##const0, float32_##const1 },                         \
        { float64_##const0, float64_##const1 },                         \
    };                                                                  \
    TRANS_FEAT(NAME##_zpzi, aa64_sve, do_fp_imm, a,                     \
               name##_const[a->esz][a->imm],                            \
               s->fpcr_ah ? name##_ah_fns[a->esz] : name##_fns[a->esz])

DO_FP_IMM(FADD, fadds, half, one)
DO_FP_IMM(FSUB, fsubs, half, one)
DO_FP_IMM(FMUL, fmuls, half, two)
DO_FP_IMM(FSUBR, fsubrs, half, one)
DO_FP_IMM(FMAXNM, fmaxnms, zero, one)
DO_FP_IMM(FMINNM, fminnms, zero, one)
DO_FP_AH_IMM(FMAX, fmaxs, zero, one)
DO_FP_AH_IMM(FMIN, fmins, zero, one)

#undef DO_FP_IMM

static bool do_fp_cmp(DisasContext *s, arg_rprr_esz *a,
                      gen_helper_gvec_4_ptr *fn)
{
    if (fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_ptr status = fpstatus_ptr(a->esz == MO_16 ? FPST_A64_F16 : FPST_A64);
        tcg_gen_gvec_4_ptr(pred_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           pred_full_reg_offset(s, a->pg),
                           status, vsz, vsz, 0, fn);
    }
    return true;
}

#define DO_FPCMP(NAME, name) \
    static gen_helper_gvec_4_ptr * const name##_fns[4] = {            \
        NULL, gen_helper_sve_##name##_h,                              \
        gen_helper_sve_##name##_s, gen_helper_sve_##name##_d          \
    };                                                                \
    TRANS_FEAT(NAME##_ppzz, aa64_sve, do_fp_cmp, a, name##_fns[a->esz])

DO_FPCMP(FCMGE, fcmge)
DO_FPCMP(FCMGT, fcmgt)
DO_FPCMP(FCMEQ, fcmeq)
DO_FPCMP(FCMNE, fcmne)
DO_FPCMP(FCMUO, fcmuo)
DO_FPCMP(FACGE, facge)
DO_FPCMP(FACGT, facgt)

#undef DO_FPCMP

static gen_helper_gvec_4_ptr * const fcadd_fns[] = {
    NULL,                   gen_helper_sve_fcadd_h,
    gen_helper_sve_fcadd_s, gen_helper_sve_fcadd_d,
};
TRANS_FEAT(FCADD, aa64_sve, gen_gvec_fpst_zzzp, fcadd_fns[a->esz],
           a->rd, a->rn, a->rm, a->pg, a->rot | (s->fpcr_ah << 1),
           a->esz == MO_16 ? FPST_A64_F16 : FPST_A64)

static bool do_fmla_zpzzz(DisasContext *s, arg_rprrr_esz *a,
                          gen_helper_gvec_5_ptr *fn)
{
    /* These insns use MO_8 to encode BFloat16 */
    if (a->esz == MO_8 && !dc_isar_feature(aa64_sve_b16b16, s)) {
        return false;
    }
    return gen_gvec_fpst_zzzzp(s, fn, a->rd, a->rn, a->rm, a->ra, a->pg, 0,
                               a->esz == MO_16 ? FPST_A64_F16 : FPST_A64);
}

#define DO_FMLA(NAME, name, ah_name)                                    \
    static gen_helper_gvec_5_ptr * const name##_fns[4] = {              \
        gen_helper_sve_##name##_b16, gen_helper_sve_##name##_h,         \
        gen_helper_sve_##name##_s, gen_helper_sve_##name##_d            \
    };                                                                  \
    static gen_helper_gvec_5_ptr * const name##_ah_fns[4] = {           \
        gen_helper_sve_##ah_name##_b16, gen_helper_sve_##ah_name##_h,   \
        gen_helper_sve_##ah_name##_s, gen_helper_sve_##ah_name##_d      \
    };                                                                  \
    TRANS_FEAT(NAME, aa64_sve, do_fmla_zpzzz, a,                        \
               s->fpcr_ah ? name##_ah_fns[a->esz] : name##_fns[a->esz])

/* We don't need an ah_fmla_zpzzz because fmla doesn't negate anything */
DO_FMLA(FMLA_zpzzz, fmla_zpzzz, fmla_zpzzz)
DO_FMLA(FMLS_zpzzz, fmls_zpzzz, ah_fmls_zpzzz)
DO_FMLA(FNMLA_zpzzz, fnmla_zpzzz, ah_fnmla_zpzzz)
DO_FMLA(FNMLS_zpzzz, fnmls_zpzzz, ah_fnmls_zpzzz)

#undef DO_FMLA

static gen_helper_gvec_5_ptr * const fcmla_fns[4] = {
    NULL,                         gen_helper_sve_fcmla_zpzzz_h,
    gen_helper_sve_fcmla_zpzzz_s, gen_helper_sve_fcmla_zpzzz_d,
};
TRANS_FEAT(FCMLA_zpzzz, aa64_sve, gen_gvec_fpst_zzzzp, fcmla_fns[a->esz],
           a->rd, a->rn, a->rm, a->ra, a->pg, a->rot | (s->fpcr_ah << 2),
           a->esz == MO_16 ? FPST_A64_F16 : FPST_A64)

static gen_helper_gvec_4_ptr * const fcmla_idx_fns[4] = {
    NULL, gen_helper_gvec_fcmlah_idx, gen_helper_gvec_fcmlas_idx, NULL
};
TRANS_FEAT(FCMLA_zzxz, aa64_sve, gen_gvec_fpst_zzzz, fcmla_idx_fns[a->esz],
           a->rd, a->rn, a->rm, a->ra, a->index * 4 + a->rot,
           a->esz == MO_16 ? FPST_A64_F16 : FPST_A64)

/*
 *** SVE Floating Point Unary Operations Predicated Group
 */

TRANS_FEAT(FCVT_sh, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_fcvt_sh, a, 0, FPST_A64)
TRANS_FEAT(FCVT_hs, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_fcvt_hs, a, 0, FPST_A64_F16)

TRANS_FEAT(BFCVT, aa64_sve_bf16, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_bfcvt, a, 0,
           s->fpcr_ah ? FPST_AH : FPST_A64)

TRANS_FEAT(FCVT_dh, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_fcvt_dh, a, 0, FPST_A64)
TRANS_FEAT(FCVT_hd, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_fcvt_hd, a, 0, FPST_A64_F16)
TRANS_FEAT(FCVT_ds, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_fcvt_ds, a, 0, FPST_A64)
TRANS_FEAT(FCVT_sd, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_fcvt_sd, a, 0, FPST_A64)

TRANS_FEAT(FCVTZS_hh, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_fcvtzs_hh, a, 0, FPST_A64_F16)
TRANS_FEAT(FCVTZU_hh, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_fcvtzu_hh, a, 0, FPST_A64_F16)
TRANS_FEAT(FCVTZS_hs, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_fcvtzs_hs, a, 0, FPST_A64_F16)
TRANS_FEAT(FCVTZU_hs, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_fcvtzu_hs, a, 0, FPST_A64_F16)
TRANS_FEAT(FCVTZS_hd, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_fcvtzs_hd, a, 0, FPST_A64_F16)
TRANS_FEAT(FCVTZU_hd, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_fcvtzu_hd, a, 0, FPST_A64_F16)

TRANS_FEAT(FCVTZS_ss, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_fcvtzs_ss, a, 0, FPST_A64)
TRANS_FEAT(FCVTZU_ss, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_fcvtzu_ss, a, 0, FPST_A64)
TRANS_FEAT(FCVTZS_sd, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_fcvtzs_sd, a, 0, FPST_A64)
TRANS_FEAT(FCVTZU_sd, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_fcvtzu_sd, a, 0, FPST_A64)
TRANS_FEAT(FCVTZS_ds, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_fcvtzs_ds, a, 0, FPST_A64)
TRANS_FEAT(FCVTZU_ds, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_fcvtzu_ds, a, 0, FPST_A64)

TRANS_FEAT(FCVTZS_dd, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_fcvtzs_dd, a, 0, FPST_A64)
TRANS_FEAT(FCVTZU_dd, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_fcvtzu_dd, a, 0, FPST_A64)

static gen_helper_gvec_3_ptr * const frint_fns[] = {
    NULL,
    gen_helper_sve_frint_h,
    gen_helper_sve_frint_s,
    gen_helper_sve_frint_d
};
TRANS_FEAT(FRINTI, aa64_sve, gen_gvec_fpst_arg_zpz, frint_fns[a->esz],
           a, 0, a->esz == MO_16 ? FPST_A64_F16 : FPST_A64)

static gen_helper_gvec_3_ptr * const frintx_fns[] = {
    NULL,
    gen_helper_sve_frintx_h,
    gen_helper_sve_frintx_s,
    gen_helper_sve_frintx_d
};
TRANS_FEAT(FRINTX, aa64_sve, gen_gvec_fpst_arg_zpz, frintx_fns[a->esz],
           a, 0, a->esz == MO_16 ? FPST_A64_F16 : FPST_A64);

static bool do_frint_mode(DisasContext *s, arg_rpr_esz *a,
                          ARMFPRounding mode, gen_helper_gvec_3_ptr *fn)
{
    unsigned vsz;
    TCGv_i32 tmode;
    TCGv_ptr status;

    if (fn == NULL) {
        return false;
    }
    if (!sve_access_check(s)) {
        return true;
    }

    vsz = vec_full_reg_size(s);
    status = fpstatus_ptr(a->esz == MO_16 ? FPST_A64_F16 : FPST_A64);
    tmode = gen_set_rmode(mode, status);

    tcg_gen_gvec_3_ptr(vec_full_reg_offset(s, a->rd),
                       vec_full_reg_offset(s, a->rn),
                       pred_full_reg_offset(s, a->pg),
                       status, vsz, vsz, 0, fn);

    gen_restore_rmode(tmode, status);
    return true;
}

TRANS_FEAT(FRINTN, aa64_sve, do_frint_mode, a,
           FPROUNDING_TIEEVEN, frint_fns[a->esz])
TRANS_FEAT(FRINTP, aa64_sve, do_frint_mode, a,
           FPROUNDING_POSINF, frint_fns[a->esz])
TRANS_FEAT(FRINTM, aa64_sve, do_frint_mode, a,
           FPROUNDING_NEGINF, frint_fns[a->esz])
TRANS_FEAT(FRINTZ, aa64_sve, do_frint_mode, a,
           FPROUNDING_ZERO, frint_fns[a->esz])
TRANS_FEAT(FRINTA, aa64_sve, do_frint_mode, a,
           FPROUNDING_TIEAWAY, frint_fns[a->esz])

static gen_helper_gvec_3_ptr * const frecpx_fns[] = {
    NULL,                    gen_helper_sve_frecpx_h,
    gen_helper_sve_frecpx_s, gen_helper_sve_frecpx_d,
};
TRANS_FEAT(FRECPX, aa64_sve, gen_gvec_fpst_arg_zpz, frecpx_fns[a->esz],
           a, 0, select_ah_fpst(s, a->esz))

static gen_helper_gvec_3_ptr * const fsqrt_fns[] = {
    NULL,                   gen_helper_sve_fsqrt_h,
    gen_helper_sve_fsqrt_s, gen_helper_sve_fsqrt_d,
};
TRANS_FEAT(FSQRT, aa64_sve, gen_gvec_fpst_arg_zpz, fsqrt_fns[a->esz],
           a, 0, a->esz == MO_16 ? FPST_A64_F16 : FPST_A64)

TRANS_FEAT(SCVTF_hh, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_scvt_hh, a, 0, FPST_A64_F16)
TRANS_FEAT(SCVTF_sh, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_scvt_sh, a, 0, FPST_A64_F16)
TRANS_FEAT(SCVTF_dh, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_scvt_dh, a, 0, FPST_A64_F16)

TRANS_FEAT(SCVTF_ss, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_scvt_ss, a, 0, FPST_A64)
TRANS_FEAT(SCVTF_ds, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_scvt_ds, a, 0, FPST_A64)

TRANS_FEAT(SCVTF_sd, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_scvt_sd, a, 0, FPST_A64)
TRANS_FEAT(SCVTF_dd, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_scvt_dd, a, 0, FPST_A64)

TRANS_FEAT(UCVTF_hh, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_ucvt_hh, a, 0, FPST_A64_F16)
TRANS_FEAT(UCVTF_sh, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_ucvt_sh, a, 0, FPST_A64_F16)
TRANS_FEAT(UCVTF_dh, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_ucvt_dh, a, 0, FPST_A64_F16)

TRANS_FEAT(UCVTF_ss, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_ucvt_ss, a, 0, FPST_A64)
TRANS_FEAT(UCVTF_ds, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_ucvt_ds, a, 0, FPST_A64)
TRANS_FEAT(UCVTF_sd, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_ucvt_sd, a, 0, FPST_A64)

TRANS_FEAT(UCVTF_dd, aa64_sve, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_ucvt_dd, a, 0, FPST_A64)

/*
 *** SVE Memory - 32-bit Gather and Unsized Contiguous Group
 */

/* Subroutine loading a vector register at VOFS of LEN bytes.
 * The load should begin at the address Rn + IMM.
 */

void gen_sve_ldr(DisasContext *s, TCGv_ptr base, int vofs,
                 int len, int rn, int imm, MemOp align)
{
    int len_align = QEMU_ALIGN_DOWN(len, 16);
    int len_remain = len % 16;
    int nparts = len / 16 + ctpop8(len_remain);
    int midx = get_mem_index(s);
    TCGv_i64 dirty_addr, clean_addr, t0, t1;
    TCGv_i128 t16;

    dirty_addr = tcg_temp_new_i64();
    tcg_gen_addi_i64(dirty_addr, cpu_reg_sp(s, rn), imm);
    clean_addr = gen_mte_checkN(s, dirty_addr, false, rn != 31, len, MO_8);

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
        t1 = tcg_temp_new_i64();
        t16 = tcg_temp_new_i128();

        for (i = 0; i < len_align; i += 16) {
            tcg_gen_qemu_ld_i128(t16, clean_addr, midx,
                                 MO_LE | MO_128 | MO_ATOM_NONE | align);
            tcg_gen_extr_i128_i64(t0, t1, t16);
            tcg_gen_st_i64(t0, base, vofs + i);
            tcg_gen_st_i64(t1, base, vofs + i + 8);
            tcg_gen_addi_i64(clean_addr, clean_addr, 16);
        }
        if (len_align) {
            align = MO_UNALN;
        }
    } else {
        TCGLabel *loop = gen_new_label();
        TCGv_ptr tp, i = tcg_temp_new_ptr();

        tcg_gen_movi_ptr(i, 0);
        gen_set_label(loop);

        t16 = tcg_temp_new_i128();
        tcg_gen_qemu_ld_i128(t16, clean_addr, midx,
                             MO_LE | MO_128 | MO_ATOM_NONE | align);
        tcg_gen_addi_i64(clean_addr, clean_addr, 16);

        tp = tcg_temp_new_ptr();
        tcg_gen_add_ptr(tp, base, i);
        tcg_gen_addi_ptr(i, i, 16);

        t0 = tcg_temp_new_i64();
        t1 = tcg_temp_new_i64();
        tcg_gen_extr_i128_i64(t0, t1, t16);

        tcg_gen_st_i64(t0, tp, vofs);
        tcg_gen_st_i64(t1, tp, vofs + 8);

        tcg_gen_brcondi_ptr(TCG_COND_LTU, i, len_align, loop);
        align = MO_UNALN;
    }

    /*
     * Predicate register loads can be any multiple of 2.
     * Note that we still store the entire 64-bit unit into tcg_env.
     */
    if (len_remain >= 8) {
        t0 = tcg_temp_new_i64();
        tcg_gen_qemu_ld_i64(t0, clean_addr, midx,
                            MO_LEUQ | MO_ATOM_NONE | align);
        align = MO_UNALN;
        tcg_gen_st_i64(t0, base, vofs + len_align);
        len_remain -= 8;
        len_align += 8;
        if (len_remain) {
            tcg_gen_addi_i64(clean_addr, clean_addr, 8);
        }
    }
    if (len_remain) {
        t0 = tcg_temp_new_i64();
        switch (len_remain) {
        case 2:
        case 4:
        case 8:
            tcg_gen_qemu_ld_i64(t0, clean_addr, midx,
                                MO_LE | ctz32(len_remain)
                                | MO_ATOM_NONE | align);
            break;

        case 6:
            t1 = tcg_temp_new_i64();
            tcg_gen_qemu_ld_i64(t0, clean_addr, midx,
                                MO_LEUL | MO_ATOM_NONE | align);
            tcg_gen_addi_i64(clean_addr, clean_addr, 4);
            tcg_gen_qemu_ld_i64(t1, clean_addr, midx, MO_LEUW | MO_ATOM_NONE);
            tcg_gen_deposit_i64(t0, t0, t1, 32, 32);
            break;

        default:
            g_assert_not_reached();
        }
        tcg_gen_st_i64(t0, base, vofs + len_align);
    }
}

/* Similarly for stores.  */
void gen_sve_str(DisasContext *s, TCGv_ptr base, int vofs,
                 int len, int rn, int imm, MemOp align)
{
    int len_align = QEMU_ALIGN_DOWN(len, 16);
    int len_remain = len % 16;
    int nparts = len / 16 + ctpop8(len_remain);
    int midx = get_mem_index(s);
    TCGv_i64 dirty_addr, clean_addr, t0, t1;
    TCGv_i128 t16;

    dirty_addr = tcg_temp_new_i64();
    tcg_gen_addi_i64(dirty_addr, cpu_reg_sp(s, rn), imm);
    clean_addr = gen_mte_checkN(s, dirty_addr, false, rn != 31, len, MO_8);

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
        t1 = tcg_temp_new_i64();
        t16 = tcg_temp_new_i128();
        for (i = 0; i < len_align; i += 16) {
            tcg_gen_ld_i64(t0, base, vofs + i);
            tcg_gen_ld_i64(t1, base, vofs + i + 8);
            tcg_gen_concat_i64_i128(t16, t0, t1);
            tcg_gen_qemu_st_i128(t16, clean_addr, midx,
                                 MO_LE | MO_128 | MO_ATOM_NONE | align);
            tcg_gen_addi_i64(clean_addr, clean_addr, 16);
        }
        if (len_align) {
            align = MO_UNALN;
        }
    } else {
        TCGLabel *loop = gen_new_label();
        TCGv_ptr tp, i = tcg_temp_new_ptr();

        tcg_gen_movi_ptr(i, 0);
        gen_set_label(loop);

        t0 = tcg_temp_new_i64();
        t1 = tcg_temp_new_i64();
        tp = tcg_temp_new_ptr();
        tcg_gen_add_ptr(tp, base, i);
        tcg_gen_ld_i64(t0, tp, vofs);
        tcg_gen_ld_i64(t1, tp, vofs + 8);
        tcg_gen_addi_ptr(i, i, 16);

        t16 = tcg_temp_new_i128();
        tcg_gen_concat_i64_i128(t16, t0, t1);

        tcg_gen_qemu_st_i128(t16, clean_addr, midx,
                             MO_LE | MO_128 | MO_ATOM_NONE);
        tcg_gen_addi_i64(clean_addr, clean_addr, 16);

        tcg_gen_brcondi_ptr(TCG_COND_LTU, i, len_align, loop);
        align = MO_UNALN;
    }

    /* Predicate register stores can be any multiple of 2.  */
    if (len_remain >= 8) {
        t0 = tcg_temp_new_i64();
        tcg_gen_ld_i64(t0, base, vofs + len_align);
        tcg_gen_qemu_st_i64(t0, clean_addr, midx,
                            MO_LEUQ | MO_ATOM_NONE | align);
        align = MO_UNALN;
        len_remain -= 8;
        len_align += 8;
        if (len_remain) {
            tcg_gen_addi_i64(clean_addr, clean_addr, 8);
        }
    }
    if (len_remain) {
        t0 = tcg_temp_new_i64();
        tcg_gen_ld_i64(t0, base, vofs + len_align);

        switch (len_remain) {
        case 2:
        case 4:
        case 8:
            tcg_gen_qemu_st_i64(t0, clean_addr, midx,
                                MO_LE | ctz32(len_remain)
                                | MO_ATOM_NONE | align);
            break;

        case 6:
            tcg_gen_qemu_st_i64(t0, clean_addr, midx,
                                MO_LEUL | MO_ATOM_NONE | align);
            tcg_gen_addi_i64(clean_addr, clean_addr, 4);
            tcg_gen_shri_i64(t0, t0, 32);
            tcg_gen_qemu_st_i64(t0, clean_addr, midx, MO_LEUW | MO_ATOM_NONE);
            break;

        default:
            g_assert_not_reached();
        }
    }
}

static bool trans_LDR_zri(DisasContext *s, arg_rri *a)
{
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        int size = vec_full_reg_size(s);
        int off = vec_full_reg_offset(s, a->rd);
        gen_sve_ldr(s, tcg_env, off, size, a->rn, a->imm * size,
                    s->align_mem ? MO_ALIGN_16 : MO_UNALN);
    }
    return true;
}

static bool trans_LDR_pri(DisasContext *s, arg_rri *a)
{
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        int size = pred_full_reg_size(s);
        int off = pred_full_reg_offset(s, a->rd);
        gen_sve_ldr(s, tcg_env, off, size, a->rn, a->imm * size,
                    s->align_mem ? MO_ALIGN_2 : MO_UNALN);
    }
    return true;
}

static bool trans_STR_zri(DisasContext *s, arg_rri *a)
{
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        int size = vec_full_reg_size(s);
        int off = vec_full_reg_offset(s, a->rd);
        gen_sve_str(s, tcg_env, off, size, a->rn, a->imm * size,
                    s->align_mem ? MO_ALIGN_16 : MO_UNALN);
    }
    return true;
}

static bool trans_STR_pri(DisasContext *s, arg_rri *a)
{
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        int size = pred_full_reg_size(s);
        int off = pred_full_reg_offset(s, a->rd);
        gen_sve_str(s, tcg_env, off, size, a->rn, a->imm * size,
                    s->align_mem ? MO_ALIGN_2 : MO_UNALN);
    }
    return true;
}

/*
 *** SVE Memory - Contiguous Load Group
 */

/* The memory mode of the dtype.  */
static const MemOp dtype_mop[19] = {
    MO_UB, MO_UB, MO_UB, MO_UB,
    MO_SL, MO_UW, MO_UW, MO_UW,
    MO_SW, MO_SW, MO_UL, MO_UL,
    MO_SB, MO_SB, MO_SB, MO_UQ,
    /* Artificial values used by decode */
    MO_UL, MO_UQ, MO_128,
};

#define dtype_msz(x)  (dtype_mop[x] & MO_SIZE)

/* The vector element size of dtype.  */
static const uint8_t dtype_esz[19] = {
    0, 1, 2, 3,
    3, 1, 2, 3,
    3, 2, 2, 3,
    3, 2, 1, 3,
    /* Artificial values used by decode */
    4, 4, 4,
};

uint64_t make_svemte_desc(DisasContext *s, unsigned vsz, uint32_t nregs,
                          uint32_t msz, bool is_write, uint32_t data)
{
    uint32_t sizem1;
    uint64_t desc = 0;

    /* Assert all of the data fits, with or without MTE enabled. */
    assert(nregs >= 1 && nregs <= 4);
    sizem1 = (nregs << msz) - 1;
    assert(sizem1 <= R_MTEDESC_SIZEM1_MASK >> R_MTEDESC_SIZEM1_SHIFT);

    if (s->mte_active[0]) {
        desc = FIELD_DP32(desc, MTEDESC, MIDX, get_mem_index(s));
        desc = FIELD_DP32(desc, MTEDESC, TBI, s->tbid);
        desc = FIELD_DP32(desc, MTEDESC, TCMA, s->tcma);
        desc = FIELD_DP32(desc, MTEDESC, WRITE, is_write);
        desc = FIELD_DP32(desc, MTEDESC, SIZEM1, sizem1);
        desc <<= 32;
    }
    return simd_desc(vsz, vsz, data) | desc;
}

static void do_mem_zpa(DisasContext *s, int zt, int pg, TCGv_i64 addr,
                       int dtype, uint32_t nregs, bool is_write,
                       gen_helper_gvec_mem *fn)
{
    TCGv_ptr t_pg;
    uint64_t desc;

    if (!s->mte_active[0]) {
        addr = clean_data_tbi(s, addr);
    }

    /*
     * For e.g. LD4, there are not enough arguments to pass all 4
     * registers as pointers, so encode the regno into the data field.
     * For consistency, do this even for LD1.
     */
    desc = make_svemte_desc(s, vec_full_reg_size(s), nregs,
                            dtype_msz(dtype), is_write, zt);
    t_pg = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(t_pg, tcg_env, pred_full_reg_offset(s, pg));
    fn(tcg_env, t_pg, addr, tcg_constant_i64(desc));
}

/* Indexed by [mte][be][dtype][nreg] */
static gen_helper_gvec_mem * const ldr_fns[2][2][19][4] = {
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
          gen_helper_sve_ld3dd_le_r, gen_helper_sve_ld4dd_le_r },

        { gen_helper_sve_ld1squ_le_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1dqu_le_r, NULL, NULL, NULL },
        { NULL,                      gen_helper_sve_ld2qq_le_r,
          gen_helper_sve_ld3qq_le_r, gen_helper_sve_ld4qq_le_r },
      },

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
          gen_helper_sve_ld3dd_be_r, gen_helper_sve_ld4dd_be_r },

        { gen_helper_sve_ld1squ_be_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1dqu_be_r, NULL, NULL, NULL },
        { NULL,                      gen_helper_sve_ld2qq_be_r,
          gen_helper_sve_ld3qq_be_r, gen_helper_sve_ld4qq_be_r },
      },
    },

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
          gen_helper_sve_ld4dd_le_r_mte },

        { gen_helper_sve_ld1squ_le_r_mte, NULL, NULL, NULL },
        { gen_helper_sve_ld1dqu_le_r_mte, NULL, NULL, NULL },
        { NULL,
          gen_helper_sve_ld2qq_le_r_mte,
          gen_helper_sve_ld3qq_le_r_mte,
          gen_helper_sve_ld4qq_le_r_mte },
      },

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
          gen_helper_sve_ld4dd_be_r_mte },

        { gen_helper_sve_ld1squ_be_r_mte, NULL, NULL, NULL },
        { gen_helper_sve_ld1dqu_be_r_mte, NULL, NULL, NULL },
        { NULL,
          gen_helper_sve_ld2qq_be_r_mte,
          gen_helper_sve_ld3qq_be_r_mte,
          gen_helper_sve_ld4qq_be_r_mte },
      },
    },
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
    do_mem_zpa(s, zt, pg, addr, dtype, nreg + 1, false, fn);
}

static bool trans_LD_zprr(DisasContext *s, arg_rprr_load *a)
{
    if (a->rm == 31) {
        return false;
    }

    /* dtypes 16-18 are artificial, representing 128-bit element */
    switch (a->dtype) {
    case 0 ... 15:
        if (!dc_isar_feature(aa64_sve, s)) {
            return false;
        }
        break;
    case 16: case 17:
        if (!dc_isar_feature(aa64_sve2p1, s)) {
            return false;
        }
        s->is_nonstreaming = true;
        break;
    case 18:
        if (!dc_isar_feature(aa64_sme2p1_or_sve2p1, s)) {
            return false;
        }
        break;
    default:
        g_assert_not_reached();
    }

    if (sve_access_check(s)) {
        TCGv_i64 addr = tcg_temp_new_i64();
        tcg_gen_shli_i64(addr, cpu_reg(s, a->rm), dtype_msz(a->dtype));
        tcg_gen_add_i64(addr, addr, cpu_reg_sp(s, a->rn));
        do_ld_zpa(s, a->rd, a->pg, addr, a->dtype, a->nreg);
    }
    return true;
}

static bool trans_LD_zpri(DisasContext *s, arg_rpri_load *a)
{
    /* dtypes 16-18 are artificial, representing 128-bit element */
    switch (a->dtype) {
    case 0 ... 15:
        if (!dc_isar_feature(aa64_sve, s)) {
            return false;
        }
        break;
    case 16: case 17:
        if (!dc_isar_feature(aa64_sve2p1, s)) {
            return false;
        }
        s->is_nonstreaming = true;
        break;
    case 18:
        if (!dc_isar_feature(aa64_sme2p1_or_sve2p1, s)) {
            return false;
        }
        break;
    default:
        g_assert_not_reached();
    }

    if (sve_access_check(s)) {
        int vsz = vec_full_reg_size(s);
        int elements = vsz >> dtype_esz[a->dtype];
        TCGv_i64 addr = tcg_temp_new_i64();

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

    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    s->is_nonstreaming = true;
    if (sve_access_check(s)) {
        TCGv_i64 addr = tcg_temp_new_i64();
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

    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    s->is_nonstreaming = true;
    if (sve_access_check(s)) {
        int vsz = vec_full_reg_size(s);
        int elements = vsz >> dtype_esz[a->dtype];
        int off = (a->imm * elements) << dtype_msz(a->dtype);
        TCGv_i64 addr = tcg_temp_new_i64();

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
    uint64_t desc;

    /* Load the first quadword using the normal predicated load helpers.  */
    if (!s->mte_active[0]) {
        addr = clean_data_tbi(s, addr);
    }

    poff = pred_full_reg_offset(s, pg);
    if (vsz > 16) {
        /*
         * Zero-extend the first 16 bits of the predicate into a temporary.
         * This avoids triggering an assert making sure we don't have bits
         * set within a predicate beyond VQ, but we have lowered VQ to 1
         * for this load operation.
         */
        TCGv_i64 tmp = tcg_temp_new_i64();
#if HOST_BIG_ENDIAN
        poff += 6;
#endif
        tcg_gen_ld16u_i64(tmp, tcg_env, poff);

        poff = offsetof(CPUARMState, vfp.preg_tmp);
        tcg_gen_st_i64(tmp, tcg_env, poff);
    }

    t_pg = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(t_pg, tcg_env, poff);

    gen_helper_gvec_mem *fn
        = ldr_fns[s->mte_active[0]][s->be_data == MO_BE][dtype][0];
    desc = make_svemte_desc(s, 16, 1, dtype_msz(dtype), false, zt);
    fn(tcg_env, t_pg, addr, tcg_constant_i64(desc));

    /* Replicate that first quadword.  */
    if (vsz > 16) {
        int doff = vec_full_reg_offset(s, zt);
        tcg_gen_gvec_dup_mem(4, doff + 16, doff, vsz - 16, vsz - 16);
    }
}

static bool trans_LD1RQ_zprr(DisasContext *s, arg_rprr_load *a)
{
    if (a->rm == 31 || !dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        int msz = dtype_msz(a->dtype);
        TCGv_i64 addr = tcg_temp_new_i64();
        tcg_gen_shli_i64(addr, cpu_reg(s, a->rm), msz);
        tcg_gen_add_i64(addr, addr, cpu_reg_sp(s, a->rn));
        do_ldrq(s, a->rd, a->pg, addr, a->dtype);
    }
    return true;
}

static bool trans_LD1RQ_zpri(DisasContext *s, arg_rpri_load *a)
{
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    if (sve_access_check(s)) {
        TCGv_i64 addr = tcg_temp_new_i64();
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
    uint64_t desc;

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
    if (!s->mte_active[0]) {
        addr = clean_data_tbi(s, addr);
    }

    poff = pred_full_reg_offset(s, pg);
    if (vsz > 32) {
        /*
         * Zero-extend the first 32 bits of the predicate into a temporary.
         * This avoids triggering an assert making sure we don't have bits
         * set within a predicate beyond VQ, but we have lowered VQ to 2
         * for this load operation.
         */
        TCGv_i64 tmp = tcg_temp_new_i64();
#if HOST_BIG_ENDIAN
        poff += 4;
#endif
        tcg_gen_ld32u_i64(tmp, tcg_env, poff);

        poff = offsetof(CPUARMState, vfp.preg_tmp);
        tcg_gen_st_i64(tmp, tcg_env, poff);
    }

    t_pg = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(t_pg, tcg_env, poff);

    gen_helper_gvec_mem *fn
        = ldr_fns[s->mte_active[0]][s->be_data == MO_BE][dtype][0];
    desc = make_svemte_desc(s, 32, 1, dtype_msz(dtype), false, zt);
    fn(tcg_env, t_pg, addr, tcg_constant_i64(desc));

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
    s->is_nonstreaming = true;
    if (sve_access_check(s)) {
        TCGv_i64 addr = tcg_temp_new_i64();
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
    s->is_nonstreaming = true;
    if (sve_access_check(s)) {
        TCGv_i64 addr = tcg_temp_new_i64();
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
    MemOp memop;

    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
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
        tcg_gen_ld_i64(temp, tcg_env, pred_full_reg_offset(s, a->pg));
        tcg_gen_andi_i64(temp, temp, pred_esz_masks[esz] & psz_mask);
        tcg_gen_brcondi_i64(TCG_COND_EQ, temp, 0, over);
    } else {
        TCGv_i32 t32 = tcg_temp_new_i32();
        find_last_active(s, t32, esz, a->pg);
        tcg_gen_brcondi_i32(TCG_COND_LT, t32, 0, over);
    }

    /* Load the data.  */
    temp = tcg_temp_new_i64();
    tcg_gen_addi_i64(temp, cpu_reg_sp(s, a->rn), a->imm << msz);

    memop = finalize_memop(s, dtype_mop[a->dtype]);
    clean_addr = gen_mte_check1(s, temp, false, true, memop);
    tcg_gen_qemu_ld_i64(temp, clean_addr, get_mem_index(s), memop);

    /* Broadcast to *all* elements.  */
    tcg_gen_gvec_dup_i64(esz, vec_full_reg_offset(s, a->rd),
                         vsz, vsz, temp);

    /* Zero the inactive elements.  */
    gen_set_label(over);
    return do_movz_zpz(s, a->rd, a->rd, a->pg, esz, false);
}

static void do_st_zpa(DisasContext *s, int zt, int pg, TCGv_i64 addr,
                      int msz, int esz, int nreg)
{
    static gen_helper_gvec_mem * const fn_single[2][2][4][5] = {
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
              gen_helper_sve_st1sd_le_r,
              gen_helper_sve_st1sq_le_r, },
            { NULL, NULL, NULL,
              gen_helper_sve_st1dd_le_r,
              gen_helper_sve_st1dq_le_r, } },
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
              gen_helper_sve_st1sd_be_r,
              gen_helper_sve_st1sq_be_r },
            { NULL, NULL, NULL,
              gen_helper_sve_st1dd_be_r,
              gen_helper_sve_st1dq_be_r } } },

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
              gen_helper_sve_st1sd_le_r_mte,
              gen_helper_sve_st1sq_le_r_mte },
            { NULL, NULL, NULL,
              gen_helper_sve_st1dd_le_r_mte,
              gen_helper_sve_st1dq_le_r_mte } },
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
              gen_helper_sve_st1sd_be_r_mte,
              gen_helper_sve_st1sq_be_r_mte },
            { NULL, NULL, NULL,
              gen_helper_sve_st1dd_be_r_mte,
              gen_helper_sve_st1dq_be_r_mte } } },
    };
    static gen_helper_gvec_mem * const fn_multiple[2][2][3][5] = {
        { { { gen_helper_sve_st2bb_r,
              gen_helper_sve_st2hh_le_r,
              gen_helper_sve_st2ss_le_r,
              gen_helper_sve_st2dd_le_r,
              gen_helper_sve_st2qq_le_r },
            { gen_helper_sve_st3bb_r,
              gen_helper_sve_st3hh_le_r,
              gen_helper_sve_st3ss_le_r,
              gen_helper_sve_st3dd_le_r,
              gen_helper_sve_st3qq_le_r },
            { gen_helper_sve_st4bb_r,
              gen_helper_sve_st4hh_le_r,
              gen_helper_sve_st4ss_le_r,
              gen_helper_sve_st4dd_le_r,
              gen_helper_sve_st4qq_le_r } },
          { { gen_helper_sve_st2bb_r,
              gen_helper_sve_st2hh_be_r,
              gen_helper_sve_st2ss_be_r,
              gen_helper_sve_st2dd_be_r,
              gen_helper_sve_st2qq_be_r },
            { gen_helper_sve_st3bb_r,
              gen_helper_sve_st3hh_be_r,
              gen_helper_sve_st3ss_be_r,
              gen_helper_sve_st3dd_be_r,
              gen_helper_sve_st3qq_be_r },
            { gen_helper_sve_st4bb_r,
              gen_helper_sve_st4hh_be_r,
              gen_helper_sve_st4ss_be_r,
              gen_helper_sve_st4dd_be_r,
              gen_helper_sve_st4qq_be_r } } },
        { { { gen_helper_sve_st2bb_r_mte,
              gen_helper_sve_st2hh_le_r_mte,
              gen_helper_sve_st2ss_le_r_mte,
              gen_helper_sve_st2dd_le_r_mte,
              gen_helper_sve_st2qq_le_r_mte },
            { gen_helper_sve_st3bb_r_mte,
              gen_helper_sve_st3hh_le_r_mte,
              gen_helper_sve_st3ss_le_r_mte,
              gen_helper_sve_st3dd_le_r_mte,
              gen_helper_sve_st3qq_le_r_mte },
            { gen_helper_sve_st4bb_r_mte,
              gen_helper_sve_st4hh_le_r_mte,
              gen_helper_sve_st4ss_le_r_mte,
              gen_helper_sve_st4dd_le_r_mte,
              gen_helper_sve_st4qq_le_r_mte } },
          { { gen_helper_sve_st2bb_r_mte,
              gen_helper_sve_st2hh_be_r_mte,
              gen_helper_sve_st2ss_be_r_mte,
              gen_helper_sve_st2dd_be_r_mte,
              gen_helper_sve_st2qq_be_r_mte },
            { gen_helper_sve_st3bb_r_mte,
              gen_helper_sve_st3hh_be_r_mte,
              gen_helper_sve_st3ss_be_r_mte,
              gen_helper_sve_st3dd_be_r_mte,
              gen_helper_sve_st3qq_be_r_mte },
            { gen_helper_sve_st4bb_r_mte,
              gen_helper_sve_st4hh_be_r_mte,
              gen_helper_sve_st4ss_be_r_mte,
              gen_helper_sve_st4dd_be_r_mte,
              gen_helper_sve_st4qq_be_r_mte } } },
    };
    gen_helper_gvec_mem *fn;
    int be = s->be_data == MO_BE;

    if (nreg == 0) {
        /* ST1 */
        fn = fn_single[s->mte_active[0]][be][msz][esz];
    } else {
        /* ST2, ST3, ST4 -- msz == esz, enforced by encoding */
        assert(msz == esz);
        fn = fn_multiple[s->mte_active[0]][be][nreg - 1][msz];
    }
    assert(fn != NULL);
    do_mem_zpa(s, zt, pg, addr, msz_dtype(s, msz), nreg + 1, true, fn);
}

static bool trans_ST_zprr(DisasContext *s, arg_rprr_store *a)
{
    if (a->rm == 31 || a->msz > a->esz) {
        return false;
    }
    switch (a->esz) {
    case MO_8 ... MO_64:
        if (!dc_isar_feature(aa64_sve, s)) {
            return false;
        }
        break;
    case MO_128:
        if (a->nreg == 0) {
            assert(a->msz < a->esz);
            if (!dc_isar_feature(aa64_sve2p1, s)) {
                return false;
            }
            s->is_nonstreaming = true;
        } else {
            if (!dc_isar_feature(aa64_sme2p1_or_sve2p1, s)) {
                return false;
            }
        }
        break;
    default:
        g_assert_not_reached();
    }

    if (sve_access_check(s)) {
        TCGv_i64 addr = tcg_temp_new_i64();
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
    switch (a->esz) {
    case MO_8 ... MO_64:
        if (!dc_isar_feature(aa64_sve, s)) {
            return false;
        }
        break;
    case MO_128:
        if (a->nreg == 0) {
            assert(a->msz < a->esz);
            if (!dc_isar_feature(aa64_sve2p1, s)) {
                return false;
            }
            s->is_nonstreaming = true;
        } else {
            if (!dc_isar_feature(aa64_sme2p1_or_sve2p1, s)) {
                return false;
            }
        }
        break;
    default:
        g_assert_not_reached();
    }

    if (sve_access_check(s)) {
        int vsz = vec_full_reg_size(s);
        int elements = vsz >> a->esz;
        TCGv_i64 addr = tcg_temp_new_i64();

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
    TCGv_ptr t_zm = tcg_temp_new_ptr();
    TCGv_ptr t_pg = tcg_temp_new_ptr();
    TCGv_ptr t_zt = tcg_temp_new_ptr();
    uint64_t desc;

    tcg_gen_addi_ptr(t_pg, tcg_env, pred_full_reg_offset(s, pg));
    tcg_gen_addi_ptr(t_zm, tcg_env, vec_full_reg_offset(s, zm));
    tcg_gen_addi_ptr(t_zt, tcg_env, vec_full_reg_offset(s, zt));

    desc = make_svemte_desc(s, vec_full_reg_size(s), 1, msz, is_write, scale);
    fn(tcg_env, t_zt, t_pg, t_zm, scalar, tcg_constant_i64(desc));
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

static gen_helper_gvec_mem_scatter * const
gather_load_fn128[2][2] = {
    { gen_helper_sve_ldqq_le_zd,
      gen_helper_sve_ldqq_be_zd },
    { gen_helper_sve_ldqq_le_zd_mte,
      gen_helper_sve_ldqq_be_zd_mte }
};

static bool trans_LD1_zprz(DisasContext *s, arg_LD1_zprz *a)
{
    gen_helper_gvec_mem_scatter *fn = NULL;
    bool be = s->be_data == MO_BE;
    bool mte = s->mte_active[0];

    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    s->is_nonstreaming = true;
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
    default:
        g_assert_not_reached();
    }
    assert(fn != NULL);

    do_mem_zpz(s, a->rd, a->pg, a->rm, a->scale * a->msz,
               cpu_reg_sp(s, a->rn), a->msz, false, fn);
    return true;
}

static bool trans_LD1Q(DisasContext *s, arg_LD1Q *a)
{
    gen_helper_gvec_mem_scatter *fn = NULL;
    bool be = s->be_data == MO_BE;
    bool mte = s->mte_active[0];

    if (!dc_isar_feature(aa64_sve2p1, s)) {
        return false;
    }
    s->is_nonstreaming = true;
    if (!sve_access_check(s)) {
        return true;
    }

    fn = gather_load_fn128[mte][be];
    assert(fn != NULL);

    /*
     * Unlike LD1_zprz, a->rm is the scalar register and it can be XZR, not XSP.
     * a->rn is the vector register.
     */
    do_mem_zpz(s, a->rd, a->pg, a->rn, 0,
               cpu_reg(s, a->rm), MO_128, false, fn);
    return true;
}

static bool trans_LD1_zpiz(DisasContext *s, arg_LD1_zpiz *a)
{
    gen_helper_gvec_mem_scatter *fn = NULL;
    bool be = s->be_data == MO_BE;
    bool mte = s->mte_active[0];

    if (a->esz < a->msz || (a->esz == a->msz && !a->u)) {
        return false;
    }
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    s->is_nonstreaming = true;
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
    do_mem_zpz(s, a->rd, a->pg, a->rn, 0,
               tcg_constant_i64(a->imm << a->msz), a->msz, false, fn);
    return true;
}

static bool trans_LDNT1_zprz(DisasContext *s, arg_LD1_zprz *a)
{
    gen_helper_gvec_mem_scatter *fn = NULL;
    bool be = s->be_data == MO_BE;
    bool mte = s->mte_active[0];

    if (a->esz < a->msz + !a->u) {
        return false;
    }
    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    s->is_nonstreaming = true;
    if (!sve_access_check(s)) {
        return true;
    }

    switch (a->esz) {
    case MO_32:
        fn = gather_load_fn32[mte][be][0][0][a->u][a->msz];
        break;
    case MO_64:
        fn = gather_load_fn64[mte][be][0][2][a->u][a->msz];
        break;
    }
    assert(fn != NULL);

    do_mem_zpz(s, a->rd, a->pg, a->rn, 0,
               cpu_reg(s, a->rm), a->msz, false, fn);
    return true;
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

static gen_helper_gvec_mem_scatter * const
scatter_store_fn128[2][2] = {
    { gen_helper_sve_stqq_le_zd,
      gen_helper_sve_stqq_be_zd },
    { gen_helper_sve_stqq_le_zd_mte,
      gen_helper_sve_stqq_be_zd_mte }
};

static bool trans_ST1_zprz(DisasContext *s, arg_ST1_zprz *a)
{
    gen_helper_gvec_mem_scatter *fn;
    bool be = s->be_data == MO_BE;
    bool mte = s->mte_active[0];

    if (a->esz < a->msz || (a->msz == 0 && a->scale)) {
        return false;
    }
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    s->is_nonstreaming = true;
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

static bool trans_ST1Q(DisasContext *s, arg_ST1Q *a)
{
    gen_helper_gvec_mem_scatter *fn;
    bool be = s->be_data == MO_BE;
    bool mte = s->mte_active[0];

    if (!dc_isar_feature(aa64_sve2p1, s)) {
        return false;
    }
    s->is_nonstreaming = true;
    if (!sve_access_check(s)) {
        return true;
    }
    fn = scatter_store_fn128[mte][be];
    /*
     * Unlike ST1_zprz, a->rm is the scalar register, and it
     * can be XZR, not XSP. a->rn is the vector register.
     */
    do_mem_zpz(s, a->rd, a->pg, a->rn, 0,
               cpu_reg(s, a->rm), MO_128, true, fn);
    return true;
}

static bool trans_ST1_zpiz(DisasContext *s, arg_ST1_zpiz *a)
{
    gen_helper_gvec_mem_scatter *fn = NULL;
    bool be = s->be_data == MO_BE;
    bool mte = s->mte_active[0];

    if (a->esz < a->msz) {
        return false;
    }
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    s->is_nonstreaming = true;
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
    do_mem_zpz(s, a->rd, a->pg, a->rn, 0,
               tcg_constant_i64(a->imm << a->msz), a->msz, true, fn);
    return true;
}

static bool trans_STNT1_zprz(DisasContext *s, arg_ST1_zprz *a)
{
    gen_helper_gvec_mem_scatter *fn;
    bool be = s->be_data == MO_BE;
    bool mte = s->mte_active[0];

    if (a->esz < a->msz) {
        return false;
    }
    if (!dc_isar_feature(aa64_sve2, s)) {
        return false;
    }
    s->is_nonstreaming = true;
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
    default:
        g_assert_not_reached();
    }

    do_mem_zpz(s, a->rd, a->pg, a->rn, 0,
               cpu_reg(s, a->rm), a->msz, true, fn);
    return true;
}

/*
 * Prefetches
 */

static bool trans_PRF(DisasContext *s, arg_PRF *a)
{
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    /* Prefetch is a nop within QEMU.  */
    (void)sve_access_check(s);
    return true;
}

static bool trans_PRF_rr(DisasContext *s, arg_PRF_rr *a)
{
    if (a->rm == 31 || !dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    /* Prefetch is a nop within QEMU.  */
    (void)sve_access_check(s);
    return true;
}

static bool trans_PRF_ns(DisasContext *s, arg_PRF_ns *a)
{
    if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    /* Prefetch is a nop within QEMU.  */
    s->is_nonstreaming = true;
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

TRANS_FEAT(MOVPRFX, aa64_sve, do_mov_z, a->rd, a->rn)
TRANS_FEAT(MOVPRFX_m, aa64_sve, do_sel_z, a->rd, a->rn, a->rd, a->pg, a->esz)
TRANS_FEAT(MOVPRFX_z, aa64_sve, do_movz_zpz, a->rd, a->rn, a->pg, a->esz, false)

/*
 * SVE2 Integer Multiply - Unpredicated
 */

TRANS_FEAT(MUL_zzz, aa64_sve2, gen_gvec_fn_arg_zzz, tcg_gen_gvec_mul, a)
TRANS_FEAT(SQDMULH_zzz, aa64_sve2, gen_gvec_fn_arg_zzz, gen_gvec_sve2_sqdmulh, a)

static gen_helper_gvec_3 * const smulh_zzz_fns[4] = {
    gen_helper_gvec_smulh_b, gen_helper_gvec_smulh_h,
    gen_helper_gvec_smulh_s, gen_helper_gvec_smulh_d,
};
TRANS_FEAT(SMULH_zzz, aa64_sve2, gen_gvec_ool_arg_zzz,
           smulh_zzz_fns[a->esz], a, 0)

static gen_helper_gvec_3 * const umulh_zzz_fns[4] = {
    gen_helper_gvec_umulh_b, gen_helper_gvec_umulh_h,
    gen_helper_gvec_umulh_s, gen_helper_gvec_umulh_d,
};
TRANS_FEAT(UMULH_zzz, aa64_sve2, gen_gvec_ool_arg_zzz,
           umulh_zzz_fns[a->esz], a, 0)

TRANS_FEAT(PMUL_zzz, aa64_sve2, gen_gvec_ool_arg_zzz,
           gen_helper_gvec_pmul_b, a, 0)

static gen_helper_gvec_3 * const sqrdmulh_zzz_fns[4] = {
    gen_helper_sve2_sqrdmulh_b, gen_helper_sve2_sqrdmulh_h,
    gen_helper_sve2_sqrdmulh_s, gen_helper_sve2_sqrdmulh_d,
};
TRANS_FEAT(SQRDMULH_zzz, aa64_sve2, gen_gvec_ool_arg_zzz,
           sqrdmulh_zzz_fns[a->esz], a, 0)

/*
 * SVE2 Integer - Predicated
 */

static gen_helper_gvec_4 * const sadlp_fns[4] = {
    NULL,                          gen_helper_sve2_sadalp_zpzz_h,
    gen_helper_sve2_sadalp_zpzz_s, gen_helper_sve2_sadalp_zpzz_d,
};
TRANS_FEAT(SADALP_zpzz, aa64_sve2, gen_gvec_ool_arg_zpzz,
           sadlp_fns[a->esz], a, 0)

static gen_helper_gvec_4 * const uadlp_fns[4] = {
    NULL,                          gen_helper_sve2_uadalp_zpzz_h,
    gen_helper_sve2_uadalp_zpzz_s, gen_helper_sve2_uadalp_zpzz_d,
};
TRANS_FEAT(UADALP_zpzz, aa64_sve2, gen_gvec_ool_arg_zpzz,
           uadlp_fns[a->esz], a, 0)

/*
 * SVE2 integer unary operations (predicated)
 */

TRANS_FEAT(URECPE, aa64_sve2, gen_gvec_ool_arg_zpz,
           a->esz == 2 ? gen_helper_sve2_urecpe_s : NULL, a, 0)

TRANS_FEAT(URSQRTE, aa64_sve2, gen_gvec_ool_arg_zpz,
           a->esz == 2 ? gen_helper_sve2_ursqrte_s : NULL, a, 0)

static gen_helper_gvec_3 * const sqabs_fns[4] = {
    gen_helper_sve2_sqabs_b, gen_helper_sve2_sqabs_h,
    gen_helper_sve2_sqabs_s, gen_helper_sve2_sqabs_d,
};
TRANS_FEAT(SQABS, aa64_sve2, gen_gvec_ool_arg_zpz, sqabs_fns[a->esz], a, 0)

static gen_helper_gvec_3 * const sqneg_fns[4] = {
    gen_helper_sve2_sqneg_b, gen_helper_sve2_sqneg_h,
    gen_helper_sve2_sqneg_s, gen_helper_sve2_sqneg_d,
};
TRANS_FEAT(SQNEG, aa64_sve2, gen_gvec_ool_arg_zpz, sqneg_fns[a->esz], a, 0)

DO_ZPZZ(SQSHL, aa64_sve2, sve2_sqshl)
DO_ZPZZ(SQRSHL, aa64_sve2, sve2_sqrshl)
DO_ZPZZ(SRSHL, aa64_sve2, sve2_srshl)

DO_ZPZZ(UQSHL, aa64_sve2, sve2_uqshl)
DO_ZPZZ(UQRSHL, aa64_sve2, sve2_uqrshl)
DO_ZPZZ(URSHL, aa64_sve2, sve2_urshl)

DO_ZPZZ(SHADD, aa64_sve2, sve2_shadd)
DO_ZPZZ(SRHADD, aa64_sve2, sve2_srhadd)
DO_ZPZZ(SHSUB, aa64_sve2, sve2_shsub)

DO_ZPZZ(UHADD, aa64_sve2, sve2_uhadd)
DO_ZPZZ(URHADD, aa64_sve2, sve2_urhadd)
DO_ZPZZ(UHSUB, aa64_sve2, sve2_uhsub)

DO_ZPZZ(ADDP, aa64_sve2, sve2_addp)
DO_ZPZZ(SMAXP, aa64_sve2, sve2_smaxp)
DO_ZPZZ(UMAXP, aa64_sve2, sve2_umaxp)
DO_ZPZZ(SMINP, aa64_sve2, sve2_sminp)
DO_ZPZZ(UMINP, aa64_sve2, sve2_uminp)

DO_ZPZZ(SQADD_zpzz, aa64_sve2, sve2_sqadd)
DO_ZPZZ(UQADD_zpzz, aa64_sve2, sve2_uqadd)
DO_ZPZZ(SQSUB_zpzz, aa64_sve2, sve2_sqsub)
DO_ZPZZ(UQSUB_zpzz, aa64_sve2, sve2_uqsub)
DO_ZPZZ(SUQADD, aa64_sve2, sve2_suqadd)
DO_ZPZZ(USQADD, aa64_sve2, sve2_usqadd)

/*
 * SVE2 Widening Integer Arithmetic
 */

static gen_helper_gvec_3 * const saddl_fns[4] = {
    NULL,                    gen_helper_sve2_saddl_h,
    gen_helper_sve2_saddl_s, gen_helper_sve2_saddl_d,
};
TRANS_FEAT(SADDLB, aa64_sve2, gen_gvec_ool_arg_zzz,
           saddl_fns[a->esz], a, 0)
TRANS_FEAT(SADDLT, aa64_sve2, gen_gvec_ool_arg_zzz,
           saddl_fns[a->esz], a, 3)
TRANS_FEAT(SADDLBT, aa64_sve2, gen_gvec_ool_arg_zzz,
           saddl_fns[a->esz], a, 2)

static gen_helper_gvec_3 * const ssubl_fns[4] = {
    NULL,                    gen_helper_sve2_ssubl_h,
    gen_helper_sve2_ssubl_s, gen_helper_sve2_ssubl_d,
};
TRANS_FEAT(SSUBLB, aa64_sve2, gen_gvec_ool_arg_zzz,
           ssubl_fns[a->esz], a, 0)
TRANS_FEAT(SSUBLT, aa64_sve2, gen_gvec_ool_arg_zzz,
           ssubl_fns[a->esz], a, 3)
TRANS_FEAT(SSUBLBT, aa64_sve2, gen_gvec_ool_arg_zzz,
           ssubl_fns[a->esz], a, 2)
TRANS_FEAT(SSUBLTB, aa64_sve2, gen_gvec_ool_arg_zzz,
           ssubl_fns[a->esz], a, 1)

static gen_helper_gvec_3 * const sabdl_fns[4] = {
    NULL,                    gen_helper_sve2_sabdl_h,
    gen_helper_sve2_sabdl_s, gen_helper_sve2_sabdl_d,
};
TRANS_FEAT(SABDLB, aa64_sve2, gen_gvec_ool_arg_zzz,
           sabdl_fns[a->esz], a, 0)
TRANS_FEAT(SABDLT, aa64_sve2, gen_gvec_ool_arg_zzz,
           sabdl_fns[a->esz], a, 3)

static gen_helper_gvec_3 * const uaddl_fns[4] = {
    NULL,                    gen_helper_sve2_uaddl_h,
    gen_helper_sve2_uaddl_s, gen_helper_sve2_uaddl_d,
};
TRANS_FEAT(UADDLB, aa64_sve2, gen_gvec_ool_arg_zzz,
           uaddl_fns[a->esz], a, 0)
TRANS_FEAT(UADDLT, aa64_sve2, gen_gvec_ool_arg_zzz,
           uaddl_fns[a->esz], a, 3)

static gen_helper_gvec_3 * const usubl_fns[4] = {
    NULL,                    gen_helper_sve2_usubl_h,
    gen_helper_sve2_usubl_s, gen_helper_sve2_usubl_d,
};
TRANS_FEAT(USUBLB, aa64_sve2, gen_gvec_ool_arg_zzz,
           usubl_fns[a->esz], a, 0)
TRANS_FEAT(USUBLT, aa64_sve2, gen_gvec_ool_arg_zzz,
           usubl_fns[a->esz], a, 3)

static gen_helper_gvec_3 * const uabdl_fns[4] = {
    NULL,                    gen_helper_sve2_uabdl_h,
    gen_helper_sve2_uabdl_s, gen_helper_sve2_uabdl_d,
};
TRANS_FEAT(UABDLB, aa64_sve2, gen_gvec_ool_arg_zzz,
           uabdl_fns[a->esz], a, 0)
TRANS_FEAT(UABDLT, aa64_sve2, gen_gvec_ool_arg_zzz,
           uabdl_fns[a->esz], a, 3)

static gen_helper_gvec_3 * const sqdmull_fns[4] = {
    NULL,                          gen_helper_sve2_sqdmull_zzz_h,
    gen_helper_sve2_sqdmull_zzz_s, gen_helper_sve2_sqdmull_zzz_d,
};
TRANS_FEAT(SQDMULLB_zzz, aa64_sve2, gen_gvec_ool_arg_zzz,
           sqdmull_fns[a->esz], a, 0)
TRANS_FEAT(SQDMULLT_zzz, aa64_sve2, gen_gvec_ool_arg_zzz,
           sqdmull_fns[a->esz], a, 3)

static gen_helper_gvec_3 * const smull_fns[4] = {
    NULL,                        gen_helper_sve2_smull_zzz_h,
    gen_helper_sve2_smull_zzz_s, gen_helper_sve2_smull_zzz_d,
};
TRANS_FEAT(SMULLB_zzz, aa64_sve2, gen_gvec_ool_arg_zzz,
           smull_fns[a->esz], a, 0)
TRANS_FEAT(SMULLT_zzz, aa64_sve2, gen_gvec_ool_arg_zzz,
           smull_fns[a->esz], a, 3)

static gen_helper_gvec_3 * const umull_fns[4] = {
    NULL,                        gen_helper_sve2_umull_zzz_h,
    gen_helper_sve2_umull_zzz_s, gen_helper_sve2_umull_zzz_d,
};
TRANS_FEAT(UMULLB_zzz, aa64_sve2, gen_gvec_ool_arg_zzz,
           umull_fns[a->esz], a, 0)
TRANS_FEAT(UMULLT_zzz, aa64_sve2, gen_gvec_ool_arg_zzz,
           umull_fns[a->esz], a, 3)

static gen_helper_gvec_3 * const eoril_fns[4] = {
    gen_helper_sve2_eoril_b, gen_helper_sve2_eoril_h,
    gen_helper_sve2_eoril_s, gen_helper_sve2_eoril_d,
};
TRANS_FEAT(EORBT, aa64_sve2, gen_gvec_ool_arg_zzz, eoril_fns[a->esz], a, 2)
TRANS_FEAT(EORTB, aa64_sve2, gen_gvec_ool_arg_zzz, eoril_fns[a->esz], a, 1)

static bool do_trans_pmull(DisasContext *s, arg_rrr_esz *a, bool sel)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_gvec_pmull_q, gen_helper_sve2_pmull_h,
        NULL,                    gen_helper_sve2_pmull_d,
    };

    if (a->esz == 0) {
        if (!dc_isar_feature(aa64_sve2_pmull128, s)) {
            return false;
        }
        s->is_nonstreaming = true;
    } else if (!dc_isar_feature(aa64_sve, s)) {
        return false;
    }
    return gen_gvec_ool_arg_zzz(s, fns[a->esz], a, sel);
}

TRANS_FEAT(PMULLB, aa64_sve2, do_trans_pmull, a, false)
TRANS_FEAT(PMULLT, aa64_sve2, do_trans_pmull, a, true)

static gen_helper_gvec_3 * const saddw_fns[4] = {
    NULL,                    gen_helper_sve2_saddw_h,
    gen_helper_sve2_saddw_s, gen_helper_sve2_saddw_d,
};
TRANS_FEAT(SADDWB, aa64_sve2, gen_gvec_ool_arg_zzz, saddw_fns[a->esz], a, 0)
TRANS_FEAT(SADDWT, aa64_sve2, gen_gvec_ool_arg_zzz, saddw_fns[a->esz], a, 1)

static gen_helper_gvec_3 * const ssubw_fns[4] = {
    NULL,                    gen_helper_sve2_ssubw_h,
    gen_helper_sve2_ssubw_s, gen_helper_sve2_ssubw_d,
};
TRANS_FEAT(SSUBWB, aa64_sve2, gen_gvec_ool_arg_zzz, ssubw_fns[a->esz], a, 0)
TRANS_FEAT(SSUBWT, aa64_sve2, gen_gvec_ool_arg_zzz, ssubw_fns[a->esz], a, 1)

static gen_helper_gvec_3 * const uaddw_fns[4] = {
    NULL,                    gen_helper_sve2_uaddw_h,
    gen_helper_sve2_uaddw_s, gen_helper_sve2_uaddw_d,
};
TRANS_FEAT(UADDWB, aa64_sve2, gen_gvec_ool_arg_zzz, uaddw_fns[a->esz], a, 0)
TRANS_FEAT(UADDWT, aa64_sve2, gen_gvec_ool_arg_zzz, uaddw_fns[a->esz], a, 1)

static gen_helper_gvec_3 * const usubw_fns[4] = {
    NULL,                    gen_helper_sve2_usubw_h,
    gen_helper_sve2_usubw_s, gen_helper_sve2_usubw_d,
};
TRANS_FEAT(USUBWB, aa64_sve2, gen_gvec_ool_arg_zzz, usubw_fns[a->esz], a, 0)
TRANS_FEAT(USUBWT, aa64_sve2, gen_gvec_ool_arg_zzz, usubw_fns[a->esz], a, 1)

static void gen_sshll_vec(unsigned vece, TCGv_vec d, TCGv_vec n, int64_t imm)
{
    int top = imm & 1;
    int shl = imm >> 1;
    int halfbits = 4 << vece;

    if (top) {
        if (shl == halfbits) {
            tcg_gen_and_vec(vece, d, n,
                            tcg_constant_vec_matching(d, vece,
                                MAKE_64BIT_MASK(halfbits, halfbits)));
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
            tcg_gen_and_vec(vece, d, n,
                            tcg_constant_vec_matching(d, vece,
                                MAKE_64BIT_MASK(halfbits, halfbits)));
        } else {
            tcg_gen_shri_vec(vece, d, n, halfbits);
            tcg_gen_shli_vec(vece, d, d, shl);
        }
    } else {
        if (shl == 0) {
            tcg_gen_and_vec(vece, d, n,
                            tcg_constant_vec_matching(d, vece,
                                MAKE_64BIT_MASK(0, halfbits)));
        } else {
            tcg_gen_shli_vec(vece, d, n, halfbits);
            tcg_gen_shri_vec(vece, d, d, halfbits - shl);
        }
    }
}

static bool do_shll_tb(DisasContext *s, arg_rri_esz *a,
                       const GVecGen2i ops[3], bool sel)
{

    if (a->esz < 0 || a->esz > 2) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_2i(vec_full_reg_offset(s, a->rd),
                        vec_full_reg_offset(s, a->rn),
                        vsz, vsz, (a->imm << 1) | sel,
                        &ops[a->esz]);
    }
    return true;
}

static const TCGOpcode sshll_list[] = {
    INDEX_op_shli_vec, INDEX_op_sari_vec, 0
};
static const GVecGen2i sshll_ops[3] = {
    { .fniv = gen_sshll_vec,
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
      .vece = MO_64 }
};
TRANS_FEAT(SSHLLB, aa64_sve2, do_shll_tb, a, sshll_ops, false)
TRANS_FEAT(SSHLLT, aa64_sve2, do_shll_tb, a, sshll_ops, true)

static const TCGOpcode ushll_list[] = {
    INDEX_op_shli_vec, INDEX_op_shri_vec, 0
};
static const GVecGen2i ushll_ops[3] = {
    { .fni8 = gen_ushll16_i64,
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
      .vece = MO_64 },
};
TRANS_FEAT(USHLLB, aa64_sve2, do_shll_tb, a, ushll_ops, false)
TRANS_FEAT(USHLLT, aa64_sve2, do_shll_tb, a, ushll_ops, true)

static gen_helper_gvec_3 * const bext_fns[4] = {
    gen_helper_sve2_bext_b, gen_helper_sve2_bext_h,
    gen_helper_sve2_bext_s, gen_helper_sve2_bext_d,
};
TRANS_FEAT_NONSTREAMING(BEXT, aa64_sve2_bitperm, gen_gvec_ool_arg_zzz,
                        bext_fns[a->esz], a, 0)

static gen_helper_gvec_3 * const bdep_fns[4] = {
    gen_helper_sve2_bdep_b, gen_helper_sve2_bdep_h,
    gen_helper_sve2_bdep_s, gen_helper_sve2_bdep_d,
};
TRANS_FEAT_NONSTREAMING(BDEP, aa64_sve2_bitperm, gen_gvec_ool_arg_zzz,
                        bdep_fns[a->esz], a, 0)

static gen_helper_gvec_3 * const bgrp_fns[4] = {
    gen_helper_sve2_bgrp_b, gen_helper_sve2_bgrp_h,
    gen_helper_sve2_bgrp_s, gen_helper_sve2_bgrp_d,
};
TRANS_FEAT_NONSTREAMING(BGRP, aa64_sve2_bitperm, gen_gvec_ool_arg_zzz,
                        bgrp_fns[a->esz], a, 0)

static gen_helper_gvec_3 * const cadd_fns[4] = {
    gen_helper_sve2_cadd_b, gen_helper_sve2_cadd_h,
    gen_helper_sve2_cadd_s, gen_helper_sve2_cadd_d,
};
TRANS_FEAT(CADD_rot90, aa64_sve2, gen_gvec_ool_arg_zzz,
           cadd_fns[a->esz], a, 0)
TRANS_FEAT(CADD_rot270, aa64_sve2, gen_gvec_ool_arg_zzz,
           cadd_fns[a->esz], a, 1)

static gen_helper_gvec_3 * const sqcadd_fns[4] = {
    gen_helper_sve2_sqcadd_b, gen_helper_sve2_sqcadd_h,
    gen_helper_sve2_sqcadd_s, gen_helper_sve2_sqcadd_d,
};
TRANS_FEAT(SQCADD_rot90, aa64_sve2, gen_gvec_ool_arg_zzz,
           sqcadd_fns[a->esz], a, 0)
TRANS_FEAT(SQCADD_rot270, aa64_sve2, gen_gvec_ool_arg_zzz,
           sqcadd_fns[a->esz], a, 1)

static gen_helper_gvec_4 * const sabal_fns[4] = {
    NULL,                    gen_helper_sve2_sabal_h,
    gen_helper_sve2_sabal_s, gen_helper_sve2_sabal_d,
};
TRANS_FEAT(SABALB, aa64_sve2, gen_gvec_ool_arg_zzzz, sabal_fns[a->esz], a, 0)
TRANS_FEAT(SABALT, aa64_sve2, gen_gvec_ool_arg_zzzz, sabal_fns[a->esz], a, 1)

static gen_helper_gvec_4 * const uabal_fns[4] = {
    NULL,                    gen_helper_sve2_uabal_h,
    gen_helper_sve2_uabal_s, gen_helper_sve2_uabal_d,
};
TRANS_FEAT(UABALB, aa64_sve2, gen_gvec_ool_arg_zzzz, uabal_fns[a->esz], a, 0)
TRANS_FEAT(UABALT, aa64_sve2, gen_gvec_ool_arg_zzzz, uabal_fns[a->esz], a, 1)

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
    return gen_gvec_ool_arg_zzzz(s, fns[a->esz & 1], a, (a->esz & 2) | sel);
}

TRANS_FEAT(ADCLB, aa64_sve2, do_adcl, a, false)
TRANS_FEAT(ADCLT, aa64_sve2, do_adcl, a, true)

TRANS_FEAT(SSRA, aa64_sve2, gen_gvec_fn_arg_zzi, gen_gvec_ssra, a)
TRANS_FEAT(USRA, aa64_sve2, gen_gvec_fn_arg_zzi, gen_gvec_usra, a)
TRANS_FEAT(SRSRA, aa64_sve2, gen_gvec_fn_arg_zzi, gen_gvec_srsra, a)
TRANS_FEAT(URSRA, aa64_sve2, gen_gvec_fn_arg_zzi, gen_gvec_ursra, a)
TRANS_FEAT(SRI, aa64_sve2, gen_gvec_fn_arg_zzi, gen_gvec_sri, a)
TRANS_FEAT(SLI, aa64_sve2, gen_gvec_fn_arg_zzi, gen_gvec_sli, a)

TRANS_FEAT(SABA, aa64_sve2, gen_gvec_fn_arg_zzz, gen_gvec_saba, a)
TRANS_FEAT(UABA, aa64_sve2, gen_gvec_fn_arg_zzz, gen_gvec_uaba, a)

static bool do_narrow_extract(DisasContext *s, arg_rri_esz *a,
                              const GVecGen2 ops[3])
{
    if (a->esz < 0 || a->esz > MO_32 || a->imm != 0) {
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
    int halfbits = 4 << vece;
    int64_t mask = (1ull << halfbits) - 1;
    int64_t min = -1ull << (halfbits - 1);
    int64_t max = -min - 1;

    tcg_gen_smax_vec(vece, d, n, tcg_constant_vec_matching(d, vece, min));
    tcg_gen_smin_vec(vece, d, d, tcg_constant_vec_matching(d, vece, max));
    tcg_gen_and_vec(vece, d, d, tcg_constant_vec_matching(d, vece, mask));
}

static const GVecGen2 sqxtnb_ops[3] = {
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
TRANS_FEAT(SQXTNB, aa64_sve2, do_narrow_extract, a, sqxtnb_ops)

static void gen_sqxtnt_vec(unsigned vece, TCGv_vec d, TCGv_vec n)
{
    int halfbits = 4 << vece;
    int64_t mask = (1ull << halfbits) - 1;
    int64_t min = -1ull << (halfbits - 1);
    int64_t max = -min - 1;

    tcg_gen_smax_vec(vece, n, n, tcg_constant_vec_matching(d, vece, min));
    tcg_gen_smin_vec(vece, n, n, tcg_constant_vec_matching(d, vece, max));
    tcg_gen_shli_vec(vece, n, n, halfbits);
    tcg_gen_bitsel_vec(vece, d, tcg_constant_vec_matching(d, vece, mask), d, n);
}

static const GVecGen2 sqxtnt_ops[3] = {
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
TRANS_FEAT(SQXTNT, aa64_sve2, do_narrow_extract, a, sqxtnt_ops)

static const TCGOpcode uqxtn_list[] = {
    INDEX_op_shli_vec, INDEX_op_umin_vec, 0
};

static void gen_uqxtnb_vec(unsigned vece, TCGv_vec d, TCGv_vec n)
{
    int halfbits = 4 << vece;
    int64_t max = (1ull << halfbits) - 1;

    tcg_gen_umin_vec(vece, d, n, tcg_constant_vec_matching(d, vece, max));
}

static const GVecGen2 uqxtnb_ops[3] = {
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
TRANS_FEAT(UQXTNB, aa64_sve2, do_narrow_extract, a, uqxtnb_ops)

static void gen_uqxtnt_vec(unsigned vece, TCGv_vec d, TCGv_vec n)
{
    int halfbits = 4 << vece;
    int64_t max = (1ull << halfbits) - 1;
    TCGv_vec maxv = tcg_constant_vec_matching(d, vece, max);

    tcg_gen_umin_vec(vece, n, n, maxv);
    tcg_gen_shli_vec(vece, n, n, halfbits);
    tcg_gen_bitsel_vec(vece, d, maxv, d, n);
}

static const GVecGen2 uqxtnt_ops[3] = {
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
TRANS_FEAT(UQXTNT, aa64_sve2, do_narrow_extract, a, uqxtnt_ops)

static const TCGOpcode sqxtun_list[] = {
    INDEX_op_shli_vec, INDEX_op_umin_vec, INDEX_op_smax_vec, 0
};

static void gen_sqxtunb_vec(unsigned vece, TCGv_vec d, TCGv_vec n)
{
    int halfbits = 4 << vece;
    int64_t max = (1ull << halfbits) - 1;

    tcg_gen_smax_vec(vece, d, n, tcg_constant_vec_matching(d, vece, 0));
    tcg_gen_umin_vec(vece, d, d, tcg_constant_vec_matching(d, vece, max));
}

static const GVecGen2 sqxtunb_ops[3] = {
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
TRANS_FEAT(SQXTUNB, aa64_sve2, do_narrow_extract, a, sqxtunb_ops)

static void gen_sqxtunt_vec(unsigned vece, TCGv_vec d, TCGv_vec n)
{
    int halfbits = 4 << vece;
    int64_t max = (1ull << halfbits) - 1;
    TCGv_vec maxv = tcg_constant_vec_matching(d, vece, max);

    tcg_gen_smax_vec(vece, n, n, tcg_constant_vec_matching(d, vece, 0));
    tcg_gen_umin_vec(vece, n, n, maxv);
    tcg_gen_shli_vec(vece, n, n, halfbits);
    tcg_gen_bitsel_vec(vece, d, maxv, d, n);
}

static const GVecGen2 sqxtunt_ops[3] = {
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
TRANS_FEAT(SQXTUNT, aa64_sve2, do_narrow_extract, a, sqxtunt_ops)

static bool do_shr_narrow(DisasContext *s, arg_rri_esz *a,
                          const GVecGen2i ops[3])
{
    if (a->esz < 0 || a->esz > MO_32) {
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
    int halfbits = 4 << vece;
    uint64_t mask = MAKE_64BIT_MASK(0, halfbits);

    tcg_gen_shri_vec(vece, n, n, shr);
    tcg_gen_and_vec(vece, d, n, tcg_constant_vec_matching(d, vece, mask));
}

static const TCGOpcode shrnb_vec_list[] = { INDEX_op_shri_vec, 0 };
static const GVecGen2i shrnb_ops[3] = {
    { .fni8 = gen_shrnb16_i64,
      .fniv = gen_shrnb_vec,
      .opt_opc = shrnb_vec_list,
      .fno = gen_helper_sve2_shrnb_h,
      .vece = MO_16 },
    { .fni8 = gen_shrnb32_i64,
      .fniv = gen_shrnb_vec,
      .opt_opc = shrnb_vec_list,
      .fno = gen_helper_sve2_shrnb_s,
      .vece = MO_32 },
    { .fni8 = gen_shrnb64_i64,
      .fniv = gen_shrnb_vec,
      .opt_opc = shrnb_vec_list,
      .fno = gen_helper_sve2_shrnb_d,
      .vece = MO_64 },
};
TRANS_FEAT(SHRNB, aa64_sve2, do_shr_narrow, a, shrnb_ops)

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
    int halfbits = 4 << vece;
    uint64_t mask = MAKE_64BIT_MASK(0, halfbits);

    tcg_gen_shli_vec(vece, n, n, halfbits - shr);
    tcg_gen_bitsel_vec(vece, d, tcg_constant_vec_matching(d, vece, mask), d, n);
}

static const TCGOpcode shrnt_vec_list[] = { INDEX_op_shli_vec, 0 };
static const GVecGen2i shrnt_ops[3] = {
    { .fni8 = gen_shrnt16_i64,
      .fniv = gen_shrnt_vec,
      .opt_opc = shrnt_vec_list,
      .load_dest = true,
      .fno = gen_helper_sve2_shrnt_h,
      .vece = MO_16 },
    { .fni8 = gen_shrnt32_i64,
      .fniv = gen_shrnt_vec,
      .opt_opc = shrnt_vec_list,
      .load_dest = true,
      .fno = gen_helper_sve2_shrnt_s,
      .vece = MO_32 },
    { .fni8 = gen_shrnt64_i64,
      .fniv = gen_shrnt_vec,
      .opt_opc = shrnt_vec_list,
      .load_dest = true,
      .fno = gen_helper_sve2_shrnt_d,
      .vece = MO_64 },
};
TRANS_FEAT(SHRNT, aa64_sve2, do_shr_narrow, a, shrnt_ops)

static const GVecGen2i rshrnb_ops[3] = {
    { .fno = gen_helper_sve2_rshrnb_h },
    { .fno = gen_helper_sve2_rshrnb_s },
    { .fno = gen_helper_sve2_rshrnb_d },
};
TRANS_FEAT(RSHRNB, aa64_sve2, do_shr_narrow, a, rshrnb_ops)

static const GVecGen2i rshrnt_ops[3] = {
    { .fno = gen_helper_sve2_rshrnt_h },
    { .fno = gen_helper_sve2_rshrnt_s },
    { .fno = gen_helper_sve2_rshrnt_d },
};
TRANS_FEAT(RSHRNT, aa64_sve2, do_shr_narrow, a, rshrnt_ops)

static void gen_sqshrunb_vec(unsigned vece, TCGv_vec d,
                             TCGv_vec n, int64_t shr)
{
    int halfbits = 4 << vece;
    uint64_t max = MAKE_64BIT_MASK(0, halfbits);

    tcg_gen_sari_vec(vece, n, n, shr);
    tcg_gen_smax_vec(vece, n, n, tcg_constant_vec_matching(d, vece, 0));
    tcg_gen_umin_vec(vece, d, n, tcg_constant_vec_matching(d, vece, max));
}

static const TCGOpcode sqshrunb_vec_list[] = {
    INDEX_op_sari_vec, INDEX_op_smax_vec, INDEX_op_umin_vec, 0
};
static const GVecGen2i sqshrunb_ops[3] = {
    { .fniv = gen_sqshrunb_vec,
      .opt_opc = sqshrunb_vec_list,
      .fno = gen_helper_sve2_sqshrunb_h,
      .vece = MO_16 },
    { .fniv = gen_sqshrunb_vec,
      .opt_opc = sqshrunb_vec_list,
      .fno = gen_helper_sve2_sqshrunb_s,
      .vece = MO_32 },
    { .fniv = gen_sqshrunb_vec,
      .opt_opc = sqshrunb_vec_list,
      .fno = gen_helper_sve2_sqshrunb_d,
      .vece = MO_64 },
};
TRANS_FEAT(SQSHRUNB, aa64_sve2, do_shr_narrow, a, sqshrunb_ops)

static void gen_sqshrunt_vec(unsigned vece, TCGv_vec d,
                             TCGv_vec n, int64_t shr)
{
    int halfbits = 4 << vece;
    uint64_t max = MAKE_64BIT_MASK(0, halfbits);
    TCGv_vec maxv = tcg_constant_vec_matching(d, vece, max);

    tcg_gen_sari_vec(vece, n, n, shr);
    tcg_gen_smax_vec(vece, n, n, tcg_constant_vec_matching(d, vece, 0));
    tcg_gen_umin_vec(vece, n, n, maxv);
    tcg_gen_shli_vec(vece, n, n, halfbits);
    tcg_gen_bitsel_vec(vece, d, maxv, d, n);
}

static const TCGOpcode sqshrunt_vec_list[] = {
    INDEX_op_shli_vec, INDEX_op_sari_vec,
    INDEX_op_smax_vec, INDEX_op_umin_vec, 0
};
static const GVecGen2i sqshrunt_ops[3] = {
    { .fniv = gen_sqshrunt_vec,
      .opt_opc = sqshrunt_vec_list,
      .load_dest = true,
      .fno = gen_helper_sve2_sqshrunt_h,
      .vece = MO_16 },
    { .fniv = gen_sqshrunt_vec,
      .opt_opc = sqshrunt_vec_list,
      .load_dest = true,
      .fno = gen_helper_sve2_sqshrunt_s,
      .vece = MO_32 },
    { .fniv = gen_sqshrunt_vec,
      .opt_opc = sqshrunt_vec_list,
      .load_dest = true,
      .fno = gen_helper_sve2_sqshrunt_d,
      .vece = MO_64 },
};
TRANS_FEAT(SQSHRUNT, aa64_sve2, do_shr_narrow, a, sqshrunt_ops)

static const GVecGen2i sqrshrunb_ops[3] = {
    { .fno = gen_helper_sve2_sqrshrunb_h },
    { .fno = gen_helper_sve2_sqrshrunb_s },
    { .fno = gen_helper_sve2_sqrshrunb_d },
};
TRANS_FEAT(SQRSHRUNB, aa64_sve2, do_shr_narrow, a, sqrshrunb_ops)

static const GVecGen2i sqrshrunt_ops[3] = {
    { .fno = gen_helper_sve2_sqrshrunt_h },
    { .fno = gen_helper_sve2_sqrshrunt_s },
    { .fno = gen_helper_sve2_sqrshrunt_d },
};
TRANS_FEAT(SQRSHRUNT, aa64_sve2, do_shr_narrow, a, sqrshrunt_ops)

static void gen_sqshrnb_vec(unsigned vece, TCGv_vec d,
                            TCGv_vec n, int64_t shr)
{
    int halfbits = 4 << vece;
    int64_t max = MAKE_64BIT_MASK(0, halfbits - 1);
    int64_t min = -max - 1;
    int64_t mask = MAKE_64BIT_MASK(0, halfbits);

    tcg_gen_sari_vec(vece, n, n, shr);
    tcg_gen_smax_vec(vece, n, n, tcg_constant_vec_matching(d, vece, min));
    tcg_gen_smin_vec(vece, n, n, tcg_constant_vec_matching(d, vece, max));
    tcg_gen_and_vec(vece, d, n, tcg_constant_vec_matching(d, vece, mask));
}

static const TCGOpcode sqshrnb_vec_list[] = {
    INDEX_op_sari_vec, INDEX_op_smax_vec, INDEX_op_smin_vec, 0
};
static const GVecGen2i sqshrnb_ops[3] = {
    { .fniv = gen_sqshrnb_vec,
      .opt_opc = sqshrnb_vec_list,
      .fno = gen_helper_sve2_sqshrnb_h,
      .vece = MO_16 },
    { .fniv = gen_sqshrnb_vec,
      .opt_opc = sqshrnb_vec_list,
      .fno = gen_helper_sve2_sqshrnb_s,
      .vece = MO_32 },
    { .fniv = gen_sqshrnb_vec,
      .opt_opc = sqshrnb_vec_list,
      .fno = gen_helper_sve2_sqshrnb_d,
      .vece = MO_64 },
};
TRANS_FEAT(SQSHRNB, aa64_sve2, do_shr_narrow, a, sqshrnb_ops)

static void gen_sqshrnt_vec(unsigned vece, TCGv_vec d,
                             TCGv_vec n, int64_t shr)
{
    int halfbits = 4 << vece;
    int64_t max = MAKE_64BIT_MASK(0, halfbits - 1);
    int64_t min = -max - 1;
    int64_t mask = MAKE_64BIT_MASK(0, halfbits);

    tcg_gen_sari_vec(vece, n, n, shr);
    tcg_gen_smax_vec(vece, n, n, tcg_constant_vec_matching(d, vece, min));
    tcg_gen_smin_vec(vece, n, n, tcg_constant_vec_matching(d, vece, max));
    tcg_gen_shli_vec(vece, n, n, halfbits);
    tcg_gen_bitsel_vec(vece, d, tcg_constant_vec_matching(d, vece, mask), d, n);
}

static const TCGOpcode sqshrnt_vec_list[] = {
    INDEX_op_shli_vec, INDEX_op_sari_vec,
    INDEX_op_smax_vec, INDEX_op_smin_vec, 0
};
static const GVecGen2i sqshrnt_ops[3] = {
    { .fniv = gen_sqshrnt_vec,
      .opt_opc = sqshrnt_vec_list,
      .load_dest = true,
      .fno = gen_helper_sve2_sqshrnt_h,
      .vece = MO_16 },
    { .fniv = gen_sqshrnt_vec,
      .opt_opc = sqshrnt_vec_list,
      .load_dest = true,
      .fno = gen_helper_sve2_sqshrnt_s,
      .vece = MO_32 },
    { .fniv = gen_sqshrnt_vec,
      .opt_opc = sqshrnt_vec_list,
      .load_dest = true,
      .fno = gen_helper_sve2_sqshrnt_d,
      .vece = MO_64 },
};
TRANS_FEAT(SQSHRNT, aa64_sve2, do_shr_narrow, a, sqshrnt_ops)

static const GVecGen2i sqrshrnb_ops[3] = {
    { .fno = gen_helper_sve2_sqrshrnb_h },
    { .fno = gen_helper_sve2_sqrshrnb_s },
    { .fno = gen_helper_sve2_sqrshrnb_d },
};
TRANS_FEAT(SQRSHRNB, aa64_sve2, do_shr_narrow, a, sqrshrnb_ops)

static const GVecGen2i sqrshrnt_ops[3] = {
    { .fno = gen_helper_sve2_sqrshrnt_h },
    { .fno = gen_helper_sve2_sqrshrnt_s },
    { .fno = gen_helper_sve2_sqrshrnt_d },
};
TRANS_FEAT(SQRSHRNT, aa64_sve2, do_shr_narrow, a, sqrshrnt_ops)

static void gen_uqshrnb_vec(unsigned vece, TCGv_vec d,
                            TCGv_vec n, int64_t shr)
{
    int halfbits = 4 << vece;
    int64_t max = MAKE_64BIT_MASK(0, halfbits);

    tcg_gen_shri_vec(vece, n, n, shr);
    tcg_gen_umin_vec(vece, d, n, tcg_constant_vec_matching(d, vece, max));
}

static const TCGOpcode uqshrnb_vec_list[] = {
    INDEX_op_shri_vec, INDEX_op_umin_vec, 0
};
static const GVecGen2i uqshrnb_ops[3] = {
    { .fniv = gen_uqshrnb_vec,
      .opt_opc = uqshrnb_vec_list,
      .fno = gen_helper_sve2_uqshrnb_h,
      .vece = MO_16 },
    { .fniv = gen_uqshrnb_vec,
      .opt_opc = uqshrnb_vec_list,
      .fno = gen_helper_sve2_uqshrnb_s,
      .vece = MO_32 },
    { .fniv = gen_uqshrnb_vec,
      .opt_opc = uqshrnb_vec_list,
      .fno = gen_helper_sve2_uqshrnb_d,
      .vece = MO_64 },
};
TRANS_FEAT(UQSHRNB, aa64_sve2, do_shr_narrow, a, uqshrnb_ops)

static void gen_uqshrnt_vec(unsigned vece, TCGv_vec d,
                            TCGv_vec n, int64_t shr)
{
    int halfbits = 4 << vece;
    int64_t max = MAKE_64BIT_MASK(0, halfbits);
    TCGv_vec maxv = tcg_constant_vec_matching(d, vece, max);

    tcg_gen_shri_vec(vece, n, n, shr);
    tcg_gen_umin_vec(vece, n, n, maxv);
    tcg_gen_shli_vec(vece, n, n, halfbits);
    tcg_gen_bitsel_vec(vece, d, maxv, d, n);
}

static const TCGOpcode uqshrnt_vec_list[] = {
    INDEX_op_shli_vec, INDEX_op_shri_vec, INDEX_op_umin_vec, 0
};
static const GVecGen2i uqshrnt_ops[3] = {
    { .fniv = gen_uqshrnt_vec,
      .opt_opc = uqshrnt_vec_list,
      .load_dest = true,
      .fno = gen_helper_sve2_uqshrnt_h,
      .vece = MO_16 },
    { .fniv = gen_uqshrnt_vec,
      .opt_opc = uqshrnt_vec_list,
      .load_dest = true,
      .fno = gen_helper_sve2_uqshrnt_s,
      .vece = MO_32 },
    { .fniv = gen_uqshrnt_vec,
      .opt_opc = uqshrnt_vec_list,
      .load_dest = true,
      .fno = gen_helper_sve2_uqshrnt_d,
      .vece = MO_64 },
};
TRANS_FEAT(UQSHRNT, aa64_sve2, do_shr_narrow, a, uqshrnt_ops)

static const GVecGen2i uqrshrnb_ops[3] = {
    { .fno = gen_helper_sve2_uqrshrnb_h },
    { .fno = gen_helper_sve2_uqrshrnb_s },
    { .fno = gen_helper_sve2_uqrshrnb_d },
};
TRANS_FEAT(UQRSHRNB, aa64_sve2, do_shr_narrow, a, uqrshrnb_ops)

static const GVecGen2i uqrshrnt_ops[3] = {
    { .fno = gen_helper_sve2_uqrshrnt_h },
    { .fno = gen_helper_sve2_uqrshrnt_s },
    { .fno = gen_helper_sve2_uqrshrnt_d },
};
TRANS_FEAT(UQRSHRNT, aa64_sve2, do_shr_narrow, a, uqrshrnt_ops)

#define DO_SVE2_ZZZ_NARROW(NAME, name)                                    \
    static gen_helper_gvec_3 * const name##_fns[4] = {                    \
        NULL,                       gen_helper_sve2_##name##_h,           \
        gen_helper_sve2_##name##_s, gen_helper_sve2_##name##_d,           \
    };                                                                    \
    TRANS_FEAT(NAME, aa64_sve2, gen_gvec_ool_arg_zzz,                     \
               name##_fns[a->esz], a, 0)

DO_SVE2_ZZZ_NARROW(ADDHNB, addhnb)
DO_SVE2_ZZZ_NARROW(ADDHNT, addhnt)
DO_SVE2_ZZZ_NARROW(RADDHNB, raddhnb)
DO_SVE2_ZZZ_NARROW(RADDHNT, raddhnt)

DO_SVE2_ZZZ_NARROW(SUBHNB, subhnb)
DO_SVE2_ZZZ_NARROW(SUBHNT, subhnt)
DO_SVE2_ZZZ_NARROW(RSUBHNB, rsubhnb)
DO_SVE2_ZZZ_NARROW(RSUBHNT, rsubhnt)

static gen_helper_gvec_flags_4 * const match_fns[4] = {
    gen_helper_sve2_match_ppzz_b, gen_helper_sve2_match_ppzz_h, NULL, NULL
};
TRANS_FEAT_NONSTREAMING(MATCH, aa64_sve2, do_ppzz_flags, a, match_fns[a->esz])

static gen_helper_gvec_flags_4 * const nmatch_fns[4] = {
    gen_helper_sve2_nmatch_ppzz_b, gen_helper_sve2_nmatch_ppzz_h, NULL, NULL
};
TRANS_FEAT_NONSTREAMING(NMATCH, aa64_sve2, do_ppzz_flags, a, nmatch_fns[a->esz])

static gen_helper_gvec_4 * const histcnt_fns[4] = {
    NULL, NULL, gen_helper_sve2_histcnt_s, gen_helper_sve2_histcnt_d
};
TRANS_FEAT_NONSTREAMING(HISTCNT, aa64_sve2, gen_gvec_ool_arg_zpzz,
                        histcnt_fns[a->esz], a, 0)

TRANS_FEAT_NONSTREAMING(HISTSEG, aa64_sve2, gen_gvec_ool_arg_zzz,
                        a->esz == 0 ? gen_helper_sve2_histseg : NULL, a, 0)

DO_ZPZZ_FP(FADDP, aa64_sve2, sve2_faddp_zpzz)
DO_ZPZZ_FP(FMAXNMP, aa64_sve2, sve2_fmaxnmp_zpzz)
DO_ZPZZ_FP(FMINNMP, aa64_sve2, sve2_fminnmp_zpzz)
DO_ZPZZ_FP(FMAXP, aa64_sve2, sve2_fmaxp_zpzz)
DO_ZPZZ_FP(FMINP, aa64_sve2, sve2_fminp_zpzz)

static bool do_fmmla(DisasContext *s, arg_rrrr_esz *a,
                     gen_helper_gvec_4_ptr *fn)
{
    if (sve_access_check(s)) {
        if (vec_full_reg_size(s) < 4 * memop_size(a->esz)) {
            unallocated_encoding(s);
        } else {
            gen_gvec_fpst_zzzz(s, fn, a->rd, a->rn, a->rm, a->ra, 0, FPST_A64);
        }
    }
    return true;
}

TRANS_FEAT_NONSTREAMING(FMMLA_s, aa64_sve_f32mm, do_fmmla, a, gen_helper_fmmla_s)
TRANS_FEAT_NONSTREAMING(FMMLA_d, aa64_sve_f64mm, do_fmmla, a, gen_helper_fmmla_d)

/*
 * SVE Integer Multiply-Add (unpredicated)
 */

static gen_helper_gvec_4 * const sqdmlal_zzzw_fns[] = {
    NULL,                           gen_helper_sve2_sqdmlal_zzzw_h,
    gen_helper_sve2_sqdmlal_zzzw_s, gen_helper_sve2_sqdmlal_zzzw_d,
};
TRANS_FEAT(SQDMLALB_zzzw, aa64_sve2, gen_gvec_ool_arg_zzzz,
           sqdmlal_zzzw_fns[a->esz], a, 0)
TRANS_FEAT(SQDMLALT_zzzw, aa64_sve2, gen_gvec_ool_arg_zzzz,
           sqdmlal_zzzw_fns[a->esz], a, 3)
TRANS_FEAT(SQDMLALBT, aa64_sve2, gen_gvec_ool_arg_zzzz,
           sqdmlal_zzzw_fns[a->esz], a, 2)

static gen_helper_gvec_4 * const sqdmlsl_zzzw_fns[] = {
    NULL,                           gen_helper_sve2_sqdmlsl_zzzw_h,
    gen_helper_sve2_sqdmlsl_zzzw_s, gen_helper_sve2_sqdmlsl_zzzw_d,
};
TRANS_FEAT(SQDMLSLB_zzzw, aa64_sve2, gen_gvec_ool_arg_zzzz,
           sqdmlsl_zzzw_fns[a->esz], a, 0)
TRANS_FEAT(SQDMLSLT_zzzw, aa64_sve2, gen_gvec_ool_arg_zzzz,
           sqdmlsl_zzzw_fns[a->esz], a, 3)
TRANS_FEAT(SQDMLSLBT, aa64_sve2, gen_gvec_ool_arg_zzzz,
           sqdmlsl_zzzw_fns[a->esz], a, 2)

static gen_helper_gvec_4 * const sqrdmlah_fns[] = {
    gen_helper_sve2_sqrdmlah_b, gen_helper_sve2_sqrdmlah_h,
    gen_helper_sve2_sqrdmlah_s, gen_helper_sve2_sqrdmlah_d,
};
TRANS_FEAT(SQRDMLAH_zzzz, aa64_sve2, gen_gvec_ool_arg_zzzz,
           sqrdmlah_fns[a->esz], a, 0)

static gen_helper_gvec_4 * const sqrdmlsh_fns[] = {
    gen_helper_sve2_sqrdmlsh_b, gen_helper_sve2_sqrdmlsh_h,
    gen_helper_sve2_sqrdmlsh_s, gen_helper_sve2_sqrdmlsh_d,
};
TRANS_FEAT(SQRDMLSH_zzzz, aa64_sve2, gen_gvec_ool_arg_zzzz,
           sqrdmlsh_fns[a->esz], a, 0)

static gen_helper_gvec_4 * const smlal_zzzw_fns[] = {
    NULL,                         gen_helper_sve2_smlal_zzzw_h,
    gen_helper_sve2_smlal_zzzw_s, gen_helper_sve2_smlal_zzzw_d,
};
TRANS_FEAT(SMLALB_zzzw, aa64_sve2, gen_gvec_ool_arg_zzzz,
           smlal_zzzw_fns[a->esz], a, 0)
TRANS_FEAT(SMLALT_zzzw, aa64_sve2, gen_gvec_ool_arg_zzzz,
           smlal_zzzw_fns[a->esz], a, 1)

static gen_helper_gvec_4 * const umlal_zzzw_fns[] = {
    NULL,                         gen_helper_sve2_umlal_zzzw_h,
    gen_helper_sve2_umlal_zzzw_s, gen_helper_sve2_umlal_zzzw_d,
};
TRANS_FEAT(UMLALB_zzzw, aa64_sve2, gen_gvec_ool_arg_zzzz,
           umlal_zzzw_fns[a->esz], a, 0)
TRANS_FEAT(UMLALT_zzzw, aa64_sve2, gen_gvec_ool_arg_zzzz,
           umlal_zzzw_fns[a->esz], a, 1)

static gen_helper_gvec_4 * const smlsl_zzzw_fns[] = {
    NULL,                         gen_helper_sve2_smlsl_zzzw_h,
    gen_helper_sve2_smlsl_zzzw_s, gen_helper_sve2_smlsl_zzzw_d,
};
TRANS_FEAT(SMLSLB_zzzw, aa64_sve2, gen_gvec_ool_arg_zzzz,
           smlsl_zzzw_fns[a->esz], a, 0)
TRANS_FEAT(SMLSLT_zzzw, aa64_sve2, gen_gvec_ool_arg_zzzz,
           smlsl_zzzw_fns[a->esz], a, 1)

static gen_helper_gvec_4 * const umlsl_zzzw_fns[] = {
    NULL,                         gen_helper_sve2_umlsl_zzzw_h,
    gen_helper_sve2_umlsl_zzzw_s, gen_helper_sve2_umlsl_zzzw_d,
};
TRANS_FEAT(UMLSLB_zzzw, aa64_sve2, gen_gvec_ool_arg_zzzz,
           umlsl_zzzw_fns[a->esz], a, 0)
TRANS_FEAT(UMLSLT_zzzw, aa64_sve2, gen_gvec_ool_arg_zzzz,
           umlsl_zzzw_fns[a->esz], a, 1)

static gen_helper_gvec_4 * const cmla_fns[] = {
    gen_helper_sve2_cmla_zzzz_b, gen_helper_sve2_cmla_zzzz_h,
    gen_helper_sve2_cmla_zzzz_s, gen_helper_sve2_cmla_zzzz_d,
};
TRANS_FEAT(CMLA_zzzz, aa64_sve2, gen_gvec_ool_zzzz,
           cmla_fns[a->esz], a->rd, a->rn, a->rm, a->ra, a->rot)

static gen_helper_gvec_4 * const cdot_fns[] = {
    NULL, NULL, gen_helper_sve2_cdot_zzzz_s, gen_helper_sve2_cdot_zzzz_d
};
TRANS_FEAT(CDOT_zzzz, aa64_sve2, gen_gvec_ool_zzzz,
           cdot_fns[a->esz], a->rd, a->rn, a->rm, a->ra, a->rot)

static gen_helper_gvec_4 * const sqrdcmlah_fns[] = {
    gen_helper_sve2_sqrdcmlah_zzzz_b, gen_helper_sve2_sqrdcmlah_zzzz_h,
    gen_helper_sve2_sqrdcmlah_zzzz_s, gen_helper_sve2_sqrdcmlah_zzzz_d,
};
TRANS_FEAT(SQRDCMLAH_zzzz, aa64_sve2, gen_gvec_ool_zzzz,
           sqrdcmlah_fns[a->esz], a->rd, a->rn, a->rm, a->ra, a->rot)

TRANS_FEAT(USDOT_zzzz_4s, aa64_sve_i8mm, gen_gvec_ool_arg_zzzz,
           gen_helper_gvec_usdot_4b, a, 0)

TRANS_FEAT(SDOT_zzzz_2s, aa64_sme2_or_sve2p1, gen_gvec_ool_arg_zzzz,
           gen_helper_gvec_sdot_2h, a, 0)
TRANS_FEAT(UDOT_zzzz_2s, aa64_sme2_or_sve2p1, gen_gvec_ool_arg_zzzz,
           gen_helper_gvec_udot_2h, a, 0)

TRANS_FEAT_NONSTREAMING(AESMC, aa64_sve2_aes, gen_gvec_ool_zz,
                        gen_helper_crypto_aesmc, a->rd, a->rd, 0)
TRANS_FEAT_NONSTREAMING(AESIMC, aa64_sve2_aes, gen_gvec_ool_zz,
                        gen_helper_crypto_aesimc, a->rd, a->rd, 0)

TRANS_FEAT_NONSTREAMING(AESE, aa64_sve2_aes, gen_gvec_ool_arg_zzz,
                        gen_helper_crypto_aese, a, 0)
TRANS_FEAT_NONSTREAMING(AESD, aa64_sve2_aes, gen_gvec_ool_arg_zzz,
                        gen_helper_crypto_aesd, a, 0)

TRANS_FEAT_NONSTREAMING(SM4E, aa64_sve2_sm4, gen_gvec_ool_arg_zzz,
                        gen_helper_crypto_sm4e, a, 0)
TRANS_FEAT_NONSTREAMING(SM4EKEY, aa64_sve2_sm4, gen_gvec_ool_arg_zzz,
                        gen_helper_crypto_sm4ekey, a, 0)

TRANS_FEAT_NONSTREAMING(RAX1, aa64_sve2_sha3, gen_gvec_fn_arg_zzz,
                        gen_gvec_rax1, a)

TRANS_FEAT(FCVTNT_sh, aa64_sve2, gen_gvec_fpst_arg_zpz,
           gen_helper_sve2_fcvtnt_sh, a, 0, FPST_A64)
TRANS_FEAT(FCVTNT_ds, aa64_sve2, gen_gvec_fpst_arg_zpz,
           gen_helper_sve2_fcvtnt_ds, a, 0, FPST_A64)

TRANS_FEAT(BFCVTNT, aa64_sve_bf16, gen_gvec_fpst_arg_zpz,
           gen_helper_sve_bfcvtnt, a, 0,
           s->fpcr_ah ? FPST_AH : FPST_A64)

TRANS_FEAT(FCVTLT_hs, aa64_sve2, gen_gvec_fpst_arg_zpz,
           gen_helper_sve2_fcvtlt_hs, a, 0, FPST_A64)
TRANS_FEAT(FCVTLT_sd, aa64_sve2, gen_gvec_fpst_arg_zpz,
           gen_helper_sve2_fcvtlt_sd, a, 0, FPST_A64)

TRANS_FEAT(FCVTX_ds, aa64_sve2, do_frint_mode, a,
           FPROUNDING_ODD, gen_helper_sve_fcvt_ds)
TRANS_FEAT(FCVTXNT_ds, aa64_sve2, do_frint_mode, a,
           FPROUNDING_ODD, gen_helper_sve2_fcvtnt_ds)

static gen_helper_gvec_3_ptr * const flogb_fns[] = {
    NULL,               gen_helper_flogb_h,
    gen_helper_flogb_s, gen_helper_flogb_d
};
TRANS_FEAT(FLOGB, aa64_sve2, gen_gvec_fpst_arg_zpz, flogb_fns[a->esz],
           a, 0, a->esz == MO_16 ? FPST_A64_F16 : FPST_A64)

static bool do_FMLAL_zzzw(DisasContext *s, arg_rrrr_esz *a, bool sub, bool sel)
{
    return gen_gvec_ptr_zzzz(s, gen_helper_sve2_fmlal_zzzw_s,
                             a->rd, a->rn, a->rm, a->ra,
                             (sel << 1) | sub, tcg_env);
}

TRANS_FEAT(FMLALB_zzzw, aa64_sve2, do_FMLAL_zzzw, a, false, false)
TRANS_FEAT(FMLALT_zzzw, aa64_sve2, do_FMLAL_zzzw, a, false, true)
TRANS_FEAT(FMLSLB_zzzw, aa64_sve2, do_FMLAL_zzzw, a, true, false)
TRANS_FEAT(FMLSLT_zzzw, aa64_sve2, do_FMLAL_zzzw, a, true, true)

static bool do_FMLAL_zzxw(DisasContext *s, arg_rrxr_esz *a, bool sub, bool sel)
{
    return gen_gvec_ptr_zzzz(s, gen_helper_sve2_fmlal_zzxw_s,
                             a->rd, a->rn, a->rm, a->ra,
                             (a->index << 3) | (sel << 1) | sub, tcg_env);
}

TRANS_FEAT(FMLALB_zzxw, aa64_sve2, do_FMLAL_zzxw, a, false, false)
TRANS_FEAT(FMLALT_zzxw, aa64_sve2, do_FMLAL_zzxw, a, false, true)
TRANS_FEAT(FMLSLB_zzxw, aa64_sve2, do_FMLAL_zzxw, a, true, false)
TRANS_FEAT(FMLSLT_zzxw, aa64_sve2, do_FMLAL_zzxw, a, true, true)

TRANS_FEAT_NONSTREAMING(SMMLA, aa64_sve_i8mm, gen_gvec_ool_arg_zzzz,
                        gen_helper_gvec_smmla_b, a, 0)
TRANS_FEAT_NONSTREAMING(USMMLA, aa64_sve_i8mm, gen_gvec_ool_arg_zzzz,
                        gen_helper_gvec_usmmla_b, a, 0)
TRANS_FEAT_NONSTREAMING(UMMLA, aa64_sve_i8mm, gen_gvec_ool_arg_zzzz,
                        gen_helper_gvec_ummla_b, a, 0)

TRANS_FEAT(FDOT_zzzz, aa64_sme2_or_sve2p1, gen_gvec_env_arg_zzzz,
           gen_helper_sme2_fdot_h, a, 0)
TRANS_FEAT(FDOT_zzxz, aa64_sme2_or_sve2p1, gen_gvec_env_arg_zzxz,
           gen_helper_sme2_fdot_idx_h, a)

TRANS_FEAT(BFDOT_zzzz, aa64_sve_bf16, gen_gvec_env_arg_zzzz,
           gen_helper_gvec_bfdot, a, 0)
TRANS_FEAT(BFDOT_zzxz, aa64_sve_bf16, gen_gvec_env_arg_zzxz,
           gen_helper_gvec_bfdot_idx, a)

TRANS_FEAT_NONSTREAMING(BFMMLA, aa64_sve_bf16, gen_gvec_env_arg_zzzz,
                        gen_helper_gvec_bfmmla, a, 0)

static bool do_BFMLAL_zzzw(DisasContext *s, arg_rrrr_esz *a, bool sel)
{
    return gen_gvec_fpst_zzzz(s, gen_helper_gvec_bfmlal,
                              a->rd, a->rn, a->rm, a->ra, sel,
                              s->fpcr_ah ? FPST_AH : FPST_A64);
}

TRANS_FEAT(BFMLALB_zzzw, aa64_sve_bf16, do_BFMLAL_zzzw, a, false)
TRANS_FEAT(BFMLALT_zzzw, aa64_sve_bf16, do_BFMLAL_zzzw, a, true)

static bool do_BFMLAL_zzxw(DisasContext *s, arg_rrxr_esz *a, bool sel)
{
    return gen_gvec_fpst_zzzz(s, gen_helper_gvec_bfmlal_idx,
                              a->rd, a->rn, a->rm, a->ra,
                              (a->index << 1) | sel,
                              s->fpcr_ah ? FPST_AH : FPST_A64);
}

TRANS_FEAT(BFMLALB_zzxw, aa64_sve_bf16, do_BFMLAL_zzxw, a, false)
TRANS_FEAT(BFMLALT_zzxw, aa64_sve_bf16, do_BFMLAL_zzxw, a, true)

static bool do_BFMLSL_zzzw(DisasContext *s, arg_rrrr_esz *a, bool sel)
{
    if (s->fpcr_ah) {
        return gen_gvec_fpst_zzzz(s, gen_helper_gvec_ah_bfmlsl,
                                  a->rd, a->rn, a->rm, a->ra, sel, FPST_AH);
    } else {
        return gen_gvec_fpst_zzzz(s, gen_helper_gvec_bfmlsl,
                                  a->rd, a->rn, a->rm, a->ra, sel, FPST_A64);
    }
}

TRANS_FEAT(BFMLSLB_zzzw, aa64_sme2_or_sve2p1, do_BFMLSL_zzzw, a, false)
TRANS_FEAT(BFMLSLT_zzzw, aa64_sme2_or_sve2p1, do_BFMLSL_zzzw, a, true)

static bool do_BFMLSL_zzxw(DisasContext *s, arg_rrxr_esz *a, bool sel)
{
    if (s->fpcr_ah) {
        return gen_gvec_fpst_zzzz(s, gen_helper_gvec_ah_bfmlsl_idx,
                                  a->rd, a->rn, a->rm, a->ra,
                                  (a->index << 1) | sel, FPST_AH);
    } else {
        return gen_gvec_fpst_zzzz(s, gen_helper_gvec_bfmlsl_idx,
                                  a->rd, a->rn, a->rm, a->ra,
                                  (a->index << 1) | sel, FPST_A64);
    }
}

TRANS_FEAT(BFMLSLB_zzxw, aa64_sme2_or_sve2p1, do_BFMLSL_zzxw, a, false)
TRANS_FEAT(BFMLSLT_zzxw, aa64_sme2_or_sve2p1, do_BFMLSL_zzxw, a, true)

static bool trans_PSEL(DisasContext *s, arg_psel *a)
{
    int vl = vec_full_reg_size(s);
    int pl = pred_gvec_reg_size(s);
    int elements = vl >> a->esz;
    TCGv_i64 tmp, didx, dbit;
    TCGv_ptr ptr;

    if (!dc_isar_feature(aa64_sme_or_sve2p1, s)) {
        return false;
    }
    if (!sve_access_check(s)) {
        return true;
    }

    tmp = tcg_temp_new_i64();
    dbit = tcg_temp_new_i64();
    didx = tcg_temp_new_i64();
    ptr = tcg_temp_new_ptr();

    /* Compute the predicate element. */
    tcg_gen_addi_i64(tmp, cpu_reg(s, a->rv), a->imm);
    if (is_power_of_2(elements)) {
        tcg_gen_andi_i64(tmp, tmp, elements - 1);
    } else {
        tcg_gen_remu_i64(tmp, tmp, tcg_constant_i64(elements));
    }

    /* Extract the predicate byte and bit indices. */
    tcg_gen_shli_i64(tmp, tmp, a->esz);
    tcg_gen_andi_i64(dbit, tmp, 7);
    tcg_gen_shri_i64(didx, tmp, 3);
    if (HOST_BIG_ENDIAN) {
        tcg_gen_xori_i64(didx, didx, 7);
    }

    /* Load the predicate word. */
    tcg_gen_trunc_i64_ptr(ptr, didx);
    tcg_gen_add_ptr(ptr, ptr, tcg_env);
    tcg_gen_ld8u_i64(tmp, ptr, pred_full_reg_offset(s, a->pm));

    /* Extract the predicate bit and replicate to MO_64. */
    tcg_gen_shr_i64(tmp, tmp, dbit);
    tcg_gen_andi_i64(tmp, tmp, 1);
    tcg_gen_neg_i64(tmp, tmp);

    /* Apply to either copy the source, or write zeros. */
    pl = size_for_gvec(pl);
    tcg_gen_gvec_ands(MO_64, pred_full_reg_offset(s, a->pd),
                      pred_full_reg_offset(s, a->pn), tmp, pl, pl);
    return true;
}

static void gen_sclamp_i32(TCGv_i32 d, TCGv_i32 n, TCGv_i32 m, TCGv_i32 a)
{
    tcg_gen_smax_i32(d, a, n);
    tcg_gen_smin_i32(d, d, m);
}

static void gen_sclamp_i64(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m, TCGv_i64 a)
{
    tcg_gen_smax_i64(d, a, n);
    tcg_gen_smin_i64(d, d, m);
}

static void gen_sclamp_vec(unsigned vece, TCGv_vec d, TCGv_vec n,
                           TCGv_vec m, TCGv_vec a)
{
    tcg_gen_smax_vec(vece, d, a, n);
    tcg_gen_smin_vec(vece, d, d, m);
}

static void gen_sclamp(unsigned vece, uint32_t d, uint32_t n, uint32_t m,
                       uint32_t a, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop[] = {
        INDEX_op_smin_vec, INDEX_op_smax_vec, 0
    };
    static const GVecGen4 ops[4] = {
        { .fniv = gen_sclamp_vec,
          .fno  = gen_helper_gvec_sclamp_b,
          .opt_opc = vecop,
          .vece = MO_8 },
        { .fniv = gen_sclamp_vec,
          .fno  = gen_helper_gvec_sclamp_h,
          .opt_opc = vecop,
          .vece = MO_16 },
        { .fni4 = gen_sclamp_i32,
          .fniv = gen_sclamp_vec,
          .fno  = gen_helper_gvec_sclamp_s,
          .opt_opc = vecop,
          .vece = MO_32 },
        { .fni8 = gen_sclamp_i64,
          .fniv = gen_sclamp_vec,
          .fno  = gen_helper_gvec_sclamp_d,
          .opt_opc = vecop,
          .vece = MO_64,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64 }
    };
    tcg_gen_gvec_4(d, n, m, a, oprsz, maxsz, &ops[vece]);
}

TRANS_FEAT(SCLAMP, aa64_sme_or_sve2p1, gen_gvec_fn_arg_zzzz, gen_sclamp, a)

static void gen_uclamp_i32(TCGv_i32 d, TCGv_i32 n, TCGv_i32 m, TCGv_i32 a)
{
    tcg_gen_umax_i32(d, a, n);
    tcg_gen_umin_i32(d, d, m);
}

static void gen_uclamp_i64(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m, TCGv_i64 a)
{
    tcg_gen_umax_i64(d, a, n);
    tcg_gen_umin_i64(d, d, m);
}

static void gen_uclamp_vec(unsigned vece, TCGv_vec d, TCGv_vec n,
                           TCGv_vec m, TCGv_vec a)
{
    tcg_gen_umax_vec(vece, d, a, n);
    tcg_gen_umin_vec(vece, d, d, m);
}

static void gen_uclamp(unsigned vece, uint32_t d, uint32_t n, uint32_t m,
                       uint32_t a, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop[] = {
        INDEX_op_umin_vec, INDEX_op_umax_vec, 0
    };
    static const GVecGen4 ops[4] = {
        { .fniv = gen_uclamp_vec,
          .fno  = gen_helper_gvec_uclamp_b,
          .opt_opc = vecop,
          .vece = MO_8 },
        { .fniv = gen_uclamp_vec,
          .fno  = gen_helper_gvec_uclamp_h,
          .opt_opc = vecop,
          .vece = MO_16 },
        { .fni4 = gen_uclamp_i32,
          .fniv = gen_uclamp_vec,
          .fno  = gen_helper_gvec_uclamp_s,
          .opt_opc = vecop,
          .vece = MO_32 },
        { .fni8 = gen_uclamp_i64,
          .fniv = gen_uclamp_vec,
          .fno  = gen_helper_gvec_uclamp_d,
          .opt_opc = vecop,
          .vece = MO_64,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64 }
    };
    tcg_gen_gvec_4(d, n, m, a, oprsz, maxsz, &ops[vece]);
}

TRANS_FEAT(UCLAMP, aa64_sme_or_sve2p1, gen_gvec_fn_arg_zzzz, gen_uclamp, a)

static bool trans_FCLAMP(DisasContext *s, arg_FCLAMP *a)
{
    static gen_helper_gvec_3_ptr * const fn[] = {
        gen_helper_sme2_bfclamp,
        gen_helper_sme2_fclamp_h,
        gen_helper_sme2_fclamp_s,
        gen_helper_sme2_fclamp_d,
    };

    /* This insn uses MO_8 to encode BFloat16. */
    if (a->esz == MO_8
        ? !dc_isar_feature(aa64_sve_b16b16, s)
        : !dc_isar_feature(aa64_sme2_or_sve2p1, s)) {
        return false;
    }

    /* So far we never optimize rda with MOVPRFX */
    assert(a->rd == a->ra);
    return gen_gvec_fpst_zzz(s, fn[a->esz], a->rd, a->rn, a->rm, 1,
                             a->esz == MO_16 ? FPST_A64_F16 : FPST_A64);
}

TRANS_FEAT(SQCVTN_sh, aa64_sme2_or_sve2p1, gen_gvec_ool_zz,
           gen_helper_sme2_sqcvtn_sh, a->rd, a->rn, 0)
TRANS_FEAT(UQCVTN_sh, aa64_sme2_or_sve2p1, gen_gvec_ool_zz,
           gen_helper_sme2_uqcvtn_sh, a->rd, a->rn, 0)
TRANS_FEAT(SQCVTUN_sh, aa64_sme2_or_sve2p1, gen_gvec_ool_zz,
           gen_helper_sme2_sqcvtun_sh, a->rd, a->rn, 0)

static bool gen_ldst_c(DisasContext *s, TCGv_i64 addr, int zd, int png,
                       MemOp esz, bool is_write, int n, bool strided)
{
    typedef void ldst_c_fn(TCGv_env, TCGv_ptr, TCGv_i64,
                           TCGv_i32, TCGv_i64);
    static ldst_c_fn * const f_ldst[2][2][4] = {
        { { gen_helper_sve2p1_ld1bb_c,
            gen_helper_sve2p1_ld1hh_le_c,
            gen_helper_sve2p1_ld1ss_le_c,
            gen_helper_sve2p1_ld1dd_le_c, },
          { gen_helper_sve2p1_ld1bb_c,
            gen_helper_sve2p1_ld1hh_be_c,
            gen_helper_sve2p1_ld1ss_be_c,
            gen_helper_sve2p1_ld1dd_be_c, } },

        { { gen_helper_sve2p1_st1bb_c,
            gen_helper_sve2p1_st1hh_le_c,
            gen_helper_sve2p1_st1ss_le_c,
            gen_helper_sve2p1_st1dd_le_c, },
          { gen_helper_sve2p1_st1bb_c,
            gen_helper_sve2p1_st1hh_be_c,
            gen_helper_sve2p1_st1ss_be_c,
            gen_helper_sve2p1_st1dd_be_c, } }
    };

    TCGv_i32 t_png;
    TCGv_i64 t_desc;
    TCGv_ptr t_zd;
    uint64_t desc, lg2_rstride = 0;
    bool be = s->be_data == MO_BE;

    assert(n == 2 || n == 4);
    if (strided) {
        lg2_rstride = 3;
        if (n == 4) {
            /* Validate ZD alignment. */
            if (zd & 4) {
                return false;
            }
            lg2_rstride = 2;
        }
        /* Ignore non-temporal bit */
        zd &= ~8;
    }

    if (strided || !dc_isar_feature(aa64_sve2p1, s)
        ? !sme_sm_enabled_check(s)
        : !sve_access_check(s)) {
        return true;
    }

    if (!s->mte_active[0]) {
        addr = clean_data_tbi(s, addr);
    }

    desc = n == 2 ? 0 : 1;
    desc = desc | (lg2_rstride << 1);
    desc = make_svemte_desc(s, vec_full_reg_size(s), 1, esz, is_write, desc);
    t_desc = tcg_constant_i64(desc);

    t_png = tcg_temp_new_i32();
    tcg_gen_ld16u_i32(t_png, tcg_env,
                      pred_full_reg_offset(s, png) ^
                      (HOST_BIG_ENDIAN ? 6 : 0));

    t_zd = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(t_zd, tcg_env, vec_full_reg_offset(s, zd));

    f_ldst[is_write][be][esz](tcg_env, t_zd, addr, t_png, t_desc);
    return true;
}

static bool gen_ldst_zcrr_c(DisasContext *s, arg_zcrr_ldst *a,
                            bool is_write, bool strided)
{
    TCGv_i64 addr = tcg_temp_new_i64();

    tcg_gen_shli_i64(addr, cpu_reg(s, a->rm), a->esz);
    tcg_gen_add_i64(addr, addr, cpu_reg_sp(s, a->rn));
    return gen_ldst_c(s, addr, a->rd, a->png, a->esz, is_write,
                      a->nreg, strided);
}

static bool gen_ldst_zcri_c(DisasContext *s, arg_zcri_ldst *a,
                            bool is_write, bool strided)
{
    TCGv_i64 addr = tcg_temp_new_i64();

    tcg_gen_addi_i64(addr, cpu_reg_sp(s, a->rn),
                     a->imm * a->nreg * vec_full_reg_size(s));
    return gen_ldst_c(s, addr, a->rd, a->png, a->esz, is_write,
                      a->nreg, strided);
}

TRANS_FEAT(LD1_zcrr, aa64_sme2_or_sve2p1, gen_ldst_zcrr_c, a, false, false)
TRANS_FEAT(LD1_zcri, aa64_sme2_or_sve2p1, gen_ldst_zcri_c, a, false, false)
TRANS_FEAT(ST1_zcrr, aa64_sme2_or_sve2p1, gen_ldst_zcrr_c, a, true, false)
TRANS_FEAT(ST1_zcri, aa64_sme2_or_sve2p1, gen_ldst_zcri_c, a, true, false)

TRANS_FEAT(LD1_zcrr_stride, aa64_sme2, gen_ldst_zcrr_c, a, false, true)
TRANS_FEAT(LD1_zcri_stride, aa64_sme2, gen_ldst_zcri_c, a, false, true)
TRANS_FEAT(ST1_zcrr_stride, aa64_sme2, gen_ldst_zcrr_c, a, true, true)
TRANS_FEAT(ST1_zcri_stride, aa64_sme2, gen_ldst_zcri_c, a, true, true)
