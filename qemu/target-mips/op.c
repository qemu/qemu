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

#define TN T0
#include "op_template.c"
#undef TN
#define TN T1
#include "op_template.c"
#undef TN
#define TN T2
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
    T0 += T1;
    RETURN();
}

void op_addo (void)
{
    target_ulong tmp;

    tmp = T0;
    T0 += T1;
    if (((tmp ^ T1 ^ (-1)) & (T0 ^ T1)) >> 31) {
       /* operands of same sign, result different sign */
        CALL_FROM_TB1(do_raise_exception_direct, EXCP_OVERFLOW);
    }
    RETURN();
}

void op_sub (void)
{
    T0 -= T1;
    RETURN();
}

void op_subo (void)
{
    target_ulong tmp;

    tmp = T0;
    T0 = (int32_t)T0 - (int32_t)T1;
    if (((tmp ^ T1) & (tmp ^ T0)) >> 31) {
       /* operands of different sign, first operand and result different sign */
        CALL_FROM_TB1(do_raise_exception_direct, EXCP_OVERFLOW);
    }
    RETURN();
}

void op_mul (void)
{
    T0 = (int32_t)T0 * (int32_t)T1;
    RETURN();
}

void op_div (void)
{
    if (T1 != 0) {
        env->LO = (int32_t)T0 / (int32_t)T1;
        env->HI = (int32_t)T0 % (int32_t)T1;
    }
    RETURN();
}

void op_divu (void)
{
    if (T1 != 0) {
        env->LO = T0 / T1;
        env->HI = T0 % T1;
    }
    RETURN();
}

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
    T0 = T0 << T1;
    RETURN();
}

void op_sra (void)
{
    T0 = (int32_t)T0 >> T1;
    RETURN();
}

void op_srl (void)
{
    T0 = T0 >> T1;
    RETURN();
}

void op_sllv (void)
{
    T0 = T1 << (T0 & 0x1F);
    RETURN();
}

void op_srav (void)
{
    T0 = (int32_t)T1 >> (T0 & 0x1F);
    RETURN();
}

void op_srlv (void)
{
    T0 = T1 >> (T0 & 0x1F);
    RETURN();
}

void op_clo (void)
{
    int n;

    if (T0 == (target_ulong)-1) {
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

/* 64 bits arithmetic */
#if (HOST_LONG_BITS == 64)
static inline uint64_t get_HILO (void)
{
    return ((uint64_t)env->HI << 32) | (uint64_t)env->LO;
}

static inline void set_HILO (uint64_t HILO)
{
    env->LO = HILO & 0xFFFFFFFF;
    env->HI = HILO >> 32;
}

void op_mult (void)
{
    set_HILO((int64_t)(int32_t)T0 * (int64_t)(int32_t)T1);
    RETURN();
}

void op_multu (void)
{
    set_HILO((uint64_t)T0 * (uint64_t)T1);
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

    tmp = ((uint64_t)T0 * (uint64_t)T1);
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

    tmp = ((uint64_t)T0 * (uint64_t)T1);
    set_HILO(get_HILO() - tmp);
    RETURN();
}
#else
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

/* Branchs */
//#undef USE_DIRECT_JUMP

void OPPROTO op_goto_tb0(void)
{
    GOTO_TB(op_goto_tb0, PARAM1, 0);
}

void OPPROTO op_goto_tb1(void)
{
    GOTO_TB(op_goto_tb1, PARAM1, 1);
}

/* Branch to register */
void op_save_breg_target (void)
{
    env->btarget = T2;
}

void op_restore_breg_target (void)
{
    T2 = env->btarget;
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
void op_mfc0 (void)
{
    CALL_FROM_TB2(do_mfc0, PARAM1, PARAM2);
    RETURN();
}

void op_mtc0 (void)
{
    CALL_FROM_TB2(do_mtc0, PARAM1, PARAM2);
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

FLOAT_OP(cvtd, w)
{
    FDT2 = int32_to_float64(WT0, &env->fp_status);
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

flag float64_is_unordered(float64 a, float64 b STATUS_PARAM)
{
    extern flag float64_is_nan( float64 a );
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
}

void op_set_lladdr (void)
{
    env->CP0_LLAddr = T2;
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
}

void op_deret (void)
{
    CALL_FROM_TB0(debug_eret);
    env->PC = env->CP0_DEPC;
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
}

void op_wait (void)
{
    env->halted = 1;
    CALL_FROM_TB1(do_raise_exception, EXCP_HLT);
}
