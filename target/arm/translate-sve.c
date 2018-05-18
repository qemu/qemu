/*
 * AArch64 SVE translation
 *
 * Copyright (c) 2018 Linaro, Ltd
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

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "tcg-op.h"
#include "tcg-op-gvec.h"
#include "qemu/log.h"
#include "arm_ldst.h"
#include "translate.h"
#include "internals.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/log.h"
#include "trace-tcg.h"
#include "translate-a64.h"

/*
 * Include the generated decoder.
 */

#include "decode-sve.inc.c"

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

/* Invoke a vector expander on two Zregs.  */
static bool do_vector2_z(DisasContext *s, GVecGen2Fn *gvec_fn,
                         int esz, int rd, int rn)
{
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        gvec_fn(esz, vec_full_reg_offset(s, rd),
                vec_full_reg_offset(s, rn), vsz, vsz);
    }
    return true;
}

/* Invoke a vector expander on three Zregs.  */
static bool do_vector3_z(DisasContext *s, GVecGen3Fn *gvec_fn,
                         int esz, int rd, int rn, int rm)
{
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        gvec_fn(esz, vec_full_reg_offset(s, rd),
                vec_full_reg_offset(s, rn),
                vec_full_reg_offset(s, rm), vsz, vsz);
    }
    return true;
}

/* Invoke a vector move on two Zregs.  */
static bool do_mov_z(DisasContext *s, int rd, int rn)
{
    return do_vector2_z(s, tcg_gen_gvec_mov, 0, rd, rn);
}

/* Invoke a vector expander on two Pregs.  */
static bool do_vector2_p(DisasContext *s, GVecGen2Fn *gvec_fn,
                         int esz, int rd, int rn)
{
    if (sve_access_check(s)) {
        unsigned psz = pred_gvec_reg_size(s);
        gvec_fn(esz, pred_full_reg_offset(s, rd),
                pred_full_reg_offset(s, rn), psz, psz);
    }
    return true;
}

/* Invoke a vector expander on three Pregs.  */
static bool do_vector3_p(DisasContext *s, GVecGen3Fn *gvec_fn,
                         int esz, int rd, int rn, int rm)
{
    if (sve_access_check(s)) {
        unsigned psz = pred_gvec_reg_size(s);
        gvec_fn(esz, pred_full_reg_offset(s, rd),
                pred_full_reg_offset(s, rn),
                pred_full_reg_offset(s, rm), psz, psz);
    }
    return true;
}

/* Invoke a vector operation on four Pregs.  */
static bool do_vecop4_p(DisasContext *s, const GVecGen4 *gvec_op,
                        int rd, int rn, int rm, int rg)
{
    if (sve_access_check(s)) {
        unsigned psz = pred_gvec_reg_size(s);
        tcg_gen_gvec_4(pred_full_reg_offset(s, rd),
                       pred_full_reg_offset(s, rn),
                       pred_full_reg_offset(s, rm),
                       pred_full_reg_offset(s, rg),
                       psz, psz, gvec_op);
    }
    return true;
}

/* Invoke a vector move on two Pregs.  */
static bool do_mov_p(DisasContext *s, int rd, int rn)
{
    return do_vector2_p(s, tcg_gen_gvec_mov, 0, rd, rn);
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

/*
 *** SVE Logical - Unpredicated Group
 */

static bool trans_AND_zzz(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    return do_vector3_z(s, tcg_gen_gvec_and, 0, a->rd, a->rn, a->rm);
}

static bool trans_ORR_zzz(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    if (a->rn == a->rm) { /* MOV */
        return do_mov_z(s, a->rd, a->rn);
    } else {
        return do_vector3_z(s, tcg_gen_gvec_or, 0, a->rd, a->rn, a->rm);
    }
}

static bool trans_EOR_zzz(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    return do_vector3_z(s, tcg_gen_gvec_xor, 0, a->rd, a->rn, a->rm);
}

static bool trans_BIC_zzz(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    return do_vector3_z(s, tcg_gen_gvec_andc, 0, a->rd, a->rn, a->rm);
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

static bool trans_AND_pppp(DisasContext *s, arg_rprr_s *a, uint32_t insn)
{
    static const GVecGen4 op = {
        .fni8 = gen_and_pg_i64,
        .fniv = gen_and_pg_vec,
        .fno = gen_helper_sve_and_pppp,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    if (a->s) {
        return do_pppp_flags(s, a, &op);
    } else if (a->rn == a->rm) {
        if (a->pg == a->rn) {
            return do_mov_p(s, a->rd, a->rn);
        } else {
            return do_vector3_p(s, tcg_gen_gvec_and, 0, a->rd, a->rn, a->pg);
        }
    } else if (a->pg == a->rn || a->pg == a->rm) {
        return do_vector3_p(s, tcg_gen_gvec_and, 0, a->rd, a->rn, a->rm);
    } else {
        return do_vecop4_p(s, &op, a->rd, a->rn, a->rm, a->pg);
    }
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

static bool trans_BIC_pppp(DisasContext *s, arg_rprr_s *a, uint32_t insn)
{
    static const GVecGen4 op = {
        .fni8 = gen_bic_pg_i64,
        .fniv = gen_bic_pg_vec,
        .fno = gen_helper_sve_bic_pppp,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    if (a->s) {
        return do_pppp_flags(s, a, &op);
    } else if (a->pg == a->rn) {
        return do_vector3_p(s, tcg_gen_gvec_andc, 0, a->rd, a->rn, a->rm);
    } else {
        return do_vecop4_p(s, &op, a->rd, a->rn, a->rm, a->pg);
    }
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

static bool trans_EOR_pppp(DisasContext *s, arg_rprr_s *a, uint32_t insn)
{
    static const GVecGen4 op = {
        .fni8 = gen_eor_pg_i64,
        .fniv = gen_eor_pg_vec,
        .fno = gen_helper_sve_eor_pppp,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    if (a->s) {
        return do_pppp_flags(s, a, &op);
    } else {
        return do_vecop4_p(s, &op, a->rd, a->rn, a->rm, a->pg);
    }
}

static void gen_sel_pg_i64(TCGv_i64 pd, TCGv_i64 pn, TCGv_i64 pm, TCGv_i64 pg)
{
    tcg_gen_and_i64(pn, pn, pg);
    tcg_gen_andc_i64(pm, pm, pg);
    tcg_gen_or_i64(pd, pn, pm);
}

static void gen_sel_pg_vec(unsigned vece, TCGv_vec pd, TCGv_vec pn,
                           TCGv_vec pm, TCGv_vec pg)
{
    tcg_gen_and_vec(vece, pn, pn, pg);
    tcg_gen_andc_vec(vece, pm, pm, pg);
    tcg_gen_or_vec(vece, pd, pn, pm);
}

static bool trans_SEL_pppp(DisasContext *s, arg_rprr_s *a, uint32_t insn)
{
    static const GVecGen4 op = {
        .fni8 = gen_sel_pg_i64,
        .fniv = gen_sel_pg_vec,
        .fno = gen_helper_sve_sel_pppp,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    if (a->s) {
        return false;
    } else {
        return do_vecop4_p(s, &op, a->rd, a->rn, a->rm, a->pg);
    }
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

static bool trans_ORR_pppp(DisasContext *s, arg_rprr_s *a, uint32_t insn)
{
    static const GVecGen4 op = {
        .fni8 = gen_orr_pg_i64,
        .fniv = gen_orr_pg_vec,
        .fno = gen_helper_sve_orr_pppp,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    if (a->s) {
        return do_pppp_flags(s, a, &op);
    } else if (a->pg == a->rn && a->rn == a->rm) {
        return do_mov_p(s, a->rd, a->rn);
    } else {
        return do_vecop4_p(s, &op, a->rd, a->rn, a->rm, a->pg);
    }
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

static bool trans_ORN_pppp(DisasContext *s, arg_rprr_s *a, uint32_t insn)
{
    static const GVecGen4 op = {
        .fni8 = gen_orn_pg_i64,
        .fniv = gen_orn_pg_vec,
        .fno = gen_helper_sve_orn_pppp,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    if (a->s) {
        return do_pppp_flags(s, a, &op);
    } else {
        return do_vecop4_p(s, &op, a->rd, a->rn, a->rm, a->pg);
    }
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

static bool trans_NOR_pppp(DisasContext *s, arg_rprr_s *a, uint32_t insn)
{
    static const GVecGen4 op = {
        .fni8 = gen_nor_pg_i64,
        .fniv = gen_nor_pg_vec,
        .fno = gen_helper_sve_nor_pppp,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    if (a->s) {
        return do_pppp_flags(s, a, &op);
    } else {
        return do_vecop4_p(s, &op, a->rd, a->rn, a->rm, a->pg);
    }
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

static bool trans_NAND_pppp(DisasContext *s, arg_rprr_s *a, uint32_t insn)
{
    static const GVecGen4 op = {
        .fni8 = gen_nand_pg_i64,
        .fniv = gen_nand_pg_vec,
        .fno = gen_helper_sve_nand_pppp,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    if (a->s) {
        return do_pppp_flags(s, a, &op);
    } else {
        return do_vecop4_p(s, &op, a->rd, a->rn, a->rm, a->pg);
    }
}

/*
 *** SVE Predicate Misc Group
 */

static bool trans_PTEST(DisasContext *s, arg_PTEST *a, uint32_t insn)
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

/*
 *** SVE Memory - 32-bit Gather and Unsized Contiguous Group
 */

/* Subroutine loading a vector register at VOFS of LEN bytes.
 * The load should begin at the address Rn + IMM.
 */

static void do_ldr(DisasContext *s, uint32_t vofs, uint32_t len,
                   int rn, int imm)
{
    uint32_t len_align = QEMU_ALIGN_DOWN(len, 8);
    uint32_t len_remain = len % 8;
    uint32_t nparts = len / 8 + ctpop8(len_remain);
    int midx = get_mem_index(s);
    TCGv_i64 addr, t0, t1;

    addr = tcg_temp_new_i64();
    t0 = tcg_temp_new_i64();

    /* Note that unpredicated load/store of vector/predicate registers
     * are defined as a stream of bytes, which equates to little-endian
     * operations on larger quantities.  There is no nice way to force
     * a little-endian load for aarch64_be-linux-user out of line.
     *
     * Attempt to keep code expansion to a minimum by limiting the
     * amount of unrolling done.
     */
    if (nparts <= 4) {
        int i;

        for (i = 0; i < len_align; i += 8) {
            tcg_gen_addi_i64(addr, cpu_reg_sp(s, rn), imm + i);
            tcg_gen_qemu_ld_i64(t0, addr, midx, MO_LEQ);
            tcg_gen_st_i64(t0, cpu_env, vofs + i);
        }
    } else {
        TCGLabel *loop = gen_new_label();
        TCGv_ptr tp, i = tcg_const_local_ptr(0);

        gen_set_label(loop);

        /* Minimize the number of local temps that must be re-read from
         * the stack each iteration.  Instead, re-compute values other
         * than the loop counter.
         */
        tp = tcg_temp_new_ptr();
        tcg_gen_addi_ptr(tp, i, imm);
        tcg_gen_extu_ptr_i64(addr, tp);
        tcg_gen_add_i64(addr, addr, cpu_reg_sp(s, rn));

        tcg_gen_qemu_ld_i64(t0, addr, midx, MO_LEQ);

        tcg_gen_add_ptr(tp, cpu_env, i);
        tcg_gen_addi_ptr(i, i, 8);
        tcg_gen_st_i64(t0, tp, vofs);
        tcg_temp_free_ptr(tp);

        tcg_gen_brcondi_ptr(TCG_COND_LTU, i, len_align, loop);
        tcg_temp_free_ptr(i);
    }

    /* Predicate register loads can be any multiple of 2.
     * Note that we still store the entire 64-bit unit into cpu_env.
     */
    if (len_remain) {
        tcg_gen_addi_i64(addr, cpu_reg_sp(s, rn), imm + len_align);

        switch (len_remain) {
        case 2:
        case 4:
        case 8:
            tcg_gen_qemu_ld_i64(t0, addr, midx, MO_LE | ctz32(len_remain));
            break;

        case 6:
            t1 = tcg_temp_new_i64();
            tcg_gen_qemu_ld_i64(t0, addr, midx, MO_LEUL);
            tcg_gen_addi_i64(addr, addr, 4);
            tcg_gen_qemu_ld_i64(t1, addr, midx, MO_LEUW);
            tcg_gen_deposit_i64(t0, t0, t1, 32, 32);
            tcg_temp_free_i64(t1);
            break;

        default:
            g_assert_not_reached();
        }
        tcg_gen_st_i64(t0, cpu_env, vofs + len_align);
    }
    tcg_temp_free_i64(addr);
    tcg_temp_free_i64(t0);
}

static bool trans_LDR_zri(DisasContext *s, arg_rri *a, uint32_t insn)
{
    if (sve_access_check(s)) {
        int size = vec_full_reg_size(s);
        int off = vec_full_reg_offset(s, a->rd);
        do_ldr(s, off, size, a->rn, a->imm * size);
    }
    return true;
}

static bool trans_LDR_pri(DisasContext *s, arg_rri *a, uint32_t insn)
{
    if (sve_access_check(s)) {
        int size = pred_full_reg_size(s);
        int off = pred_full_reg_offset(s, a->rd);
        do_ldr(s, off, size, a->rn, a->imm * size);
    }
    return true;
}
