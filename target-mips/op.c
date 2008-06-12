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

/* 64 bits arithmetic */
#if TARGET_LONG_BITS > HOST_LONG_BITS
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
    return ((uint64_t)env->HI[env->current_tc][0] << 32) |
            ((uint64_t)(uint32_t)env->LO[env->current_tc][0]);
}

static always_inline void set_HILO (uint64_t HILO)
{
    env->LO[env->current_tc][0] = (int32_t)(HILO & 0xFFFFFFFF);
    env->HI[env->current_tc][0] = (int32_t)(HILO >> 32);
}

static always_inline void set_HIT0_LO (uint64_t HILO)
{
    env->LO[env->current_tc][0] = (int32_t)(HILO & 0xFFFFFFFF);
    T0 = env->HI[env->current_tc][0] = (int32_t)(HILO >> 32);
}

static always_inline void set_HI_LOT0 (uint64_t HILO)
{
    T0 = env->LO[env->current_tc][0] = (int32_t)(HILO & 0xFFFFFFFF);
    env->HI[env->current_tc][0] = (int32_t)(HILO >> 32);
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

/* CP1 functions */
#if 0
# define DEBUG_FPU_STATE() CALL_FROM_TB1(dump_fpu, env)
#else
# define DEBUG_FPU_STATE() do { } while(0)
#endif

/* Float support.
   Single precition routines have a "s" suffix, double precision a
   "d" suffix, 32bit integer "w", 64bit integer "l", paired singe "ps",
   paired single lowwer "pl", paired single upper "pu".  */

#define FLOAT_OP(name, p) void OPPROTO op_float_##name##_##p(void)

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
    unsigned int mask = GET_FP_COND (env->fpu) >> PARAM1;
    if (!(mask & 1))
        WT2 = WT0;
    if (!(mask & 2))
        WTH2 = WTH0;
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
    unsigned int mask = GET_FP_COND (env->fpu) >> PARAM1;
    if (mask & 1)
        WT2 = WT0;
    if (mask & 2)
        WTH2 = WTH0;
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
