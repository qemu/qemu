/*
 *  Alpha emulation cpu micro-operations for qemu.
 *
 *  Copyright (c) 2007 Jocelyn Mayer
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

#define DEBUG_OP

#include "config.h"
#include "exec.h"
#include "host-utils.h"

#include "op_helper.h"

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

/* Debug stuff */
void OPPROTO op_no_op (void)
{
#if !defined (DEBUG_OP)
    __asm__ __volatile__("nop" : : : "memory");
#endif
    RETURN();
}

/* Load and stores */
#define MEMSUFFIX _raw
#include "op_mem.h"
#if !defined(CONFIG_USER_ONLY)
#define MEMSUFFIX _kernel
#include "op_mem.h"
#define MEMSUFFIX _executive
#include "op_mem.h"
#define MEMSUFFIX _supervisor
#include "op_mem.h"
#define MEMSUFFIX _user
#include "op_mem.h"
/* This is used for pal modes */
#define MEMSUFFIX _data
#include "op_mem.h"
#endif

/* Special operation for load and store */
void OPPROTO op_n7 (void)
{
    T0 &= ~(uint64_t)0x7;
    RETURN();
}

/* Misc */
void OPPROTO op_excp (void)
{
    helper_excp(PARAM(1), PARAM(2));
    RETURN();
}

void OPPROTO op_load_amask (void)
{
    helper_amask();
    RETURN();
}

void OPPROTO op_load_pcc (void)
{
    helper_load_pcc();
    RETURN();
}

void OPPROTO op_load_implver (void)
{
    helper_load_implver();
    RETURN();
}

void OPPROTO op_load_fpcr (void)
{
    helper_load_fpcr();
    RETURN();
}

void OPPROTO op_store_fpcr (void)
{
    helper_store_fpcr();
    RETURN();
}

void OPPROTO op_load_irf (void)
{
    helper_load_irf();
    RETURN();
}

void OPPROTO op_set_irf (void)
{
    helper_set_irf();
    RETURN();
}

void OPPROTO op_clear_irf (void)
{
    helper_clear_irf();
    RETURN();
}

/* Arithmetic */
void OPPROTO op_addq (void)
{
    T0 += T1;
    RETURN();
}

void OPPROTO op_addqv (void)
{
    helper_addqv();
    RETURN();
}

void OPPROTO op_addl (void)
{
    T0 = (int64_t)((int32_t)(T0 + T1));
    RETURN();
}

void OPPROTO op_addlv (void)
{
    helper_addlv();
    RETURN();
}

void OPPROTO op_subq (void)
{
    T0 -= T1;
    RETURN();
}

void OPPROTO op_subqv (void)
{
    helper_subqv();
    RETURN();
}

void OPPROTO op_subl (void)
{
    T0 = (int64_t)((int32_t)(T0 - T1));
    RETURN();
}

void OPPROTO op_sublv (void)
{
    helper_sublv();
    RETURN();
}

void OPPROTO op_s4 (void)
{
    T0 <<= 2;
    RETURN();
}

void OPPROTO op_s8 (void)
{
    T0 <<= 3;
    RETURN();
}

void OPPROTO op_mull (void)
{
    T0 = (int64_t)((int32_t)T0 * (int32_t)T1);
    RETURN();
}

void OPPROTO op_mullv (void)
{
    helper_mullv();
    RETURN();
}

void OPPROTO op_mulq (void)
{
    T0 = (int64_t)T0 * (int64_t)T1;
    RETURN();
}

void OPPROTO op_mulqv (void)
{
    helper_mulqv();
    RETURN();
}

void OPPROTO op_umulh (void)
{
    uint64_t tl, th;

    mulu64(&tl, &th, T0, T1);
    T0 = th;
    RETURN();
}

/* Logical */
void OPPROTO op_and (void)
{
    T0 &= T1;
    RETURN();
}

void OPPROTO op_bic (void)
{
    T0 &= ~T1;
    RETURN();
}

void OPPROTO op_bis (void)
{
    T0 |= T1;
    RETURN();
}

void OPPROTO op_eqv (void)
{
    T0 ^= ~T1;
    RETURN();
}

void OPPROTO op_ornot (void)
{
    T0 |= ~T1;
    RETURN();
}

void OPPROTO op_xor (void)
{
    T0 ^= T1;
    RETURN();
}

void OPPROTO op_sll (void)
{
    T0 <<= T1;
    RETURN();
}

void OPPROTO op_srl (void)
{
    T0 >>= T1;
    RETURN();
}

void OPPROTO op_sra (void)
{
    T0 = (int64_t)T0 >> T1;
    RETURN();
}

void OPPROTO op_sextb (void)
{
    T0 = (int64_t)((int8_t)T0);
    RETURN();
}

void OPPROTO op_sextw (void)
{
    T0 = (int64_t)((int16_t)T0);
    RETURN();

}

void OPPROTO op_ctpop (void)
{
    helper_ctpop();
    RETURN();
}

void OPPROTO op_ctlz (void)
{
    helper_ctlz();
    RETURN();
}

void OPPROTO op_cttz (void)
{
    helper_cttz();
    RETURN();
}

void OPPROTO op_mskbl (void)
{
    helper_mskbl();
    RETURN();
}

void OPPROTO op_extbl (void)
{
    helper_extbl();
    RETURN();
}

void OPPROTO op_insbl (void)
{
    helper_insbl();
    RETURN();
}

void OPPROTO op_mskwl (void)
{
    helper_mskwl();
    RETURN();
}

void OPPROTO op_extwl (void)
{
    helper_extwl();
    RETURN();
}

void OPPROTO op_inswl (void)
{
    helper_inswl();
    RETURN();
}

void OPPROTO op_mskll (void)
{
    helper_mskll();
    RETURN();
}

void OPPROTO op_extll (void)
{
    helper_extll();
    RETURN();
}

void OPPROTO op_insll (void)
{
    helper_insll();
    RETURN();
}

void OPPROTO op_zap (void)
{
    helper_zap();
    RETURN();
}

void OPPROTO op_zapnot (void)
{
    helper_zapnot();
    RETURN();
}

void OPPROTO op_mskql (void)
{
    helper_mskql();
    RETURN();
}

void OPPROTO op_extql (void)
{
    helper_extql();
    RETURN();
}

void OPPROTO op_insql (void)
{
    helper_insql();
    RETURN();
}

void OPPROTO op_mskwh (void)
{
    helper_mskwh();
    RETURN();
}

void OPPROTO op_inswh (void)
{
    helper_inswh();
    RETURN();
}

void OPPROTO op_extwh (void)
{
    helper_extwh();
    RETURN();
}

void OPPROTO op_msklh (void)
{
    helper_msklh();
    RETURN();
}

void OPPROTO op_inslh (void)
{
    helper_inslh();
    RETURN();
}

void OPPROTO op_extlh (void)
{
    helper_extlh();
    RETURN();
}

void OPPROTO op_mskqh (void)
{
    helper_mskqh();
    RETURN();
}

void OPPROTO op_insqh (void)
{
    helper_insqh();
    RETURN();
}

void OPPROTO op_extqh (void)
{
    helper_extqh();
    RETURN();
}

/* Tests */
void OPPROTO op_cmpult (void)
{
    if (T0 < T1)
        T0 = 1;
    else
        T0 = 0;
    RETURN();
}

void OPPROTO op_cmpule (void)
{
    if (T0 <= T1)
        T0 = 1;
    else
        T0 = 0;
    RETURN();
}

void OPPROTO op_cmpeq (void)
{
    if (T0 == T1)
        T0 = 1;
    else
        T0 = 0;
    RETURN();
}

void OPPROTO op_cmplt (void)
{
    if ((int64_t)T0 < (int64_t)T1)
        T0 = 1;
    else
        T0 = 0;
    RETURN();
}

void OPPROTO op_cmple (void)
{
    if ((int64_t)T0 <= (int64_t)T1)
        T0 = 1;
    else
        T0 = 0;
    RETURN();
}

void OPPROTO op_cmpbge (void)
{
    helper_cmpbge();
    RETURN();
}

void OPPROTO op_cmpeqz (void)
{
    if (T0 == 0)
        T0 = 1;
    else
        T0 = 0;
    RETURN();
}

void OPPROTO op_cmpnez (void)
{
    if (T0 != 0)
        T0 = 1;
    else
        T0 = 0;
    RETURN();
}

void OPPROTO op_cmpltz (void)
{
    if ((int64_t)T0 < 0)
        T0 = 1;
    else
        T0 = 0;
    RETURN();
}

void OPPROTO op_cmplez (void)
{
    if ((int64_t)T0 <= 0)
        T0 = 1;
    else
        T0 = 0;
    RETURN();
}

void OPPROTO op_cmpgtz (void)
{
    if ((int64_t)T0 > 0)
        T0 = 1;
    else
        T0 = 0;
    RETURN();
}

void OPPROTO op_cmpgez (void)
{
    if ((int64_t)T0 >= 0)
        T0 = 1;
    else
        T0 = 0;
    RETURN();
}

void OPPROTO op_cmplbs (void)
{
    T0 &= 1;
    RETURN();
}

void OPPROTO op_cmplbc (void)
{
    T0 = (~T0) & 1;
    RETURN();
}

/* Branches */
void OPPROTO op_branch (void)
{
    env->pc = T0 & ~3;
    RETURN();
}

void OPPROTO op_addq1 (void)
{
    T1 += T0;
    RETURN();
}

#if 0 // Qemu does not know how to do this...
void OPPROTO op_bcond (void)
{
    if (T0)
        env->pc = T1 & ~3;
    else
        env->pc = PARAM(1);
    RETURN();
}
#else
void OPPROTO op_bcond (void)
{
    if (T0)
        env->pc = T1 & ~3;
    else
        env->pc = ((uint64_t)PARAM(1) << 32) | (uint64_t)PARAM(2);
    RETURN();
}
#endif

/* IEEE floating point arithmetic */
/* S floating (single) */
void OPPROTO op_adds (void)
{
    FT0 = float32_add(FT0, FT1, &FP_STATUS);
    RETURN();
}

void OPPROTO op_subs (void)
{
    FT0 = float32_sub(FT0, FT1, &FP_STATUS);
    RETURN();
}

void OPPROTO op_muls (void)
{
    FT0 = float32_mul(FT0, FT1, &FP_STATUS);
    RETURN();
}

void OPPROTO op_divs (void)
{
    FT0 = float32_div(FT0, FT1, &FP_STATUS);
    RETURN();
}

void OPPROTO op_sqrts (void)
{
    helper_sqrts();
    RETURN();
}

void OPPROTO op_cpys (void)
{
    helper_cpys();
    RETURN();
}

void OPPROTO op_cpysn (void)
{
    helper_cpysn();
    RETURN();
}

void OPPROTO op_cpyse (void)
{
    helper_cpyse();
    RETURN();
}

void OPPROTO op_itofs (void)
{
    helper_itofs();
    RETURN();
}

void OPPROTO op_ftois (void)
{
    helper_ftois();
    RETURN();
}

/* T floating (double) */
void OPPROTO op_addt (void)
{
    FT0 = float64_add(FT0, FT1, &FP_STATUS);
    RETURN();
}

void OPPROTO op_subt (void)
{
    FT0 = float64_sub(FT0, FT1, &FP_STATUS);
    RETURN();
}

void OPPROTO op_mult (void)
{
    FT0 = float64_mul(FT0, FT1, &FP_STATUS);
    RETURN();
}

void OPPROTO op_divt (void)
{
    FT0 = float64_div(FT0, FT1, &FP_STATUS);
    RETURN();
}

void OPPROTO op_sqrtt (void)
{
    helper_sqrtt();
    RETURN();
}

void OPPROTO op_cmptun (void)
{
    helper_cmptun();
    RETURN();
}

void OPPROTO op_cmpteq (void)
{
    helper_cmpteq();
    RETURN();
}

void OPPROTO op_cmptle (void)
{
    helper_cmptle();
    RETURN();
}

void OPPROTO op_cmptlt (void)
{
    helper_cmptlt();
    RETURN();
}

void OPPROTO op_itoft (void)
{
    helper_itoft();
    RETURN();
}

void OPPROTO op_ftoit (void)
{
    helper_ftoit();
    RETURN();
}

/* VAX floating point arithmetic */
/* F floating */
void OPPROTO op_addf (void)
{
    helper_addf();
    RETURN();
}

void OPPROTO op_subf (void)
{
    helper_subf();
    RETURN();
}

void OPPROTO op_mulf (void)
{
    helper_mulf();
    RETURN();
}

void OPPROTO op_divf (void)
{
    helper_divf();
    RETURN();
}

void OPPROTO op_sqrtf (void)
{
    helper_sqrtf();
    RETURN();
}

void OPPROTO op_cmpfeq (void)
{
    helper_cmpfeq();
    RETURN();
}

void OPPROTO op_cmpfne (void)
{
    helper_cmpfne();
    RETURN();
}

void OPPROTO op_cmpflt (void)
{
    helper_cmpflt();
    RETURN();
}

void OPPROTO op_cmpfle (void)
{
    helper_cmpfle();
    RETURN();
}

void OPPROTO op_cmpfgt (void)
{
    helper_cmpfgt();
    RETURN();
}

void OPPROTO op_cmpfge (void)
{
    helper_cmpfge();
    RETURN();
}

void OPPROTO op_itoff (void)
{
    helper_itoff();
    RETURN();
}

/* G floating */
void OPPROTO op_addg (void)
{
    helper_addg();
    RETURN();
}

void OPPROTO op_subg (void)
{
    helper_subg();
    RETURN();
}

void OPPROTO op_mulg (void)
{
    helper_mulg();
    RETURN();
}

void OPPROTO op_divg (void)
{
    helper_divg();
    RETURN();
}

void OPPROTO op_sqrtg (void)
{
    helper_sqrtg();
    RETURN();
}

void OPPROTO op_cmpgeq (void)
{
    helper_cmpgeq();
    RETURN();
}

void OPPROTO op_cmpglt (void)
{
    helper_cmpglt();
    RETURN();
}

void OPPROTO op_cmpgle (void)
{
    helper_cmpgle();
    RETURN();
}

/* Floating point format conversion */
void OPPROTO op_cvtst (void)
{
    FT0 = (float)FT0;
    RETURN();
}

void OPPROTO op_cvtqs (void)
{
    helper_cvtqs();
    RETURN();
}

void OPPROTO op_cvtts (void)
{
    FT0 = (float)FT0;
    RETURN();
}

void OPPROTO op_cvttq (void)
{
    helper_cvttq();
    RETURN();
}

void OPPROTO op_cvtqt (void)
{
    helper_cvtqt();
    RETURN();
}

void OPPROTO op_cvtqf (void)
{
    helper_cvtqf();
    RETURN();
}

void OPPROTO op_cvtgf (void)
{
    helper_cvtgf();
    RETURN();
}

void OPPROTO op_cvtgd (void)
{
    helper_cvtgd();
    RETURN();
}

void OPPROTO op_cvtgq (void)
{
    helper_cvtgq();
    RETURN();
}

void OPPROTO op_cvtqg (void)
{
    helper_cvtqg();
    RETURN();
}

void OPPROTO op_cvtdg (void)
{
    helper_cvtdg();
    RETURN();
}

void OPPROTO op_cvtlq (void)
{
    helper_cvtlq();
    RETURN();
}

void OPPROTO op_cvtql (void)
{
    helper_cvtql();
    RETURN();
}

void OPPROTO op_cvtqlv (void)
{
    helper_cvtqlv();
    RETURN();
}

void OPPROTO op_cvtqlsv (void)
{
    helper_cvtqlsv();
    RETURN();
}

/* PALcode support special instructions */
#if !defined (CONFIG_USER_ONLY)
void OPPROTO op_hw_rei (void)
{
    env->pc = env->ipr[IPR_EXC_ADDR] & ~3;
    env->ipr[IPR_EXC_ADDR] = env->ipr[IPR_EXC_ADDR] & 1;
    /* XXX: re-enable interrupts and memory mapping */
    RETURN();
}

void OPPROTO op_hw_ret (void)
{
    env->pc = T0 & ~3;
    env->ipr[IPR_EXC_ADDR] = T0 & 1;
    /* XXX: re-enable interrupts and memory mapping */
    RETURN();
}

void OPPROTO op_mfpr (void)
{
    helper_mfpr(PARAM(1));
    RETURN();
}

void OPPROTO op_mtpr (void)
{
    helper_mtpr(PARAM(1));
    RETURN();
}

void OPPROTO op_set_alt_mode (void)
{
    env->saved_mode = env->ps & 0xC;
    env->ps = (env->ps & ~0xC) | (env->ipr[IPR_ALT_MODE] & 0xC);
    RETURN();
}

void OPPROTO op_restore_mode (void)
{
    env->ps = (env->ps & ~0xC) | env->saved_mode;
    RETURN();
}

void OPPROTO op_ld_phys_to_virt (void)
{
    helper_ld_phys_to_virt();
    RETURN();
}

void OPPROTO op_st_phys_to_virt (void)
{
    helper_st_phys_to_virt();
    RETURN();
}
#endif /* !defined (CONFIG_USER_ONLY) */
