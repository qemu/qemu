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
#define CALL_FROM_TB0(func) func();
#endif
#ifndef CALL_FROM_TB1
#define CALL_FROM_TB1(func, arg0) func(arg0);
#endif
#ifndef CALL_FROM_TB1_CONST16
#define CALL_FROM_TB1_CONST16(func, arg0) CALL_FROM_TB1(func, arg0);
#endif
#ifndef CALL_FROM_TB2
#define CALL_FROM_TB2(func, arg0, arg1) func(arg0, arg1);
#endif
#ifndef CALL_FROM_TB2_CONST16
#define CALL_FROM_TB2_CONST16(func, arg0, arg1)     \
CALL_FROM_TB2(func, arg0, arg1);
#endif
#ifndef CALL_FROM_TB3
#define CALL_FROM_TB3(func, arg0, arg1, arg2) func(arg0, arg1, arg2);
#endif
#ifndef CALL_FROM_TB4
#define CALL_FROM_TB4(func, arg0, arg1, arg2, arg3) \
        func(arg0, arg1, arg2, arg3);
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

#ifdef MIPS_USES_FPU

#define SFREG 0
#define DFREG 0
#include "fop_template.c"
#undef SFREG
#undef DFREG
#define SFREG 1
#include "fop_template.c"
#undef SFREG
#define SFREG 2
#define DFREG 2
#include "fop_template.c"
#undef SFREG
#undef DFREG
#define SFREG 3
#include "fop_template.c"
#undef SFREG
#define SFREG 4
#define DFREG 4
#include "fop_template.c"
#undef SFREG
#undef DFREG
#define SFREG 5
#include "fop_template.c"
#undef SFREG
#define SFREG 6
#define DFREG 6
#include "fop_template.c"
#undef SFREG
#undef DFREG
#define SFREG 7
#include "fop_template.c"
#undef SFREG
#define SFREG 8
#define DFREG 8
#include "fop_template.c"
#undef SFREG
#undef DFREG
#define SFREG 9
#include "fop_template.c"
#undef SFREG
#define SFREG 10
#define DFREG 10
#include "fop_template.c"
#undef SFREG
#undef DFREG
#define SFREG 11
#include "fop_template.c"
#undef SFREG
#define SFREG 12
#define DFREG 12
#include "fop_template.c"
#undef SFREG
#undef DFREG
#define SFREG 13
#include "fop_template.c"
#undef SFREG
#define SFREG 14
#define DFREG 14
#include "fop_template.c"
#undef SFREG
#undef DFREG
#define SFREG 15
#include "fop_template.c"
#undef SFREG
#define SFREG 16
#define DFREG 16
#include "fop_template.c"
#undef SFREG
#undef DFREG
#define SFREG 17
#include "fop_template.c"
#undef SFREG
#define SFREG 18
#define DFREG 18
#include "fop_template.c"
#undef SFREG
#undef DFREG
#define SFREG 19
#include "fop_template.c"
#undef SFREG
#define SFREG 20
#define DFREG 20
#include "fop_template.c"
#undef SFREG
#undef DFREG
#define SFREG 21
#include "fop_template.c"
#undef SFREG
#define SFREG 22
#define DFREG 22
#include "fop_template.c"
#undef SFREG
#undef DFREG
#define SFREG 23
#include "fop_template.c"
#undef SFREG
#define SFREG 24
#define DFREG 24
#include "fop_template.c"
#undef SFREG
#undef DFREG
#define SFREG 25
#include "fop_template.c"
#undef SFREG
#define SFREG 26
#define DFREG 26
#include "fop_template.c"
#undef SFREG
#undef DFREG
#define SFREG 27
#include "fop_template.c"
#undef SFREG
#define SFREG 28
#define DFREG 28
#include "fop_template.c"
#undef SFREG
#undef DFREG
#define SFREG 29
#include "fop_template.c"
#undef SFREG
#define SFREG 30
#define DFREG 30
#include "fop_template.c"
#undef SFREG
#undef DFREG
#define SFREG 31
#include "fop_template.c"
#undef SFREG

#define FTN
#include "fop_template.c"
#undef FTN

#endif

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
        CALL_FROM_TB1(do_raise_exception_direct, EXCP_OVERFLOW);
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
        CALL_FROM_TB1(do_raise_exception_direct, EXCP_OVERFLOW);
    }
    T0 = (int32_t)T0;
    RETURN();
}

void op_mul (void)
{
    T0 = (int32_t)((int32_t)T0 * (int32_t)T1);
    RETURN();
}

void op_div (void)
{
    if (T1 != 0) {
        env->LO = (int32_t)((int32_t)T0 / (int32_t)T1);
        env->HI = (int32_t)((int32_t)T0 % (int32_t)T1);
    }
    RETURN();
}

void op_divu (void)
{
    if (T1 != 0) {
        env->LO = (int32_t)((uint32_t)T0 / (uint32_t)T1);
        env->HI = (int32_t)((uint32_t)T0 % (uint32_t)T1);
    }
    RETURN();
}

#ifdef MIPS_HAS_MIPS64
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
        CALL_FROM_TB1(do_raise_exception_direct, EXCP_OVERFLOW);
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
        CALL_FROM_TB1(do_raise_exception_direct, EXCP_OVERFLOW);
    }
    RETURN();
}

void op_dmul (void)
{
    T0 = (int64_t)T0 * (int64_t)T1;
    RETURN();
}

#if TARGET_LONG_BITS > HOST_LONG_BITS
/* Those might call libgcc functions.  */
void op_ddiv (void)
{
    do_ddiv();
    RETURN();
}

void op_ddivu (void)
{
    do_ddivu();
    RETURN();
}
#else
void op_ddiv (void)
{
    if (T1 != 0) {
        env->LO = (int64_t)T0 / (int64_t)T1;
        env->HI = (int64_t)T0 % (int64_t)T1;
    }
    RETURN();
}

void op_ddivu (void)
{
    if (T1 != 0) {
        env->LO = T0 / T1;
        env->HI = T0 % T1;
    }
    RETURN();
}
#endif
#endif /* MIPS_HAS_MIPS64 */

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
    T0 = (int32_t)((uint32_t)T0 << (uint32_t)T1);
    RETURN();
}

void op_sra (void)
{
    T0 = (int32_t)((int32_t)T0 >> (uint32_t)T1);
    RETURN();
}

void op_srl (void)
{
    T0 = (int32_t)((uint32_t)T0 >> (uint32_t)T1);
    RETURN();
}

void op_rotr (void)
{
    target_ulong tmp;

    if (T1) {
       tmp = (int32_t)((uint32_t)T0 << (0x20 - (uint32_t)T1));
       T0 = (int32_t)((uint32_t)T0 >> (uint32_t)T1) | tmp;
    } else
       T0 = T1;
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

#ifdef MIPS_HAS_MIPS64

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
    } else
       T0 = T1;
    RETURN();
}

void op_drotr32 (void)
{
    target_ulong tmp;

    if (T1) {
       tmp = T0 << (0x40 - (32 + T1));
       T0 = (T0 >> (32 + T1)) | tmp;
    } else
       T0 = T1;
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

#ifdef MIPS_HAS_MIPS64
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

#ifdef MIPS_USES_FPU
void op_movf (void)
{
    if (!(env->fcr31 & PARAM1))
        env->gpr[PARAM2] = env->gpr[PARAM3];
    RETURN();
}

void op_movt (void)
{
    if (env->fcr31 & PARAM1)
        env->gpr[PARAM2] = env->gpr[PARAM3];
    RETURN();
}
#endif

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
    if (env->hflags & MIPS_HFLAG_UM)
        T0 |= (1 << CP0St_UM);
    if (env->hflags & MIPS_HFLAG_ERL)
        T0 |= (1 << CP0St_ERL);
    if (env->hflags & MIPS_HFLAG_EXL)
        T0 |= (1 << CP0St_EXL);
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
    env->CP0_Index = (env->CP0_Index & 0x80000000) | (T0 & (MIPS_TLB_NB - 1));
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
    env->CP0_Context = (env->CP0_Context & ~0x007FFFFF) | (T0 & 0x007FFFF0);
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
    env->CP0_Wired = T0 & (MIPS_TLB_NB - 1);
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
    val = (int32_t)T0 & 0xFFFFE0FF;
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

    val = (int32_t)T0 & 0xFA78FF01;
    old = env->CP0_Status;
    if (T0 & (1 << CP0St_UM))
        env->hflags |= MIPS_HFLAG_UM;
    else
        env->hflags &= ~MIPS_HFLAG_UM;
    if (T0 & (1 << CP0St_ERL))
        env->hflags |= MIPS_HFLAG_ERL;
    else
        env->hflags &= ~MIPS_HFLAG_ERL;
    if (T0 & (1 << CP0St_EXL))
        env->hflags |= MIPS_HFLAG_EXL;
    else
        env->hflags &= ~MIPS_HFLAG_EXL;
    env->CP0_Status = val;
    if (loglevel & CPU_LOG_TB_IN_ASM)
       CALL_FROM_TB2(do_mtc0_status_debug, old, val);
    CALL_FROM_TB1(cpu_mips_update_irq, env);
    RETURN();
}

void op_mtc0_intctl (void)
{
    /* vectored interrupts not implemented */
    env->CP0_IntCtl = 0;
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
    env->CP0_Cause = (env->CP0_Cause & 0xB000F87C) | (T0 & 0x00C00300);

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
    env->CP0_WatchLo = (int32_t)T0;
    RETURN();
}

void op_mtc0_watchhi0 (void)
{
    env->CP0_WatchHi = T0 & 0x40FF0FF8;
    RETURN();
}

void op_mtc0_xcontext (void)
{
    env->CP0_XContext = (int32_t)T0; /* XXX */
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
    env->CP0_Context = (env->CP0_Context & ~0x007FFFFF) | (T0 & 0x007FFFF0);
    RETURN();
}

void op_dmtc0_epc (void)
{
    env->CP0_EPC = T0;
    RETURN();
}

void op_dmtc0_watchlo0 (void)
{
    env->CP0_WatchLo = T0;
    RETURN();
}

void op_dmtc0_xcontext (void)
{
    env->CP0_XContext = T0; /* XXX */
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

#ifdef MIPS_USES_FPU

#if 0
# define DEBUG_FPU_STATE() CALL_FROM_TB1(dump_fpu, env)
#else
# define DEBUG_FPU_STATE() do { } while(0)
#endif

void op_cp1_enabled(void)
{
    if (!(env->CP0_Status & (1 << CP0St_CU1))) {
        CALL_FROM_TB2(do_raise_exception_err, EXCP_CpU, 1);
    }
    RETURN();
}

/* CP1 functions */
void op_cfc1 (void)
{
    if (T1 == 0) {
        T0 = env->fcr0;
    }
    else {
        /* fetch fcr31, masking unused bits */
        T0 = env->fcr31 & 0x0183FFFF;
    }
    DEBUG_FPU_STATE();
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

void op_ctc1 (void)
{
    if (T1 == 0) {
        /* XXX should this throw an exception?
         * don't write to FCR0.
         * env->fcr0 = T0; 
         */
    }
    else {
        /* store new fcr31, masking unused bits */  
        env->fcr31 = T0 & 0x0183FFFF;

        /* set rounding mode */
        RESTORE_ROUNDING_MODE;

#ifndef CONFIG_SOFTFLOAT
        /* no floating point exception for native float */
        SET_FP_ENABLE(env->fcr31, 0);
#endif
    }
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

/* Float support.
   Single precition routines have a "s" suffix, double precision a
   "d" suffix.  */

#define FLOAT_OP(name, p) void OPPROTO op_float_##name##_##p(void)

FLOAT_OP(cvtd, s)
{
    FDT2 = float32_to_float64(WT0, &env->fp_status);
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(cvtd, w)
{
    FDT2 = int32_to_float64(WT0, &env->fp_status);
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(cvts, d)
{
    FST2 = float64_to_float32(FDT0, &env->fp_status);
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(cvts, w)
{
    FST2 = int32_to_float32(WT0, &env->fp_status);
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(cvtw, s)
{
    WT2 = float32_to_int32(FST0, &env->fp_status);
    DEBUG_FPU_STATE();
    RETURN();
}
FLOAT_OP(cvtw, d)
{
    WT2 = float64_to_int32(FDT0, &env->fp_status);
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

/* binary operations */
#define FLOAT_BINOP(name) \
FLOAT_OP(name, d)         \
{                         \
    FDT2 = float64_ ## name (FDT0, FDT1, &env->fp_status);    \
    DEBUG_FPU_STATE();    \
}                         \
FLOAT_OP(name, s)         \
{                         \
    FST2 = float32_ ## name (FST0, FST1, &env->fp_status);    \
    DEBUG_FPU_STATE();    \
}
FLOAT_BINOP(add)
FLOAT_BINOP(sub)
FLOAT_BINOP(mul)
FLOAT_BINOP(div)
#undef FLOAT_BINOP

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

#define FOP_COND(fmt, op, sig, cond)           \
void op_cmp_ ## fmt ## _ ## op (void)          \
{                                              \
    if (cond)                                  \
        SET_FP_COND(env->fcr31);               \
    else                                       \
        CLEAR_FP_COND(env->fcr31);             \
    if (!sig)                                  \
        clear_invalid();                       \
    /*CALL_FROM_TB1(dump_fpu_s, env);*/ \
    DEBUG_FPU_STATE();                         \
    RETURN();                                  \
}

int float64_is_unordered(float64 a, float64 b STATUS_PARAM)
{
    if (float64_is_nan(a) || float64_is_nan(b)) {
        float_raise(float_flag_invalid, status);
        return 1;
    }
    else {
        return 0;
    }
}

FOP_COND(d, f,   0,                                                      0) 
FOP_COND(d, un,  0, float64_is_unordered(FDT1, FDT0, &env->fp_status))
FOP_COND(d, eq,  0,                                                      float64_eq(FDT0, FDT1, &env->fp_status))
FOP_COND(d, ueq, 0, float64_is_unordered(FDT1, FDT0, &env->fp_status) || float64_eq(FDT0, FDT1, &env->fp_status))
FOP_COND(d, olt, 0,                                                      float64_lt(FDT0, FDT1, &env->fp_status))
FOP_COND(d, ult, 0, float64_is_unordered(FDT1, FDT0, &env->fp_status) || float64_lt(FDT0, FDT1, &env->fp_status))
FOP_COND(d, ole, 0,                                                      float64_le(FDT0, FDT1, &env->fp_status))
FOP_COND(d, ule, 0, float64_is_unordered(FDT1, FDT0, &env->fp_status) || float64_le(FDT0, FDT1, &env->fp_status))
/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called
 */
FOP_COND(d, sf,  1,                                                      (float64_is_unordered(FDT0, FDT1, &env->fp_status), 0))
FOP_COND(d, ngle,1, float64_is_unordered(FDT1, FDT0, &env->fp_status))
FOP_COND(d, seq, 1,                                                      float64_eq(FDT0, FDT1, &env->fp_status))
FOP_COND(d, ngl, 1, float64_is_unordered(FDT1, FDT0, &env->fp_status) || float64_eq(FDT0, FDT1, &env->fp_status))
FOP_COND(d, lt,  1,                                                      float64_lt(FDT0, FDT1, &env->fp_status))
FOP_COND(d, nge, 1, float64_is_unordered(FDT1, FDT0, &env->fp_status) || float64_lt(FDT0, FDT1, &env->fp_status))
FOP_COND(d, le,  1,                                                      float64_le(FDT0, FDT1, &env->fp_status))
FOP_COND(d, ngt, 1, float64_is_unordered(FDT1, FDT0, &env->fp_status) || float64_le(FDT0, FDT1, &env->fp_status))

flag float32_is_unordered(float32 a, float32 b STATUS_PARAM)
{
    extern flag float32_is_nan( float32 a );
    if (float32_is_nan(a) || float32_is_nan(b)) {
        float_raise(float_flag_invalid, status);
        return 1;
    }
    else {
        return 0;
    }
}

/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called
 */
FOP_COND(s, f,   0,                                                      0) 
FOP_COND(s, un,  0, float32_is_unordered(FST1, FST0, &env->fp_status))
FOP_COND(s, eq,  0,                                                      float32_eq(FST0, FST1, &env->fp_status))
FOP_COND(s, ueq, 0, float32_is_unordered(FST1, FST0, &env->fp_status) || float32_eq(FST0, FST1, &env->fp_status))
FOP_COND(s, olt, 0,                                                      float32_lt(FST0, FST1, &env->fp_status))
FOP_COND(s, ult, 0, float32_is_unordered(FST1, FST0, &env->fp_status) || float32_lt(FST0, FST1, &env->fp_status))
FOP_COND(s, ole, 0,                                                      float32_le(FST0, FST1, &env->fp_status))
FOP_COND(s, ule, 0, float32_is_unordered(FST1, FST0, &env->fp_status) || float32_le(FST0, FST1, &env->fp_status))
/* NOTE: the comma operator will make "cond" to eval to false,
 * but float*_is_unordered() is still called
 */
FOP_COND(s, sf,  1,                                                      (float32_is_unordered(FST0, FST1, &env->fp_status), 0))
FOP_COND(s, ngle,1, float32_is_unordered(FST1, FST0, &env->fp_status))
FOP_COND(s, seq, 1,                                                      float32_eq(FST0, FST1, &env->fp_status))
FOP_COND(s, ngl, 1, float32_is_unordered(FST1, FST0, &env->fp_status) || float32_eq(FST0, FST1, &env->fp_status))
FOP_COND(s, lt,  1,                                                      float32_lt(FST0, FST1, &env->fp_status))
FOP_COND(s, nge, 1, float32_is_unordered(FST1, FST0, &env->fp_status) || float32_lt(FST0, FST1, &env->fp_status))
FOP_COND(s, le,  1,                                                      float32_le(FST0, FST1, &env->fp_status))
FOP_COND(s, ngt, 1, float32_is_unordered(FST1, FST0, &env->fp_status) || float32_le(FST0, FST1, &env->fp_status))

void op_bc1f (void)
{
    T0 = ! IS_FP_COND_SET(env->fcr31);
    DEBUG_FPU_STATE();
    RETURN();
}

void op_bc1t (void)
{
    T0 = IS_FP_COND_SET(env->fcr31);
    DEBUG_FPU_STATE();
    RETURN();
}
#endif /* MIPS_USES_FPU */

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
        CALL_FROM_TB1(do_raise_exception_direct, EXCP_TRAP);
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

void debug_eret (void);
void op_eret (void)
{
    CALL_FROM_TB0(debug_eret);
    if (env->hflags & MIPS_HFLAG_ERL) {
        env->PC = env->CP0_ErrorEPC;
        env->hflags &= ~MIPS_HFLAG_ERL;
	env->CP0_Status &= ~(1 << CP0St_ERL);
    } else {
        env->PC = env->CP0_EPC;
        env->hflags &= ~MIPS_HFLAG_EXL;
	env->CP0_Status &= ~(1 << CP0St_EXL);
    }
    env->CP0_LLAddr = 1;
    RETURN();
}

void op_deret (void)
{
    CALL_FROM_TB0(debug_eret);
    env->PC = env->CP0_DEPC;
    RETURN();
}

void op_rdhwr_cpunum(void)
{
    if (env->CP0_HWREna & (1 << 0))
       T0 = env->CP0_EBase & 0x2ff;
    else
       CALL_FROM_TB1(do_raise_exception_direct, EXCP_RI);
    RETURN();
}

void op_rdhwr_synci_step(void)
{
    if (env->CP0_HWREna & (1 << 1))
       T0 = env->SYNCI_Step;
    else
       CALL_FROM_TB1(do_raise_exception_direct, EXCP_RI);
    RETURN();
}

void op_rdhwr_cc(void)
{
    if (env->CP0_HWREna & (1 << 2))
       T0 = env->CP0_Count;
    else
       CALL_FROM_TB1(do_raise_exception_direct, EXCP_RI);
    RETURN();
}

void op_rdhwr_ccres(void)
{
    if (env->CP0_HWREna & (1 << 3))
       T0 = env->CCRes;
    else
       CALL_FROM_TB1(do_raise_exception_direct, EXCP_RI);
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

    T0 = ((uint32_t)T1 >> pos) & ((1 << size) - 1);
    RETURN();
}

void op_ins(void)
{
    unsigned int pos = PARAM1;
    unsigned int size = PARAM2;
    target_ulong mask = ((1 << size) - 1) << pos;

    T0 = (T2 & ~mask) | (((uint32_t)T1 << pos) & mask);
    RETURN();
}

void op_wsbh(void)
{
    T0 = ((T1 << 8) & ~0x00FF00FF) | ((T1 >> 8) & 0x00FF00FF);
    RETURN();
}

#ifdef MIPS_HAS_MIPS64
void op_dext(void)
{
    unsigned int pos = PARAM1;
    unsigned int size = PARAM2;

    T0 = (T1 >> pos) & ((1 << size) - 1);
    RETURN();
}

void op_dins(void)
{
    unsigned int pos = PARAM1;
    unsigned int size = PARAM2;
    target_ulong mask = ((1 << size) - 1) << pos;

    T0 = (T2 & ~mask) | ((T1 << pos) & mask);
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
