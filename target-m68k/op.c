/*
 *  m68k micro operations
 * 
 *  Copyright (c) 2006 CodeSourcery
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
    if (qreg == QREG_T0) {
        return T0;
    } else if (qreg < TARGET_NUM_QREGS) {
        return *(uint32_t *)(((long)env) + qreg_offsets[qreg]);
    } else {
        return env->qregs[qreg - TARGET_NUM_QREGS];
    }
}

void set_op(int qreg, uint32_t val)
{
    if (qreg == QREG_T0) {
        T0 = val;
    } else if (qreg < TARGET_NUM_QREGS) {
        *(uint32_t *)(((long)env) + qreg_offsets[qreg]) = val;
    } else {
        env->qregs[qreg - TARGET_NUM_QREGS] = val;
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

#define OP(name) void OPPROTO op_##name (void)

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
    set_opf64(PARAM1, 0);
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

OP(addx_cc)
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

OP(subx_cc)
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

/* Load/store ops.  */
OP(ld8u32)
{
    uint32_t addr = get_op(PARAM2);
    set_op(PARAM1, ldub(addr));
    FORCE_RET();
}

OP(ld8s32)
{
    uint32_t addr = get_op(PARAM2);
    set_op(PARAM1, ldsb(addr));
    FORCE_RET();
}

OP(ld16u32)
{
    uint32_t addr = get_op(PARAM2);
    set_op(PARAM1, lduw(addr));
    FORCE_RET();
}

OP(ld16s32)
{
    uint32_t addr = get_op(PARAM2);
    set_op(PARAM1, ldsw(addr));
    FORCE_RET();
}

OP(ld32)
{
    uint32_t addr = get_op(PARAM2);
    set_op(PARAM1, ldl(addr));
    FORCE_RET();
}

OP(st8)
{
    uint32_t addr = get_op(PARAM1);
    stb(addr, get_op(PARAM2));
    FORCE_RET();
}

OP(st16)
{
    uint32_t addr = get_op(PARAM1);
    stw(addr, get_op(PARAM2));
    FORCE_RET();
}

OP(st32)
{
    uint32_t addr = get_op(PARAM1);
    stl(addr, get_op(PARAM2));
    FORCE_RET();
}

OP(ldf64)
{
    uint32_t addr = get_op(PARAM2);
    set_opf64(PARAM1, ldfq(addr));
    FORCE_RET();
}

OP(stf64)
{
    uint32_t addr = get_op(PARAM1);
    stfq(addr, get_opf64(PARAM2));
    FORCE_RET();
}

OP(flush_flags)
{
    int cc_op  = PARAM1;
    if (cc_op == CC_OP_DYNAMIC)
        cc_op = env->cc_op;
    cpu_m68k_flush_flags(env, cc_op);
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

OP(jmp)
{
    GOTO_LABEL_PARAM(1);
}

/* These ops involve a function call, which probably requires a stack frame
   and breaks things on some hosts.  */
OP(jmp_z32)
{
    uint32_t arg = get_op(PARAM1);
    if (arg == 0)
        GOTO_LABEL_PARAM(2);
    FORCE_RET();
}

OP(jmp_nz32)
{
    uint32_t arg = get_op(PARAM1);
    if (arg != 0)
        GOTO_LABEL_PARAM(2);
    FORCE_RET();
}

OP(jmp_s32)
{
    int32_t arg = get_op(PARAM1);
    if (arg < 0)
        GOTO_LABEL_PARAM(2);
    FORCE_RET();
}

OP(jmp_ns32)
{
    int32_t arg = get_op(PARAM1);
    if (arg >= 0)
        GOTO_LABEL_PARAM(2);
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
