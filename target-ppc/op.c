/*
 *  PPC emulation micro-operations for qemu.
 * 
 *  Copyright (c) 2003 Jocelyn Mayer
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

//#define DEBUG_OP

#include "config.h"
#include "exec.h"

#define regs (env)
#define Ts0 (int32_t)T0
#define Ts1 (int32_t)T1
#define Ts2 (int32_t)T2

#define FT0 (env->ft0)
#define FT1 (env->ft1)
#define FT2 (env->ft2)

#define FTS0 ((float)env->ft0)
#define FTS1 ((float)env->ft1)
#define FTS2 ((float)env->ft2)

#define PPC_OP(name) void glue(op_, name)(void)

#define REG 0
#include "op_template.h"

#define REG 1
#include "op_template.h"

#define REG 2
#include "op_template.h"

#define REG 3
#include "op_template.h"

#define REG 4
#include "op_template.h"

#define REG 5
#include "op_template.h"

#define REG 6
#include "op_template.h"

#define REG 7
#include "op_template.h"

#define REG 8
#include "op_template.h"

#define REG 9
#include "op_template.h"

#define REG 10
#include "op_template.h"

#define REG 11
#include "op_template.h"

#define REG 12
#include "op_template.h"

#define REG 13
#include "op_template.h"

#define REG 14
#include "op_template.h"

#define REG 15
#include "op_template.h"

#define REG 16
#include "op_template.h"

#define REG 17
#include "op_template.h"

#define REG 18
#include "op_template.h"

#define REG 19
#include "op_template.h"

#define REG 20
#include "op_template.h"

#define REG 21
#include "op_template.h"

#define REG 22
#include "op_template.h"

#define REG 23
#include "op_template.h"

#define REG 24
#include "op_template.h"

#define REG 25
#include "op_template.h"

#define REG 26
#include "op_template.h"

#define REG 27
#include "op_template.h"

#define REG 28
#include "op_template.h"

#define REG 29
#include "op_template.h"

#define REG 30
#include "op_template.h"

#define REG 31
#include "op_template.h"

/* PPC state maintenance operations */
/* set_Rc0 */
PPC_OP(set_Rc0)
{
    uint32_t tmp;

    if (Ts0 < 0) {
        tmp = 0x08;
    } else if (Ts0 > 0) {
        tmp = 0x04;
    } else {
        tmp = 0x02;
    }
    env->crf[0] = tmp;
    RETURN();
}

PPC_OP(set_Rc0_ov)
{
    uint32_t tmp;

    if (Ts0 < 0) {
        tmp = 0x08;
    } else if (Ts0 > 0) {
        tmp = 0x04;
    } else {
        tmp = 0x02;
    }
    tmp |= xer_ov;
    env->crf[0] = tmp;
    RETURN();
}

/* reset_Rc0 */
PPC_OP(reset_Rc0)
{
    env->crf[0] = 0x02 | xer_ov;
    RETURN();
}

/* set_Rc0_1 */
PPC_OP(set_Rc0_1)
{
    env->crf[0] = 0x04 | xer_ov;
    RETURN();
}

/* Set Rc1 (for floating point arithmetic) */
PPC_OP(set_Rc1)
{
    env->crf[1] = regs->fpscr[7];
    RETURN();
}

/* Constants load */
PPC_OP(set_T0)
{
    T0 = PARAM(1);
    RETURN();
}

PPC_OP(set_T1)
{
    T1 = PARAM(1);
    RETURN();
}

PPC_OP(set_T2)
{
    T2 = PARAM(1);
    RETURN();
}

/* Generate exceptions */
PPC_OP(raise_exception_err)
{
    do_raise_exception_err(PARAM(1), PARAM(2));
}

PPC_OP(raise_exception)
{
    do_raise_exception(PARAM(1));
}

PPC_OP(update_nip)
{
    env->nip = PARAM(1);
}

PPC_OP(debug)
{
    env->nip = PARAM(1);
#if defined (DEBUG_OP)
    dump_state();
#endif
    do_raise_exception(EXCP_DEBUG);
    RETURN();
}

/* Segment registers load and store with immediate index */
PPC_OP(load_srin)
{
    T0 = regs->sr[T1 >> 28];
    RETURN();
}

PPC_OP(store_srin)
{
    do_store_sr(T1 >> 28);
    RETURN();
}

PPC_OP(load_sdr1)
{
    T0 = regs->sdr1;
    RETURN();
}

PPC_OP(store_sdr1)
{
    regs->sdr1 = T0;
    RETURN();
}

PPC_OP(exit_tb)
{
    EXIT_TB();
}

/* Load/store special registers */
PPC_OP(load_cr)
{
    do_load_cr();
    RETURN();
}

PPC_OP(store_cr)
{
    do_store_cr(PARAM(1));
    RETURN();
}

PPC_OP(load_xer_cr)
{
    T0 = (xer_so << 3) | (xer_ov << 2) | (xer_ca << 1);
    RETURN();
}

PPC_OP(clear_xer_cr)
{
    xer_so = 0;
    xer_ov = 0;
    xer_ca = 0;
    RETURN();
}

PPC_OP(load_xer_bc)
{
    T1 = xer_bc;
    RETURN();
}

PPC_OP(load_xer)
{
    do_load_xer();
    RETURN();
}

PPC_OP(store_xer)
{
    do_store_xer();
    RETURN();
}

PPC_OP(load_msr)
{
    do_load_msr();
    RETURN();
}

PPC_OP(store_msr)
{
    do_store_msr();
    RETURN();
}

/* SPR */
PPC_OP(load_spr)
{
    T0 = regs->spr[PARAM(1)];
    RETURN();
}

PPC_OP(store_spr)
{
    regs->spr[PARAM(1)] = T0;
    RETURN();
}

PPC_OP(load_lr)
{
    T0 = regs->lr;
    RETURN();
}

PPC_OP(store_lr)
{
    regs->lr = T0;
    RETURN();
}

PPC_OP(load_ctr)
{
    T0 = regs->ctr;
    RETURN();
}

PPC_OP(store_ctr)
{
    regs->ctr = T0;
    RETURN();
}

PPC_OP(load_tbl)
{
    T0 = cpu_ppc_load_tbl(regs);
    RETURN();
}

PPC_OP(load_tbu)
{
    T0 = cpu_ppc_load_tbu(regs);
    RETURN();
}

PPC_OP(store_tbl)
{
    cpu_ppc_store_tbl(regs, T0);
    RETURN();
}

PPC_OP(store_tbu)
{
    cpu_ppc_store_tbu(regs, T0);
    RETURN();
}

PPC_OP(load_decr)
{
    T0 = cpu_ppc_load_decr(regs);
    }

PPC_OP(store_decr)
{
    cpu_ppc_store_decr(regs, T0);
    RETURN();
}

PPC_OP(load_ibat)
{
    T0 = regs->IBAT[PARAM(1)][PARAM(2)];
}

PPC_OP(store_ibat)
{
    do_store_ibat(PARAM(1), PARAM(2));
}

PPC_OP(load_dbat)
{
    T0 = regs->DBAT[PARAM(1)][PARAM(2)];
}

PPC_OP(store_dbat)
{
    do_store_dbat(PARAM(1), PARAM(2));
}

/* FPSCR */
PPC_OP(load_fpscr)
{
    do_load_fpscr();
    RETURN();
}

PPC_OP(store_fpscr)
{
    do_store_fpscr(PARAM(1));
    RETURN();
}

PPC_OP(reset_scrfx)
{
    regs->fpscr[7] &= ~0x8;
    RETURN();
}

/* crf operations */
PPC_OP(getbit_T0)
{
    T0 = (T0 >> PARAM(1)) & 1;
    RETURN();
}

PPC_OP(getbit_T1)
{
    T1 = (T1 >> PARAM(1)) & 1;
    RETURN();
}

PPC_OP(setcrfbit)
{
    T1 = (T1 & PARAM(1)) | (T0 << PARAM(2)); 
    RETURN();
}

/* Branch */
#define EIP regs->nip

PPC_OP(setlr)
{
    regs->lr = PARAM1;
}

PPC_OP(b)
{
    JUMP_TB(b1, PARAM1, 0, PARAM2);
}

PPC_OP(b_T1)
{
    regs->nip = T1;
}

PPC_OP(btest) 
{
    if (T0) {
        JUMP_TB(btest, PARAM1, 0, PARAM2);
    } else {
        JUMP_TB(btest, PARAM1, 1, PARAM3);
    }
    RETURN();
}

PPC_OP(btest_T1) 
{
    if (T0) {
        regs->nip = T1 & ~3;
    } else {
        regs->nip = PARAM1;
    }
    RETURN();
}

PPC_OP(movl_T1_ctr)
{
    T1 = regs->ctr;
}

PPC_OP(movl_T1_lr)
{
    T1 = regs->lr;
}

/* tests with result in T0 */

PPC_OP(test_ctr)
{
    T0 = regs->ctr;
}

PPC_OP(test_ctr_true)
{
    T0 = (regs->ctr != 0 && (T0 & PARAM(1)) != 0);
}

PPC_OP(test_ctr_false)
{
    T0 = (regs->ctr != 0 && (T0 & PARAM(1)) == 0);
}

PPC_OP(test_ctrz)
{
    T0 = (regs->ctr == 0);
}

PPC_OP(test_ctrz_true)
{
    T0 = (regs->ctr == 0 && (T0 & PARAM(1)) != 0);
}

PPC_OP(test_ctrz_false)
{
    T0 = (regs->ctr == 0 && (T0 & PARAM(1)) == 0);
}

PPC_OP(test_true)
{
    T0 = (T0 & PARAM(1));
}

PPC_OP(test_false)
{
    T0 = ((T0 & PARAM(1)) == 0);
}

/* CTR maintenance */
PPC_OP(dec_ctr)
{
    regs->ctr--;
    RETURN();
}

/***                           Integer arithmetic                          ***/
/* add */
PPC_OP(add)
{
    T0 += T1;
    RETURN();
}

PPC_OP(addo)
{
    T2 = T0;
    T0 += T1;
    if ((T2 ^ T1 ^ (-1)) & (T2 ^ T0) & (1 << 31)) {
        xer_so = 1;
        xer_ov = 1;
    } else {
        xer_ov = 0;
    }
    RETURN();
}

/* add carrying */
PPC_OP(addc)
{
    T2 = T0;
    T0 += T1;
    if (T0 < T2) {
        xer_ca = 1;
    } else {
        xer_ca = 0;
    }
    RETURN();
}

PPC_OP(addco)
{
    T2 = T0;
    T0 += T1;
    if (T0 < T2) {
        xer_ca = 1;
    } else {
        xer_ca = 0;
    }
    if ((T2 ^ T1 ^ (-1)) & (T2 ^ T0) & (1 << 31)) {
        xer_so = 1;
        xer_ov = 1;
    } else {
        xer_ov = 0;
    }
    RETURN();
}

/* add extended */
/* candidate for helper (too long) */
PPC_OP(adde)
{
    T2 = T0;
    T0 += T1 + xer_ca;
    if (T0 < T2 || (xer_ca == 1 && T0 == T2)) {
        xer_ca = 1;
    } else {
        xer_ca = 0;
    }
    RETURN();
}

PPC_OP(addeo)
{
    T2 = T0;
    T0 += T1 + xer_ca;
    if (T0 < T2 || (xer_ca == 1 && T0 == T2)) {
        xer_ca = 1;
    } else {
        xer_ca = 0;
    }
    if ((T2 ^ T1 ^ (-1)) & (T2 ^ T0) & (1 << 31)) {
        xer_so = 1;
        xer_ov = 1;
    } else {
        xer_ov = 0;
    }
    RETURN();
}

/* add immediate */
PPC_OP(addi)
{
    T0 += PARAM(1);
    RETURN();
}

/* add immediate carrying */
PPC_OP(addic)
{
    T1 = T0;
    T0 += PARAM(1);
    if (T0 < T1) {
        xer_ca = 1;
    } else {
        xer_ca = 0;
    }
    RETURN();
}

/* add to minus one extended */
PPC_OP(addme)
{
    T1 = T0;
    T0 += xer_ca + (-1);
    if (T1 != 0)
        xer_ca = 1;
    RETURN();
}

PPC_OP(addmeo)
{
    T1 = T0;
    T0 += xer_ca + (-1);
    if (T1 & (T1 ^ T0) & (1 << 31)) {
        xer_so = 1;
        xer_ov = 1;
    } else {
        xer_ov = 0;
    }
    if (T1 != 0)
        xer_ca = 1;
    RETURN();
}

/* add to zero extended */
PPC_OP(addze)
{
    T1 = T0;
    T0 += xer_ca;
    if (T0 < T1) {
        xer_ca = 1;
    } else {
        xer_ca = 0;
    }
    RETURN();
}

PPC_OP(addzeo)
{
    T1 = T0;
    T0 += xer_ca;
    if ((T1 ^ (-1)) & (T1 ^ T0) & (1 << 31)) {
        xer_so = 1;
        xer_ov = 1;
    } else {
        xer_ov = 0;
    }
    if (T0 < T1) {
        xer_ca = 1;
    } else {
        xer_ca = 0;
    }
    RETURN();
}

/* divide word */
/* candidate for helper (too long) */
PPC_OP(divw)
{
    if ((Ts0 == INT32_MIN && Ts1 == -1) || Ts1 == 0) {
        Ts0 = (-1) * (T0 >> 31);
    } else {
        Ts0 /= Ts1;
    }
    RETURN();
}

PPC_OP(divwo)
{
    if ((Ts0 == INT32_MIN && Ts1 == -1) || Ts1 == 0) {
        xer_so = 1;
        xer_ov = 1;
        T0 = (-1) * (T0 >> 31);
    } else {
        xer_ov = 0;
        Ts0 /= Ts1;
    }
    RETURN();
}

/* divide word unsigned */
PPC_OP(divwu)
{
    if (T1 == 0) {
        T0 = 0;
    } else {
        T0 /= T1;
    }
    RETURN();
}

PPC_OP(divwuo)
{
    if (T1 == 0) {
        xer_so = 1;
        xer_ov = 1;
        T0 = 0;
    } else {
        xer_ov = 0;
        T0 /= T1;
    }
    RETURN();
}

/* multiply high word */
PPC_OP(mulhw)
{
    Ts0 = ((int64_t)Ts0 * (int64_t)Ts1) >> 32;
    RETURN();
}

/* multiply high word unsigned */
PPC_OP(mulhwu)
{
    T0 = ((uint64_t)T0 * (uint64_t)T1) >> 32;
    RETURN();
}

/* multiply low immediate */
PPC_OP(mulli)
{
    Ts0 *= SPARAM(1);
    RETURN();
}

/* multiply low word */
PPC_OP(mullw)
{
    T0 *= T1;
    RETURN();
}

PPC_OP(mullwo)
{
    int64_t res = (int64_t)Ts0 * (int64_t)Ts1;

    if ((int32_t)res != res) {
        xer_ov = 1;
        xer_so = 1;
    } else {
        xer_ov = 0;
    }
    Ts0 = res;
    RETURN();
}

/* negate */
PPC_OP(neg)
{
    if (T0 != 0x80000000) {
        Ts0 = -Ts0;
    }
    RETURN();
}

PPC_OP(nego)
{
    if (T0 == 0x80000000) {
        xer_ov = 1;
        xer_so = 1;
    } else {
        xer_ov = 0;
        Ts0 = -Ts0;
    }
    RETURN();
}

/* substract from */
PPC_OP(subf)
{
    T0 = T1 - T0;
    RETURN();
}

PPC_OP(subfo)
{
    T2 = T0;
    T0 = T1 - T0;
    if (((~T2) ^ T1 ^ (-1)) & ((~T2) ^ T0) & (1 << 31)) {
        xer_so = 1;
        xer_ov = 1;
    } else {
        xer_ov = 0;
    }
    RETURN();
}

/* substract from carrying */
PPC_OP(subfc)
{
    T0 = T1 - T0;
    if (T0 <= T1) {
        xer_ca = 1;
    } else {
        xer_ca = 0;
    }
    RETURN();
}

PPC_OP(subfco)
{
    T2 = T0;
    T0 = T1 - T0;
    if (T0 <= T1) {
        xer_ca = 1;
    } else {
        xer_ca = 0;
    }
    if (((~T2) ^ T1 ^ (-1)) & ((~T2) ^ T0) & (1 << 31)) {
        xer_so = 1;
        xer_ov = 1;
    } else {
        xer_ov = 0;
    }
    RETURN();
}

/* substract from extended */
/* candidate for helper (too long) */
PPC_OP(subfe)
{
    T0 = T1 + ~T0 + xer_ca;
    if (T0 < T1 || (xer_ca == 1 && T0 == T1)) {
        xer_ca = 1;
    } else {
        xer_ca = 0;
    }
    RETURN();
}

PPC_OP(subfeo)
{
    T2 = T0;
    T0 = T1 + ~T0 + xer_ca;
    if ((~T2 ^ T1 ^ (-1)) & (~T2 ^ T0) & (1 << 31)) {
        xer_so = 1;
        xer_ov = 1;
    } else {
        xer_ov = 0;
    }
    if (T0 < T1 || (xer_ca == 1 && T0 == T1)) {
        xer_ca = 1;
    } else {
        xer_ca = 0;
    }
    RETURN();
}

/* substract from immediate carrying */
PPC_OP(subfic)
{
    T0 = PARAM(1) + ~T0 + 1;
    if (T0 <= PARAM(1)) {
        xer_ca = 1;
    } else {
        xer_ca = 0;
    }
    RETURN();
}

/* substract from minus one extended */
PPC_OP(subfme)
{
    T0 = ~T0 + xer_ca - 1;

    if (T0 != -1)
        xer_ca = 1;
    RETURN();
}

PPC_OP(subfmeo)
{
    T1 = T0;
    T0 = ~T0 + xer_ca - 1;
    if (~T1 & (~T1 ^ T0) & (1 << 31)) {
        xer_so = 1;
        xer_ov = 1;
    } else {
        xer_ov = 0;
    }
    if (T1 != -1)
        xer_ca = 1;
    RETURN();
}

/* substract from zero extended */
PPC_OP(subfze)
{
    T1 = ~T0;
    T0 = T1 + xer_ca;
    if (T0 < T1) {
        xer_ca = 1;
    } else {
        xer_ca = 0;
    }
    RETURN();
}

PPC_OP(subfzeo)
{
    T1 = T0;
    T0 = ~T0 + xer_ca;
    if ((~T1 ^ (-1)) & ((~T1) ^ T0) & (1 << 31)) {
        xer_ov = 1;
        xer_so = 1;
    } else {
        xer_ov = 0;
    }
    if (T0 < ~T1) {
        xer_ca = 1;
    } else {
        xer_ca = 0;
    }
    RETURN();
}

/***                           Integer comparison                          ***/
/* compare */
PPC_OP(cmp)
{
    if (Ts0 < Ts1) {
        T0 = 0x08;
    } else if (Ts0 > Ts1) {
        T0 = 0x04;
    } else {
        T0 = 0x02;
    }
    RETURN();
}

/* compare immediate */
PPC_OP(cmpi)
{
    if (Ts0 < SPARAM(1)) {
        T0 = 0x08;
    } else if (Ts0 > SPARAM(1)) {
        T0 = 0x04;
    } else {
        T0 = 0x02;
    }
    RETURN();
}

/* compare logical */
PPC_OP(cmpl)
{
    if (T0 < T1) {
        T0 = 0x08;
    } else if (T0 > T1) {
        T0 = 0x04;
    } else {
        T0 = 0x02;
    }
    RETURN();
}

/* compare logical immediate */
PPC_OP(cmpli)
{
    if (T0 < PARAM(1)) {
        T0 = 0x08;
    } else if (T0 > PARAM(1)) {
        T0 = 0x04;
    } else {
        T0 = 0x02;
    }
    RETURN();
}

/***                            Integer logical                            ***/
/* and */
PPC_OP(and)
{
    T0 &= T1;
    RETURN();
}

/* andc */
PPC_OP(andc)
{
    T0 &= ~T1;
    RETURN();
}

/* andi. */
PPC_OP(andi_)
{
    T0 &= PARAM(1);
    RETURN();
}

/* count leading zero */
PPC_OP(cntlzw)
{
    T1 = T0;
    for (T0 = 32; T1 > 0; T0--)
        T1 = T1 >> 1;
    RETURN();
}

/* eqv */
PPC_OP(eqv)
{
    T0 = ~(T0 ^ T1);
    RETURN();
}

/* extend sign byte */
PPC_OP(extsb)
{
    Ts0 = s_ext8(Ts0);
    RETURN();
}

/* extend sign half word */
PPC_OP(extsh)
{
    Ts0 = s_ext16(Ts0);
    RETURN();
}

/* nand */
PPC_OP(nand)
{
    T0 = ~(T0 & T1);
    RETURN();
}

/* nor */
PPC_OP(nor)
{
    T0 = ~(T0 | T1);
    RETURN();
}

/* or */
PPC_OP(or)
{
    T0 |= T1;
    RETURN();
}

/* orc */
PPC_OP(orc)
{
    T0 |= ~T1;
    RETURN();
}

/* ori */
PPC_OP(ori)
{
    T0 |= PARAM(1);
    RETURN();
}

/* xor */
PPC_OP(xor)
{
    T0 ^= T1;
    RETURN();
}

/* xori */
PPC_OP(xori)
{
    T0 ^= PARAM(1);
    RETURN();
}

/***                             Integer rotate                            ***/
/* rotate left word immediate then mask insert */
PPC_OP(rlwimi)
{
    T0 = (rotl(T0, PARAM(1)) & PARAM(2)) | (T1 & PARAM(3));
    RETURN();
}

/* rotate left immediate then and with mask insert */
PPC_OP(rotlwi)
{
    T0 = rotl(T0, PARAM(1));
    RETURN();
}

PPC_OP(slwi)
{
    T0 = T0 << PARAM(1);
    RETURN();
}

PPC_OP(srwi)
{
    T0 = T0 >> PARAM(1);
    RETURN();
}

/* rotate left word then and with mask insert */
PPC_OP(rlwinm)
{
    T0 = rotl(T0, PARAM(1)) & PARAM(2);
    RETURN();
}

PPC_OP(rotl)
{
    T0 = rotl(T0, T1);
    RETURN();
}

PPC_OP(rlwnm)
{
    T0 = rotl(T0, T1) & PARAM(1);
    RETURN();
}

/***                             Integer shift                             ***/
/* shift left word */
PPC_OP(slw)
{
    if (T1 & 0x20) {
        T0 = 0;
    } else {
        T0 = T0 << T1;
    }
    RETURN();
}

/* shift right algebraic word */
PPC_OP(sraw)
{
    do_sraw();
    RETURN();
}

/* shift right algebraic word immediate */
PPC_OP(srawi)
{
    Ts1 = Ts0;
    Ts0 = Ts0 >> PARAM(1);
    if (Ts1 < 0 && (Ts1 & PARAM(2)) != 0) {
        xer_ca = 1;
    } else {
        xer_ca = 0;
    }
    RETURN();
}

/* shift right word */
PPC_OP(srw)
{
    if (T1 & 0x20) {
        T0 = 0;
    } else {
        T0 = T0 >> T1;
    }
    RETURN();
}

/***                       Floating-Point arithmetic                       ***/
/* fadd - fadd. */
PPC_OP(fadd)
{
    FT0 += FT1;
    RETURN();
}

/* fadds - fadds. */
PPC_OP(fadds)
{
    FTS0 += FTS1;
    RETURN();
}

/* fsub - fsub. */
PPC_OP(fsub)
{
    FT0 -= FT1;
    RETURN();
}

/* fsubs - fsubs. */
PPC_OP(fsubs)
{
    FTS0 -= FTS1;
    RETURN();
}

/* fmul - fmul. */
PPC_OP(fmul)
{
    FT0 *= FT1;
    RETURN();
}

/* fmuls - fmuls. */
PPC_OP(fmuls)
{
    FTS0 *= FTS1;
    RETURN();
}

/* fdiv - fdiv. */
PPC_OP(fdiv)
{
    FT0 /= FT1;
    RETURN();
}

/* fdivs - fdivs. */
PPC_OP(fdivs)
{
    FTS0 /= FTS1;
    RETURN();
}

/* fsqrt - fsqrt. */
PPC_OP(fsqrt)
{
    do_fsqrt();
    RETURN();
}

/* fsqrts - fsqrts. */
PPC_OP(fsqrts)
{
    do_fsqrts();
    RETURN();
}

/* fres - fres. */
PPC_OP(fres)
{
    do_fres();
    RETURN();
}

/* frsqrte  - frsqrte. */
PPC_OP(frsqrte)
{
    do_fsqrte();
    RETURN();
}

/* fsel - fsel. */
PPC_OP(fsel)
{
    do_fsel();
    RETURN();
}

/***                     Floating-Point multiply-and-add                   ***/
/* fmadd - fmadd. */
PPC_OP(fmadd)
{
    FT0 = (FT0 * FT1) + FT2;
    RETURN();
}

/* fmadds - fmadds. */
PPC_OP(fmadds)
{
    FTS0 = (FTS0 * FTS1) + FTS2;
    RETURN();
}

/* fmsub - fmsub. */
PPC_OP(fmsub)
{
    FT0 = (FT0 * FT1) - FT2;
    RETURN();
}

/* fmsubs - fmsubs. */
PPC_OP(fmsubs)
{
    FTS0 = (FTS0 * FTS1) - FTS2;
    RETURN();
}

/* fnmadd - fnmadd. - fnmadds - fnmadds. */
PPC_OP(fnmadd)
{
    do_fnmadd();
    RETURN();
}

/* fnmadds - fnmadds. */
PPC_OP(fnmadds)
{
    do_fnmadds();
    RETURN();
}

/* fnmsub - fnmsub. */
PPC_OP(fnmsub)
{
    do_fnmsub();
    RETURN();
}

/* fnmsubs - fnmsubs. */
PPC_OP(fnmsubs)
{
    do_fnmsubs();
    RETURN();
}

/***                     Floating-Point round & convert                    ***/
/* frsp - frsp. */
PPC_OP(frsp)
{
    FTS0 = FT0;
    RETURN();
}

/* fctiw - fctiw. */
PPC_OP(fctiw)
{
    do_fctiw();
    RETURN();
}

/* fctiwz - fctiwz. */
PPC_OP(fctiwz)
{
    do_fctiwz();
    RETURN();
}


/***                         Floating-Point compare                        ***/
/* fcmpu */
PPC_OP(fcmpu)
{
    do_fcmpu();
    RETURN();
}

/* fcmpo */
PPC_OP(fcmpo)
{
    do_fcmpo();
    RETURN();
}

/***                         Floating-point move                           ***/
/* fabs */
PPC_OP(fabs)
{
    do_fabs();
    RETURN();
}

/* fnabs */
PPC_OP(fnabs)
{
    do_fnabs();
    RETURN();
}

/* fneg */
PPC_OP(fneg)
{
    FT0 = -FT0;
    RETURN();
}

/* Load and store */
#define MEMSUFFIX _raw
#include "op_mem.h"
#if !defined(CONFIG_USER_ONLY)
#define MEMSUFFIX _user
#include "op_mem.h"

#define MEMSUFFIX _kernel
#include "op_mem.h"
#endif

/* Special op to check and maybe clear reservation */
PPC_OP(check_reservation)
{
    do_check_reservation();
    RETURN();
}

/* Return from interrupt */
PPC_OP(rfi)
{
    regs->nip = regs->spr[SRR0] & ~0x00000003;
#if 1 // TRY
    T0 = regs->spr[SRR1] & ~0xFFF00000;
#else
    T0 = regs->spr[SRR1] & ~0xFFFF0000;
#endif
    do_store_msr();
#if defined (DEBUG_OP)
    dump_rfi();
#endif
    //    do_tlbia();
    do_raise_exception(EXCP_RFI);
    RETURN();
}

/* Trap word */
PPC_OP(tw)
{
    if ((Ts0 < Ts1 && (PARAM(1) & 0x10)) ||
        (Ts0 > Ts1 && (PARAM(1) & 0x08)) ||
        (Ts0 == Ts1 && (PARAM(1) & 0x04)) ||
        (T0 < T1 && (PARAM(1) & 0x02)) ||
        (T0 > T1 && (PARAM(1) & 0x01)))
        do_raise_exception_err(EXCP_PROGRAM, EXCP_TRAP);
    RETURN();
}

PPC_OP(twi)
{
    if ((Ts0 < SPARAM(1) && (PARAM(2) & 0x10)) ||
        (Ts0 > SPARAM(1) && (PARAM(2) & 0x08)) ||
        (Ts0 == SPARAM(1) && (PARAM(2) & 0x04)) ||
        (T0 < (uint32_t)SPARAM(1) && (PARAM(2) & 0x02)) ||
        (T0 > (uint32_t)SPARAM(1) && (PARAM(2) & 0x01)))
        do_raise_exception_err(EXCP_PROGRAM, EXCP_TRAP);
    RETURN();
}

/* Instruction cache block invalidate */
PPC_OP(icbi)
{
    do_icbi();
    RETURN();
}

/* tlbia */
PPC_OP(tlbia)
{
    do_tlbia();
    RETURN();
}

/* tlbie */
PPC_OP(tlbie)
{
    do_tlbie();
    RETURN();
}
