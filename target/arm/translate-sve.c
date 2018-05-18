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
