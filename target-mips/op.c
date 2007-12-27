/*
 *  MIPS emulation micro-operations for qemu.
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *  Copyright (c) 2006 Marius Groeger (FPU operations)
 *  Copyright (c) 2007 Thiemo Seufer (64-bit FPU support)
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
#include "host-utils.h"

#ifndef CALL_FROM_TB0
#define CALL_FROM_TB0(func) func()
#endif
#ifndef CALL_FROM_TB1
#define CALL_FROM_TB1(func, arg0) func(arg0)
#endif
#ifndef CALL_FROM_TB1_CONST16
#define CALL_FROM_TB1_CONST16(func, arg0) CALL_FROM_TB1(func, arg0)
#endif
#ifndef CALL_FROM_TB2
#define CALL_FROM_TB2(func, arg0, arg1) func(arg0, arg1)
#endif
#ifndef CALL_FROM_TB2_CONST16
#define CALL_FROM_TB2_CONST16(func, arg0, arg1)     \
        CALL_FROM_TB2(func, arg0, arg1)
#endif
#ifndef CALL_FROM_TB3
#define CALL_FROM_TB3(func, arg0, arg1, arg2) func(arg0, arg1, arg2)
#endif
#ifndef CALL_FROM_TB4
#define CALL_FROM_TB4(func, arg0, arg1, arg2, arg3) \
        func(arg0, arg1, arg2, arg3)
#endif

#define REG 1
#include "op_template.c"
#undef REG
#define REG 2
#include "op_template.c"
#undef REG
#define REG 3
#include "op_template.c"
#undef REG
#define REG 4
#include "op_template.c"
#undef REG
#define REG 5
#include "op_template.c"
#undef REG
#define REG 6
#include "op_template.c"
#undef REG
#define REG 7
#include "op_template.c"
#undef REG
#define REG 8
#include "op_template.c"
#undef REG
#define REG 9
#include "op_template.c"
#undef REG
#define REG 10
#include "op_template.c"
#undef REG
#define REG 11
#include "op_template.c"
#undef REG
#define REG 12
#include "op_template.c"
#undef REG
#define REG 13
#include "op_template.c"
#undef REG
#define REG 14
#include "op_template.c"
#undef REG
#define REG 15
#include "op_template.c"
#undef REG
#define REG 16
#include "op_template.c"
#undef REG
#define REG 17
#include "op_template.c"
#undef REG
#define REG 18
#include "op_template.c"
#undef REG
#define REG 19
#include "op_template.c"
#undef REG
#define REG 20
#include "op_template.c"
#undef REG
#define REG 21
#include "op_template.c"
#undef REG
#define REG 22
#include "op_template.c"
#undef REG
#define REG 23
#include "op_template.c"
#undef REG
#define REG 24
#include "op_template.c"
#undef REG
#define REG 25
#include "op_template.c"
#undef REG
#define REG 26
#include "op_template.c"
#undef REG
#define REG 27
#include "op_template.c"
#undef REG
#define REG 28
#include "op_template.c"
#undef REG
#define REG 29
#include "op_template.c"
#undef REG
#define REG 30
#include "op_template.c"
#undef REG
#define REG 31
#include "op_template.c"
#undef REG

#define TN
#include "op_template.c"
#undef TN

#define FREG 0
#include "fop_template.c"
#undef FREG
#define FREG 1
#include "fop_template.c"
#undef FREG
#define FREG 2
#include "fop_template.c"
#undef FREG
#define FREG 3
#include "fop_template.c"
#undef FREG
#define FREG 4
#include "fop_template.c"
#undef FREG
#define FREG 5
#include "fop_template.c"
#undef FREG
#define FREG 6
#include "fop_template.c"
#undef FREG
#define FREG 7
#include "fop_template.c"
#undef FREG
#define FREG 8
#include "fop_template.c"
#undef FREG
#define FREG 9
#include "fop_template.c"
#undef FREG
#define FREG 10
#include "fop_template.c"
#undef FREG
#define FREG 11
#include "fop_template.c"
#undef FREG
#define FREG 12
#include "fop_template.c"
#undef FREG
#define FREG 13
#include "fop_template.c"
#undef FREG
#define FREG 14
#include "fop_template.c"
#undef FREG
#define FREG 15
#include "fop_template.c"
#undef FREG
#define FREG 16
#include "fop_template.c"
#undef FREG
#define FREG 17
#include "fop_template.c"
#undef FREG
#define FREG 18
#include "fop_template.c"
#undef FREG
#define FREG 19
#include "fop_template.c"
#undef FREG
#define FREG 20
#include "fop_template.c"
#undef FREG
#define FREG 21
#include "fop_template.c"
#undef FREG
#define FREG 22
#include "fop_template.c"
#undef FREG
#define FREG 23
#include "fop_template.c"
#undef FREG
#define FREG 24
#include "fop_template.c"
#undef FREG
#define FREG 25
#include "fop_template.c"
#undef FREG
#define FREG 26
#include "fop_template.c"
#undef FREG
#define FREG 27
#include "fop_template.c"
#undef FREG
#define FREG 28
#include "fop_template.c"
#undef FREG
#define FREG 29
#include "fop_template.c"
#undef FREG
#define FREG 30
#include "fop_template.c"
#undef FREG
#define FREG 31
#include "fop_template.c"
#undef FREG

#define FTN
#include "fop_template.c"
#undef FTN

void op_dup_T0 (void)
{
    T2 = T0;
    FORCE_RET();
}

void op_load_HI (void)
{
    T0 = env->HI[PARAM1][env->current_tc];
    FORCE_RET();
}

void op_store_HI (void)
{
    env->HI[PARAM1][env->current_tc] = T0;
    FORCE_RET();
}

void op_load_LO (void)
{
    T0 = env->LO[PARAM1][env->current_tc];
    FORCE_RET();
}

void op_store_LO (void)
{
    env->LO[PARAM1][env->current_tc] = T0;
    FORCE_RET();
}

/* Load and store */
#define MEMSUFFIX _raw
#include "op_mem.c"
#undef MEMSUFFIX
#if !defined(CONFIG_USER_ONLY)
#define MEMSUFFIX _user
#include "op_mem.c"
#undef MEMSUFFIX

#define MEMSUFFIX _super
#include "op_mem.c"
#undef MEMSUFFIX

#define MEMSUFFIX _kernel
#include "op_mem.c"
#undef MEMSUFFIX
#endif

/* Addresses computation */
void op_addr_add (void)
{
/* For compatibility with 32-bit code, data reference in user mode
   with Status_UX = 0 should be casted to 32-bit and sign extended.
   See the MIPS64 PRA manual, section 4.10. */
#if defined(TARGET_MIPS64)
    if (((env->hflags & MIPS_HFLAG_KSU) == MIPS_HFLAG_UM) &&
        !(env->CP0_Status & (1 << CP0St_UX)))
        T0 = (int64_t)(int32_t)(T0 + T1);
    else
#endif
        T0 += T1;
    FORCE_RET();
}

/* Arithmetic */
void op_add (void)
{
    T0 = (int32_t)((int32_t)T0 + (int32_t)T1);
    FORCE_RET();
}

void op_addo (void)
{
    target_ulong tmp;

    tmp = (int32_t)T0;
    T0 = (int32_t)T0 + (int32_t)T1;
    if (((tmp ^ T1 ^ (-1)) & (T0 ^ T1)) >> 31) {
        /* operands of same sign, result different sign */
        CALL_FROM_TB1(do_raise_exception, EXCP_OVERFLOW);
    }
    T0 = (int32_t)T0;
    FORCE_RET();
}

void op_sub (void)
{
    T0 = (int32_t)((int32_t)T0 - (int32_t)T1);
    FORCE_RET();
}

void op_subo (void)
{
    target_ulong tmp;

    tmp = (int32_t)T0;
    T0 = (int32_t)T0 - (int32_t)T1;
    if (((tmp ^ T1) & (tmp ^ T0)) >> 31) {
        /* operands of different sign, first operand and result different sign */
        CALL_FROM_TB1(do_raise_exception, EXCP_OVERFLOW);
    }
    T0 = (int32_t)T0;
    FORCE_RET();
}

void op_mul (void)
{
    T0 = (int32_t)((int32_t)T0 * (int32_t)T1);
    FORCE_RET();
}

#if HOST_LONG_BITS < 64
void op_div (void)
{
    CALL_FROM_TB0(do_div);
    FORCE_RET();
}
#else
void op_div (void)
{
    if (T1 != 0) {
        env->LO[0][env->current_tc] = (int32_t)((int64_t)(int32_t)T0 / (int32_t)T1);
        env->HI[0][env->current_tc] = (int32_t)((int64_t)(int32_t)T0 % (int32_t)T1);
    }
    FORCE_RET();
}
#endif

void op_divu (void)
{
    if (T1 != 0) {
        env->LO[0][env->current_tc] = (int32_t)((uint32_t)T0 / (uint32_t)T1);
        env->HI[0][env->current_tc] = (int32_t)((uint32_t)T0 % (uint32_t)T1);
    }
    FORCE_RET();
}

#if defined(TARGET_MIPS64)
/* Arithmetic */
void op_dadd (void)
{
    T0 += T1;
    FORCE_RET();
}

void op_daddo (void)
{
    target_long tmp;

    tmp = T0;
    T0 += T1;
    if (((tmp ^ T1 ^ (-1)) & (T0 ^ T1)) >> 63) {
        /* operands of same sign, result different sign */
        CALL_FROM_TB1(do_raise_exception, EXCP_OVERFLOW);
    }
    FORCE_RET();
}

void op_dsub (void)
{
    T0 -= T1;
    FORCE_RET();
}

void op_dsubo (void)
{
    target_long tmp;

    tmp = T0;
    T0 = (int64_t)T0 - (int64_t)T1;
    if (((tmp ^ T1) & (tmp ^ T0)) >> 63) {
        /* operands of different sign, first operand and result different sign */
        CALL_FROM_TB1(do_raise_exception, EXCP_OVERFLOW);
    }
    FORCE_RET();
}

void op_dmul (void)
{
    T0 = (int64_t)T0 * (int64_t)T1;
    FORCE_RET();
}

/* Those might call libgcc functions.  */
void op_ddiv (void)
{
    do_ddiv();
    FORCE_RET();
}

#if TARGET_LONG_BITS > HOST_LONG_BITS
void op_ddivu (void)
{
    do_ddivu();
    FORCE_RET();
}
#else
void op_ddivu (void)
{
    if (T1 != 0) {
        env->LO[0][env->current_tc] = T0 / T1;
        env->HI[0][env->current_tc] = T0 % T1;
    }
    FORCE_RET();
}
#endif
#endif /* TARGET_MIPS64 */

/* Logical */
void op_and (void)
{
    T0 &= T1;
    FORCE_RET();
}

void op_nor (void)
{
    T0 = ~(T0 | T1);
    FORCE_RET();
}

void op_or (void)
{
    T0 |= T1;
    FORCE_RET();
}

void op_xor (void)
{
    T0 ^= T1;
    FORCE_RET();
}

void op_sll (void)
{
    T0 = (int32_t)((uint32_t)T0 << T1);
    FORCE_RET();
}

void op_sra (void)
{
    T0 = (int32_t)((int32_t)T0 >> T1);
    FORCE_RET();
}

void op_srl (void)
{
    T0 = (int32_t)((uint32_t)T0 >> T1);
    FORCE_RET();
}

void op_rotr (void)
{
    target_ulong tmp;

    if (T1) {
       tmp = (int32_t)((uint32_t)T0 << (0x20 - T1));
       T0 = (int32_t)((uint32_t)T0 >> T1) | tmp;
    }
    FORCE_RET();
}

void op_sllv (void)
{
    T0 = (int32_t)((uint32_t)T1 << ((uint32_t)T0 & 0x1F));
    FORCE_RET();
}

void op_srav (void)
{
    T0 = (int32_t)((int32_t)T1 >> (T0 & 0x1F));
    FORCE_RET();
}

void op_srlv (void)
{
    T0 = (int32_t)((uint32_t)T1 >> (T0 & 0x1F));
    FORCE_RET();
}

void op_rotrv (void)
{
    target_ulong tmp;

    T0 &= 0x1F;
    if (T0) {
       tmp = (int32_t)((uint32_t)T1 << (0x20 - T0));
       T0 = (int32_t)((uint32_t)T1 >> T0) | tmp;
    } else
       T0 = T1;
    FORCE_RET();
}

void op_clo (void)
{
    T0 = clo32(T0);
    FORCE_RET();
}

void op_clz (void)
{
    T0 = clz32(T0);
    FORCE_RET();
}

#if defined(TARGET_MIPS64)

#if TARGET_LONG_BITS > HOST_LONG_BITS
/* Those might call libgcc functions.  */
void op_dsll (void)
{
    CALL_FROM_TB0(do_dsll);
    FORCE_RET();
}

void op_dsll32 (void)
{
    CALL_FROM_TB0(do_dsll32);
    FORCE_RET();
}

void op_dsra (void)
{
    CALL_FROM_TB0(do_dsra);
    FORCE_RET();
}

void op_dsra32 (void)
{
    CALL_FROM_TB0(do_dsra32);
    FORCE_RET();
}

void op_dsrl (void)
{
    CALL_FROM_TB0(do_dsrl);
    FORCE_RET();
}

void op_dsrl32 (void)
{
    CALL_FROM_TB0(do_dsrl32);
    FORCE_RET();
}

void op_drotr (void)
{
    CALL_FROM_TB0(do_drotr);
    FORCE_RET();
}

void op_drotr32 (void)
{
    CALL_FROM_TB0(do_drotr32);
    FORCE_RET();
}

void op_dsllv (void)
{
    CALL_FROM_TB0(do_dsllv);
    FORCE_RET();
}

void op_dsrav (void)
{
    CALL_FROM_TB0(do_dsrav);
    FORCE_RET();
}

void op_dsrlv (void)
{
    CALL_FROM_TB0(do_dsrlv);
    FORCE_RET();
}

void op_drotrv (void)
{
    CALL_FROM_TB0(do_drotrv);
    FORCE_RET();
}

void op_dclo (void)
{
    CALL_FROM_TB0(do_dclo);
    FORCE_RET();
}

void op_dclz (void)
{
    CALL_FROM_TB0(do_dclz);
    FORCE_RET();
}

#else /* TARGET_LONG_BITS > HOST_LONG_BITS */

void op_dsll (void)
{
    T0 = T0 << T1;
    FORCE_RET();
}

void op_dsll32 (void)
{
    T0 = T0 << (T1 + 32);
    FORCE_RET();
}

void op_dsra (void)
{
    T0 = (int64_t)T0 >> T1;
    FORCE_RET();
}

void op_dsra32 (void)
{
    T0 = (int64_t)T0 >> (T1 + 32);
    FORCE_RET();
}

void op_dsrl (void)
{
    T0 = T0 >> T1;
    FORCE_RET();
}

void op_dsrl32 (void)
{
    T0 = T0 >> (T1 + 32);
    FORCE_RET();
}

void op_drotr (void)
{
    target_ulong tmp;

    if (T1) {
        tmp = T0 << (0x40 - T1);
        T0 = (T0 >> T1) | tmp;
    }
    FORCE_RET();
}

void op_drotr32 (void)
{
    target_ulong tmp;

    tmp = T0 << (0x40 - (32 + T1));
    T0 = (T0 >> (32 + T1)) | tmp;
    FORCE_RET();
}

void op_dsllv (void)
{
    T0 = T1 << (T0 & 0x3F);
    FORCE_RET();
}

void op_dsrav (void)
{
    T0 = (int64_t)T1 >> (T0 & 0x3F);
    FORCE_RET();
}

void op_dsrlv (void)
{
    T0 = T1 >> (T0 & 0x3F);
    FORCE_RET();
}

void op_drotrv (void)
{
    target_ulong tmp;

    T0 &= 0x3F;
    if (T0) {
        tmp = T1 << (0x40 - T0);
        T0 = (T1 >> T0) | tmp;
    } else
        T0 = T1;
    FORCE_RET();
}

void op_dclo (void)
{
    T0 = clo64(T0);
    FORCE_RET();
}

void op_dclz (void)
{
    T0 = clz64(T0);
    FORCE_RET();
}
#endif /* TARGET_LONG_BITS > HOST_LONG_BITS */
#endif /* TARGET_MIPS64 */

/* 64 bits arithmetic */
#if TARGET_LONG_BITS > HOST_LONG_BITS
void op_mult (void)
{
    CALL_FROM_TB0(do_mult);
    FORCE_RET();
}

void op_multu (void)
{
    CALL_FROM_TB0(do_multu);
    FORCE_RET();
}

void op_madd (void)
{
    CALL_FROM_TB0(do_madd);
    FORCE_RET();
}

void op_maddu (void)
{
    CALL_FROM_TB0(do_maddu);
    FORCE_RET();
}

void op_msub (void)
{
    CALL_FROM_TB0(do_msub);
    FORCE_RET();
}

void op_msubu (void)
{
    CALL_FROM_TB0(do_msubu);
    FORCE_RET();
}

/* Multiplication variants of the vr54xx. */
void op_muls (void)
{
    CALL_FROM_TB0(do_muls);
    FORCE_RET();
}

void op_mulsu (void)
{
    CALL_FROM_TB0(do_mulsu);
    FORCE_RET();
}

void op_macc (void)
{
    CALL_FROM_TB0(do_macc);
    FORCE_RET();
}

void op_macchi (void)
{
    CALL_FROM_TB0(do_macchi);
    FORCE_RET();
}

void op_maccu (void)
{
    CALL_FROM_TB0(do_maccu);
    FORCE_RET();
}
void op_macchiu (void)
{
    CALL_FROM_TB0(do_macchiu);
    FORCE_RET();
}

void op_msac (void)
{
    CALL_FROM_TB0(do_msac);
    FORCE_RET();
}

void op_msachi (void)
{
    CALL_FROM_TB0(do_msachi);
    FORCE_RET();
}

void op_msacu (void)
{
    CALL_FROM_TB0(do_msacu);
    FORCE_RET();
}

void op_msachiu (void)
{
    CALL_FROM_TB0(do_msachiu);
    FORCE_RET();
}

void op_mulhi (void)
{
    CALL_FROM_TB0(do_mulhi);
    FORCE_RET();
}

void op_mulhiu (void)
{
    CALL_FROM_TB0(do_mulhiu);
    FORCE_RET();
}

void op_mulshi (void)
{
    CALL_FROM_TB0(do_mulshi);
    FORCE_RET();
}

void op_mulshiu (void)
{
    CALL_FROM_TB0(do_mulshiu);
    FORCE_RET();
}

#else /* TARGET_LONG_BITS > HOST_LONG_BITS */

static always_inline uint64_t get_HILO (void)
{
    return ((uint64_t)env->HI[0][env->current_tc] << 32) |
            ((uint64_t)(uint32_t)env->LO[0][env->current_tc]);
}

static always_inline void set_HILO (uint64_t HILO)
{
    env->LO[0][env->current_tc] = (int32_t)(HILO & 0xFFFFFFFF);
    env->HI[0][env->current_tc] = (int32_t)(HILO >> 32);
}

static always_inline void set_HIT0_LO (uint64_t HILO)
{
    env->LO[0][env->current_tc] = (int32_t)(HILO & 0xFFFFFFFF);
    T0 = env->HI[0][env->current_tc] = (int32_t)(HILO >> 32);
}

static always_inline void set_HI_LOT0 (uint64_t HILO)
{
    T0 = env->LO[0][env->current_tc] = (int32_t)(HILO & 0xFFFFFFFF);
    env->HI[0][env->current_tc] = (int32_t)(HILO >> 32);
}

void op_mult (void)
{
    set_HILO((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1);
    FORCE_RET();
}

void op_multu (void)
{
    set_HILO((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1);
    FORCE_RET();
}

void op_madd (void)
{
    int64_t tmp;

    tmp = ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1);
    set_HILO((int64_t)get_HILO() + tmp);
    FORCE_RET();
}

void op_maddu (void)
{
    uint64_t tmp;

    tmp = ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1);
    set_HILO(get_HILO() + tmp);
    FORCE_RET();
}

void op_msub (void)
{
    int64_t tmp;

    tmp = ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1);
    set_HILO((int64_t)get_HILO() - tmp);
    FORCE_RET();
}

void op_msubu (void)
{
    uint64_t tmp;

    tmp = ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1);
    set_HILO(get_HILO() - tmp);
    FORCE_RET();
}

/* Multiplication variants of the vr54xx. */
void op_muls (void)
{
    set_HI_LOT0(0 - ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1));
    FORCE_RET();
}

void op_mulsu (void)
{
    set_HI_LOT0(0 - ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1));
    FORCE_RET();
}

void op_macc (void)
{
    set_HI_LOT0(get_HILO() + ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1));
    FORCE_RET();
}

void op_macchi (void)
{
    set_HIT0_LO(get_HILO() + ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1));
    FORCE_RET();
}

void op_maccu (void)
{
    set_HI_LOT0(get_HILO() + ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1));
    FORCE_RET();
}

void op_macchiu (void)
{
    set_HIT0_LO(get_HILO() + ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1));
    FORCE_RET();
}

void op_msac (void)
{
    set_HI_LOT0(get_HILO() - ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1));
    FORCE_RET();
}

void op_msachi (void)
{
    set_HIT0_LO(get_HILO() - ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1));
    FORCE_RET();
}

void op_msacu (void)
{
    set_HI_LOT0(get_HILO() - ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1));
    FORCE_RET();
}

void op_msachiu (void)
{
    set_HIT0_LO(get_HILO() - ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1));
    FORCE_RET();
}

void op_mulhi (void)
{
    set_HIT0_LO((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1);
    FORCE_RET();
}

void op_mulhiu (void)
{
    set_HIT0_LO((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1);
    FORCE_RET();
}

void op_mulshi (void)
{
    set_HIT0_LO(0 - ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1));
    FORCE_RET();
}

void op_mulshiu (void)
{
    set_HIT0_LO(0 - ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1));
    FORCE_RET();
}

#endif /* TARGET_LONG_BITS > HOST_LONG_BITS */

#if defined(TARGET_MIPS64)
void op_dmult (void)
{
    CALL_FROM_TB4(muls64, &(env->LO[0][env->current_tc]), &(env->HI[0][env->current_tc]), T0, T1);
    FORCE_RET();
}

void op_dmultu (void)
{
    CALL_FROM_TB4(mulu64, &(env->LO[0][env->current_tc]), &(env->HI[0][env->current_tc]), T0, T1);
    FORCE_RET();
}
#endif

/* Conditional moves */
void op_movn (void)
{
    if (T1 != 0)
        env->gpr[PARAM1][env->current_tc] = T0;
    FORCE_RET();
}

void op_movz (void)
{
    if (T1 == 0)
        env->gpr[PARAM1][env->current_tc] = T0;
    FORCE_RET();
}

void op_movf (void)
{
    if (!(env->fpu->fcr31 & PARAM1))
        T0 = T1;
    FORCE_RET();
}

void op_movt (void)
{
    if (env->fpu->fcr31 & PARAM1)
        T0 = T1;
    FORCE_RET();
}

/* Tests */
#define OP_COND(name, cond) \
void glue(op_, name) (void) \
{                           \
    if (cond) {             \
        T0 = 1;             \
    } else {                \
        T0 = 0;             \
    }                       \
    FORCE_RET();            \
}

OP_COND(eq, T0 == T1);
OP_COND(ne, T0 != T1);
OP_COND(ge, (target_long)T0 >= (target_long)T1);
OP_COND(geu, T0 >= T1);
OP_COND(lt, (target_long)T0 < (target_long)T1);
OP_COND(ltu, T0 < T1);
OP_COND(gez, (target_long)T0 >= 0);
OP_COND(gtz, (target_long)T0 > 0);
OP_COND(lez, (target_long)T0 <= 0);
OP_COND(ltz, (target_long)T0 < 0);

/* Branches */
void OPPROTO op_goto_tb0(void)
{
    GOTO_TB(op_goto_tb0, PARAM1, 0);
    FORCE_RET();
}

void OPPROTO op_goto_tb1(void)
{
    GOTO_TB(op_goto_tb1, PARAM1, 1);
    FORCE_RET();
}

/* Branch to register */
void op_save_breg_target (void)
{
    env->btarget = T2;
    FORCE_RET();
}

void op_restore_breg_target (void)
{
    T2 = env->btarget;
    FORCE_RET();
}

void op_breg (void)
{
    env->PC[env->current_tc] = T2;
    FORCE_RET();
}

void op_save_btarget (void)
{
    env->btarget = PARAM1;
    FORCE_RET();
}

#if defined(TARGET_MIPS64)
void op_save_btarget64 (void)
{
    env->btarget = ((uint64_t)PARAM1 << 32) | (uint32_t)PARAM2;
    FORCE_RET();
}
#endif

/* Conditional branch */
void op_set_bcond (void)
{
    T2 = T0;
    FORCE_RET();
}

void op_save_bcond (void)
{
    env->bcond = T2;
    FORCE_RET();
}

void op_restore_bcond (void)
{
    T2 = env->bcond;
    FORCE_RET();
}

void op_jnz_T2 (void)
{
    if (T2)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

/* CP0 functions */
void op_mfc0_index (void)
{
    T0 = env->CP0_Index;
    FORCE_RET();
}

void op_mfc0_mvpcontrol (void)
{
    T0 = env->mvp->CP0_MVPControl;
    FORCE_RET();
}

void op_mfc0_mvpconf0 (void)
{
    T0 = env->mvp->CP0_MVPConf0;
    FORCE_RET();
}

void op_mfc0_mvpconf1 (void)
{
    T0 = env->mvp->CP0_MVPConf1;
    FORCE_RET();
}

void op_mfc0_random (void)
{
    CALL_FROM_TB0(do_mfc0_random);
    FORCE_RET();
}

void op_mfc0_vpecontrol (void)
{
    T0 = env->CP0_VPEControl;
    FORCE_RET();
}

void op_mfc0_vpeconf0 (void)
{
    T0 = env->CP0_VPEConf0;
    FORCE_RET();
}

void op_mfc0_vpeconf1 (void)
{
    T0 = env->CP0_VPEConf1;
    FORCE_RET();
}

void op_mfc0_yqmask (void)
{
    T0 = env->CP0_YQMask;
    FORCE_RET();
}

void op_mfc0_vpeschedule (void)
{
    T0 = env->CP0_VPESchedule;
    FORCE_RET();
}

void op_mfc0_vpeschefback (void)
{
    T0 = env->CP0_VPEScheFBack;
    FORCE_RET();
}

void op_mfc0_vpeopt (void)
{
    T0 = env->CP0_VPEOpt;
    FORCE_RET();
}

void op_mfc0_entrylo0 (void)
{
    T0 = (int32_t)env->CP0_EntryLo0;
    FORCE_RET();
}

void op_mfc0_tcstatus (void)
{
    T0 = env->CP0_TCStatus[env->current_tc];
    FORCE_RET();
}

void op_mftc0_tcstatus(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->CP0_TCStatus[other_tc];
    FORCE_RET();
}

void op_mfc0_tcbind (void)
{
    T0 = env->CP0_TCBind[env->current_tc];
    FORCE_RET();
}

void op_mftc0_tcbind(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->CP0_TCBind[other_tc];
    FORCE_RET();
}

void op_mfc0_tcrestart (void)
{
    T0 = env->PC[env->current_tc];
    FORCE_RET();
}

void op_mftc0_tcrestart(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->PC[other_tc];
    FORCE_RET();
}

void op_mfc0_tchalt (void)
{
    T0 = env->CP0_TCHalt[env->current_tc];
    FORCE_RET();
}

void op_mftc0_tchalt(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->CP0_TCHalt[other_tc];
    FORCE_RET();
}

void op_mfc0_tccontext (void)
{
    T0 = env->CP0_TCContext[env->current_tc];
    FORCE_RET();
}

void op_mftc0_tccontext(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->CP0_TCContext[other_tc];
    FORCE_RET();
}

void op_mfc0_tcschedule (void)
{
    T0 = env->CP0_TCSchedule[env->current_tc];
    FORCE_RET();
}

void op_mftc0_tcschedule(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->CP0_TCSchedule[other_tc];
    FORCE_RET();
}

void op_mfc0_tcschefback (void)
{
    T0 = env->CP0_TCScheFBack[env->current_tc];
    FORCE_RET();
}

void op_mftc0_tcschefback(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->CP0_TCScheFBack[other_tc];
    FORCE_RET();
}

void op_mfc0_entrylo1 (void)
{
    T0 = (int32_t)env->CP0_EntryLo1;
    FORCE_RET();
}

void op_mfc0_context (void)
{
    T0 = (int32_t)env->CP0_Context;
    FORCE_RET();
}

void op_mfc0_pagemask (void)
{
    T0 = env->CP0_PageMask;
    FORCE_RET();
}

void op_mfc0_pagegrain (void)
{
    T0 = env->CP0_PageGrain;
    FORCE_RET();
}

void op_mfc0_wired (void)
{
    T0 = env->CP0_Wired;
    FORCE_RET();
}

void op_mfc0_srsconf0 (void)
{
    T0 = env->CP0_SRSConf0;
    FORCE_RET();
}

void op_mfc0_srsconf1 (void)
{
    T0 = env->CP0_SRSConf1;
    FORCE_RET();
}

void op_mfc0_srsconf2 (void)
{
    T0 = env->CP0_SRSConf2;
    FORCE_RET();
}

void op_mfc0_srsconf3 (void)
{
    T0 = env->CP0_SRSConf3;
    FORCE_RET();
}

void op_mfc0_srsconf4 (void)
{
    T0 = env->CP0_SRSConf4;
    FORCE_RET();
}

void op_mfc0_hwrena (void)
{
    T0 = env->CP0_HWREna;
    FORCE_RET();
}

void op_mfc0_badvaddr (void)
{
    T0 = (int32_t)env->CP0_BadVAddr;
    FORCE_RET();
}

void op_mfc0_count (void)
{
    CALL_FROM_TB0(do_mfc0_count);
    FORCE_RET();
}

void op_mfc0_entryhi (void)
{
    T0 = (int32_t)env->CP0_EntryHi;
    FORCE_RET();
}

void op_mftc0_entryhi(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = (env->CP0_EntryHi & ~0xff) | (env->CP0_TCStatus[other_tc] & 0xff);
    FORCE_RET();
}

void op_mfc0_compare (void)
{
    T0 = env->CP0_Compare;
    FORCE_RET();
}

void op_mfc0_status (void)
{
    T0 = env->CP0_Status;
    FORCE_RET();
}

void op_mftc0_status(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    uint32_t tcstatus = env->CP0_TCStatus[other_tc];

    T0 = env->CP0_Status & ~0xf1000018;
    T0 |= tcstatus & (0xf << CP0TCSt_TCU0);
    T0 |= (tcstatus & (1 << CP0TCSt_TMX)) >> (CP0TCSt_TMX - CP0St_MX);
    T0 |= (tcstatus & (0x3 << CP0TCSt_TKSU)) >> (CP0TCSt_TKSU - CP0St_KSU);
    FORCE_RET();
}

void op_mfc0_intctl (void)
{
    T0 = env->CP0_IntCtl;
    FORCE_RET();
}

void op_mfc0_srsctl (void)
{
    T0 = env->CP0_SRSCtl;
    FORCE_RET();
}

void op_mfc0_srsmap (void)
{
    T0 = env->CP0_SRSMap;
    FORCE_RET();
}

void op_mfc0_cause (void)
{
    T0 = env->CP0_Cause;
    FORCE_RET();
}

void op_mfc0_epc (void)
{
    T0 = (int32_t)env->CP0_EPC;
    FORCE_RET();
}

void op_mfc0_prid (void)
{
    T0 = env->CP0_PRid;
    FORCE_RET();
}

void op_mfc0_ebase (void)
{
    T0 = env->CP0_EBase;
    FORCE_RET();
}

void op_mfc0_config0 (void)
{
    T0 = env->CP0_Config0;
    FORCE_RET();
}

void op_mfc0_config1 (void)
{
    T0 = env->CP0_Config1;
    FORCE_RET();
}

void op_mfc0_config2 (void)
{
    T0 = env->CP0_Config2;
    FORCE_RET();
}

void op_mfc0_config3 (void)
{
    T0 = env->CP0_Config3;
    FORCE_RET();
}

void op_mfc0_config6 (void)
{
    T0 = env->CP0_Config6;
    FORCE_RET();
}

void op_mfc0_config7 (void)
{
    T0 = env->CP0_Config7;
    FORCE_RET();
}

void op_mfc0_lladdr (void)
{
    T0 = (int32_t)env->CP0_LLAddr >> 4;
    FORCE_RET();
}

void op_mfc0_watchlo (void)
{
    T0 = (int32_t)env->CP0_WatchLo[PARAM1];
    FORCE_RET();
}

void op_mfc0_watchhi (void)
{
    T0 = env->CP0_WatchHi[PARAM1];
    FORCE_RET();
}

void op_mfc0_xcontext (void)
{
    T0 = (int32_t)env->CP0_XContext;
    FORCE_RET();
}

void op_mfc0_framemask (void)
{
    T0 = env->CP0_Framemask;
    FORCE_RET();
}

void op_mfc0_debug (void)
{
    T0 = env->CP0_Debug;
    if (env->hflags & MIPS_HFLAG_DM)
        T0 |= 1 << CP0DB_DM;
    FORCE_RET();
}

void op_mftc0_debug(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    /* XXX: Might be wrong, check with EJTAG spec. */
    T0 = (env->CP0_Debug & ~((1 << CP0DB_SSt) | (1 << CP0DB_Halt))) |
         (env->CP0_Debug_tcstatus[other_tc] &
          ((1 << CP0DB_SSt) | (1 << CP0DB_Halt)));
    FORCE_RET();
}

void op_mfc0_depc (void)
{
    T0 = (int32_t)env->CP0_DEPC;
    FORCE_RET();
}

void op_mfc0_performance0 (void)
{
    T0 = env->CP0_Performance0;
    FORCE_RET();
}

void op_mfc0_taglo (void)
{
    T0 = env->CP0_TagLo;
    FORCE_RET();
}

void op_mfc0_datalo (void)
{
    T0 = env->CP0_DataLo;
    FORCE_RET();
}

void op_mfc0_taghi (void)
{
    T0 = env->CP0_TagHi;
    FORCE_RET();
}

void op_mfc0_datahi (void)
{
    T0 = env->CP0_DataHi;
    FORCE_RET();
}

void op_mfc0_errorepc (void)
{
    T0 = (int32_t)env->CP0_ErrorEPC;
    FORCE_RET();
}

void op_mfc0_desave (void)
{
    T0 = env->CP0_DESAVE;
    FORCE_RET();
}

void op_mtc0_index (void)
{
    int num = 1;
    unsigned int tmp = env->tlb->nb_tlb;

    do {
        tmp >>= 1;
        num <<= 1;
    } while (tmp);
    env->CP0_Index = (env->CP0_Index & 0x80000000) | (T0 & (num - 1));
    FORCE_RET();
}

void op_mtc0_mvpcontrol (void)
{
    uint32_t mask = 0;
    uint32_t newval;

    if (env->CP0_VPEConf0 & (1 << CP0VPEC0_MVP))
        mask |= (1 << CP0MVPCo_CPA) | (1 << CP0MVPCo_VPC) |
                (1 << CP0MVPCo_EVP);
    if (env->mvp->CP0_MVPControl & (1 << CP0MVPCo_VPC))
        mask |= (1 << CP0MVPCo_STLB);
    newval = (env->mvp->CP0_MVPControl & ~mask) | (T0 & mask);

    // TODO: Enable/disable shared TLB, enable/disable VPEs.

    env->mvp->CP0_MVPControl = newval;
    FORCE_RET();
}

void op_mtc0_vpecontrol (void)
{
    uint32_t mask;
    uint32_t newval;

    mask = (1 << CP0VPECo_YSI) | (1 << CP0VPECo_GSI) |
           (1 << CP0VPECo_TE) | (0xff << CP0VPECo_TargTC);
    newval = (env->CP0_VPEControl & ~mask) | (T0 & mask);

    /* Yield scheduler intercept not implemented. */
    /* Gating storage scheduler intercept not implemented. */

    // TODO: Enable/disable TCs.

    env->CP0_VPEControl = newval;
    FORCE_RET();
}

void op_mtc0_vpeconf0 (void)
{
    uint32_t mask = 0;
    uint32_t newval;

    if (env->CP0_VPEConf0 & (1 << CP0VPEC0_MVP)) {
        if (env->CP0_VPEConf0 & (1 << CP0VPEC0_VPA))
            mask |= (0xff << CP0VPEC0_XTC);
        mask |= (1 << CP0VPEC0_MVP) | (1 << CP0VPEC0_VPA);
    }
    newval = (env->CP0_VPEConf0 & ~mask) | (T0 & mask);

    // TODO: TC exclusive handling due to ERL/EXL.

    env->CP0_VPEConf0 = newval;
    FORCE_RET();
}

void op_mtc0_vpeconf1 (void)
{
    uint32_t mask = 0;
    uint32_t newval;

    if (env->mvp->CP0_MVPControl & (1 << CP0MVPCo_VPC))
        mask |= (0xff << CP0VPEC1_NCX) | (0xff << CP0VPEC1_NCP2) |
                (0xff << CP0VPEC1_NCP1);
    newval = (env->CP0_VPEConf1 & ~mask) | (T0 & mask);

    /* UDI not implemented. */
    /* CP2 not implemented. */

    // TODO: Handle FPU (CP1) binding.

    env->CP0_VPEConf1 = newval;
    FORCE_RET();
}

void op_mtc0_yqmask (void)
{
    /* Yield qualifier inputs not implemented. */
    env->CP0_YQMask = 0x00000000;
    FORCE_RET();
}

void op_mtc0_vpeschedule (void)
{
    env->CP0_VPESchedule = T0;
    FORCE_RET();
}

void op_mtc0_vpeschefback (void)
{
    env->CP0_VPEScheFBack = T0;
    FORCE_RET();
}

void op_mtc0_vpeopt (void)
{
    env->CP0_VPEOpt = T0 & 0x0000ffff;
    FORCE_RET();
}

void op_mtc0_entrylo0 (void)
{
    /* Large physaddr (PABITS) not implemented */
    /* 1k pages not implemented */
    env->CP0_EntryLo0 = T0 & 0x3FFFFFFF;
    FORCE_RET();
}

void op_mtc0_tcstatus (void)
{
    uint32_t mask = env->CP0_TCStatus_rw_bitmask;
    uint32_t newval;

    newval = (env->CP0_TCStatus[env->current_tc] & ~mask) | (T0 & mask);

    // TODO: Sync with CP0_Status.

    env->CP0_TCStatus[env->current_tc] = newval;
    FORCE_RET();
}

void op_mttc0_tcstatus (void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    // TODO: Sync with CP0_Status.

    env->CP0_TCStatus[other_tc] = T0;
    FORCE_RET();
}

void op_mtc0_tcbind (void)
{
    uint32_t mask = (1 << CP0TCBd_TBE);
    uint32_t newval;

    if (env->mvp->CP0_MVPControl & (1 << CP0MVPCo_VPC))
        mask |= (1 << CP0TCBd_CurVPE);
    newval = (env->CP0_TCBind[env->current_tc] & ~mask) | (T0 & mask);
    env->CP0_TCBind[env->current_tc] = newval;
    FORCE_RET();
}

void op_mttc0_tcbind (void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    uint32_t mask = (1 << CP0TCBd_TBE);
    uint32_t newval;

    if (env->mvp->CP0_MVPControl & (1 << CP0MVPCo_VPC))
        mask |= (1 << CP0TCBd_CurVPE);
    newval = (env->CP0_TCBind[other_tc] & ~mask) | (T0 & mask);
    env->CP0_TCBind[other_tc] = newval;
    FORCE_RET();
}

void op_mtc0_tcrestart (void)
{
    env->PC[env->current_tc] = T0;
    env->CP0_TCStatus[env->current_tc] &= ~(1 << CP0TCSt_TDS);
    env->CP0_LLAddr = 0ULL;
    /* MIPS16 not implemented. */
    FORCE_RET();
}

void op_mttc0_tcrestart (void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    env->PC[other_tc] = T0;
    env->CP0_TCStatus[other_tc] &= ~(1 << CP0TCSt_TDS);
    env->CP0_LLAddr = 0ULL;
    /* MIPS16 not implemented. */
    FORCE_RET();
}

void op_mtc0_tchalt (void)
{
    env->CP0_TCHalt[env->current_tc] = T0 & 0x1;

    // TODO: Halt TC / Restart (if allocated+active) TC.

    FORCE_RET();
}

void op_mttc0_tchalt (void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    // TODO: Halt TC / Restart (if allocated+active) TC.

    env->CP0_TCHalt[other_tc] = T0;
    FORCE_RET();
}

void op_mtc0_tccontext (void)
{
    env->CP0_TCContext[env->current_tc] = T0;
    FORCE_RET();
}

void op_mttc0_tccontext (void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    env->CP0_TCContext[other_tc] = T0;
    FORCE_RET();
}

void op_mtc0_tcschedule (void)
{
    env->CP0_TCSchedule[env->current_tc] = T0;
    FORCE_RET();
}

void op_mttc0_tcschedule (void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    env->CP0_TCSchedule[other_tc] = T0;
    FORCE_RET();
}

void op_mtc0_tcschefback (void)
{
    env->CP0_TCScheFBack[env->current_tc] = T0;
    FORCE_RET();
}

void op_mttc0_tcschefback (void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    env->CP0_TCScheFBack[other_tc] = T0;
    FORCE_RET();
}

void op_mtc0_entrylo1 (void)
{
    /* Large physaddr (PABITS) not implemented */
    /* 1k pages not implemented */
    env->CP0_EntryLo1 = T0 & 0x3FFFFFFF;
    FORCE_RET();
}

void op_mtc0_context (void)
{
    env->CP0_Context = (env->CP0_Context & 0x007FFFFF) | (T0 & ~0x007FFFFF);
    FORCE_RET();
}

void op_mtc0_pagemask (void)
{
    /* 1k pages not implemented */
    env->CP0_PageMask = T0 & (0x1FFFFFFF & (TARGET_PAGE_MASK << 1));
    FORCE_RET();
}

void op_mtc0_pagegrain (void)
{
    /* SmartMIPS not implemented */
    /* Large physaddr (PABITS) not implemented */
    /* 1k pages not implemented */
    env->CP0_PageGrain = 0;
    FORCE_RET();
}

void op_mtc0_wired (void)
{
    env->CP0_Wired = T0 % env->tlb->nb_tlb;
    FORCE_RET();
}

void op_mtc0_srsconf0 (void)
{
    env->CP0_SRSConf0 |= T0 & env->CP0_SRSConf0_rw_bitmask;
    FORCE_RET();
}

void op_mtc0_srsconf1 (void)
{
    env->CP0_SRSConf1 |= T0 & env->CP0_SRSConf1_rw_bitmask;
    FORCE_RET();
}

void op_mtc0_srsconf2 (void)
{
    env->CP0_SRSConf2 |= T0 & env->CP0_SRSConf2_rw_bitmask;
    FORCE_RET();
}

void op_mtc0_srsconf3 (void)
{
    env->CP0_SRSConf3 |= T0 & env->CP0_SRSConf3_rw_bitmask;
    FORCE_RET();
}

void op_mtc0_srsconf4 (void)
{
    env->CP0_SRSConf4 |= T0 & env->CP0_SRSConf4_rw_bitmask;
    FORCE_RET();
}

void op_mtc0_hwrena (void)
{
    env->CP0_HWREna = T0 & 0x0000000F;
    FORCE_RET();
}

void op_mtc0_count (void)
{
    CALL_FROM_TB2(cpu_mips_store_count, env, T0);
    FORCE_RET();
}

void op_mtc0_entryhi (void)
{
    target_ulong old, val;

    /* 1k pages not implemented */
    val = T0 & ((TARGET_PAGE_MASK << 1) | 0xFF);
#if defined(TARGET_MIPS64)
    val &= env->SEGMask;
#endif
    old = env->CP0_EntryHi;
    env->CP0_EntryHi = val;
    if (env->CP0_Config3 & (1 << CP0C3_MT)) {
        uint32_t tcst = env->CP0_TCStatus[env->current_tc] & ~0xff;
        env->CP0_TCStatus[env->current_tc] = tcst | (val & 0xff);
    }
    /* If the ASID changes, flush qemu's TLB.  */
    if ((old & 0xFF) != (val & 0xFF))
        CALL_FROM_TB2(cpu_mips_tlb_flush, env, 1);
    FORCE_RET();
}

void op_mttc0_entryhi(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    env->CP0_EntryHi = (env->CP0_EntryHi & 0xff) | (T0 & ~0xff);
    env->CP0_TCStatus[other_tc] = (env->CP0_TCStatus[other_tc] & ~0xff) | (T0 & 0xff);
    FORCE_RET();
}

void op_mtc0_compare (void)
{
    CALL_FROM_TB2(cpu_mips_store_compare, env, T0);
    FORCE_RET();
}

void op_mtc0_status (void)
{
    uint32_t val, old;
    uint32_t mask = env->CP0_Status_rw_bitmask;

    val = T0 & mask;
    old = env->CP0_Status;
    env->CP0_Status = (env->CP0_Status & ~mask) | val;
    CALL_FROM_TB1(compute_hflags, env);
    if (loglevel & CPU_LOG_EXEC)
        CALL_FROM_TB2(do_mtc0_status_debug, old, val);
    CALL_FROM_TB1(cpu_mips_update_irq, env);
    FORCE_RET();
}

void op_mttc0_status(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    uint32_t tcstatus = env->CP0_TCStatus[other_tc];

    env->CP0_Status = T0 & ~0xf1000018;
    tcstatus = (tcstatus & ~(0xf << CP0TCSt_TCU0)) | (T0 & (0xf << CP0St_CU0));
    tcstatus = (tcstatus & ~(1 << CP0TCSt_TMX)) | ((T0 & (1 << CP0St_MX)) << (CP0TCSt_TMX - CP0St_MX));
    tcstatus = (tcstatus & ~(0x3 << CP0TCSt_TKSU)) | ((T0 & (0x3 << CP0St_KSU)) << (CP0TCSt_TKSU - CP0St_KSU));
    env->CP0_TCStatus[other_tc] = tcstatus;
    FORCE_RET();
}

void op_mtc0_intctl (void)
{
    /* vectored interrupts not implemented, no performance counters. */
    env->CP0_IntCtl = (env->CP0_IntCtl & ~0x000002e0) | (T0 & 0x000002e0);
    FORCE_RET();
}

void op_mtc0_srsctl (void)
{
    uint32_t mask = (0xf << CP0SRSCtl_ESS) | (0xf << CP0SRSCtl_PSS);
    env->CP0_SRSCtl = (env->CP0_SRSCtl & ~mask) | (T0 & mask);
    FORCE_RET();
}

void op_mtc0_srsmap (void)
{
    env->CP0_SRSMap = T0;
    FORCE_RET();
}

void op_mtc0_cause (void)
{
    uint32_t mask = 0x00C00300;
    uint32_t old = env->CP0_Cause;

    if (env->insn_flags & ISA_MIPS32R2)
        mask |= 1 << CP0Ca_DC;

    env->CP0_Cause = (env->CP0_Cause & ~mask) | (T0 & mask);

    if ((old ^ env->CP0_Cause) & (1 << CP0Ca_DC)) {
        if (env->CP0_Cause & (1 << CP0Ca_DC))
            CALL_FROM_TB1(cpu_mips_stop_count, env);
        else
            CALL_FROM_TB1(cpu_mips_start_count, env);
    }

    /* Handle the software interrupt as an hardware one, as they
       are very similar */
    if (T0 & CP0Ca_IP_mask) {
        CALL_FROM_TB1(cpu_mips_update_irq, env);
    }
    FORCE_RET();
}

void op_mtc0_epc (void)
{
    env->CP0_EPC = T0;
    FORCE_RET();
}

void op_mtc0_ebase (void)
{
    /* vectored interrupts not implemented */
    /* Multi-CPU not implemented */
    env->CP0_EBase = 0x80000000 | (T0 & 0x3FFFF000);
    FORCE_RET();
}

void op_mtc0_config0 (void)
{
    env->CP0_Config0 = (env->CP0_Config0 & 0x81FFFFF8) | (T0 & 0x00000007);
    FORCE_RET();
}

void op_mtc0_config2 (void)
{
    /* tertiary/secondary caches not implemented */
    env->CP0_Config2 = (env->CP0_Config2 & 0x8FFF0FFF);
    FORCE_RET();
}

void op_mtc0_watchlo (void)
{
    /* Watch exceptions for instructions, data loads, data stores
       not implemented. */
    env->CP0_WatchLo[PARAM1] = (T0 & ~0x7);
    FORCE_RET();
}

void op_mtc0_watchhi (void)
{
    env->CP0_WatchHi[PARAM1] = (T0 & 0x40FF0FF8);
    env->CP0_WatchHi[PARAM1] &= ~(env->CP0_WatchHi[PARAM1] & T0 & 0x7);
    FORCE_RET();
}

void op_mtc0_xcontext (void)
{
    target_ulong mask = (1ULL << (env->SEGBITS - 7)) - 1;
    env->CP0_XContext = (env->CP0_XContext & mask) | (T0 & ~mask);
    FORCE_RET();
}

void op_mtc0_framemask (void)
{
    env->CP0_Framemask = T0; /* XXX */
    FORCE_RET();
}

void op_mtc0_debug (void)
{
    env->CP0_Debug = (env->CP0_Debug & 0x8C03FC1F) | (T0 & 0x13300120);
    if (T0 & (1 << CP0DB_DM))
        env->hflags |= MIPS_HFLAG_DM;
    else
        env->hflags &= ~MIPS_HFLAG_DM;
    FORCE_RET();
}

void op_mttc0_debug(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    /* XXX: Might be wrong, check with EJTAG spec. */
    env->CP0_Debug_tcstatus[other_tc] = T0 & ((1 << CP0DB_SSt) | (1 << CP0DB_Halt));
    env->CP0_Debug = (env->CP0_Debug & ((1 << CP0DB_SSt) | (1 << CP0DB_Halt))) |
                     (T0 & ~((1 << CP0DB_SSt) | (1 << CP0DB_Halt)));
    FORCE_RET();
}

void op_mtc0_depc (void)
{
    env->CP0_DEPC = T0;
    FORCE_RET();
}

void op_mtc0_performance0 (void)
{
    env->CP0_Performance0 = T0 & 0x000007ff;
    FORCE_RET();
}

void op_mtc0_taglo (void)
{
    env->CP0_TagLo = T0 & 0xFFFFFCF6;
    FORCE_RET();
}

void op_mtc0_datalo (void)
{
    env->CP0_DataLo = T0; /* XXX */
    FORCE_RET();
}

void op_mtc0_taghi (void)
{
    env->CP0_TagHi = T0; /* XXX */
    FORCE_RET();
}

void op_mtc0_datahi (void)
{
    env->CP0_DataHi = T0; /* XXX */
    FORCE_RET();
}

void op_mtc0_errorepc (void)
{
    env->CP0_ErrorEPC = T0;
    FORCE_RET();
}

void op_mtc0_desave (void)
{
    env->CP0_DESAVE = T0;
    FORCE_RET();
}

#if defined(TARGET_MIPS64)
void op_dmfc0_yqmask (void)
{
    T0 = env->CP0_YQMask;
    FORCE_RET();
}

void op_dmfc0_vpeschedule (void)
{
    T0 = env->CP0_VPESchedule;
    FORCE_RET();
}

void op_dmfc0_vpeschefback (void)
{
    T0 = env->CP0_VPEScheFBack;
    FORCE_RET();
}

void op_dmfc0_entrylo0 (void)
{
    T0 = env->CP0_EntryLo0;
    FORCE_RET();
}

void op_dmfc0_tcrestart (void)
{
    T0 = env->PC[env->current_tc];
    FORCE_RET();
}

void op_dmfc0_tchalt (void)
{
    T0 = env->CP0_TCHalt[env->current_tc];
    FORCE_RET();
}

void op_dmfc0_tccontext (void)
{
    T0 = env->CP0_TCContext[env->current_tc];
    FORCE_RET();
}

void op_dmfc0_tcschedule (void)
{
    T0 = env->CP0_TCSchedule[env->current_tc];
    FORCE_RET();
}

void op_dmfc0_tcschefback (void)
{
    T0 = env->CP0_TCScheFBack[env->current_tc];
    FORCE_RET();
}

void op_dmfc0_entrylo1 (void)
{
    T0 = env->CP0_EntryLo1;
    FORCE_RET();
}

void op_dmfc0_context (void)
{
    T0 = env->CP0_Context;
    FORCE_RET();
}

void op_dmfc0_badvaddr (void)
{
    T0 = env->CP0_BadVAddr;
    FORCE_RET();
}

void op_dmfc0_entryhi (void)
{
    T0 = env->CP0_EntryHi;
    FORCE_RET();
}

void op_dmfc0_epc (void)
{
    T0 = env->CP0_EPC;
    FORCE_RET();
}

void op_dmfc0_lladdr (void)
{
    T0 = env->CP0_LLAddr >> 4;
    FORCE_RET();
}

void op_dmfc0_watchlo (void)
{
    T0 = env->CP0_WatchLo[PARAM1];
    FORCE_RET();
}

void op_dmfc0_xcontext (void)
{
    T0 = env->CP0_XContext;
    FORCE_RET();
}

void op_dmfc0_depc (void)
{
    T0 = env->CP0_DEPC;
    FORCE_RET();
}

void op_dmfc0_errorepc (void)
{
    T0 = env->CP0_ErrorEPC;
    FORCE_RET();
}
#endif /* TARGET_MIPS64 */

/* MIPS MT functions */
void op_mftgpr(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->gpr[PARAM1][other_tc];
    FORCE_RET();
}

void op_mftlo(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->LO[PARAM1][other_tc];
    FORCE_RET();
}

void op_mfthi(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->HI[PARAM1][other_tc];
    FORCE_RET();
}

void op_mftacx(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->ACX[PARAM1][other_tc];
    FORCE_RET();
}

void op_mftdsp(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->DSPControl[other_tc];
    FORCE_RET();
}

void op_mttgpr(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->gpr[PARAM1][other_tc];
    FORCE_RET();
}

void op_mttlo(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->LO[PARAM1][other_tc];
    FORCE_RET();
}

void op_mtthi(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->HI[PARAM1][other_tc];
    FORCE_RET();
}

void op_mttacx(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->ACX[PARAM1][other_tc];
    FORCE_RET();
}

void op_mttdsp(void)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);

    T0 = env->DSPControl[other_tc];
    FORCE_RET();
}


void op_dmt(void)
{
    // TODO
    T0 = 0;
    // rt = T0
    FORCE_RET();
}

void op_emt(void)
{
    // TODO
    T0 = 0;
    // rt = T0
    FORCE_RET();
}

void op_dvpe(void)
{
    // TODO
    T0 = 0;
    // rt = T0
    FORCE_RET();
}

void op_evpe(void)
{
    // TODO
    T0 = 0;
    // rt = T0
    FORCE_RET();
}

void op_fork(void)
{
    // T0 = rt, T1 = rs
    T0 = 0;
    // TODO: store to TC register
    FORCE_RET();
}

void op_yield(void)
{
    if (T0 < 0) {
        /* No scheduling policy implemented. */
        if (T0 != -2) {
            if (env->CP0_VPEControl & (1 << CP0VPECo_YSI) &&
                env->CP0_TCStatus[env->current_tc] & (1 << CP0TCSt_DT)) {
                env->CP0_VPEControl &= ~(0x7 << CP0VPECo_EXCPT);
                env->CP0_VPEControl |= 4 << CP0VPECo_EXCPT;
                CALL_FROM_TB1(do_raise_exception, EXCP_THREAD);
            }
        }
    } else if (T0 == 0) {
	if (0 /* TODO: TC underflow */) {
            env->CP0_VPEControl &= ~(0x7 << CP0VPECo_EXCPT);
            CALL_FROM_TB1(do_raise_exception, EXCP_THREAD);
        } else {
            // TODO: Deallocate TC
        }
    } else if (T0 > 0) {
        /* Yield qualifier inputs not implemented. */
        env->CP0_VPEControl &= ~(0x7 << CP0VPECo_EXCPT);
        env->CP0_VPEControl |= 2 << CP0VPECo_EXCPT;
        CALL_FROM_TB1(do_raise_exception, EXCP_THREAD);
    }
    T0 = env->CP0_YQMask;
    FORCE_RET();
}

/* CP1 functions */
#if 0
# define DEBUG_FPU_STATE() CALL_FROM_TB1(dump_fpu, env)
#else
# define DEBUG_FPU_STATE() do { } while(0)
#endif

void op_cfc1 (void)
{
    CALL_FROM_TB1(do_cfc1, PARAM1);
    DEBUG_FPU_STATE();
    FORCE_RET();
}

void op_ctc1 (void)
{
    CALL_FROM_TB1(do_ctc1, PARAM1);
    DEBUG_FPU_STATE();
    FORCE_RET();
}

void op_mfc1 (void)
{
    T0 = (int32_t)WT0;
    DEBUG_FPU_STATE();
    FORCE_RET();
}

void op_mtc1 (void)
{
    WT0 = T0;
    DEBUG_FPU_STATE();
    FORCE_RET();
}

void op_dmfc1 (void)
{
    T0 = DT0;
    DEBUG_FPU_STATE();
    FORCE_RET();
}

void op_dmtc1 (void)
{
    DT0 = T0;
    DEBUG_FPU_STATE();
    FORCE_RET();
}

void op_mfhc1 (void)
{
    T0 = (int32_t)WTH0;
    DEBUG_FPU_STATE();
    FORCE_RET();
}

void op_mthc1 (void)
{
    WTH0 = T0;
    DEBUG_FPU_STATE();
    FORCE_RET();
}

/* Float support.
   Single precition routines have a "s" suffix, double precision a
   "d" suffix, 32bit integer "w", 64bit integer "l", paired singe "ps",
   paired single lowwer "pl", paired single upper "pu".  */

#define FLOAT_OP(name, p) void OPPROTO op_float_##name##_##p(void)

FLOAT_OP(cvtd, s)
{
    CALL_FROM_TB0(do_float_cvtd_s);
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(cvtd, w)
{
    CALL_FROM_TB0(do_float_cvtd_w);
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(cvtd, l)
{
    CALL_FROM_TB0(do_float_cvtd_l);
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(cvtl, d)
{
    CALL_FROM_TB0(do_float_cvtl_d);
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(cvtl, s)
{
    CALL_FROM_TB0(do_float_cvtl_s);
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(cvtps, s)
{
    WT2 = WT0;
    WTH2 = WT1;
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(cvtps, pw)
{
    CALL_FROM_TB0(do_float_cvtps_pw);
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(cvtpw, ps)
{
    CALL_FROM_TB0(do_float_cvtpw_ps);
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(cvts, d)
{
    CALL_FROM_TB0(do_float_cvts_d);
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(cvts, w)
{
    CALL_FROM_TB0(do_float_cvts_w);
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(cvts, l)
{
    CALL_FROM_TB0(do_float_cvts_l);
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(cvts, pl)
{
    CALL_FROM_TB0(do_float_cvts_pl);
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(cvts, pu)
{
    CALL_FROM_TB0(do_float_cvts_pu);
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(cvtw, s)
{
    CALL_FROM_TB0(do_float_cvtw_s);
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(cvtw, d)
{
    CALL_FROM_TB0(do_float_cvtw_d);
    DEBUG_FPU_STATE();
    FORCE_RET();
}

FLOAT_OP(pll, ps)
{
    DT2 = ((uint64_t)WT0 << 32) | WT1;
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(plu, ps)
{
    DT2 = ((uint64_t)WT0 << 32) | WTH1;
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(pul, ps)
{
    DT2 = ((uint64_t)WTH0 << 32) | WT1;
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(puu, ps)
{
    DT2 = ((uint64_t)WTH0 << 32) | WTH1;
    DEBUG_FPU_STATE();
    FORCE_RET();
}

#define FLOAT_ROUNDOP(op, ttype, stype)                    \
FLOAT_OP(op ## ttype, stype)                               \
{                                                          \
    CALL_FROM_TB0(do_float_ ## op ## ttype ## _ ## stype); \
    DEBUG_FPU_STATE();                                     \
    FORCE_RET();                                           \
}

FLOAT_ROUNDOP(round, l, d)
FLOAT_ROUNDOP(round, l, s)
FLOAT_ROUNDOP(round, w, d)
FLOAT_ROUNDOP(round, w, s)

FLOAT_ROUNDOP(trunc, l, d)
FLOAT_ROUNDOP(trunc, l, s)
FLOAT_ROUNDOP(trunc, w, d)
FLOAT_ROUNDOP(trunc, w, s)

FLOAT_ROUNDOP(ceil, l, d)
FLOAT_ROUNDOP(ceil, l, s)
FLOAT_ROUNDOP(ceil, w, d)
FLOAT_ROUNDOP(ceil, w, s)

FLOAT_ROUNDOP(floor, l, d)
FLOAT_ROUNDOP(floor, l, s)
FLOAT_ROUNDOP(floor, w, d)
FLOAT_ROUNDOP(floor, w, s)
#undef FLOAR_ROUNDOP

FLOAT_OP(movf, d)
{
    if (!(env->fpu->fcr31 & PARAM1))
        DT2 = DT0;
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(movf, s)
{
    if (!(env->fpu->fcr31 & PARAM1))
        WT2 = WT0;
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(movf, ps)
{
    if (!(env->fpu->fcr31 & PARAM1)) {
        WT2 = WT0;
        WTH2 = WTH0;
    }
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(movt, d)
{
    if (env->fpu->fcr31 & PARAM1)
        DT2 = DT0;
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(movt, s)
{
    if (env->fpu->fcr31 & PARAM1)
        WT2 = WT0;
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(movt, ps)
{
    if (env->fpu->fcr31 & PARAM1) {
        WT2 = WT0;
        WTH2 = WTH0;
    }
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(movz, d)
{
    if (!T0)
        DT2 = DT0;
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(movz, s)
{
    if (!T0)
        WT2 = WT0;
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(movz, ps)
{
    if (!T0) {
        WT2 = WT0;
        WTH2 = WTH0;
    }
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(movn, d)
{
    if (T0)
        DT2 = DT0;
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(movn, s)
{
    if (T0)
        WT2 = WT0;
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(movn, ps)
{
    if (T0) {
        WT2 = WT0;
        WTH2 = WTH0;
    }
    DEBUG_FPU_STATE();
    FORCE_RET();
}

/* operations calling helpers, for s, d and ps */
#define FLOAT_HOP(name)   \
FLOAT_OP(name, d)         \
{                         \
    CALL_FROM_TB0(do_float_ ## name ## _d);  \
    DEBUG_FPU_STATE();    \
    FORCE_RET();          \
}                         \
FLOAT_OP(name, s)         \
{                         \
    CALL_FROM_TB0(do_float_ ## name ## _s);  \
    DEBUG_FPU_STATE();    \
    FORCE_RET();          \
}                         \
FLOAT_OP(name, ps)        \
{                         \
    CALL_FROM_TB0(do_float_ ## name ## _ps); \
    DEBUG_FPU_STATE();    \
    FORCE_RET();          \
}
FLOAT_HOP(add)
FLOAT_HOP(sub)
FLOAT_HOP(mul)
FLOAT_HOP(div)
FLOAT_HOP(recip2)
FLOAT_HOP(rsqrt2)
FLOAT_HOP(rsqrt1)
FLOAT_HOP(recip1)
#undef FLOAT_HOP

/* operations calling helpers, for s and d */
#define FLOAT_HOP(name)   \
FLOAT_OP(name, d)         \
{                         \
    CALL_FROM_TB0(do_float_ ## name ## _d);  \
    DEBUG_FPU_STATE();    \
    FORCE_RET();          \
}                         \
FLOAT_OP(name, s)         \
{                         \
    CALL_FROM_TB0(do_float_ ## name ## _s);  \
    DEBUG_FPU_STATE();    \
    FORCE_RET();          \
}
FLOAT_HOP(rsqrt)
FLOAT_HOP(recip)
#undef FLOAT_HOP

/* operations calling helpers, for ps */
#define FLOAT_HOP(name)   \
FLOAT_OP(name, ps)        \
{                         \
    CALL_FROM_TB0(do_float_ ## name ## _ps); \
    DEBUG_FPU_STATE();    \
    FORCE_RET();          \
}
FLOAT_HOP(addr)
FLOAT_HOP(mulr)
#undef FLOAT_HOP

/* ternary operations */
#define FLOAT_TERNOP(name1, name2) \
FLOAT_OP(name1 ## name2, d)        \
{                                  \
    FDT0 = float64_ ## name1 (FDT0, FDT1, &env->fpu->fp_status);    \
    FDT2 = float64_ ## name2 (FDT0, FDT2, &env->fpu->fp_status);    \
    DEBUG_FPU_STATE();             \
    FORCE_RET();                   \
}                                  \
FLOAT_OP(name1 ## name2, s)        \
{                                  \
    FST0 = float32_ ## name1 (FST0, FST1, &env->fpu->fp_status);    \
    FST2 = float32_ ## name2 (FST0, FST2, &env->fpu->fp_status);    \
    DEBUG_FPU_STATE();             \
    FORCE_RET();                   \
}                                  \
FLOAT_OP(name1 ## name2, ps)       \
{                                  \
    FST0 = float32_ ## name1 (FST0, FST1, &env->fpu->fp_status);    \
    FSTH0 = float32_ ## name1 (FSTH0, FSTH1, &env->fpu->fp_status); \
    FST2 = float32_ ## name2 (FST0, FST2, &env->fpu->fp_status);    \
    FSTH2 = float32_ ## name2 (FSTH0, FSTH2, &env->fpu->fp_status); \
    DEBUG_FPU_STATE();             \
    FORCE_RET();                   \
}
FLOAT_TERNOP(mul, add)
FLOAT_TERNOP(mul, sub)
#undef FLOAT_TERNOP

/* negated ternary operations */
#define FLOAT_NTERNOP(name1, name2) \
FLOAT_OP(n ## name1 ## name2, d)    \
{                                   \
    FDT0 = float64_ ## name1 (FDT0, FDT1, &env->fpu->fp_status);    \
    FDT2 = float64_ ## name2 (FDT0, FDT2, &env->fpu->fp_status);    \
    FDT2 = float64_chs(FDT2);       \
    DEBUG_FPU_STATE();              \
    FORCE_RET();                    \
}                                   \
FLOAT_OP(n ## name1 ## name2, s)    \
{                                   \
    FST0 = float32_ ## name1 (FST0, FST1, &env->fpu->fp_status);    \
    FST2 = float32_ ## name2 (FST0, FST2, &env->fpu->fp_status);    \
    FST2 = float32_chs(FST2);       \
    DEBUG_FPU_STATE();              \
    FORCE_RET();                    \
}                                   \
FLOAT_OP(n ## name1 ## name2, ps)   \
{                                   \
    FST0 = float32_ ## name1 (FST0, FST1, &env->fpu->fp_status);    \
    FSTH0 = float32_ ## name1 (FSTH0, FSTH1, &env->fpu->fp_status); \
    FST2 = float32_ ## name2 (FST0, FST2, &env->fpu->fp_status);    \
    FSTH2 = float32_ ## name2 (FSTH0, FSTH2, &env->fpu->fp_status); \
    FST2 = float32_chs(FST2);       \
    FSTH2 = float32_chs(FSTH2);     \
    DEBUG_FPU_STATE();              \
    FORCE_RET();                    \
}
FLOAT_NTERNOP(mul, add)
FLOAT_NTERNOP(mul, sub)
#undef FLOAT_NTERNOP

/* unary operations, modifying fp status  */
#define FLOAT_UNOP(name)  \
FLOAT_OP(name, d)         \
{                         \
    FDT2 = float64_ ## name(FDT0, &env->fpu->fp_status); \
    DEBUG_FPU_STATE();    \
    FORCE_RET();          \
}                         \
FLOAT_OP(name, s)         \
{                         \
    FST2 = float32_ ## name(FST0, &env->fpu->fp_status); \
    DEBUG_FPU_STATE();    \
    FORCE_RET();          \
}
FLOAT_UNOP(sqrt)
#undef FLOAT_UNOP

/* unary operations, not modifying fp status  */
#define FLOAT_UNOP(name)  \
FLOAT_OP(name, d)         \
{                         \
    FDT2 = float64_ ## name(FDT0);   \
    DEBUG_FPU_STATE();    \
    FORCE_RET();          \
}                         \
FLOAT_OP(name, s)         \
{                         \
    FST2 = float32_ ## name(FST0);   \
    DEBUG_FPU_STATE();    \
    FORCE_RET();          \
}                         \
FLOAT_OP(name, ps)        \
{                         \
    FST2 = float32_ ## name(FST0);   \
    FSTH2 = float32_ ## name(FSTH0); \
    DEBUG_FPU_STATE();    \
    FORCE_RET();          \
}
FLOAT_UNOP(abs)
FLOAT_UNOP(chs)
#undef FLOAT_UNOP

FLOAT_OP(mov, d)
{
    FDT2 = FDT0;
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(mov, s)
{
    FST2 = FST0;
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(mov, ps)
{
    FST2 = FST0;
    FSTH2 = FSTH0;
    DEBUG_FPU_STATE();
    FORCE_RET();
}
FLOAT_OP(alnv, ps)
{
    switch (T0 & 0x7) {
    case 0:
        FST2 = FST0;
        FSTH2 = FSTH0;
        break;
    case 4:
#ifdef TARGET_WORDS_BIGENDIAN
        FSTH2 = FST0;
        FST2 = FSTH1;
#else
        FSTH2 = FST1;
        FST2 = FSTH0;
#endif
        break;
    default: /* unpredictable */
        break;
    }
    DEBUG_FPU_STATE();
    FORCE_RET();
}

#ifdef CONFIG_SOFTFLOAT
#define clear_invalid() do {                                \
    int flags = get_float_exception_flags(&env->fpu->fp_status); \
    flags &= ~float_flag_invalid;                           \
    set_float_exception_flags(flags, &env->fpu->fp_status); \
} while(0)
#else
#define clear_invalid() do { } while(0)
#endif

extern void dump_fpu_s(CPUState *env);

#define CMP_OP(fmt, op)                                \
void OPPROTO op_cmp ## _ ## fmt ## _ ## op(void)       \
{                                                      \
    CALL_FROM_TB1(do_cmp ## _ ## fmt ## _ ## op, PARAM1); \
    DEBUG_FPU_STATE();                                 \
    FORCE_RET();                                       \
}                                                      \
void OPPROTO op_cmpabs ## _ ## fmt ## _ ## op(void)    \
{                                                      \
    CALL_FROM_TB1(do_cmpabs ## _ ## fmt ## _ ## op, PARAM1); \
    DEBUG_FPU_STATE();                                 \
    FORCE_RET();                                       \
}
#define CMP_OPS(op)   \
CMP_OP(d, op)         \
CMP_OP(s, op)         \
CMP_OP(ps, op)

CMP_OPS(f)
CMP_OPS(un)
CMP_OPS(eq)
CMP_OPS(ueq)
CMP_OPS(olt)
CMP_OPS(ult)
CMP_OPS(ole)
CMP_OPS(ule)
CMP_OPS(sf)
CMP_OPS(ngle)
CMP_OPS(seq)
CMP_OPS(ngl)
CMP_OPS(lt)
CMP_OPS(nge)
CMP_OPS(le)
CMP_OPS(ngt)
#undef CMP_OPS
#undef CMP_OP

void op_bc1f (void)
{
    T0 = !!(~GET_FP_COND(env->fpu) & (0x1 << PARAM1));
    DEBUG_FPU_STATE();
    FORCE_RET();
}
void op_bc1any2f (void)
{
    T0 = !!(~GET_FP_COND(env->fpu) & (0x3 << PARAM1));
    DEBUG_FPU_STATE();
    FORCE_RET();
}
void op_bc1any4f (void)
{
    T0 = !!(~GET_FP_COND(env->fpu) & (0xf << PARAM1));
    DEBUG_FPU_STATE();
    FORCE_RET();
}

void op_bc1t (void)
{
    T0 = !!(GET_FP_COND(env->fpu) & (0x1 << PARAM1));
    DEBUG_FPU_STATE();
    FORCE_RET();
}
void op_bc1any2t (void)
{
    T0 = !!(GET_FP_COND(env->fpu) & (0x3 << PARAM1));
    DEBUG_FPU_STATE();
    FORCE_RET();
}
void op_bc1any4t (void)
{
    T0 = !!(GET_FP_COND(env->fpu) & (0xf << PARAM1));
    DEBUG_FPU_STATE();
    FORCE_RET();
}

void op_tlbwi (void)
{
    CALL_FROM_TB0(env->tlb->do_tlbwi);
    FORCE_RET();
}

void op_tlbwr (void)
{
    CALL_FROM_TB0(env->tlb->do_tlbwr);
    FORCE_RET();
}

void op_tlbp (void)
{
    CALL_FROM_TB0(env->tlb->do_tlbp);
    FORCE_RET();
}

void op_tlbr (void)
{
    CALL_FROM_TB0(env->tlb->do_tlbr);
    FORCE_RET();
}

/* Specials */
#if defined (CONFIG_USER_ONLY)
void op_tls_value (void)
{
    T0 = env->tls_value;
}
#endif

void op_pmon (void)
{
    CALL_FROM_TB1(do_pmon, PARAM1);
    FORCE_RET();
}

void op_di (void)
{
    T0 = env->CP0_Status;
    env->CP0_Status = T0 & ~(1 << CP0St_IE);
    CALL_FROM_TB1(cpu_mips_update_irq, env);
    FORCE_RET();
}

void op_ei (void)
{
    T0 = env->CP0_Status;
    env->CP0_Status = T0 | (1 << CP0St_IE);
    CALL_FROM_TB1(cpu_mips_update_irq, env);
    FORCE_RET();
}

void op_trap (void)
{
    if (T0) {
        CALL_FROM_TB1(do_raise_exception, EXCP_TRAP);
    }
    FORCE_RET();
}

void op_debug (void)
{
    CALL_FROM_TB1(do_raise_exception, EXCP_DEBUG);
    FORCE_RET();
}

void op_set_lladdr (void)
{
    env->CP0_LLAddr = T2;
    FORCE_RET();
}

void debug_pre_eret (void);
void debug_post_eret (void);
void op_eret (void)
{
    if (loglevel & CPU_LOG_EXEC)
        CALL_FROM_TB0(debug_pre_eret);
    if (env->CP0_Status & (1 << CP0St_ERL)) {
        env->PC[env->current_tc] = env->CP0_ErrorEPC;
        env->CP0_Status &= ~(1 << CP0St_ERL);
    } else {
        env->PC[env->current_tc] = env->CP0_EPC;
        env->CP0_Status &= ~(1 << CP0St_EXL);
    }
    CALL_FROM_TB1(compute_hflags, env);
    if (loglevel & CPU_LOG_EXEC)
        CALL_FROM_TB0(debug_post_eret);
    env->CP0_LLAddr = 1;
    FORCE_RET();
}

void op_deret (void)
{
    if (loglevel & CPU_LOG_EXEC)
        CALL_FROM_TB0(debug_pre_eret);
    env->PC[env->current_tc] = env->CP0_DEPC;
    env->hflags &= MIPS_HFLAG_DM;
    CALL_FROM_TB1(compute_hflags, env);
    if (loglevel & CPU_LOG_EXEC)
        CALL_FROM_TB0(debug_post_eret);
    env->CP0_LLAddr = 1;
    FORCE_RET();
}

void op_rdhwr_cpunum(void)
{
    if ((env->hflags & MIPS_HFLAG_CP0) ||
        (env->CP0_HWREna & (1 << 0)))
        T0 = env->CP0_EBase & 0x3ff;
    else
        CALL_FROM_TB1(do_raise_exception, EXCP_RI);
    FORCE_RET();
}

void op_rdhwr_synci_step(void)
{
    if ((env->hflags & MIPS_HFLAG_CP0) ||
        (env->CP0_HWREna & (1 << 1)))
        T0 = env->SYNCI_Step;
    else
        CALL_FROM_TB1(do_raise_exception, EXCP_RI);
    FORCE_RET();
}

void op_rdhwr_cc(void)
{
    if ((env->hflags & MIPS_HFLAG_CP0) ||
        (env->CP0_HWREna & (1 << 2)))
        T0 = env->CP0_Count;
    else
        CALL_FROM_TB1(do_raise_exception, EXCP_RI);
    FORCE_RET();
}

void op_rdhwr_ccres(void)
{
    if ((env->hflags & MIPS_HFLAG_CP0) ||
        (env->CP0_HWREna & (1 << 3)))
        T0 = env->CCRes;
    else
        CALL_FROM_TB1(do_raise_exception, EXCP_RI);
    FORCE_RET();
}

void op_save_state (void)
{
    env->hflags = PARAM1;
    FORCE_RET();
}

void op_save_pc (void)
{
    env->PC[env->current_tc] = PARAM1;
    FORCE_RET();
}

#if defined(TARGET_MIPS64)
void op_save_pc64 (void)
{
    env->PC[env->current_tc] = ((uint64_t)PARAM1 << 32) | (uint32_t)PARAM2;
    FORCE_RET();
}
#endif

void op_interrupt_restart (void)
{
    if (!(env->CP0_Status & (1 << CP0St_EXL)) &&
        !(env->CP0_Status & (1 << CP0St_ERL)) &&
        !(env->hflags & MIPS_HFLAG_DM) &&
        (env->CP0_Status & (1 << CP0St_IE)) &&
        (env->CP0_Status & env->CP0_Cause & CP0Ca_IP_mask)) {
        env->CP0_Cause &= ~(0x1f << CP0Ca_EC);
        CALL_FROM_TB1(do_raise_exception, EXCP_EXT_INTERRUPT);
    }
    FORCE_RET();
}

void op_raise_exception (void)
{
    CALL_FROM_TB1(do_raise_exception, PARAM1);
    FORCE_RET();
}

void op_raise_exception_err (void)
{
    CALL_FROM_TB2(do_raise_exception_err, PARAM1, PARAM2);
    FORCE_RET();
}

void op_exit_tb (void)
{
    EXIT_TB();
    FORCE_RET();
}

void op_wait (void)
{
    env->halted = 1;
    CALL_FROM_TB1(do_raise_exception, EXCP_HLT);
    FORCE_RET();
}

/* Bitfield operations. */
void op_ext(void)
{
    unsigned int pos = PARAM1;
    unsigned int size = PARAM2;

    T0 = (int32_t)((T1 >> pos) & ((size < 32) ? ((1 << size) - 1) : ~0));
    FORCE_RET();
}

void op_ins(void)
{
    unsigned int pos = PARAM1;
    unsigned int size = PARAM2;
    target_ulong mask = ((size < 32) ? ((1 << size) - 1) : ~0) << pos;

    T0 = (int32_t)((T0 & ~mask) | ((T1 << pos) & mask));
    FORCE_RET();
}

void op_wsbh(void)
{
    T0 = (int32_t)(((T1 << 8) & ~0x00FF00FF) | ((T1 >> 8) & 0x00FF00FF));
    FORCE_RET();
}

#if defined(TARGET_MIPS64)
void op_dext(void)
{
    unsigned int pos = PARAM1;
    unsigned int size = PARAM2;

    T0 = (T1 >> pos) & ((size < 64) ? ((1ULL << size) - 1) : ~0ULL);
    FORCE_RET();
}

void op_dins(void)
{
    unsigned int pos = PARAM1;
    unsigned int size = PARAM2;
    target_ulong mask = ((size < 64) ? ((1ULL << size) - 1) : ~0ULL) << pos;

    T0 = (T0 & ~mask) | ((T1 << pos) & mask);
    FORCE_RET();
}

void op_dsbh(void)
{
    T0 = ((T1 << 8) & ~0x00FF00FF00FF00FFULL) | ((T1 >> 8) & 0x00FF00FF00FF00FFULL);
    FORCE_RET();
}

void op_dshd(void)
{
    T1 = ((T1 << 16) & ~0x0000FFFF0000FFFFULL) | ((T1 >> 16) & 0x0000FFFF0000FFFFULL);
    T0 = (T1 << 32) | (T1 >> 32);
    FORCE_RET();
}
#endif

void op_seb(void)
{
    T0 = ((T1 & 0xFF) ^ 0x80) - 0x80;
    FORCE_RET();
}

void op_seh(void)
{
    T0 = ((T1 & 0xFFFF) ^ 0x8000) - 0x8000;
    FORCE_RET();
}
