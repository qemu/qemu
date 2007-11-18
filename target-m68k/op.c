/*
 *  m68k micro operations
 *
 *  Copyright (c) 2006-2007 CodeSourcery
 *  Written by Paul Brook
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "exec.h"
#include "m68k-qreg.h"

#ifndef offsetof
#define offsetof(type, field) ((size_t) &((type *)0)->field)
#endif

static long qreg_offsets[] = {
#define DEFO32(name, offset) offsetof(CPUState, offset),
#define DEFR(name, reg, mode) -1,
#define DEFF64(name, offset) offsetof(CPUState, offset),
    0,
#include "qregs.def"
};

#define CPU_FP_STATUS env->fp_status

#define RAISE_EXCEPTION(n) do { \
    env->exception_index = n; \
    cpu_loop_exit(); \
    } while(0)

#define get_op helper_get_op
#define set_op helper_set_op
#define get_opf64 helper_get_opf64
#define set_opf64 helper_set_opf64
uint32_t
get_op(int qreg)
{
    if (qreg >= TARGET_NUM_QREGS) {
        return env->qregs[qreg - TARGET_NUM_QREGS];
    } else if (qreg == QREG_T0) {
        return T0;
    } else {
        return *(uint32_t *)(((long)env) + qreg_offsets[qreg]);
    }
}

void set_op(int qreg, uint32_t val)
{
    if (qreg >= TARGET_NUM_QREGS) {
        env->qregs[qreg - TARGET_NUM_QREGS] = val;
    } else if (qreg == QREG_T0) {
        T0 = val;
    } else {
        *(uint32_t *)(((long)env) + qreg_offsets[qreg]) = val;
    }
}

float64 get_opf64(int qreg)
{
    if (qreg < TARGET_NUM_QREGS) {
        return *(float64 *)(((long)env) + qreg_offsets[qreg]);
    } else {
        return *(float64 *)&env->qregs[qreg - TARGET_NUM_QREGS];
    }
}

void set_opf64(int qreg, float64 val)
{
    if (qreg < TARGET_NUM_QREGS) {
        *(float64 *)(((long)env) + qreg_offsets[qreg]) = val;
    } else {
        *(float64 *)&env->qregs[qreg - TARGET_NUM_QREGS] = val;
    }
}

#define OP(name) void OPPROTO glue(op_,name) (void)

OP(mov32)
{
    set_op(PARAM1, get_op(PARAM2));
    FORCE_RET();
}

OP(mov32_im)
{
    set_op(PARAM1, PARAM2);
    FORCE_RET();
}

OP(movf64)
{
    set_opf64(PARAM1, get_opf64(PARAM2));
    FORCE_RET();
}

OP(zerof64)
{
    set_opf64(PARAM1, float64_zero);
    FORCE_RET();
}

OP(add32)
{
    uint32_t op2 = get_op(PARAM2);
    uint32_t op3 = get_op(PARAM3);
    set_op(PARAM1, op2 + op3);
    FORCE_RET();
}

OP(sub32)
{
    uint32_t op2 = get_op(PARAM2);
    uint32_t op3 = get_op(PARAM3);
    set_op(PARAM1, op2 - op3);
    FORCE_RET();
}

OP(mul32)
{
    uint32_t op2 = get_op(PARAM2);
    uint32_t op3 = get_op(PARAM3);
    set_op(PARAM1, op2 * op3);
    FORCE_RET();
}

OP(not32)
{
    uint32_t arg = get_op(PARAM2);
    set_op(PARAM1, ~arg);
    FORCE_RET();
}

OP(neg32)
{
    uint32_t arg = get_op(PARAM2);
    set_op(PARAM1, -arg);
    FORCE_RET();
}

OP(bswap32)
{
    uint32_t arg = get_op(PARAM2);
    arg = (arg >> 24) | (arg << 24)
          | ((arg >> 16) & 0xff00) | ((arg << 16) & 0xff0000);
    set_op(PARAM1, arg);
    FORCE_RET();
}

OP(btest)
{
    uint32_t op1 = get_op(PARAM1);
    uint32_t op2 = get_op(PARAM2);
    if (op1 & op2)
        env->cc_dest &= ~CCF_Z;
    else
        env->cc_dest |= CCF_Z;
    FORCE_RET();
}

OP(ff1)
{
    uint32_t arg = get_op(PARAM2);
    int n;
    for (n = 32; arg; n--)
        arg >>= 1;
    set_op(PARAM1, n);
    FORCE_RET();
}

OP(subx_cc)
{
    uint32_t op1 = get_op(PARAM1);
    uint32_t op2 = get_op(PARAM2);
    uint32_t res;
    if (env->cc_x) {
        env->cc_x = (op1 <= op2);
        env->cc_op = CC_OP_SUBX;
        res = op1 - (op2 + 1);
    } else {
        env->cc_x = (op1 < op2);
        env->cc_op = CC_OP_SUB;
        res = op1 - op2;
    }
    set_op(PARAM1, res);
    FORCE_RET();
}

OP(addx_cc)
{
    uint32_t op1 = get_op(PARAM1);
    uint32_t op2 = get_op(PARAM2);
    uint32_t res;
    if (env->cc_x) {
        res = op1 + op2 + 1;
        env->cc_x = (res <= op2);
        env->cc_op = CC_OP_ADDX;
    } else {
        res = op1 + op2;
        env->cc_x = (res < op2);
        env->cc_op = CC_OP_ADD;
    }
    set_op(PARAM1, res);
    FORCE_RET();
}

/* Logic ops.  */

OP(and32)
{
    uint32_t op2 = get_op(PARAM2);
    uint32_t op3 = get_op(PARAM3);
    set_op(PARAM1, op2 & op3);
    FORCE_RET();
}

OP(or32)
{
    uint32_t op2 = get_op(PARAM2);
    uint32_t op3 = get_op(PARAM3);
    set_op(PARAM1, op2 | op3);
    FORCE_RET();
}

OP(xor32)
{
    uint32_t op2 = get_op(PARAM2);
    uint32_t op3 = get_op(PARAM3);
    set_op(PARAM1, op2 ^ op3);
    FORCE_RET();
}

/* Shifts.  */
OP(shl32)
{
    uint32_t op2 = get_op(PARAM2);
    uint32_t op3 = get_op(PARAM3);
    uint32_t result;
    result = op2 << op3;
    set_op(PARAM1, result);
    FORCE_RET();
}

OP(shl_cc)
{
    uint32_t op1 = get_op(PARAM1);
    uint32_t op2 = get_op(PARAM2);
    uint32_t result;
    result = op1 << op2;
    set_op(PARAM1, result);
    env->cc_x = (op1 << (op2 - 1)) & 1;
    FORCE_RET();
}

OP(shr32)
{
    uint32_t op2 = get_op(PARAM2);
    uint32_t op3 = get_op(PARAM3);
    uint32_t result;
    result = op2 >> op3;
    set_op(PARAM1, result);
    FORCE_RET();
}

OP(shr_cc)
{
    uint32_t op1 = get_op(PARAM1);
    uint32_t op2 = get_op(PARAM2);
    uint32_t result;
    result = op1 >> op2;
    set_op(PARAM1, result);
    env->cc_x = (op1 >> (op2 - 1)) & 1;
    FORCE_RET();
}

OP(sar32)
{
    int32_t op2 = get_op(PARAM2);
    uint32_t op3 = get_op(PARAM3);
    uint32_t result;
    result = op2 >> op3;
    set_op(PARAM1, result);
    FORCE_RET();
}

OP(sar_cc)
{
    int32_t op1 = get_op(PARAM1);
    uint32_t op2 = get_op(PARAM2);
    uint32_t result;
    result = op1 >> op2;
    set_op(PARAM1, result);
    env->cc_x = (op1 >> (op2 - 1)) & 1;
    FORCE_RET();
}

/* Value extend.  */

OP(ext8u32)
{
    uint32_t op2 = get_op(PARAM2);
    set_op(PARAM1, (uint8_t)op2);
    FORCE_RET();
}

OP(ext8s32)
{
    uint32_t op2 = get_op(PARAM2);
    set_op(PARAM1, (int8_t)op2);
    FORCE_RET();
}

OP(ext16u32)
{
    uint32_t op2 = get_op(PARAM2);
    set_op(PARAM1, (uint16_t)op2);
    FORCE_RET();
}

OP(ext16s32)
{
    uint32_t op2 = get_op(PARAM2);
    set_op(PARAM1, (int16_t)op2);
    FORCE_RET();
}

OP(flush_flags)
{
    cpu_m68k_flush_flags(env, env->cc_op);
    FORCE_RET();
}

OP(divu)
{
    uint32_t num;
    uint32_t den;
    uint32_t quot;
    uint32_t rem;
    uint32_t flags;

    num = env->div1;
    den = env->div2;
    /* ??? This needs to make sure the throwing location is accurate.  */
    if (den == 0)
        RAISE_EXCEPTION(EXCP_DIV0);
    quot = num / den;
    rem = num % den;
    flags = 0;
    /* Avoid using a PARAM1 of zero.  This breaks dyngen because it uses
       the address of a symbol, and gcc knows symbols can't have address
       zero.  */
    if (PARAM1 == 2 && quot > 0xffff)
        flags |= CCF_V;
    if (quot == 0)
        flags |= CCF_Z;
    else if ((int32_t)quot < 0)
        flags |= CCF_N;
    env->div1 = quot;
    env->div2 = rem;
    env->cc_dest = flags;
    FORCE_RET();
}

OP(divs)
{
    int32_t num;
    int32_t den;
    int32_t quot;
    int32_t rem;
    int32_t flags;

    num = env->div1;
    den = env->div2;
    if (den == 0)
        RAISE_EXCEPTION(EXCP_DIV0);
    quot = num / den;
    rem = num % den;
    flags = 0;
    if (PARAM1 == 2 && quot != (int16_t)quot)
        flags |= CCF_V;
    if (quot == 0)
        flags |= CCF_Z;
    else if (quot < 0)
        flags |= CCF_N;
    env->div1 = quot;
    env->div2 = rem;
    env->cc_dest = flags;
    FORCE_RET();
}

/* Halt is special because it may be a semihosting call.  */
OP(halt)
{
    RAISE_EXCEPTION(EXCP_HALT_INSN);
    FORCE_RET();
}

OP(stop)
{
    env->halted = 1;
    RAISE_EXCEPTION(EXCP_HLT);
    FORCE_RET();
}

OP(raise_exception)
{
    RAISE_EXCEPTION(PARAM1);
    FORCE_RET();
}

/* Floating point comparison sets flags differently to other instructions.  */

OP(sub_cmpf64)
{
    float64 src0;
    float64 src1;
    src0 = get_opf64(PARAM2);
    src1 = get_opf64(PARAM3);
    set_opf64(PARAM1, helper_sub_cmpf64(env, src0, src1));
    FORCE_RET();
}

OP(update_xflag_tst)
{
    uint32_t op1 = get_op(PARAM1);
    env->cc_x = op1;
    FORCE_RET();
}

OP(update_xflag_lt)
{
    uint32_t op1 = get_op(PARAM1);
    uint32_t op2 = get_op(PARAM2);
    env->cc_x = (op1 < op2);
    FORCE_RET();
}

OP(get_xflag)
{
    set_op(PARAM1, env->cc_x);
    FORCE_RET();
}

OP(logic_cc)
{
    uint32_t op1 = get_op(PARAM1);
    env->cc_dest = op1;
    FORCE_RET();
}

OP(update_cc_add)
{
    uint32_t op1 = get_op(PARAM1);
    uint32_t op2 = get_op(PARAM2);
    env->cc_dest = op1;
    env->cc_src = op2;
    FORCE_RET();
}

OP(fp_result)
{
    env->fp_result = get_opf64(PARAM1);
    FORCE_RET();
}

OP(set_sr)
{
    env->sr = get_op(PARAM1) & 0xffff;
    m68k_switch_sp(env);
    FORCE_RET();
}

OP(jmp)
{
    GOTO_LABEL_PARAM(1);
}

OP(set_T0_z32)
{
    uint32_t arg = get_op(PARAM1);
    T0 = (arg == 0);
    FORCE_RET();
}

OP(set_T0_nz32)
{
    uint32_t arg = get_op(PARAM1);
    T0 = (arg != 0);
    FORCE_RET();
}

OP(set_T0_s32)
{
    int32_t arg = get_op(PARAM1);
    T0 = (arg > 0);
    FORCE_RET();
}

OP(set_T0_ns32)
{
    int32_t arg = get_op(PARAM1);
    T0 = (arg >= 0);
    FORCE_RET();
}

OP(jmp_T0)
{
    if (T0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_goto_tb0(void)
{
    GOTO_TB(op_goto_tb0, PARAM1, 0);
}

void OPPROTO op_goto_tb1(void)
{
    GOTO_TB(op_goto_tb1, PARAM1, 1);
}

OP(exit_tb)
{
    EXIT_TB();
}


/* Floating point.  */
OP(f64_to_i32)
{
    set_op(PARAM1, float64_to_int32(get_opf64(PARAM2), &CPU_FP_STATUS));
    FORCE_RET();
}

OP(f64_to_f32)
{
    union {
        float32 f;
        uint32_t i;
    } u;
    u.f = float64_to_float32(get_opf64(PARAM2), &CPU_FP_STATUS);
    set_op(PARAM1, u.i);
    FORCE_RET();
}

OP(i32_to_f64)
{
    set_opf64(PARAM1, int32_to_float64(get_op(PARAM2), &CPU_FP_STATUS));
    FORCE_RET();
}

OP(f32_to_f64)
{
    union {
        float32 f;
        uint32_t i;
    } u;
    u.i = get_op(PARAM2);
    set_opf64(PARAM1, float32_to_float64(u.f, &CPU_FP_STATUS));
    FORCE_RET();
}

OP(absf64)
{
    float64 op0 = get_opf64(PARAM2);
    set_opf64(PARAM1, float64_abs(op0));
    FORCE_RET();
}

OP(chsf64)
{
    float64 op0 = get_opf64(PARAM2);
    set_opf64(PARAM1, float64_chs(op0));
    FORCE_RET();
}

OP(sqrtf64)
{
    float64 op0 = get_opf64(PARAM2);
    set_opf64(PARAM1, float64_sqrt(op0, &CPU_FP_STATUS));
    FORCE_RET();
}

OP(addf64)
{
    float64 op0 = get_opf64(PARAM2);
    float64 op1 = get_opf64(PARAM3);
    set_opf64(PARAM1, float64_add(op0, op1, &CPU_FP_STATUS));
    FORCE_RET();
}

OP(subf64)
{
    float64 op0 = get_opf64(PARAM2);
    float64 op1 = get_opf64(PARAM3);
    set_opf64(PARAM1, float64_sub(op0, op1, &CPU_FP_STATUS));
    FORCE_RET();
}

OP(mulf64)
{
    float64 op0 = get_opf64(PARAM2);
    float64 op1 = get_opf64(PARAM3);
    set_opf64(PARAM1, float64_mul(op0, op1, &CPU_FP_STATUS));
    FORCE_RET();
}

OP(divf64)
{
    float64 op0 = get_opf64(PARAM2);
    float64 op1 = get_opf64(PARAM3);
    set_opf64(PARAM1, float64_div(op0, op1, &CPU_FP_STATUS));
    FORCE_RET();
}

OP(iround_f64)
{
    float64 op0 = get_opf64(PARAM2);
    set_opf64(PARAM1, float64_round_to_int(op0, &CPU_FP_STATUS));
    FORCE_RET();
}

OP(itrunc_f64)
{
    float64 op0 = get_opf64(PARAM2);
    set_opf64(PARAM1, float64_trunc_to_int(op0, &CPU_FP_STATUS));
    FORCE_RET();
}

OP(compare_quietf64)
{
    float64 op0 = get_opf64(PARAM2);
    float64 op1 = get_opf64(PARAM3);
    set_op(PARAM1, float64_compare_quiet(op0, op1, &CPU_FP_STATUS));
    FORCE_RET();
}

OP(movec)
{
    int op1 = get_op(PARAM1);
    uint32_t op2 = get_op(PARAM2);
    helper_movec(env, op1, op2);
}

/* Memory access.  */

#define MEMSUFFIX _raw
#include "op_mem.h"

#if !defined(CONFIG_USER_ONLY)
#define MEMSUFFIX _user
#include "op_mem.h"
#define MEMSUFFIX _kernel
#include "op_mem.h"
#endif

/* MAC unit.  */
/* TODO: The MAC instructions use 64-bit arithmetic fairly extensively.
   This results in fairly large ops (and sometimes other issues) on 32-bit
   hosts.  Maybe move most of them into helpers.  */
OP(macmuls)
{
    uint32_t op1 = get_op(PARAM1);
    uint32_t op2 = get_op(PARAM2);
    int64_t product;
    int64_t res;

    product = (uint64_t)op1 * op2;
    res = (product << 24) >> 24;
    if (res != product) {
        env->macsr |= MACSR_V;
        if (env->macsr & MACSR_OMC) {
            /* Make sure the accumulate operation overflows.  */
            if (product < 0)
                res = ~(1ll << 50);
            else
                res = 1ll << 50;
        }
    }
    env->mactmp = res;
    FORCE_RET();
}

OP(macmulu)
{
    uint32_t op1 = get_op(PARAM1);
    uint32_t op2 = get_op(PARAM2);
    uint64_t product;

    product = (uint64_t)op1 * op2;
    if (product & (0xffffffull << 40)) {
        env->macsr |= MACSR_V;
        if (env->macsr & MACSR_OMC) {
            /* Make sure the accumulate operation overflows.  */
            product = 1ll << 50;
        } else {
            product &= ((1ull << 40) - 1);
        }
    }
    env->mactmp = product;
    FORCE_RET();
}

OP(macmulf)
{
    int32_t op1 = get_op(PARAM1);
    int32_t op2 = get_op(PARAM2);
    uint64_t product;
    uint32_t remainder;

    product = (uint64_t)op1 * op2;
    if (env->macsr & MACSR_RT) {
        remainder = product & 0xffffff;
        product >>= 24;
        if (remainder > 0x800000)
            product++;
        else if (remainder == 0x800000)
            product += (product & 1);
    } else {
        product >>= 24;
    }
    env->mactmp = product;
    FORCE_RET();
}

OP(macshl)
{
    env->mactmp <<= 1;
}

OP(macshr)
{
    env->mactmp >>= 1;
}

OP(macadd)
{
    int acc = PARAM1;
    env->macc[acc] += env->mactmp;
    FORCE_RET();
}

OP(macsub)
{
    int acc = PARAM1;
    env->macc[acc] -= env->mactmp;
    FORCE_RET();
}

OP(macsats)
{
    int acc = PARAM1;
    int64_t sum;
    int64_t result;

    sum = env->macc[acc];
    result = (sum << 16) >> 16;
    if (result != sum) {
        env->macsr |= MACSR_V;
    }
    if (env->macsr & MACSR_V) {
        env->macsr |= MACSR_PAV0 << acc;
        if (env->macsr & MACSR_OMC) {
            /* The result is saturated to 32 bits, despite overflow occuring
               at 48 bits.  Seems weird, but that's what the hardware docs
               say.  */
            result = (result >> 63) ^ 0x7fffffff;
        }
    }
    env->macc[acc] = result;
    FORCE_RET();
}

OP(macsatu)
{
    int acc = PARAM1;
    uint64_t sum;

    sum = env->macc[acc];
    if (sum & (0xffffull << 48)) {
        env->macsr |= MACSR_V;
    }
    if (env->macsr & MACSR_V) {
        env->macsr |= MACSR_PAV0 << acc;
        if (env->macsr & MACSR_OMC) {
            if (sum > (1ull << 53))
                sum = 0;
            else
                sum = (1ull << 48) - 1;
        } else {
            sum &= ((1ull << 48) - 1);
        }
    }
    FORCE_RET();
}

OP(macsatf)
{
    int acc = PARAM1;
    int64_t sum;
    int64_t result;

    sum = env->macc[acc];
    result = (sum << 16) >> 16;
    if (result != sum) {
        env->macsr |= MACSR_V;
    }
    if (env->macsr & MACSR_V) {
        env->macsr |= MACSR_PAV0 << acc;
        if (env->macsr & MACSR_OMC) {
            result = (result >> 63) ^ 0x7fffffffffffll;
        }
    }
    env->macc[acc] = result;
    FORCE_RET();
}

OP(mac_clear_flags)
{
    env->macsr &= ~(MACSR_V | MACSR_Z | MACSR_N | MACSR_EV);
}

OP(mac_set_flags)
{
    int acc = PARAM1;
    uint64_t val;
    val = env->macc[acc];
    if (val == 0)
        env->macsr |= MACSR_Z;
    else if (val & (1ull << 47));
        env->macsr |= MACSR_N;
    if (env->macsr & (MACSR_PAV0 << acc)) {
        env->macsr |= MACSR_V;
    }
    if (env->macsr & MACSR_FI) {
        val = ((int64_t)val) >> 40;
        if (val != 0 && val != -1)
            env->macsr |= MACSR_EV;
    } else if (env->macsr & MACSR_SU) {
        val = ((int64_t)val) >> 32;
        if (val != 0 && val != -1)
            env->macsr |= MACSR_EV;
    } else {
        if ((val >> 32) != 0)
            env->macsr |= MACSR_EV;
    }
    FORCE_RET();
}

OP(get_macf)
{
    int acc = PARAM2;
    int64_t val;
    int rem;
    uint32_t result;

    val = env->macc[acc];
    if (env->macsr & MACSR_SU) {
        /* 16-bit rounding.  */
        rem = val & 0xffffff;
        val = (val >> 24) & 0xffffu;
        if (rem > 0x800000)
            val++;
        else if (rem == 0x800000)
            val += (val & 1);
    } else if (env->macsr & MACSR_RT) {
        /* 32-bit rounding.  */
        rem = val & 0xff;
        val >>= 8;
        if (rem > 0x80)
            val++;
        else if (rem == 0x80)
            val += (val & 1);
    } else {
        /* No rounding.  */
        val >>= 8;
    }
    if (env->macsr & MACSR_OMC) {
        /* Saturate.  */
        if (env->macsr & MACSR_SU) {
            if (val != (uint16_t) val) {
                result = ((val >> 63) ^ 0x7fff) & 0xffff;
            } else {
                result = val & 0xffff;
            }
        } else {
            if (val != (uint32_t)val) {
                result = ((uint32_t)(val >> 63) & 0x7fffffff);
            } else {
                result = (uint32_t)val;
            }
        }
    } else {
        /* No saturation.  */
        if (env->macsr & MACSR_SU) {
            result = val & 0xffff;
        } else {
            result = (uint32_t)val;
        }
    }
    set_op(PARAM1, result);
    FORCE_RET();
}

OP(get_maci)
{
    int acc = PARAM2;
    set_op(PARAM1, (uint32_t)env->macc[acc]);
    FORCE_RET();
}

OP(get_macs)
{
    int acc = PARAM2;
    int64_t val = env->macc[acc];
    uint32_t result;
    if (val == (int32_t)val) {
        result = (int32_t)val;
    } else {
        result = (val >> 61) ^ 0x7fffffff;
    }
    set_op(PARAM1, result);
    FORCE_RET();
}

OP(get_macu)
{
    int acc = PARAM2;
    uint64_t val = env->macc[acc];
    uint32_t result;
    if ((val >> 32) == 0) {
        result = (uint32_t)val;
    } else {
        result = 0xffffffffu;
    }
    set_op(PARAM1, result);
    FORCE_RET();
}

OP(clear_mac)
{
    int acc = PARAM1;

    env->macc[acc] = 0;
    env->macsr &= ~(MACSR_PAV0 << acc);
    FORCE_RET();
}

OP(move_mac)
{
    int dest = PARAM1;
    int src = PARAM2;
    uint32_t mask;
    env->macc[dest] = env->macc[src];
    mask = MACSR_PAV0 << dest;
    if (env->macsr & (MACSR_PAV0 << src))
        env->macsr |= mask;
    else
        env->macsr &= ~mask;
    FORCE_RET();
}

OP(get_mac_extf)
{
    uint32_t val;
    int acc = PARAM2;
    val = env->macc[acc] & 0x00ff;
    val = (env->macc[acc] >> 32) & 0xff00;
    val |= (env->macc[acc + 1] << 16) & 0x00ff0000;
    val |= (env->macc[acc + 1] >> 16) & 0xff000000;
    set_op(PARAM1, val);
    FORCE_RET();
}

OP(get_mac_exti)
{
    uint32_t val;
    int acc = PARAM2;
    val = (env->macc[acc] >> 32) & 0xffff;
    val |= (env->macc[acc + 1] >> 16) & 0xffff0000;
    set_op(PARAM1, val);
    FORCE_RET();
}

OP(set_macf)
{
    int acc = PARAM2;
    int32_t val = get_op(PARAM1);
    env->macc[acc] = ((int64_t)val) << 8;
    env->macsr &= ~(MACSR_PAV0 << acc);
    FORCE_RET();
}

OP(set_macs)
{
    int acc = PARAM2;
    int32_t val = get_op(PARAM1);
    env->macc[acc] = val;
    env->macsr &= ~(MACSR_PAV0 << acc);
    FORCE_RET();
}

OP(set_macu)
{
    int acc = PARAM2;
    uint32_t val = get_op(PARAM1);
    env->macc[acc] = val;
    env->macsr &= ~(MACSR_PAV0 << acc);
    FORCE_RET();
}

OP(set_mac_extf)
{
    int acc = PARAM2;
    int32_t val = get_op(PARAM1);
    int64_t res;
    int32_t tmp;
    res = env->macc[acc] & 0xffffffff00ull;
    tmp = (int16_t)(val & 0xff00);
    res |= ((int64_t)tmp) << 32;
    res |= val & 0xff;
    env->macc[acc] = res;
    res = env->macc[acc + 1] & 0xffffffff00ull;
    tmp = (val & 0xff000000);
    res |= ((int64_t)tmp) << 16;
    res |= (val >> 16) & 0xff;
    env->macc[acc + 1] = res;
}

OP(set_mac_exts)
{
    int acc = PARAM2;
    int32_t val = get_op(PARAM1);
    int64_t res;
    int32_t tmp;
    res = (uint32_t)env->macc[acc];
    tmp = (int16_t)val;
    res |= ((int64_t)tmp) << 32;
    env->macc[acc] = res;
    res = (uint32_t)env->macc[acc + 1];
    tmp = val & 0xffff0000;
    res |= (int64_t)tmp << 16;
    env->macc[acc + 1] = res;
}

OP(set_mac_extu)
{
    int acc = PARAM2;
    int32_t val = get_op(PARAM1);
    uint64_t res;
    res = (uint32_t)env->macc[acc];
    res |= ((uint64_t)(val & 0xffff)) << 32;
    env->macc[acc] = res;
    res = (uint32_t)env->macc[acc + 1];
    res |= (uint64_t)(val & 0xffff0000) << 16;
    env->macc[acc + 1] = res;
}

OP(set_macsr)
{
    m68k_set_macsr(env, get_op(PARAM1));
}
