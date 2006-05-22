/*
 *  PowerPC emulation micro-operations for qemu.
 * 
 *  Copyright (c) 2003-2005 Jocelyn Mayer
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

/* PowerPC state maintenance operations */
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
    do_raise_exception(EXCP_DEBUG);
}

/* Segment registers load and store with immediate index */
PPC_OP(load_srin)
{
    T0 = regs->sr[T1 >> 28];
    RETURN();
}

PPC_OP(store_srin)
{
    do_store_sr(env, ((uint32_t)T1 >> 28), T0);
    RETURN();
}

PPC_OP(load_sdr1)
{
    T0 = regs->sdr1;
    RETURN();
}

PPC_OP(store_sdr1)
{
    do_store_sdr1(env, T0);
    RETURN();
}

PPC_OP(exit_tb)
{
    EXIT_TB();
}

/* Load/store special registers */
PPC_OP(load_cr)
{
    T0 = do_load_cr(env);
    RETURN();
}

PPC_OP(store_cr)
{
    do_store_cr(env, T0, PARAM(1));
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
    T0 = do_load_xer(env);
    RETURN();
}

PPC_OP(store_xer)
{
    do_store_xer(env, T0);
    RETURN();
}

PPC_OP(load_msr)
{
    T0 = do_load_msr(env);
    RETURN();
}

PPC_OP(store_msr)
{
    do_store_msr(env, T0);
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

void op_store_ibatu (void)
{
    do_store_ibatu(env, PARAM1, T0);
    RETURN();
}

void op_store_ibatl (void)
{
#if 1
    env->IBAT[1][PARAM1] = T0;
#else
    do_store_ibatl(env, PARAM1, T0);
#endif
    RETURN();
}

PPC_OP(load_dbat)
{
    T0 = regs->DBAT[PARAM(1)][PARAM(2)];
}

void op_store_dbatu (void)
{
    do_store_dbatu(env, PARAM1, T0);
    RETURN();
}

void op_store_dbatl (void)
{
#if 1
    env->DBAT[1][PARAM1] = T0;
#else
    do_store_dbatl(env, PARAM1, T0);
#endif
    RETURN();
}

/* FPSCR */
PPC_OP(load_fpscr)
{
    FT0 = do_load_fpscr(env);
    RETURN();
}

PPC_OP(store_fpscr)
{
    do_store_fpscr(env, FT0, PARAM1);
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

PPC_OP(goto_tb0)
{
    GOTO_TB(op_goto_tb0, PARAM1, 0);
}

PPC_OP(goto_tb1)
{
    GOTO_TB(op_goto_tb1, PARAM1, 1);
}

PPC_OP(b_T1)
{
    regs->nip = T1 & ~3;
}

PPC_OP(jz_T0)
{
    if (!T0)
        GOTO_LABEL_PARAM(1);
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

void do_addo (void);
void op_addo (void)
{
    do_addo();
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

void do_addco (void);
void op_addco (void)
{
    do_addco();
    RETURN();
}

/* add extended */
void do_adde (void);
void op_adde (void)
{
    do_adde();
}

void do_addeo (void);
PPC_OP(addeo)
{
    do_addeo();
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

void do_addmeo (void);
void op_addmeo (void)
{
    do_addmeo();
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

void do_addzeo (void);
void op_addzeo (void)
{
    do_addzeo();
    RETURN();
}

/* divide word */
PPC_OP(divw)
{
    if ((Ts0 == INT32_MIN && Ts1 == -1) || Ts1 == 0) {
        T0 = (int32_t)((-1) * (T0 >> 31));
    } else {
        T0 = (Ts0 / Ts1);
    }
    RETURN();
}

void do_divwo (void);
void op_divwo (void)
{
    do_divwo();
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

void do_divwuo (void);
void op_divwuo (void)
{
    do_divwuo();
    RETURN();
}

/* multiply high word */
PPC_OP(mulhw)
{
    T0 = ((int64_t)Ts0 * (int64_t)Ts1) >> 32;
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
    T0 = (Ts0 * SPARAM(1));
    RETURN();
}

/* multiply low word */
PPC_OP(mullw)
{
    T0 *= T1;
    RETURN();
}

void do_mullwo (void);
void op_mullwo (void)
{
    do_mullwo();
    RETURN();
}

/* negate */
PPC_OP(neg)
{
    if (T0 != 0x80000000) {
        T0 = -Ts0;
    }
    RETURN();
}

void do_nego (void);
void op_nego (void)
{
    do_nego();
    RETURN();
}

/* substract from */
PPC_OP(subf)
{
    T0 = T1 - T0;
    RETURN();
}

void do_subfo (void);
void op_subfo (void)
{
    do_subfo();
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

void do_subfco (void);
void op_subfco (void)
{
    do_subfco();
    RETURN();
}

/* substract from extended */
void do_subfe (void);
void op_subfe (void)
{
    do_subfe();
    RETURN();
}

void do_subfeo (void);
PPC_OP(subfeo)
{
    do_subfeo();
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

void do_subfmeo (void);
void op_subfmeo (void)
{
    do_subfmeo();
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

void do_subfzeo (void);
void op_subfzeo (void)
{
    do_subfzeo();
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
    T0 = (int32_t)((int8_t)(Ts0));
    RETURN();
}

/* extend sign half word */
PPC_OP(extsh)
{
    T0 = (int32_t)((int16_t)(Ts0));
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
void op_sraw (void)
{
    do_sraw();
    RETURN();
}

/* shift right algebraic word immediate */
PPC_OP(srawi)
{
    T1 = T0;
    T0 = (Ts0 >> PARAM(1));
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

/* fsub - fsub. */
PPC_OP(fsub)
{
    FT0 -= FT1;
    RETURN();
}

/* fmul - fmul. */
PPC_OP(fmul)
{
    FT0 *= FT1;
    RETURN();
}

/* fdiv - fdiv. */
PPC_OP(fdiv)
{
    FT0 = float64_div(FT0, FT1, &env->fp_status);
    RETURN();
}

/* fsqrt - fsqrt. */
PPC_OP(fsqrt)
{
    do_fsqrt();
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
    do_frsqrte();
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

/* fmsub - fmsub. */
PPC_OP(fmsub)
{
    FT0 = (FT0 * FT1) - FT2;
    RETURN();
}

/* fnmadd - fnmadd. - fnmadds - fnmadds. */
PPC_OP(fnmadd)
{
    do_fnmadd();
    RETURN();
}

/* fnmsub - fnmsub. */
PPC_OP(fnmsub)
{
    do_fnmsub();
    RETURN();
}

/***                     Floating-Point round & convert                    ***/
/* frsp - frsp. */
PPC_OP(frsp)
{
    FT0 = (float)FT0;
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
    FT0 = float64_abs(FT0);
    RETURN();
}

/* fnabs */
PPC_OP(fnabs)
{
    FT0 = float64_abs(FT0);
    FT0 = float64_chs(FT0);
    RETURN();
}

/* fneg */
PPC_OP(fneg)
{
    FT0 = float64_chs(FT0);
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
    if ((uint32_t)env->reserve == (uint32_t)(T0 & ~0x00000003))
        env->reserve = -1;
    RETURN();
}

/* Return from interrupt */
void do_rfi (void);
void op_rfi (void)
{
    do_rfi();
    RETURN();
}

/* Trap word */
void do_tw (uint32_t cmp, int flags);
void op_tw (void)
{
    do_tw(T1, PARAM(1));
    RETURN();
}

void op_twi (void)
{
    do_tw(PARAM(1), PARAM(2));
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

void op_store_pir (void)
{
    env->spr[SPR_PIR] = T0 & 0x0000000FUL;
    RETURN();
}
