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

#include "config.h"
#include "exec.h"

#define regs (env)
#define Ts0 (int32_t)T0
#define Ts1 (int32_t)T1
#define Ts2 (int32_t)T2

#define FT0 (env->ft0)

#define PPC_OP(name) void op_##name(void)

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
    set_CRn(0, tmp);
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
    set_CRn(0, tmp);
    RETURN();
}

/* reset_Rc0 */
PPC_OP(reset_Rc0)
{
    set_CRn(0, 0x02 | xer_ov);
    RETURN();
}

/* set_Rc0_1 */
PPC_OP(set_Rc0_1)
{
    set_CRn(0, 0x04 | xer_ov);
    RETURN();
}

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

/* Update time base */
PPC_OP(update_tb)
{
    T0 = regs->spr[SPR_ENCODE(268)];
    T1 = T0;
    T0 += PARAM(1);
    if (T0 < T1) {
        T1 = regs->spr[SPR_ENCODE(269)] + 1;
        regs->spr[SPR_ENCODE(269)] = T1;
    }
    regs->spr[SPR_ENCODE(268)] = T0;
    RETURN();
}

PPC_OP(raise_exception)
{
    raise_exception(PARAM(1));
    RETURN();
}

PPC_OP(exit_tb)
{
    EXIT_TB();
}

PPC_OP(load_cr)
{
    T0 = do_load_cr();
    RETURN();
}

PPC_OP(store_cr)
{
    do_store_cr(PARAM(1), T0);
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
    T0 = xer_bc;
    RETURN();
}

PPC_OP(load_xer)
{
    T0 = do_load_xer();
    RETURN();
}

PPC_OP(store_xer)
{
    do_store_xer(T0);
    RETURN();
}

PPC_OP(load_msr)
{
    T0 = do_load_msr();
    RETURN();
}

PPC_OP(store_msr)
{
    do_store_msr(T0);
    RETURN();
}

PPC_OP(load_lr)
{
    regs->LR = PARAM(1);
    RETURN();
}

/* Set reservation */
PPC_OP(set_reservation)
{
    regs->reserve = T1 & ~0x03;
    RETURN();
}

/* Reset reservation */
PPC_OP(reset_reservation)
{
    regs->reserve = 0;
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
#define __PPC_OP_B(name, target)                                              \
PPC_OP(name)                                                                  \
{                                                                             \
    regs->nip = (target);                                                     \
    RETURN();                                                                 \
}

#define __PPC_OP_BL(name, target)                                             \
PPC_OP(name)                                                                  \
{                                                                             \
    regs->LR = PARAM(1);                                                      \
    regs->nip = (target);                                                     \
    RETURN();                                                                 \
}

#define PPC_OP_B(name, target)                                                \
__PPC_OP_B(name, target);                                                     \
__PPC_OP_BL(name##l, target)

#define __PPC_OP_BC(name, cond, target)                                       \
PPC_OP(name)                                                                  \
{                                                                             \
    if (cond) {                                                               \
        T0 = (target);                                                        \
    } else {                                                                  \
        T0 = PARAM(1);                                                        \
    }                                                                         \
    regs->nip = T0;                                                           \
    RETURN();                                                                 \
}

#define __PPC_OP_BCL(name, cond, target)                                      \
PPC_OP(name)                                                                  \
{                                                                             \
    if (cond) {                                                               \
        T0 = (target);                                                        \
        regs->LR = PARAM(1);                                                  \
    } else {                                                                  \
        T0 = PARAM(1);                                                        \
    }                                                                         \
    regs->nip = T0;                                                           \
    RETURN();                                                                 \
}

#define _PPC_OP_BC(name, namel, cond, target)                                 \
__PPC_OP_BC(name, cond, target);                                              \
__PPC_OP_BCL(namel, cond, target)

/* Branch to target */
#define PPC_OP_BC(name, cond)                                                 \
_PPC_OP_BC(b_##name, bl_##name, cond, PARAM(2))

PPC_OP_B(b, PARAM(1));
PPC_OP_BC(ctr,        (regs->CTR != 0));
PPC_OP_BC(ctr_true,   (regs->CTR != 0 && (T0 & PARAM(3)) != 0));
PPC_OP_BC(ctr_false,  (regs->CTR != 0 && (T0 & PARAM(3)) == 0));
PPC_OP_BC(ctrz,       (regs->CTR == 0));
PPC_OP_BC(ctrz_true,  (regs->CTR == 0 && (T0 & PARAM(3)) != 0));
PPC_OP_BC(ctrz_false, (regs->CTR == 0 && (T0 & PARAM(3)) == 0));
PPC_OP_BC(true,       ((T0 & PARAM(3)) != 0));
PPC_OP_BC(false,      ((T0 & PARAM(3)) == 0));

/* Branch to CTR */
#define PPC_OP_BCCTR(name, cond)                                              \
_PPC_OP_BC(bctr_##name, bctrl_##name, cond, regs->CTR & ~0x03)

PPC_OP_B(bctr, regs->CTR & ~0x03);
PPC_OP_BCCTR(ctr,        (regs->CTR != 0));
PPC_OP_BCCTR(ctr_true,   (regs->CTR != 0 && (T0 & PARAM(2)) != 0));
PPC_OP_BCCTR(ctr_false,  (regs->CTR != 0 && (T0 & PARAM(2)) == 0));
PPC_OP_BCCTR(ctrz,       (regs->CTR == 0));
PPC_OP_BCCTR(ctrz_true,  (regs->CTR == 0 && (T0 & PARAM(2)) != 0));
PPC_OP_BCCTR(ctrz_false, (regs->CTR == 0 && (T0 & PARAM(2)) == 0));
PPC_OP_BCCTR(true,       ((T0 & PARAM(2)) != 0));
PPC_OP_BCCTR(false,      ((T0 & PARAM(2)) == 0));

/* Branch to LR */
#define PPC_OP_BCLR(name, cond)                                               \
_PPC_OP_BC(blr_##name, blrl_##name, cond, regs->LR & ~0x03)

PPC_OP_B(blr, regs->LR & ~0x03);
PPC_OP_BCLR(ctr,        (regs->CTR != 0));
PPC_OP_BCLR(ctr_true,   (regs->CTR != 0 && (T0 & PARAM(2)) != 0));
PPC_OP_BCLR(ctr_false,  (regs->CTR != 0 && (T0 & PARAM(2)) == 0));
PPC_OP_BCLR(ctrz,       (regs->CTR == 0));
PPC_OP_BCLR(ctrz_true,  (regs->CTR == 0 && (T0 & PARAM(2)) != 0));
PPC_OP_BCLR(ctrz_false, (regs->CTR == 0 && (T0 & PARAM(2)) == 0));
PPC_OP_BCLR(true,       ((T0 & PARAM(2)) != 0));
PPC_OP_BCLR(false,      ((T0 & PARAM(2)) == 0));

/* CTR maintenance */
PPC_OP(dec_ctr)
{
    regs->CTR--;
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
    T0 = rotl(T0, PARAM(1) & PARAM(2)) | (T0 & PARAM(3));
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
    Ts0 = do_sraw(Ts0, T1);
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

/***                     Floating-Point multiply-and-add                   ***/

/***                     Floating-Point round & convert                    ***/

/***                         Floating-Point compare                        ***/

/***                  Floating-Point status & ctrl register                ***/

/***                             Integer load                              ***/
#define ld16x(x) s_ext16(ld16(x))
#define PPC_ILD_OPX(name, op)                                                 \
PPC_OP(l##name##x_z)                                                          \
{                                                                             \
    T1 = op(T0);                                                              \
    RETURN();                                                                 \
}                                                                             \
PPC_OP(l##name##x)                                                            \
{                                                                             \
    T0 += T1;                                                                 \
    T1 = op(T0);                                                              \
    RETURN();                                                                 \
}

#define PPC_ILD_OP(name, op)                                                  \
PPC_OP(l##name##_z)                                                           \
{                                                                             \
    T1 = op(SPARAM(1));                                                       \
    RETURN();                                                                 \
}                                                                             \
PPC_OP(l##name)                                                               \
{                                                                             \
    T0 += SPARAM(1);                                                          \
    T1 = op(T0);                                                              \
    RETURN();                                                                 \
}                                                                             \
PPC_ILD_OPX(name, op)

PPC_ILD_OP(bz, ld8);
PPC_ILD_OP(ha, ld16x);
PPC_ILD_OP(hz, ld16);
PPC_ILD_OP(wz, ld32);

/***                              Integer store                            ***/
#define PPC_IST_OPX(name, op)                                                 \
PPC_OP(st##name##x_z)                                                         \
{                                                                             \
    op(T0, T1);                                                               \
    RETURN();                                                                 \
}                                                                             \
PPC_OP(st##name##x)                                                           \
{                                                                             \
    T0 += T1;                                                                 \
    op(T0, T2);                                                               \
    RETURN();                                                                 \
}

#define PPC_IST_OP(name, op)                                                  \
PPC_OP(st##name##_z)                                                          \
{                                                                             \
    op(SPARAM(1), T0);                                                        \
    RETURN();                                                                 \
}                                                                             \
PPC_OP(st##name)                                                              \
{                                                                             \
    T0 += SPARAM(1);                                                          \
    op(T0, T1);                                                               \
    RETURN();                                                                 \
}                                                                             \
PPC_IST_OPX(name, op);

PPC_IST_OP(b, st8);
PPC_IST_OP(h, st16);
PPC_IST_OP(w, st32);

/***                Integer load and store with byte reverse               ***/
PPC_ILD_OPX(hbr, ld16r);
PPC_ILD_OPX(wbr, ld32r);
PPC_IST_OPX(hbr, st16r);
PPC_IST_OPX(wbr, st32r);

/***                    Integer load and store multiple                    ***/
PPC_OP(lmw)
{
    do_lmw(PARAM(1), SPARAM(2) + T0);
    RETURN();
}

PPC_OP(stmw)
{
    do_stmw(PARAM(1), SPARAM(2) + T0);
    RETURN();
}

/***                    Integer load and store strings                     ***/
PPC_OP(lswi)
{
    do_lsw(PARAM(1), PARAM(2), T0);
    RETURN();
}

PPC_OP(lswx)
{
    do_lsw(PARAM(1), T0, T1 + T2);
    RETURN();
}

PPC_OP(stswi_z)
{
    do_stsw(PARAM(1), PARAM(2), 0);
    RETURN();
}

PPC_OP(stswi)
{
    do_stsw(PARAM(1), PARAM(2), T0);
    RETURN();
}

PPC_OP(stswx_z)
{
    do_stsw(PARAM(1), T0, T1);
    RETURN();
}

PPC_OP(stswx)
{
    do_stsw(PARAM(1), T0, T1 + T2);
    RETURN();
}

/* SPR */
PPC_OP(load_spr)
{
    T0 = regs->spr[PARAM(1)];
}

PPC_OP(store_spr)
{
    regs->spr[PARAM(1)] = T0;
}

/* FPSCR */
PPC_OP(load_fpscr)
{
    T0 = do_load_fpscr();
}

PPC_OP(store_fpscr)
{
    do_store_fpscr(PARAM(1), T0);
}

/***                         Floating-point store                          ***/

static inline uint32_t dtos(uint64_t f)
{
    unsigned int e, m, s;
    e = (((f >> 52) & 0x7ff) - 1022) + 126;
    s = (f >> 63);
    m = (f >> 29);
    return (s << 31) | (e << 23) | m;
}

static inline uint64_t stod(uint32_t f)
{
    unsigned int e, m, s;
    e = ((f >> 23) & 0xff) - 126 + 1022;
    s = f >> 31;
    m = f & ((1 << 23) - 1);
    return ((uint64_t)s << 63) | ((uint64_t)e << 52) | ((uint64_t)m << 29);
}

PPC_OP(stfd_z_FT0)
{
    st64(SPARAM(1), FT0);
}

PPC_OP(stfd_FT0)
{
    T0 += SPARAM(1);
    st64(T0, FT0);
}

PPC_OP(stfdx_z_FT0)
{
    st64(T0, FT0);
}

PPC_OP(stfdx_FT0)
{
    T0 += T1;
    st64(T0, FT0);
}


PPC_OP(stfs_z_FT0)
{
    st32(SPARAM(1), dtos(FT0));
}

PPC_OP(stfs_FT0)
{
    T0 += SPARAM(1);
    st32(T0, dtos(FT0));
}

PPC_OP(stfsx_z_FT0)
{
    st32(T0, dtos(FT0));
}

PPC_OP(stfsx_FT0)
{
    T0 += T1;
    st32(T0, dtos(FT0));
}

/***                         Floating-point load                          ***/
PPC_OP(lfd_z_FT0)
{
    FT0 = ld64(SPARAM(1));
}

PPC_OP(lfd_FT0)
{
    T0 += SPARAM(1);
    FT0 = ld64(T0);
}

PPC_OP(lfdx_z_FT0)
{
    FT0 = ld64(T0);
}

PPC_OP(lfdx_FT0)
{
    T0 += T1;
    FT0 = ld64(T0);
}

PPC_OP(lfs_z_FT0)
{
    FT0 = stod(ld32(SPARAM(1)));
}

PPC_OP(lfs_FT0)
{
    T0 += SPARAM(1);
    FT0 = stod(ld32(T0));
}

PPC_OP(lfsx_z_FT0)
{
    FT0 = stod(ld32(T0));
}

PPC_OP(lfsx_FT0)
{
    T0 += T1;
    FT0 = stod(ld32(T0));
}
