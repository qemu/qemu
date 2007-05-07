/*
 *  MIPS emulation micro-operations for qemu.
 * 
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *  Copyright (c) 2006 Marius Groeger (FPU operations)
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
    RETURN();
}

void op_load_HI (void)
{
    T0 = env->HI;
    RETURN();
}

void op_store_HI (void)
{
    env->HI = T0;
    RETURN();
}

void op_load_LO (void)
{
    T0 = env->LO;
    RETURN();
}

void op_store_LO (void)
{
    env->LO = T0;
    RETURN();
}

/* Load and store */
#define MEMSUFFIX _raw
#include "op_mem.c"
#undef MEMSUFFIX
#if !defined(CONFIG_USER_ONLY)
#define MEMSUFFIX _user
#include "op_mem.c"
#undef MEMSUFFIX

#define MEMSUFFIX _kernel
#include "op_mem.c"
#undef MEMSUFFIX
#endif

/* Arithmetic */
void op_add (void)
{
    T0 = (int32_t)((int32_t)T0 + (int32_t)T1);
    RETURN();
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
    RETURN();
}

void op_sub (void)
{
    T0 = (int32_t)((int32_t)T0 - (int32_t)T1);
    RETURN();
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
    RETURN();
}

void op_mul (void)
{
    T0 = (int32_t)((int32_t)T0 * (int32_t)T1);
    RETURN();
}

#if HOST_LONG_BITS < 64
void op_div (void)
{
    CALL_FROM_TB0(do_div);
    RETURN();
}
#else
void op_div (void)
{
    if (T1 != 0) {
        env->LO = (int32_t)((int64_t)(int32_t)T0 / (int32_t)T1);
        env->HI = (int32_t)((int64_t)(int32_t)T0 % (int32_t)T1);
    }
    RETURN();
}
#endif

void op_divu (void)
{
    if (T1 != 0) {
        env->LO = (int32_t)((uint32_t)T0 / (uint32_t)T1);
        env->HI = (int32_t)((uint32_t)T0 % (uint32_t)T1);
    }
    RETURN();
}

#ifdef TARGET_MIPS64
/* Arithmetic */
void op_dadd (void)
{
    T0 += T1;
    RETURN();
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
    RETURN();
}

void op_dsub (void)
{
    T0 -= T1;
    RETURN();
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
    RETURN();
}

void op_dmul (void)
{
    T0 = (int64_t)T0 * (int64_t)T1;
    RETURN();
}

/* Those might call libgcc functions.  */
void op_ddiv (void)
{
    do_ddiv();
    RETURN();
}

#if TARGET_LONG_BITS > HOST_LONG_BITS
void op_ddivu (void)
{
    do_ddivu();
    RETURN();
}
#else
void op_ddivu (void)
{
    if (T1 != 0) {
        env->LO = T0 / T1;
        env->HI = T0 % T1;
    }
    RETURN();
}
#endif
#endif /* TARGET_MIPS64 */

/* Logical */
void op_and (void)
{
    T0 &= T1;
    RETURN();
}

void op_nor (void)
{
    T0 = ~(T0 | T1);
    RETURN();
}

void op_or (void)
{
    T0 |= T1;
    RETURN();
}

void op_xor (void)
{
    T0 ^= T1;
    RETURN();
}

void op_sll (void)
{
    T0 = (int32_t)((uint32_t)T0 << T1);
    RETURN();
}

void op_sra (void)
{
    T0 = (int32_t)((int32_t)T0 >> T1);
    RETURN();
}

void op_srl (void)
{
    T0 = (int32_t)((uint32_t)T0 >> T1);
    RETURN();
}

void op_rotr (void)
{
    target_ulong tmp;

    if (T1) {
       tmp = (int32_t)((uint32_t)T0 << (0x20 - T1));
       T0 = (int32_t)((uint32_t)T0 >> T1) | tmp;
    }
    RETURN();
}

void op_sllv (void)
{
    T0 = (int32_t)((uint32_t)T1 << ((uint32_t)T0 & 0x1F));
    RETURN();
}

void op_srav (void)
{
    T0 = (int32_t)((int32_t)T1 >> (T0 & 0x1F));
    RETURN();
}

void op_srlv (void)
{
    T0 = (int32_t)((uint32_t)T1 >> (T0 & 0x1F));
    RETURN();
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
    RETURN();
}

void op_clo (void)
{
    int n;

    if (T0 == ~((target_ulong)0)) {
        T0 = 32;
    } else {
        for (n = 0; n < 32; n++) {
            if (!(T0 & (1 << 31)))
                break;
            T0 = T0 << 1;
        }
        T0 = n;
    }
    RETURN();
}

void op_clz (void)
{
    int n;

    if (T0 == 0) {
        T0 = 32;
    } else {
        for (n = 0; n < 32; n++) {
            if (T0 & (1 << 31))
                break;
            T0 = T0 << 1;
        }
        T0 = n;
    }
    RETURN();
}

#ifdef TARGET_MIPS64

#if TARGET_LONG_BITS > HOST_LONG_BITS
/* Those might call libgcc functions.  */
void op_dsll (void)
{
    CALL_FROM_TB0(do_dsll);
    RETURN();
}

void op_dsll32 (void)
{
    CALL_FROM_TB0(do_dsll32);
    RETURN();
}

void op_dsra (void)
{
    CALL_FROM_TB0(do_dsra);
    RETURN();
}

void op_dsra32 (void)
{
    CALL_FROM_TB0(do_dsra32);
    RETURN();
}

void op_dsrl (void)
{
    CALL_FROM_TB0(do_dsrl);
    RETURN();
}

void op_dsrl32 (void)
{
    CALL_FROM_TB0(do_dsrl32);
    RETURN();
}

void op_drotr (void)
{
    CALL_FROM_TB0(do_drotr);
    RETURN();
}

void op_drotr32 (void)
{
    CALL_FROM_TB0(do_drotr32);
    RETURN();
}

void op_dsllv (void)
{
    CALL_FROM_TB0(do_dsllv);
    RETURN();
}

void op_dsrav (void)
{
    CALL_FROM_TB0(do_dsrav);
    RETURN();
}

void op_dsrlv (void)
{
    CALL_FROM_TB0(do_dsrlv);
    RETURN();
}

void op_drotrv (void)
{
    CALL_FROM_TB0(do_drotrv);
    RETURN();
}

#else /* TARGET_LONG_BITS > HOST_LONG_BITS */

void op_dsll (void)
{
    T0 = T0 << T1;
    RETURN();
}

void op_dsll32 (void)
{
    T0 = T0 << (T1 + 32);
    RETURN();
}

void op_dsra (void)
{
    T0 = (int64_t)T0 >> T1;
    RETURN();
}

void op_dsra32 (void)
{
    T0 = (int64_t)T0 >> (T1 + 32);
    RETURN();
}

void op_dsrl (void)
{
    T0 = T0 >> T1;
    RETURN();
}

void op_dsrl32 (void)
{
    T0 = T0 >> (T1 + 32);
    RETURN();
}

void op_drotr (void)
{
    target_ulong tmp;

    if (T1) {
       tmp = T0 << (0x40 - T1);
       T0 = (T0 >> T1) | tmp;
    }
    RETURN();
}

void op_drotr32 (void)
{
    target_ulong tmp;

    if (T1) {
       tmp = T0 << (0x40 - (32 + T1));
       T0 = (T0 >> (32 + T1)) | tmp;
    }
    RETURN();
}

void op_dsllv (void)
{
    T0 = T1 << (T0 & 0x3F);
    RETURN();
}

void op_dsrav (void)
{
    T0 = (int64_t)T1 >> (T0 & 0x3F);
    RETURN();
}

void op_dsrlv (void)
{
    T0 = T1 >> (T0 & 0x3F);
    RETURN();
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
    RETURN();
}
#endif /* TARGET_LONG_BITS > HOST_LONG_BITS */

void op_dclo (void)
{
    int n;

    if (T0 == ~((target_ulong)0)) {
        T0 = 64;
    } else {
        for (n = 0; n < 64; n++) {
            if (!(T0 & (1ULL << 63)))
                break;
            T0 = T0 << 1;
        }
        T0 = n;
    }
    RETURN();
}

void op_dclz (void)
{
    int n;

    if (T0 == 0) {
        T0 = 64;
    } else {
        for (n = 0; n < 64; n++) {
            if (T0 & (1ULL << 63))
                break;
            T0 = T0 << 1;
        }
        T0 = n;
    }
    RETURN();
}
#endif

/* 64 bits arithmetic */
#if TARGET_LONG_BITS > HOST_LONG_BITS
void op_mult (void)
{
    CALL_FROM_TB0(do_mult);
    RETURN();
}

void op_multu (void)
{
    CALL_FROM_TB0(do_multu);
    RETURN();
}

void op_madd (void)
{
    CALL_FROM_TB0(do_madd);
    RETURN();
}

void op_maddu (void)
{
    CALL_FROM_TB0(do_maddu);
    RETURN();
}

void op_msub (void)
{
    CALL_FROM_TB0(do_msub);
    RETURN();
}

void op_msubu (void)
{
    CALL_FROM_TB0(do_msubu);
    RETURN();
}

#else /* TARGET_LONG_BITS > HOST_LONG_BITS */

static inline uint64_t get_HILO (void)
{
    return ((uint64_t)env->HI << 32) | ((uint64_t)(uint32_t)env->LO);
}

static inline void set_HILO (uint64_t HILO)
{
    env->LO = (int32_t)(HILO & 0xFFFFFFFF);
    env->HI = (int32_t)(HILO >> 32);
}

void op_mult (void)
{
    set_HILO((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1);
    RETURN();
}

void op_multu (void)
{
    set_HILO((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1);
    RETURN();
}

void op_madd (void)
{
    int64_t tmp;

    tmp = ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1);
    set_HILO((int64_t)get_HILO() + tmp);
    RETURN();
}

void op_maddu (void)
{
    uint64_t tmp;

    tmp = ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1);
    set_HILO(get_HILO() + tmp);
    RETURN();
}

void op_msub (void)
{
    int64_t tmp;

    tmp = ((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1);
    set_HILO((int64_t)get_HILO() - tmp);
    RETURN();
}

void op_msubu (void)
{
    uint64_t tmp;

    tmp = ((uint64_t)(uint32_t)T0 * (uint64_t)(uint32_t)T1);
    set_HILO(get_HILO() - tmp);
    RETURN();
}
#endif /* TARGET_LONG_BITS > HOST_LONG_BITS */

#ifdef TARGET_MIPS64
void op_dmult (void)
{
    CALL_FROM_TB0(do_dmult);
    RETURN();
}

void op_dmultu (void)
{
    CALL_FROM_TB0(do_dmultu);
    RETURN();
}
#endif

/* Conditional moves */
void op_movn (void)
{
    if (T1 != 0)
        env->gpr[PARAM1] = T0;
    RETURN();
}

void op_movz (void)
{
    if (T1 == 0)
        env->gpr[PARAM1] = T0;
    RETURN();
}

void op_movf (void)
{
    if (!(env->fcr31 & PARAM1))
        T0 = T1;
    RETURN();
}

void op_movt (void)
{
    if (env->fcr31 & PARAM1)
        T0 = T1;
    RETURN();
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
    RETURN();               \
}

OP_COND(eq, T0 == T1);
OP_COND(ne, T0 != T1);
OP_COND(ge, (int32_t)T0 >= (int32_t)T1);
OP_COND(geu, T0 >= T1);
OP_COND(lt, (int32_t)T0 < (int32_t)T1);
OP_COND(ltu, T0 < T1);
OP_COND(gez, (int32_t)T0 >= 0);
OP_COND(gtz, (int32_t)T0 > 0);
OP_COND(lez, (int32_t)T0 <= 0);
OP_COND(ltz, (int32_t)T0 < 0);

/* Branches */
//#undef USE_DIRECT_JUMP

void OPPROTO op_goto_tb0(void)
{
    GOTO_TB(op_goto_tb0, PARAM1, 0);
    RETURN();
}

void OPPROTO op_goto_tb1(void)
{
    GOTO_TB(op_goto_tb1, PARAM1, 1);
    RETURN();
}

/* Branch to register */
void op_save_breg_target (void)
{
    env->btarget = T2;
    RETURN();
}

void op_restore_breg_target (void)
{
    T2 = env->btarget;
    RETURN();
}

void op_breg (void)
{
    env->PC = T2;
    RETURN();
}

void op_save_btarget (void)
{
    env->btarget = PARAM1;
    RETURN();
}

/* Conditional branch */
void op_set_bcond (void)
{
    T2 = T0;
    RETURN();
}

void op_save_bcond (void)
{
    env->bcond = T2;
    RETURN();
}

void op_restore_bcond (void)
{
    T2 = env->bcond;
    RETURN();
}

void op_jnz_T2 (void)
{
    if (T2)
        GOTO_LABEL_PARAM(1);
    RETURN();
}

/* CP0 functions */
void op_mfc0_index (void)
{
    T0 = env->CP0_Index;
    RETURN();
}

void op_mfc0_random (void)
{
    CALL_FROM_TB0(do_mfc0_random);
    RETURN();
}

void op_mfc0_entrylo0 (void)
{
    T0 = (int32_t)env->CP0_EntryLo0;
    RETURN();
}

void op_mfc0_entrylo1 (void)
{
    T0 = (int32_t)env->CP0_EntryLo1;
    RETURN();
}

void op_mfc0_context (void)
{
    T0 = (int32_t)env->CP0_Context;
    RETURN();
}

void op_mfc0_pagemask (void)
{
    T0 = env->CP0_PageMask;
    RETURN();
}

void op_mfc0_pagegrain (void)
{
    T0 = env->CP0_PageGrain;
    RETURN();
}

void op_mfc0_wired (void)
{
    T0 = env->CP0_Wired;
    RETURN();
}

void op_mfc0_hwrena (void)
{
    T0 = env->CP0_HWREna;
    RETURN();
}

void op_mfc0_badvaddr (void)
{
    T0 = (int32_t)env->CP0_BadVAddr;
    RETURN();
}

void op_mfc0_count (void)
{
    CALL_FROM_TB0(do_mfc0_count);
    RETURN();
}

void op_mfc0_entryhi (void)
{
    T0 = (int32_t)env->CP0_EntryHi;
    RETURN();
}

void op_mfc0_compare (void)
{
    T0 = env->CP0_Compare;
    RETURN();
}

void op_mfc0_status (void)
{
    T0 = env->CP0_Status;
    RETURN();
}

void op_mfc0_intctl (void)
{
    T0 = env->CP0_IntCtl;
    RETURN();
}

void op_mfc0_srsctl (void)
{
    T0 = env->CP0_SRSCtl;
    RETURN();
}

void op_mfc0_srsmap (void)
{
    T0 = env->CP0_SRSMap;
    RETURN();
}

void op_mfc0_cause (void)
{
    T0 = env->CP0_Cause;
    RETURN();
}

void op_mfc0_epc (void)
{
    T0 = (int32_t)env->CP0_EPC;
    RETURN();
}

void op_mfc0_prid (void)
{
    T0 = env->CP0_PRid;
    RETURN();
}

void op_mfc0_ebase (void)
{
    T0 = env->CP0_EBase;
    RETURN();
}

void op_mfc0_config0 (void)
{
    T0 = env->CP0_Config0;
    RETURN();
}

void op_mfc0_config1 (void)
{
    T0 = env->CP0_Config1;
    RETURN();
}

void op_mfc0_config2 (void)
{
    T0 = env->CP0_Config2;
    RETURN();
}

void op_mfc0_config3 (void)
{
    T0 = env->CP0_Config3;
    RETURN();
}

void op_mfc0_config6 (void)
{
    T0 = env->CP0_Config6;
    RETURN();
}

void op_mfc0_config7 (void)
{
    T0 = env->CP0_Config7;
    RETURN();
}

void op_mfc0_lladdr (void)
{
    T0 = (int32_t)env->CP0_LLAddr >> 4;
    RETURN();
}

void op_mfc0_watchlo0 (void)
{
    T0 = (int32_t)env->CP0_WatchLo;
    RETURN();
}

void op_mfc0_watchhi0 (void)
{
    T0 = env->CP0_WatchHi;
    RETURN();
}

void op_mfc0_xcontext (void)
{
    T0 = (int32_t)env->CP0_XContext;
    RETURN();
}

void op_mfc0_framemask (void)
{
    T0 = env->CP0_Framemask;
    RETURN();
}

void op_mfc0_debug (void)
{
    T0 = env->CP0_Debug;
    if (env->hflags & MIPS_HFLAG_DM)
        T0 |= 1 << CP0DB_DM;
    RETURN();
}

void op_mfc0_depc (void)
{
    T0 = (int32_t)env->CP0_DEPC;
    RETURN();
}

void op_mfc0_performance0 (void)
{
    T0 = env->CP0_Performance0;
    RETURN();
}

void op_mfc0_taglo (void)
{
    T0 = env->CP0_TagLo;
    RETURN();
}

void op_mfc0_datalo (void)
{
    T0 = env->CP0_DataLo;
    RETURN();
}

void op_mfc0_taghi (void)
{
    T0 = env->CP0_TagHi;
    RETURN();
}

void op_mfc0_datahi (void)
{
    T0 = env->CP0_DataHi;
    RETURN();
}

void op_mfc0_errorepc (void)
{
    T0 = (int32_t)env->CP0_ErrorEPC;
    RETURN();
}

void op_mfc0_desave (void)
{
    T0 = env->CP0_DESAVE;
    RETURN();
}

void op_mtc0_index (void)
{
    env->CP0_Index = (env->CP0_Index & 0x80000000) | (T0 % env->nb_tlb);
    RETURN();
}

void op_mtc0_entrylo0 (void)
{
    /* Large physaddr not implemented */
    /* 1k pages not implemented */
    env->CP0_EntryLo0 = (int32_t)T0 & 0x3FFFFFFF;
    RETURN();
}

void op_mtc0_entrylo1 (void)
{
    /* Large physaddr not implemented */
    /* 1k pages not implemented */
    env->CP0_EntryLo1 = (int32_t)T0 & 0x3FFFFFFF;
    RETURN();
}

void op_mtc0_context (void)
{
    env->CP0_Context = (env->CP0_Context & 0x007FFFFF) | (T0 & ~0x007FFFFF);
    RETURN();
}

void op_mtc0_pagemask (void)
{
    /* 1k pages not implemented */
    env->CP0_PageMask = T0 & 0x1FFFE000;
    RETURN();
}

void op_mtc0_pagegrain (void)
{
    /* SmartMIPS not implemented */
    /* Large physaddr not implemented */
    /* 1k pages not implemented */
    env->CP0_PageGrain = 0;
    RETURN();
}

void op_mtc0_wired (void)
{
    env->CP0_Wired = T0 % env->nb_tlb;
    RETURN();
}

void op_mtc0_hwrena (void)
{
    env->CP0_HWREna = T0 & 0x0000000F;
    RETURN();
}

void op_mtc0_count (void)
{
    CALL_FROM_TB2(cpu_mips_store_count, env, T0);
    RETURN();
}

void op_mtc0_entryhi (void)
{
    target_ulong old, val;

    /* 1k pages not implemented */
    /* Ignore MIPS64 TLB for now */
    val = (target_ulong)(int32_t)T0 & ~(target_ulong)0x1F00;
    old = env->CP0_EntryHi;
    env->CP0_EntryHi = val;
    /* If the ASID changes, flush qemu's TLB.  */
    if ((old & 0xFF) != (val & 0xFF))
        CALL_FROM_TB2(cpu_mips_tlb_flush, env, 1);
    RETURN();
}

void op_mtc0_compare (void)
{
    CALL_FROM_TB2(cpu_mips_store_compare, env, T0);
    RETURN();
}

void op_mtc0_status (void)
{
    uint32_t val, old;
    uint32_t mask = env->Status_rw_bitmask;

    /* No reverse endianness, no MDMX/DSP, no 64bit ops,
       no 64bit addressing implemented. */
    val = (int32_t)T0 & mask;
    old = env->CP0_Status;
    if (!(val & (1 << CP0St_EXL)) &&
        !(val & (1 << CP0St_ERL)) &&
        !(env->hflags & MIPS_HFLAG_DM) &&
        (val & (1 << CP0St_UM)))
        env->hflags |= MIPS_HFLAG_UM;
    env->CP0_Status = (env->CP0_Status & ~mask) | val;
    if (loglevel & CPU_LOG_EXEC)
        CALL_FROM_TB2(do_mtc0_status_debug, old, val);
    CALL_FROM_TB1(cpu_mips_update_irq, env);
    RETURN();
}

void op_mtc0_intctl (void)
{
    /* vectored interrupts not implemented, timer on int 7,
       no performance counters. */
    env->CP0_IntCtl |= T0 & 0x000002e0;
    RETURN();
}

void op_mtc0_srsctl (void)
{
    /* shadow registers not implemented */
    env->CP0_SRSCtl = 0;
    RETURN();
}

void op_mtc0_srsmap (void)
{
    /* shadow registers not implemented */
    env->CP0_SRSMap = 0;
    RETURN();
}

void op_mtc0_cause (void)
{
    uint32_t mask = 0x00C00300;

    if ((env->CP0_Config0 & (0x7 << CP0C0_AR)) == (1 << CP0C0_AR))
        mask |= 1 << CP0Ca_DC;

    env->CP0_Cause = (env->CP0_Cause & ~mask) | (T0 & mask);

    /* Handle the software interrupt as an hardware one, as they
       are very similar */
    if (T0 & CP0Ca_IP_mask) {
        CALL_FROM_TB1(cpu_mips_update_irq, env);
    }
    RETURN();
}

void op_mtc0_epc (void)
{
    env->CP0_EPC = (int32_t)T0;
    RETURN();
}

void op_mtc0_ebase (void)
{
    /* vectored interrupts not implemented */
    /* Multi-CPU not implemented */
    env->CP0_EBase = 0x80000000 | (T0 & 0x3FFFF000);
    RETURN();
}

void op_mtc0_config0 (void)
{
#if defined(MIPS_USES_R4K_TLB)
     /* Fixed mapping MMU not implemented */
    env->CP0_Config0 = (env->CP0_Config0 & 0x8017FF88) | (T0 & 0x00000001);
#else
    env->CP0_Config0 = (env->CP0_Config0 & 0xFE17FF88) | (T0 & 0x00000001);
#endif
    RETURN();
}

void op_mtc0_config2 (void)
{
    /* tertiary/secondary caches not implemented */
    env->CP0_Config2 = (env->CP0_Config2 & 0x8FFF0FFF);
    RETURN();
}

void op_mtc0_watchlo0 (void)
{
    /* Watch exceptions for instructions, data loads, data stores
       not implemented. */
    env->CP0_WatchLo = (int32_t)(T0 & ~0x7);
    RETURN();
}

void op_mtc0_watchhi0 (void)
{
    env->CP0_WatchHi = (T0 & 0x40FF0FF8);
    env->CP0_WatchHi &= ~(env->CP0_WatchHi & T0 & 0x7);
    RETURN();
}

void op_mtc0_framemask (void)
{
    env->CP0_Framemask = T0; /* XXX */
    RETURN();
}

void op_mtc0_debug (void)
{
    env->CP0_Debug = (env->CP0_Debug & 0x8C03FC1F) | (T0 & 0x13300120);
    if (T0 & (1 << CP0DB_DM))
        env->hflags |= MIPS_HFLAG_DM;
    else
        env->hflags &= ~MIPS_HFLAG_DM;
    RETURN();
}

void op_mtc0_depc (void)
{
    env->CP0_DEPC = (int32_t)T0;
    RETURN();
}

void op_mtc0_performance0 (void)
{
    env->CP0_Performance0 = T0; /* XXX */
    RETURN();
}

void op_mtc0_taglo (void)
{
    env->CP0_TagLo = T0 & 0xFFFFFCF6;
    RETURN();
}

void op_mtc0_datalo (void)
{
    env->CP0_DataLo = T0; /* XXX */
    RETURN();
}

void op_mtc0_taghi (void)
{
    env->CP0_TagHi = T0; /* XXX */
    RETURN();
}

void op_mtc0_datahi (void)
{
    env->CP0_DataHi = T0; /* XXX */
    RETURN();
}

void op_mtc0_errorepc (void)
{
    env->CP0_ErrorEPC = (int32_t)T0;
    RETURN();
}

void op_mtc0_desave (void)
{
    env->CP0_DESAVE = T0;
    RETURN();
}

#ifdef TARGET_MIPS64
void op_dmfc0_entrylo0 (void)
{
    T0 = env->CP0_EntryLo0;
    RETURN();
}

void op_dmfc0_entrylo1 (void)
{
    T0 = env->CP0_EntryLo1;
    RETURN();
}

void op_dmfc0_context (void)
{
    T0 = env->CP0_Context;
    RETURN();
}

void op_dmfc0_badvaddr (void)
{
    T0 = env->CP0_BadVAddr;
    RETURN();
}

void op_dmfc0_entryhi (void)
{
    T0 = env->CP0_EntryHi;
    RETURN();
}

void op_dmfc0_epc (void)
{
    T0 = env->CP0_EPC;
    RETURN();
}

void op_dmfc0_lladdr (void)
{
    T0 = env->CP0_LLAddr >> 4;
    RETURN();
}

void op_dmfc0_watchlo0 (void)
{
    T0 = env->CP0_WatchLo;
    RETURN();
}

void op_dmfc0_xcontext (void)
{
    T0 = env->CP0_XContext;
    RETURN();
}

void op_dmfc0_depc (void)
{
    T0 = env->CP0_DEPC;
    RETURN();
}

void op_dmfc0_errorepc (void)
{
    T0 = env->CP0_ErrorEPC;
    RETURN();
}

void op_dmtc0_entrylo0 (void)
{
    /* Large physaddr not implemented */
    /* 1k pages not implemented */
    env->CP0_EntryLo0 = T0 & 0x3FFFFFFF;
    RETURN();
}

void op_dmtc0_entrylo1 (void)
{
    /* Large physaddr not implemented */
    /* 1k pages not implemented */
    env->CP0_EntryLo1 = T0 & 0x3FFFFFFF;
    RETURN();
}

void op_dmtc0_context (void)
{
    env->CP0_Context = (env->CP0_Context & 0x007FFFFF) | (T0 & ~0x007FFFFF);
    RETURN();
}

void op_dmtc0_epc (void)
{
    env->CP0_EPC = T0;
    RETURN();
}

void op_dmtc0_watchlo0 (void)
{
    /* Watch exceptions for instructions, data loads, data stores
       not implemented. */
    env->CP0_WatchLo = T0 & ~0x7;
    RETURN();
}

void op_dmtc0_xcontext (void)
{
    env->CP0_XContext = (env->CP0_XContext & 0xffffffff) | (T0 & ~0xffffffff);
    RETURN();
}

void op_dmtc0_depc (void)
{
    env->CP0_DEPC = T0;
    RETURN();
}

void op_dmtc0_errorepc (void)
{
    env->CP0_ErrorEPC = T0;
    RETURN();
}
#endif /* TARGET_MIPS64 */

/* CP1 functions */
#if 0
# define DEBUG_FPU_STATE() CALL_FROM_TB1(dump_fpu, env)
#else
# define DEBUG_FPU_STATE() do { } while(0)
#endif

void op_cp0_enabled(void)
{
    if (!(env->CP0_Status & (1 << CP0St_CU0)) &&
	(env->hflags & MIPS_HFLAG_UM)) {
        CALL_FROM_TB2(do_raise_exception_err, EXCP_CpU, 0);
    }
    RETURN();
}

void op_cp1_enabled(void)
{
    if (!(env->CP0_Status & (1 << CP0St_CU1))) {
        CALL_FROM_TB2(do_raise_exception_err, EXCP_CpU, 1);
    }
    RETURN();
}

/* convert MIPS rounding mode in FCR31 to IEEE library */
unsigned int ieee_rm[] = { 
    float_round_nearest_even,
    float_round_to_zero,
    float_round_up,
    float_round_down
};

#define RESTORE_ROUNDING_MODE \
    set_float_rounding_mode(ieee_rm[env->fcr31 & 3], &env->fp_status)

inline char ieee_ex_to_mips(char ieee)
{
    return (ieee & float_flag_inexact) >> 5 |
           (ieee & float_flag_underflow) >> 3 |
           (ieee & float_flag_overflow) >> 1 |
           (ieee & float_flag_divbyzero) << 1 |
           (ieee & float_flag_invalid) << 4;
}

inline char mips_ex_to_ieee(char mips)
{
    return (mips & FP_INEXACT) << 5 |
           (mips & FP_UNDERFLOW) << 3 |
           (mips & FP_OVERFLOW) << 1 |
           (mips & FP_DIV0) >> 1 |
           (mips & FP_INVALID) >> 4;
}

inline void update_fcr31(void)
{
    int tmp = ieee_ex_to_mips(get_float_exception_flags(&env->fp_status));

    SET_FP_CAUSE(env->fcr31, tmp);
    if (GET_FP_ENABLE(env->fcr31) & tmp)
        CALL_FROM_TB1(do_raise_exception, EXCP_FPE);
    else
        UPDATE_FP_FLAGS(env->fcr31, tmp);
}


void op_cfc1 (void)
{
    switch (T1) {
    case 0:
        T0 = (int32_t)env->fcr0;
        break;
    case 25:
        T0 = ((env->fcr31 >> 24) & 0xfe) | ((env->fcr31 >> 23) & 0x1);
        break;
    case 26:
        T0 = env->fcr31 & 0x0003f07c;
        break;
    case 28:
        T0 = (env->fcr31 & 0x00000f83) | ((env->fcr31 >> 22) & 0x4);
        break;
    default:
        T0 = (int32_t)env->fcr31;
        break;
    }
    DEBUG_FPU_STATE();
    RETURN();
}

void op_ctc1 (void)
{
    switch(T1) {
    case 25:
        if (T0 & 0xffffff00)
            goto leave;
        env->fcr31 = (env->fcr31 & 0x017fffff) | ((T0 & 0xfe) << 24) |
                     ((T0 & 0x1) << 23);
        break;
    case 26:
        if (T0 & 0x007c0000)
            goto leave;
        env->fcr31 = (env->fcr31 & 0xfffc0f83) | (T0 & 0x0003f07c);
        break;
    case 28:
        if (T0 & 0x007c0000)
            goto leave;
        env->fcr31 = (env->fcr31 & 0xfefff07c) | (T0 & 0x00000f83) |
                     ((T0 & 0x4) << 22);
        break;
    case 31:
        if (T0 & 0x007c0000)
            goto leave;
        env->fcr31 = T0;
        break;
    default:
        goto leave;
    }
    /* set rounding mode */
    RESTORE_ROUNDING_MODE;
    set_float_exception_flags(0, &env->fp_status);
    if ((GET_FP_ENABLE(env->fcr31) | 0x20) & GET_FP_CAUSE(env->fcr31))
        CALL_FROM_TB1(do_raise_exception, EXCP_FPE);
 leave:
    DEBUG_FPU_STATE();
    RETURN();
}

void op_mfc1 (void)
{
    T0 = WT0;
    DEBUG_FPU_STATE();
    RETURN();
}

void op_mtc1 (void)
{
    WT0 = T0;
    DEBUG_FPU_STATE();
    RETURN();
}

void op_dmfc1 (void)
{
    T0 = DT0;
    DEBUG_FPU_STATE();
    RETURN();
}

void op_dmtc1 (void)
{
    DT0 = T0;
    DEBUG_FPU_STATE();
    RETURN();
}

void op_mfhc1 (void)
{
    T0 = WTH0;
    DEBUG_FPU_STATE();
    RETURN();
}

void op_mthc1 (void)
{
    WTH0 = T0;
    DEBUG_FPU_STATE();
    RETURN();
}

/* Float support.
   Single precition routines have a "s" suffix, double precision a
   "d" suffix, 32bit integer "w", 64bit integer "l", paired singe "ps",
   paired single lowwer "pl", paired single upper "pu".  */

#define FLOAT_OP(name, p) void OPPROTO op_float_##name##_##p(void)

FLOAT_OP(cvtd, s)
{
    set_float_exception_flags(0, &env->fp_status);
    FDT2 = float32_to_float64(FST0, &env->fp_status);
    update_fcr31();
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(cvtd, w)
{
    set_float_exception_flags(0, &env->fp_status);
    FDT2 = int32_to_float64(WT0, &env->fp_status);
    update_fcr31();
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(cvtd, l)
{
    set_float_exception_flags(0, &env->fp_status);
    FDT2 = int64_to_float64(DT0, &env->fp_status);
    update_fcr31();
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(cvtl, d)
{
    set_float_exception_flags(0, &env->fp_status);
    DT2 = float64_to_int64(FDT0, &env->fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->fcr31) & (FP_OVERFLOW | FP_INVALID))
        DT2 = 0x7fffffffffffffffULL;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(cvtl, s)
{
    set_float_exception_flags(0, &env->fp_status);
    DT2 = float32_to_int64(FST0, &env->fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->fcr31) & (FP_OVERFLOW | FP_INVALID))
        DT2 = 0x7fffffffffffffffULL;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(cvtps, s)
{
    WT2 = WT0;
    WTH2 = WT1;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(cvtps, pw)
{
    set_float_exception_flags(0, &env->fp_status);
    FST2 = int32_to_float32(WT0, &env->fp_status);
    FSTH2 = int32_to_float32(WTH0, &env->fp_status);
    update_fcr31();
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(cvtpw, ps)
{
    set_float_exception_flags(0, &env->fp_status);
    WT2 = float32_to_int32(FST0, &env->fp_status);
    WTH2 = float32_to_int32(FSTH0, &env->fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = 0x7fffffff;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(cvts, d)
{
    set_float_exception_flags(0, &env->fp_status);
    FST2 = float64_to_float32(FDT0, &env->fp_status);
    update_fcr31();
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(cvts, w)
{
    set_float_exception_flags(0, &env->fp_status);
    FST2 = int32_to_float32(WT0, &env->fp_status);
    update_fcr31();
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(cvts, l)
{
    set_float_exception_flags(0, &env->fp_status);
    FST2 = int64_to_float32(DT0, &env->fp_status);
    update_fcr31();
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(cvts, pl)
{
    set_float_exception_flags(0, &env->fp_status);
    WT2 = WT0;
    update_fcr31();
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(cvts, pu)
{
    set_float_exception_flags(0, &env->fp_status);
    WT2 = WTH0;
    update_fcr31();
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(cvtw, s)
{
    set_float_exception_flags(0, &env->fp_status);
    WT2 = float32_to_int32(FST0, &env->fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = 0x7fffffff;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(cvtw, d)
{
    set_float_exception_flags(0, &env->fp_status);
    WT2 = float64_to_int32(FDT0, &env->fp_status);
    update_fcr31();
    if (GET_FP_CAUSE(env->fcr31) & (FP_OVERFLOW | FP_INVALID))
        WT2 = 0x7fffffff;
    DEBUG_FPU_STATE();
    RETURN();
}

FLOAT_OP(pll, ps)
{
    DT2 = ((uint64_t)WT0 << 32) | WT1;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(plu, ps)
{
    DT2 = ((uint64_t)WT0 << 32) | WTH1;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(pul, ps)
{
    DT2 = ((uint64_t)WTH0 << 32) | WT1;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(puu, ps)
{
    DT2 = ((uint64_t)WTH0 << 32) | WTH1;
    DEBUG_FPU_STATE();
    RETURN();
}

FLOAT_OP(roundl, d)
{
    set_float_rounding_mode(float_round_nearest_even, &env->fp_status);
    DT2 = float64_round_to_int(FDT0, &env->fp_status);
    RESTORE_ROUNDING_MODE;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(roundl, s)
{
    set_float_rounding_mode(float_round_nearest_even, &env->fp_status);
    DT2 = float32_round_to_int(FST0, &env->fp_status);
    RESTORE_ROUNDING_MODE;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(roundw, d)
{
    set_float_rounding_mode(float_round_nearest_even, &env->fp_status);
    WT2 = float64_round_to_int(FDT0, &env->fp_status);
    RESTORE_ROUNDING_MODE;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(roundw, s)
{
    set_float_rounding_mode(float_round_nearest_even, &env->fp_status);
    WT2 = float32_round_to_int(FST0, &env->fp_status);
    RESTORE_ROUNDING_MODE;
    DEBUG_FPU_STATE();
    RETURN();
}

FLOAT_OP(truncl, d)
{
    DT2 = float64_to_int64_round_to_zero(FDT0, &env->fp_status);
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(truncl, s)
{
    DT2 = float32_to_int64_round_to_zero(FST0, &env->fp_status);
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(truncw, d)
{
    WT2 = float64_to_int32_round_to_zero(FDT0, &env->fp_status);
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(truncw, s)
{
    WT2 = float32_to_int32_round_to_zero(FST0, &env->fp_status);
    DEBUG_FPU_STATE();
    RETURN();
}

FLOAT_OP(ceill, d)
{
    set_float_rounding_mode(float_round_up, &env->fp_status);
    DT2 = float64_round_to_int(FDT0, &env->fp_status);
    RESTORE_ROUNDING_MODE;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(ceill, s)
{
    set_float_rounding_mode(float_round_up, &env->fp_status);
    DT2 = float32_round_to_int(FST0, &env->fp_status);
    RESTORE_ROUNDING_MODE;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(ceilw, d)
{
    set_float_rounding_mode(float_round_up, &env->fp_status);
    WT2 = float64_round_to_int(FDT0, &env->fp_status);
    RESTORE_ROUNDING_MODE;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(ceilw, s)
{
    set_float_rounding_mode(float_round_up, &env->fp_status);
    WT2 = float32_round_to_int(FST0, &env->fp_status);
    RESTORE_ROUNDING_MODE;
    DEBUG_FPU_STATE();
    RETURN();
}

FLOAT_OP(floorl, d)
{
    set_float_rounding_mode(float_round_down, &env->fp_status);
    DT2 = float64_round_to_int(FDT0, &env->fp_status);
    RESTORE_ROUNDING_MODE;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(floorl, s)
{
    set_float_rounding_mode(float_round_down, &env->fp_status);
    DT2 = float32_round_to_int(FST0, &env->fp_status);
    RESTORE_ROUNDING_MODE;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(floorw, d)
{
    set_float_rounding_mode(float_round_down, &env->fp_status);
    WT2 = float64_round_to_int(FDT0, &env->fp_status);
    RESTORE_ROUNDING_MODE;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(floorw, s)
{
    set_float_rounding_mode(float_round_down, &env->fp_status);
    WT2 = float32_round_to_int(FST0, &env->fp_status);
    RESTORE_ROUNDING_MODE;
    DEBUG_FPU_STATE();
    RETURN();
}

FLOAT_OP(movf, d)
{
    if (!(env->fcr31 & PARAM1))
        DT2 = DT0;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(movf, s)
{
    if (!(env->fcr31 & PARAM1))
        WT2 = WT0;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(movf, ps)
{
    if (!(env->fcr31 & PARAM1)) {
        WT2 = WT0;
        WTH2 = WTH0;
    }
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(movt, d)
{
    if (env->fcr31 & PARAM1)
        DT2 = DT0;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(movt, s)
{
    if (env->fcr31 & PARAM1)
        WT2 = WT0;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(movt, ps)
{
    if (env->fcr31 & PARAM1) {
        WT2 = WT0;
        WTH2 = WTH0;
    }
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(movz, d)
{
    if (!T0)
        DT2 = DT0;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(movz, s)
{
    if (!T0)
        WT2 = WT0;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(movz, ps)
{
    if (!T0) {
        WT2 = WT0;
        WTH2 = WTH0;
    }
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(movn, d)
{
    if (T0)
        DT2 = DT0;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(movn, s)
{
    if (T0)
        WT2 = WT0;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(movn, ps)
{
    if (T0) {
        WT2 = WT0;
        WTH2 = WTH0;
    }
    DEBUG_FPU_STATE();
    RETURN();
}

/* binary operations */
#define FLOAT_BINOP(name) \
FLOAT_OP(name, d)         \
{                         \
    set_float_exception_flags(0, &env->fp_status);            \
    FDT2 = float64_ ## name (FDT0, FDT1, &env->fp_status);    \
    update_fcr31();       \
    DEBUG_FPU_STATE();    \
}                         \
FLOAT_OP(name, s)         \
{                         \
    set_float_exception_flags(0, &env->fp_status);            \
    FST2 = float32_ ## name (FST0, FST1, &env->fp_status);    \
    update_fcr31();       \
    DEBUG_FPU_STATE();    \
}                         \
FLOAT_OP(name, ps)        \
{                         \
    set_float_exception_flags(0, &env->fp_status);            \
    FST2 = float32_ ## name (FST0, FST1, &env->fp_status);    \
    FSTH2 = float32_ ## name (FSTH0, FSTH1, &env->fp_status); \
    update_fcr31();       \
    DEBUG_FPU_STATE();    \
}
FLOAT_BINOP(add)
FLOAT_BINOP(sub)
FLOAT_BINOP(mul)
FLOAT_BINOP(div)
#undef FLOAT_BINOP

/* ternary operations */
#define FLOAT_TERNOP(name1, name2) \
FLOAT_OP(name1 ## name2, d)        \
{                                  \
    FDT0 = float64_ ## name1 (FDT0, FDT1, &env->fp_status);    \
    FDT2 = float64_ ## name2 (FDT0, FDT2, &env->fp_status);    \
    DEBUG_FPU_STATE();             \
}                                  \
FLOAT_OP(name1 ## name2, s)        \
{                                  \
    FST0 = float32_ ## name1 (FST0, FST1, &env->fp_status);    \
    FST2 = float32_ ## name2 (FST0, FST2, &env->fp_status);    \
    DEBUG_FPU_STATE();             \
}                                  \
FLOAT_OP(name1 ## name2, ps)       \
{                                  \
    FST0 = float32_ ## name1 (FST0, FST1, &env->fp_status);    \
    FSTH0 = float32_ ## name1 (FSTH0, FSTH1, &env->fp_status); \
    FST2 = float32_ ## name2 (FST0, FST2, &env->fp_status);    \
    FSTH2 = float32_ ## name2 (FSTH0, FSTH2, &env->fp_status); \
    DEBUG_FPU_STATE();             \
}
FLOAT_TERNOP(mul, add)
FLOAT_TERNOP(mul, sub)
#undef FLOAT_TERNOP

/* unary operations, modifying fp status  */
#define FLOAT_UNOP(name)  \
FLOAT_OP(name, d)         \
{                         \
    FDT2 = float64_ ## name(FDT0, &env->fp_status);   \
    DEBUG_FPU_STATE();    \
}                         \
FLOAT_OP(name, s)         \
{                         \
    FST2 = float32_ ## name(FST0, &env->fp_status);   \
    DEBUG_FPU_STATE();    \
}                         \
FLOAT_OP(name, ps)        \
{                         \
    FST2 = float32_ ## name(FST0, &env->fp_status);   \
    FSTH2 = float32_ ## name(FSTH0, &env->fp_status); \
    DEBUG_FPU_STATE();    \
}
FLOAT_UNOP(sqrt)
#undef FLOAT_UNOP

/* unary operations, not modifying fp status  */
#define FLOAT_UNOP(name)  \
FLOAT_OP(name, d)         \
{                         \
    FDT2 = float64_ ## name(FDT0);   \
    DEBUG_FPU_STATE();    \
}                         \
FLOAT_OP(name, s)         \
{                         \
    FST2 = float32_ ## name(FST0);   \
    DEBUG_FPU_STATE();    \
}                         \
FLOAT_OP(name, ps)        \
{                         \
    FST2 = float32_ ## name(FST0);   \
    FSTH2 = float32_ ## name(FSTH0); \
    DEBUG_FPU_STATE();    \
}
FLOAT_UNOP(abs)
FLOAT_UNOP(chs)
#undef FLOAT_UNOP

FLOAT_OP(mov, d)
{
    FDT2 = FDT0;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(mov, s)
{
    FST2 = FST0;
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(mov, ps)
{
    FST2 = FST0;
    FSTH2 = FSTH0;
    DEBUG_FPU_STATE();
    RETURN();
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
    RETURN();
}

#ifdef CONFIG_SOFTFLOAT
#define clear_invalid() do {                                \
    int flags = get_float_exception_flags(&env->fp_status); \
    flags &= ~float_flag_invalid;                           \
    set_float_exception_flags(flags, &env->fp_status);      \
} while(0)
#else
#define clear_invalid() do { } while(0)
#endif

extern void dump_fpu_s(CPUState *env);

#define FOP_COND_D(op, cond)                   \
void op_cmp_d_ ## op (void)                    \
{                                              \
    int c = cond;                              \
    update_fcr31();                            \
    if (c)                                     \
        SET_FP_COND(PARAM1, env);              \
    else                                       \
        CLEAR_FP_COND(PARAM1, env);            \
    DEBUG_FPU_STATE();                         \
    RETURN();                                  \
}

int float64_is_unordered(int sig, float64 a, float64 b STATUS_PARAM)
{
    if (float64_is_signaling_nan(a) ||
        float64_is_signaling_nan(b) ||
        (sig && (float64_is_nan(a) || float64_is_nan(b)))) {
        float_raise(float_flag_invalid, status);
        return 1;
    } else if (float64_is_nan(a) || float64_is_nan(b)) {
        return 1;
    } else {
        return 0;
    }
}

/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called. */
FOP_COND_D(f,   (float64_is_unordered(0, FDT1, FDT0, &env->fp_status), 0))
FOP_COND_D(un,  float64_is_unordered(0, FDT1, FDT0, &env->fp_status))
FOP_COND_D(eq,  !float64_is_unordered(0, FDT1, FDT0, &env->fp_status) && float64_eq(FDT0, FDT1, &env->fp_status))
FOP_COND_D(ueq, float64_is_unordered(0, FDT1, FDT0, &env->fp_status)  || float64_eq(FDT0, FDT1, &env->fp_status))
FOP_COND_D(olt, !float64_is_unordered(0, FDT1, FDT0, &env->fp_status) && float64_lt(FDT0, FDT1, &env->fp_status))
FOP_COND_D(ult, float64_is_unordered(0, FDT1, FDT0, &env->fp_status)  || float64_lt(FDT0, FDT1, &env->fp_status))
FOP_COND_D(ole, !float64_is_unordered(0, FDT1, FDT0, &env->fp_status) && float64_le(FDT0, FDT1, &env->fp_status))
FOP_COND_D(ule, float64_is_unordered(0, FDT1, FDT0, &env->fp_status)  || float64_le(FDT0, FDT1, &env->fp_status))
/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called. */
FOP_COND_D(sf,  (float64_is_unordered(1, FDT1, FDT0, &env->fp_status), 0))
FOP_COND_D(ngle,float64_is_unordered(1, FDT1, FDT0, &env->fp_status))
FOP_COND_D(seq, !float64_is_unordered(1, FDT1, FDT0, &env->fp_status) && float64_eq(FDT0, FDT1, &env->fp_status))
FOP_COND_D(ngl, float64_is_unordered(1, FDT1, FDT0, &env->fp_status)  || float64_eq(FDT0, FDT1, &env->fp_status))
FOP_COND_D(lt,  !float64_is_unordered(1, FDT1, FDT0, &env->fp_status) && float64_lt(FDT0, FDT1, &env->fp_status))
FOP_COND_D(nge, float64_is_unordered(1, FDT1, FDT0, &env->fp_status)  || float64_lt(FDT0, FDT1, &env->fp_status))
FOP_COND_D(le,  !float64_is_unordered(1, FDT1, FDT0, &env->fp_status) && float64_le(FDT0, FDT1, &env->fp_status))
FOP_COND_D(ngt, float64_is_unordered(1, FDT1, FDT0, &env->fp_status)  || float64_le(FDT0, FDT1, &env->fp_status))

#define FOP_COND_S(op, cond)                   \
void op_cmp_s_ ## op (void)                    \
{                                              \
    int c = cond;                              \
    update_fcr31();                            \
    if (c)                                     \
        SET_FP_COND(PARAM1, env);              \
    else                                       \
        CLEAR_FP_COND(PARAM1, env);            \
    DEBUG_FPU_STATE();                         \
    RETURN();                                  \
}

flag float32_is_unordered(int sig, float32 a, float32 b STATUS_PARAM)
{
    extern flag float32_is_nan(float32 a);
    if (float32_is_signaling_nan(a) ||
        float32_is_signaling_nan(b) ||
        (sig && (float32_is_nan(a) || float32_is_nan(b)))) {
        float_raise(float_flag_invalid, status);
        return 1;
    } else if (float32_is_nan(a) || float32_is_nan(b)) {
        return 1;
    } else {
        return 0;
    }
}

/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called. */
FOP_COND_S(f,   (float32_is_unordered(0, FST1, FST0, &env->fp_status), 0))
FOP_COND_S(un,  float32_is_unordered(0, FST1, FST0, &env->fp_status))
FOP_COND_S(eq,  !float32_is_unordered(0, FST1, FST0, &env->fp_status) && float32_eq(FST0, FST1, &env->fp_status))
FOP_COND_S(ueq, float32_is_unordered(0, FST1, FST0, &env->fp_status)  || float32_eq(FST0, FST1, &env->fp_status))
FOP_COND_S(olt, !float32_is_unordered(0, FST1, FST0, &env->fp_status) && float32_lt(FST0, FST1, &env->fp_status))
FOP_COND_S(ult, float32_is_unordered(0, FST1, FST0, &env->fp_status)  || float32_lt(FST0, FST1, &env->fp_status))
FOP_COND_S(ole, !float32_is_unordered(0, FST1, FST0, &env->fp_status) && float32_le(FST0, FST1, &env->fp_status))
FOP_COND_S(ule, float32_is_unordered(0, FST1, FST0, &env->fp_status)  || float32_le(FST0, FST1, &env->fp_status))
/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called. */
FOP_COND_S(sf,  (float32_is_unordered(1, FST1, FST0, &env->fp_status), 0))
FOP_COND_S(ngle,float32_is_unordered(1, FST1, FST0, &env->fp_status))
FOP_COND_S(seq, !float32_is_unordered(1, FST1, FST0, &env->fp_status) && float32_eq(FST0, FST1, &env->fp_status))
FOP_COND_S(ngl, float32_is_unordered(1, FST1, FST0, &env->fp_status)  || float32_eq(FST0, FST1, &env->fp_status))
FOP_COND_S(lt,  !float32_is_unordered(1, FST1, FST0, &env->fp_status) && float32_lt(FST0, FST1, &env->fp_status))
FOP_COND_S(nge, float32_is_unordered(1, FST1, FST0, &env->fp_status)  || float32_lt(FST0, FST1, &env->fp_status))
FOP_COND_S(le,  !float32_is_unordered(1, FST1, FST0, &env->fp_status) && float32_le(FST0, FST1, &env->fp_status))
FOP_COND_S(ngt, float32_is_unordered(1, FST1, FST0, &env->fp_status)  || float32_le(FST0, FST1, &env->fp_status))

#define FOP_COND_PS(op, condl, condh)          \
void op_cmp_ps_ ## op (void)                   \
{                                              \
    int cl = condl;                            \
    int ch = condh;                            \
    update_fcr31();                            \
    if (cl)                                    \
        SET_FP_COND(PARAM1, env);              \
    else                                       \
        CLEAR_FP_COND(PARAM1, env);            \
    if (ch)                                    \
        SET_FP_COND(PARAM1 + 1, env);          \
    else                                       \
        CLEAR_FP_COND(PARAM1 + 1, env);        \
    DEBUG_FPU_STATE();                         \
    RETURN();                                  \
}

/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called. */
FOP_COND_PS(f,   (float32_is_unordered(0, FST1, FST0, &env->fp_status), 0),
                 (float32_is_unordered(0, FSTH1, FSTH0, &env->fp_status), 0))
FOP_COND_PS(un,  float32_is_unordered(0, FST1, FST0, &env->fp_status),
                 float32_is_unordered(0, FSTH1, FSTH0, &env->fp_status))
FOP_COND_PS(eq,  !float32_is_unordered(0, FST1, FST0, &env->fp_status)   && float32_eq(FST0, FST1, &env->fp_status),
                 !float32_is_unordered(0, FSTH1, FSTH0, &env->fp_status) && float32_eq(FSTH0, FSTH1, &env->fp_status))
FOP_COND_PS(ueq, float32_is_unordered(0, FST1, FST0, &env->fp_status)    || float32_eq(FST0, FST1, &env->fp_status),
                 float32_is_unordered(0, FSTH1, FSTH0, &env->fp_status)  || float32_eq(FSTH0, FSTH1, &env->fp_status))
FOP_COND_PS(olt, !float32_is_unordered(0, FST1, FST0, &env->fp_status)   && float32_lt(FST0, FST1, &env->fp_status),
                 !float32_is_unordered(0, FSTH1, FSTH0, &env->fp_status) && float32_lt(FSTH0, FSTH1, &env->fp_status))
FOP_COND_PS(ult, float32_is_unordered(0, FST1, FST0, &env->fp_status)    || float32_lt(FST0, FST1, &env->fp_status),
                 float32_is_unordered(0, FSTH1, FSTH0, &env->fp_status)  || float32_lt(FSTH0, FSTH1, &env->fp_status))
FOP_COND_PS(ole, !float32_is_unordered(0, FST1, FST0, &env->fp_status)   && float32_le(FST0, FST1, &env->fp_status),
                 !float32_is_unordered(0, FSTH1, FSTH0, &env->fp_status) && float32_le(FSTH0, FSTH1, &env->fp_status))
FOP_COND_PS(ule, float32_is_unordered(0, FST1, FST0, &env->fp_status)    || float32_le(FST0, FST1, &env->fp_status),
                 float32_is_unordered(0, FSTH1, FSTH0, &env->fp_status)  || float32_le(FSTH0, FSTH1, &env->fp_status))
/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called. */
FOP_COND_PS(sf,  (float32_is_unordered(1, FST1, FST0, &env->fp_status), 0),
                 (float32_is_unordered(1, FSTH1, FSTH0, &env->fp_status), 0))
FOP_COND_PS(ngle,float32_is_unordered(1, FST1, FST0, &env->fp_status),
                 float32_is_unordered(1, FSTH1, FSTH0, &env->fp_status))
FOP_COND_PS(seq, !float32_is_unordered(1, FST1, FST0, &env->fp_status)   && float32_eq(FST0, FST1, &env->fp_status),
                 !float32_is_unordered(1, FSTH1, FSTH0, &env->fp_status) && float32_eq(FSTH0, FSTH1, &env->fp_status))
FOP_COND_PS(ngl, float32_is_unordered(1, FST1, FST0, &env->fp_status)    || float32_eq(FST0, FST1, &env->fp_status),
                 float32_is_unordered(1, FSTH1, FSTH0, &env->fp_status)  || float32_eq(FSTH0, FSTH1, &env->fp_status))
FOP_COND_PS(lt,  !float32_is_unordered(1, FST1, FST0, &env->fp_status)   && float32_lt(FST0, FST1, &env->fp_status),
                 !float32_is_unordered(1, FSTH1, FSTH0, &env->fp_status) && float32_lt(FSTH0, FSTH1, &env->fp_status))
FOP_COND_PS(nge, float32_is_unordered(1, FST1, FST0, &env->fp_status)    || float32_lt(FST0, FST1, &env->fp_status),
                 float32_is_unordered(1, FSTH1, FSTH0, &env->fp_status)  || float32_lt(FSTH0, FSTH1, &env->fp_status))
FOP_COND_PS(le,  !float32_is_unordered(1, FST1, FST0, &env->fp_status)   && float32_le(FST0, FST1, &env->fp_status),
                 !float32_is_unordered(1, FSTH1, FSTH0, &env->fp_status) && float32_le(FSTH0, FSTH1, &env->fp_status))
FOP_COND_PS(ngt, float32_is_unordered(1, FST1, FST0, &env->fp_status)    || float32_le(FST0, FST1, &env->fp_status),
                 float32_is_unordered(1, FSTH1, FSTH0, &env->fp_status)  || float32_le(FSTH0, FSTH1, &env->fp_status))

void op_bc1f (void)
{
    T0 = !IS_FP_COND_SET(PARAM1, env);
    DEBUG_FPU_STATE();
    RETURN();
}
void op_bc1fany2 (void)
{
    T0 = (!IS_FP_COND_SET(PARAM1, env) ||
          !IS_FP_COND_SET(PARAM1 + 1, env));
    DEBUG_FPU_STATE();
    RETURN();
}
void op_bc1fany4 (void)
{
    T0 = (!IS_FP_COND_SET(PARAM1, env) ||
          !IS_FP_COND_SET(PARAM1 + 1, env) ||
          !IS_FP_COND_SET(PARAM1 + 2, env) ||
          !IS_FP_COND_SET(PARAM1 + 3, env));
    DEBUG_FPU_STATE();
    RETURN();
}

void op_bc1t (void)
{
    T0 = IS_FP_COND_SET(PARAM1, env);
    DEBUG_FPU_STATE();
    RETURN();
}
void op_bc1tany2 (void)
{
    T0 = (IS_FP_COND_SET(PARAM1, env) ||
          IS_FP_COND_SET(PARAM1 + 1, env));
    DEBUG_FPU_STATE();
    RETURN();
}
void op_bc1tany4 (void)
{
    T0 = (IS_FP_COND_SET(PARAM1, env) ||
          IS_FP_COND_SET(PARAM1 + 1, env) ||
          IS_FP_COND_SET(PARAM1 + 2, env) ||
          IS_FP_COND_SET(PARAM1 + 3, env));
    DEBUG_FPU_STATE();
    RETURN();
}

#if defined(MIPS_USES_R4K_TLB)
void op_tlbwi (void)
{
    CALL_FROM_TB0(do_tlbwi);
    RETURN();
}

void op_tlbwr (void)
{
    CALL_FROM_TB0(do_tlbwr);
    RETURN();
}

void op_tlbp (void)
{
    CALL_FROM_TB0(do_tlbp);
    RETURN();
}

void op_tlbr (void)
{
    CALL_FROM_TB0(do_tlbr);
    RETURN();
}
#endif

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
    RETURN();
}

void op_di (void)
{
    T0 = env->CP0_Status;
    env->CP0_Status = T0 & ~(1 << CP0St_IE);
    CALL_FROM_TB1(cpu_mips_update_irq, env);
    RETURN();
}

void op_ei (void)
{
    T0 = env->CP0_Status;
    env->CP0_Status = T0 | (1 << CP0St_IE);
    CALL_FROM_TB1(cpu_mips_update_irq, env);
    RETURN();
}

void op_trap (void)
{
    if (T0) {
        CALL_FROM_TB1(do_raise_exception, EXCP_TRAP);
    }
    RETURN();
}

void op_debug (void)
{
    CALL_FROM_TB1(do_raise_exception, EXCP_DEBUG);
    RETURN();
}

void op_set_lladdr (void)
{
    env->CP0_LLAddr = T2;
    RETURN();
}

void debug_pre_eret (void);
void debug_post_eret (void);
void op_eret (void)
{
    if (loglevel & CPU_LOG_EXEC)
        CALL_FROM_TB0(debug_pre_eret);
    if (env->CP0_Status & (1 << CP0St_ERL)) {
        env->PC = env->CP0_ErrorEPC;
        env->CP0_Status &= ~(1 << CP0St_ERL);
    } else {
        env->PC = env->CP0_EPC;
        env->CP0_Status &= ~(1 << CP0St_EXL);
    }
    if (!(env->CP0_Status & (1 << CP0St_EXL)) &&
        !(env->CP0_Status & (1 << CP0St_ERL)) &&
        !(env->hflags & MIPS_HFLAG_DM) &&
        (env->CP0_Status & (1 << CP0St_UM)))
        env->hflags |= MIPS_HFLAG_UM;
    if (loglevel & CPU_LOG_EXEC)
        CALL_FROM_TB0(debug_post_eret);
    env->CP0_LLAddr = 1;
    RETURN();
}

void op_deret (void)
{
    if (loglevel & CPU_LOG_EXEC)
        CALL_FROM_TB0(debug_pre_eret);
    env->PC = env->CP0_DEPC;
    env->hflags |= MIPS_HFLAG_DM;
    if (!(env->CP0_Status & (1 << CP0St_EXL)) &&
        !(env->CP0_Status & (1 << CP0St_ERL)) &&
        !(env->hflags & MIPS_HFLAG_DM) &&
        (env->CP0_Status & (1 << CP0St_UM)))
        env->hflags |= MIPS_HFLAG_UM;
    if (loglevel & CPU_LOG_EXEC)
        CALL_FROM_TB0(debug_post_eret);
    env->CP0_LLAddr = 1;
    RETURN();
}

void op_rdhwr_cpunum(void)
{
    if (!(env->hflags & MIPS_HFLAG_UM) ||
        (env->CP0_HWREna & (1 << 0)) ||
        (env->CP0_Status & (1 << CP0St_CU0)))
        T0 = env->CP0_EBase & 0x3ff;
    else
        CALL_FROM_TB1(do_raise_exception, EXCP_RI);
    RETURN();
}

void op_rdhwr_synci_step(void)
{
    if (!(env->hflags & MIPS_HFLAG_UM) ||
        (env->CP0_HWREna & (1 << 1)) ||
        (env->CP0_Status & (1 << CP0St_CU0)))
        T0 = env->SYNCI_Step;
    else
        CALL_FROM_TB1(do_raise_exception, EXCP_RI);
    RETURN();
}

void op_rdhwr_cc(void)
{
    if (!(env->hflags & MIPS_HFLAG_UM) ||
        (env->CP0_HWREna & (1 << 2)) ||
        (env->CP0_Status & (1 << CP0St_CU0)))
        T0 = env->CP0_Count;
    else
        CALL_FROM_TB1(do_raise_exception, EXCP_RI);
    RETURN();
}

void op_rdhwr_ccres(void)
{
    if (!(env->hflags & MIPS_HFLAG_UM) ||
        (env->CP0_HWREna & (1 << 3)) ||
        (env->CP0_Status & (1 << CP0St_CU0)))
        T0 = env->CCRes;
    else
        CALL_FROM_TB1(do_raise_exception, EXCP_RI);
    RETURN();
}

void op_save_state (void)
{
    env->hflags = PARAM1;
    RETURN();
}

void op_save_pc (void)
{
    env->PC = PARAM1;
    RETURN();
}

void op_save_fp_status (void)
{
    union fps {
        uint32_t i;
        float_status f;
    } fps;
    fps.i = PARAM1;
    env->fp_status = fps.f;
    RETURN();
}

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
    RETURN();
}

void op_raise_exception (void)
{
    CALL_FROM_TB1(do_raise_exception, PARAM1);
    RETURN();
}

void op_raise_exception_err (void)
{
    CALL_FROM_TB2(do_raise_exception_err, PARAM1, PARAM2);
    RETURN();
}

void op_exit_tb (void)
{
    EXIT_TB();
    RETURN();
}

void op_wait (void)
{
    env->halted = 1;
    CALL_FROM_TB1(do_raise_exception, EXCP_HLT);
    RETURN();
}

/* Bitfield operations. */
void op_ext(void)
{
    unsigned int pos = PARAM1;
    unsigned int size = PARAM2;

    T0 = ((uint32_t)T1 >> pos) & ((size < 32) ? ((1 << size) - 1) : ~0);
    RETURN();
}

void op_ins(void)
{
    unsigned int pos = PARAM1;
    unsigned int size = PARAM2;
    target_ulong mask = ((size < 32) ? ((1 << size) - 1) : ~0) << pos;

    T0 = (T0 & ~mask) | (((uint32_t)T1 << pos) & mask);
    RETURN();
}

void op_wsbh(void)
{
    T0 = ((T1 << 8) & ~0x00FF00FF) | ((T1 >> 8) & 0x00FF00FF);
    RETURN();
}

#ifdef TARGET_MIPS64
void op_dext(void)
{
    unsigned int pos = PARAM1;
    unsigned int size = PARAM2;

    T0 = (T1 >> pos) & ((size < 32) ? ((1 << size) - 1) : ~0);
    RETURN();
}

void op_dins(void)
{
    unsigned int pos = PARAM1;
    unsigned int size = PARAM2;
    target_ulong mask = ((size < 32) ? ((1 << size) - 1) : ~0) << pos;

    T0 = (T0 & ~mask) | ((T1 << pos) & mask);
    RETURN();
}

void op_dsbh(void)
{
    T0 = ((T1 << 8) & ~0x00FF00FF00FF00FFULL) | ((T1 >> 8) & 0x00FF00FF00FF00FFULL);
    RETURN();
}

void op_dshd(void)
{
    T0 = ((T1 << 16) & ~0x0000FFFF0000FFFFULL) | ((T1 >> 16) & 0x0000FFFF0000FFFFULL);
    RETURN();
}
#endif

void op_seb(void)
{
    T0 = ((T1 & 0xFF) ^ 0x80) - 0x80;
    RETURN();
}

void op_seh(void)
{
    T0 = ((T1 & 0xFFFF) ^ 0x8000) - 0x8000;
    RETURN();
}
