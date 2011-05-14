/*
 *  Alpha emulation cpu micro-operations helpers for qemu.
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "exec.h"
#include "host-utils.h"
#include "softfloat.h"
#include "helper.h"
#include "qemu-timer.h"

/*****************************************************************************/
/* Exceptions processing helpers */

/* This should only be called from translate, via gen_excp.
   We expect that ENV->PC has already been updated.  */
void QEMU_NORETURN helper_excp(int excp, int error)
{
    env->exception_index = excp;
    env->error_code = error;
    cpu_loop_exit(env);
}

static void do_restore_state(void *retaddr)
{
    unsigned long pc = (unsigned long)retaddr;

    if (pc) {
        TranslationBlock *tb = tb_find_pc(pc);
        if (tb) {
            cpu_restore_state(tb, env, pc);
        }
    }
}

/* This may be called from any of the helpers to set up EXCEPTION_INDEX.  */
static void QEMU_NORETURN dynamic_excp(int excp, int error)
{
    env->exception_index = excp;
    env->error_code = error;
    do_restore_state(GETPC());
    cpu_loop_exit(env);
}

static void QEMU_NORETURN arith_excp(int exc, uint64_t mask)
{
    env->trap_arg0 = exc;
    env->trap_arg1 = mask;
    dynamic_excp(EXCP_ARITH, 0);
}

uint64_t helper_load_pcc (void)
{
#ifndef CONFIG_USER_ONLY
    /* In system mode we have access to a decent high-resolution clock.
       In order to make OS-level time accounting work with the RPCC,
       present it with a well-timed clock fixed at 250MHz.  */
    return (((uint64_t)env->pcc_ofs << 32)
            | (uint32_t)(qemu_get_clock_ns(vm_clock) >> 2));
#else
    /* In user-mode, vm_clock doesn't exist.  Just pass through the host cpu
       clock ticks.  Also, don't bother taking PCC_OFS into account.  */
    return (uint32_t)cpu_get_real_ticks();
#endif
}

uint64_t helper_load_fpcr (void)
{
    return cpu_alpha_load_fpcr (env);
}

void helper_store_fpcr (uint64_t val)
{
    cpu_alpha_store_fpcr (env, val);
}

uint64_t helper_addqv (uint64_t op1, uint64_t op2)
{
    uint64_t tmp = op1;
    op1 += op2;
    if (unlikely((tmp ^ op2 ^ (-1ULL)) & (tmp ^ op1) & (1ULL << 63))) {
        arith_excp(EXC_M_IOV, 0);
    }
    return op1;
}

uint64_t helper_addlv (uint64_t op1, uint64_t op2)
{
    uint64_t tmp = op1;
    op1 = (uint32_t)(op1 + op2);
    if (unlikely((tmp ^ op2 ^ (-1UL)) & (tmp ^ op1) & (1UL << 31))) {
        arith_excp(EXC_M_IOV, 0);
    }
    return op1;
}

uint64_t helper_subqv (uint64_t op1, uint64_t op2)
{
    uint64_t res;
    res = op1 - op2;
    if (unlikely((op1 ^ op2) & (res ^ op1) & (1ULL << 63))) {
        arith_excp(EXC_M_IOV, 0);
    }
    return res;
}

uint64_t helper_sublv (uint64_t op1, uint64_t op2)
{
    uint32_t res;
    res = op1 - op2;
    if (unlikely((op1 ^ op2) & (res ^ op1) & (1UL << 31))) {
        arith_excp(EXC_M_IOV, 0);
    }
    return res;
}

uint64_t helper_mullv (uint64_t op1, uint64_t op2)
{
    int64_t res = (int64_t)op1 * (int64_t)op2;

    if (unlikely((int32_t)res != res)) {
        arith_excp(EXC_M_IOV, 0);
    }
    return (int64_t)((int32_t)res);
}

uint64_t helper_mulqv (uint64_t op1, uint64_t op2)
{
    uint64_t tl, th;

    muls64(&tl, &th, op1, op2);
    /* If th != 0 && th != -1, then we had an overflow */
    if (unlikely((th + 1) > 1)) {
        arith_excp(EXC_M_IOV, 0);
    }
    return tl;
}

uint64_t helper_umulh (uint64_t op1, uint64_t op2)
{
    uint64_t tl, th;

    mulu64(&tl, &th, op1, op2);
    return th;
}

uint64_t helper_ctpop (uint64_t arg)
{
    return ctpop64(arg);
}

uint64_t helper_ctlz (uint64_t arg)
{
    return clz64(arg);
}

uint64_t helper_cttz (uint64_t arg)
{
    return ctz64(arg);
}

static inline uint64_t byte_zap(uint64_t op, uint8_t mskb)
{
    uint64_t mask;

    mask = 0;
    mask |= ((mskb >> 0) & 1) * 0x00000000000000FFULL;
    mask |= ((mskb >> 1) & 1) * 0x000000000000FF00ULL;
    mask |= ((mskb >> 2) & 1) * 0x0000000000FF0000ULL;
    mask |= ((mskb >> 3) & 1) * 0x00000000FF000000ULL;
    mask |= ((mskb >> 4) & 1) * 0x000000FF00000000ULL;
    mask |= ((mskb >> 5) & 1) * 0x0000FF0000000000ULL;
    mask |= ((mskb >> 6) & 1) * 0x00FF000000000000ULL;
    mask |= ((mskb >> 7) & 1) * 0xFF00000000000000ULL;

    return op & ~mask;
}

uint64_t helper_zap(uint64_t val, uint64_t mask)
{
    return byte_zap(val, mask);
}

uint64_t helper_zapnot(uint64_t val, uint64_t mask)
{
    return byte_zap(val, ~mask);
}

uint64_t helper_cmpbge (uint64_t op1, uint64_t op2)
{
    uint8_t opa, opb, res;
    int i;

    res = 0;
    for (i = 0; i < 8; i++) {
        opa = op1 >> (i * 8);
        opb = op2 >> (i * 8);
        if (opa >= opb)
            res |= 1 << i;
    }
    return res;
}

uint64_t helper_minub8 (uint64_t op1, uint64_t op2)
{
    uint64_t res = 0;
    uint8_t opa, opb, opr;
    int i;

    for (i = 0; i < 8; ++i) {
        opa = op1 >> (i * 8);
        opb = op2 >> (i * 8);
        opr = opa < opb ? opa : opb;
        res |= (uint64_t)opr << (i * 8);
    }
    return res;
}

uint64_t helper_minsb8 (uint64_t op1, uint64_t op2)
{
    uint64_t res = 0;
    int8_t opa, opb;
    uint8_t opr;
    int i;

    for (i = 0; i < 8; ++i) {
        opa = op1 >> (i * 8);
        opb = op2 >> (i * 8);
        opr = opa < opb ? opa : opb;
        res |= (uint64_t)opr << (i * 8);
    }
    return res;
}

uint64_t helper_minuw4 (uint64_t op1, uint64_t op2)
{
    uint64_t res = 0;
    uint16_t opa, opb, opr;
    int i;

    for (i = 0; i < 4; ++i) {
        opa = op1 >> (i * 16);
        opb = op2 >> (i * 16);
        opr = opa < opb ? opa : opb;
        res |= (uint64_t)opr << (i * 16);
    }
    return res;
}

uint64_t helper_minsw4 (uint64_t op1, uint64_t op2)
{
    uint64_t res = 0;
    int16_t opa, opb;
    uint16_t opr;
    int i;

    for (i = 0; i < 4; ++i) {
        opa = op1 >> (i * 16);
        opb = op2 >> (i * 16);
        opr = opa < opb ? opa : opb;
        res |= (uint64_t)opr << (i * 16);
    }
    return res;
}

uint64_t helper_maxub8 (uint64_t op1, uint64_t op2)
{
    uint64_t res = 0;
    uint8_t opa, opb, opr;
    int i;

    for (i = 0; i < 8; ++i) {
        opa = op1 >> (i * 8);
        opb = op2 >> (i * 8);
        opr = opa > opb ? opa : opb;
        res |= (uint64_t)opr << (i * 8);
    }
    return res;
}

uint64_t helper_maxsb8 (uint64_t op1, uint64_t op2)
{
    uint64_t res = 0;
    int8_t opa, opb;
    uint8_t opr;
    int i;

    for (i = 0; i < 8; ++i) {
        opa = op1 >> (i * 8);
        opb = op2 >> (i * 8);
        opr = opa > opb ? opa : opb;
        res |= (uint64_t)opr << (i * 8);
    }
    return res;
}

uint64_t helper_maxuw4 (uint64_t op1, uint64_t op2)
{
    uint64_t res = 0;
    uint16_t opa, opb, opr;
    int i;

    for (i = 0; i < 4; ++i) {
        opa = op1 >> (i * 16);
        opb = op2 >> (i * 16);
        opr = opa > opb ? opa : opb;
        res |= (uint64_t)opr << (i * 16);
    }
    return res;
}

uint64_t helper_maxsw4 (uint64_t op1, uint64_t op2)
{
    uint64_t res = 0;
    int16_t opa, opb;
    uint16_t opr;
    int i;

    for (i = 0; i < 4; ++i) {
        opa = op1 >> (i * 16);
        opb = op2 >> (i * 16);
        opr = opa > opb ? opa : opb;
        res |= (uint64_t)opr << (i * 16);
    }
    return res;
}

uint64_t helper_perr (uint64_t op1, uint64_t op2)
{
    uint64_t res = 0;
    uint8_t opa, opb, opr;
    int i;

    for (i = 0; i < 8; ++i) {
        opa = op1 >> (i * 8);
        opb = op2 >> (i * 8);
        if (opa >= opb)
            opr = opa - opb;
        else
            opr = opb - opa;
        res += opr;
    }
    return res;
}

uint64_t helper_pklb (uint64_t op1)
{
    return (op1 & 0xff) | ((op1 >> 24) & 0xff00);
}

uint64_t helper_pkwb (uint64_t op1)
{
    return ((op1 & 0xff)
            | ((op1 >> 8) & 0xff00)
            | ((op1 >> 16) & 0xff0000)
            | ((op1 >> 24) & 0xff000000));
}

uint64_t helper_unpkbl (uint64_t op1)
{
    return (op1 & 0xff) | ((op1 & 0xff00) << 24);
}

uint64_t helper_unpkbw (uint64_t op1)
{
    return ((op1 & 0xff)
            | ((op1 & 0xff00) << 8)
            | ((op1 & 0xff0000) << 16)
            | ((op1 & 0xff000000) << 24));
}

/* Floating point helpers */

void helper_setroundmode (uint32_t val)
{
    set_float_rounding_mode(val, &FP_STATUS);
}

void helper_setflushzero (uint32_t val)
{
    set_flush_to_zero(val, &FP_STATUS);
}

void helper_fp_exc_clear (void)
{
    set_float_exception_flags(0, &FP_STATUS);
}

uint32_t helper_fp_exc_get (void)
{
    return get_float_exception_flags(&FP_STATUS);
}

/* Raise exceptions for ieee fp insns without software completion.
   In that case there are no exceptions that don't trap; the mask
   doesn't apply.  */
void helper_fp_exc_raise(uint32_t exc, uint32_t regno)
{
    if (exc) {
        uint32_t hw_exc = 0;

        if (exc & float_flag_invalid) {
            hw_exc |= EXC_M_INV;
        }
        if (exc & float_flag_divbyzero) {
            hw_exc |= EXC_M_DZE;
        }
        if (exc & float_flag_overflow) {
            hw_exc |= EXC_M_FOV;
        }
        if (exc & float_flag_underflow) {
            hw_exc |= EXC_M_UNF;
        }
        if (exc & float_flag_inexact) {
            hw_exc |= EXC_M_INE;
        }

        arith_excp(hw_exc, 1ull << regno);
    }
}

/* Raise exceptions for ieee fp insns with software completion.  */
void helper_fp_exc_raise_s(uint32_t exc, uint32_t regno)
{
    if (exc) {
        env->fpcr_exc_status |= exc;

        exc &= ~env->fpcr_exc_mask;
        if (exc) {
            helper_fp_exc_raise(exc, regno);
        }
    }
}

/* Input remapping without software completion.  Handle denormal-map-to-zero
   and trap for all other non-finite numbers.  */
uint64_t helper_ieee_input(uint64_t val)
{
    uint32_t exp = (uint32_t)(val >> 52) & 0x7ff;
    uint64_t frac = val & 0xfffffffffffffull;

    if (exp == 0) {
        if (frac != 0) {
            /* If DNZ is set flush denormals to zero on input.  */
            if (env->fpcr_dnz) {
                val &= 1ull << 63;
            } else {
                arith_excp(EXC_M_UNF, 0);
            }
        }
    } else if (exp == 0x7ff) {
        /* Infinity or NaN.  */
        /* ??? I'm not sure these exception bit flags are correct.  I do
           know that the Linux kernel, at least, doesn't rely on them and
           just emulates the insn to figure out what exception to use.  */
        arith_excp(frac ? EXC_M_INV : EXC_M_FOV, 0);
    }
    return val;
}

/* Similar, but does not trap for infinities.  Used for comparisons.  */
uint64_t helper_ieee_input_cmp(uint64_t val)
{
    uint32_t exp = (uint32_t)(val >> 52) & 0x7ff;
    uint64_t frac = val & 0xfffffffffffffull;

    if (exp == 0) {
        if (frac != 0) {
            /* If DNZ is set flush denormals to zero on input.  */
            if (env->fpcr_dnz) {
                val &= 1ull << 63;
            } else {
                arith_excp(EXC_M_UNF, 0);
            }
        }
    } else if (exp == 0x7ff && frac) {
        /* NaN.  */
        arith_excp(EXC_M_INV, 0);
    }
    return val;
}

/* Input remapping with software completion enabled.  All we have to do
   is handle denormal-map-to-zero; all other inputs get exceptions as
   needed from the actual operation.  */
uint64_t helper_ieee_input_s(uint64_t val)
{
    if (env->fpcr_dnz) {
        uint32_t exp = (uint32_t)(val >> 52) & 0x7ff;
        if (exp == 0) {
            val &= 1ull << 63;
        }
    }
    return val;
}

/* F floating (VAX) */
static inline uint64_t float32_to_f(float32 fa)
{
    uint64_t r, exp, mant, sig;
    CPU_FloatU a;

    a.f = fa;
    sig = ((uint64_t)a.l & 0x80000000) << 32;
    exp = (a.l >> 23) & 0xff;
    mant = ((uint64_t)a.l & 0x007fffff) << 29;

    if (exp == 255) {
        /* NaN or infinity */
        r = 1; /* VAX dirty zero */
    } else if (exp == 0) {
        if (mant == 0) {
            /* Zero */
            r = 0;
        } else {
            /* Denormalized */
            r = sig | ((exp + 1) << 52) | mant;
        }
    } else {
        if (exp >= 253) {
            /* Overflow */
            r = 1; /* VAX dirty zero */
        } else {
            r = sig | ((exp + 2) << 52);
        }
    }

    return r;
}

static inline float32 f_to_float32(uint64_t a)
{
    uint32_t exp, mant_sig;
    CPU_FloatU r;

    exp = ((a >> 55) & 0x80) | ((a >> 52) & 0x7f);
    mant_sig = ((a >> 32) & 0x80000000) | ((a >> 29) & 0x007fffff);

    if (unlikely(!exp && mant_sig)) {
        /* Reserved operands / Dirty zero */
        dynamic_excp(EXCP_OPCDEC, 0);
    }

    if (exp < 3) {
        /* Underflow */
        r.l = 0;
    } else {
        r.l = ((exp - 2) << 23) | mant_sig;
    }

    return r.f;
}

uint32_t helper_f_to_memory (uint64_t a)
{
    uint32_t r;
    r =  (a & 0x00001fffe0000000ull) >> 13;
    r |= (a & 0x07ffe00000000000ull) >> 45;
    r |= (a & 0xc000000000000000ull) >> 48;
    return r;
}

uint64_t helper_memory_to_f (uint32_t a)
{
    uint64_t r;
    r =  ((uint64_t)(a & 0x0000c000)) << 48;
    r |= ((uint64_t)(a & 0x003fffff)) << 45;
    r |= ((uint64_t)(a & 0xffff0000)) << 13;
    if (!(a & 0x00004000))
        r |= 0x7ll << 59;
    return r;
}

/* ??? Emulating VAX arithmetic with IEEE arithmetic is wrong.  We should
   either implement VAX arithmetic properly or just signal invalid opcode.  */

uint64_t helper_addf (uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = f_to_float32(a);
    fb = f_to_float32(b);
    fr = float32_add(fa, fb, &FP_STATUS);
    return float32_to_f(fr);
}

uint64_t helper_subf (uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = f_to_float32(a);
    fb = f_to_float32(b);
    fr = float32_sub(fa, fb, &FP_STATUS);
    return float32_to_f(fr);
}

uint64_t helper_mulf (uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = f_to_float32(a);
    fb = f_to_float32(b);
    fr = float32_mul(fa, fb, &FP_STATUS);
    return float32_to_f(fr);
}

uint64_t helper_divf (uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = f_to_float32(a);
    fb = f_to_float32(b);
    fr = float32_div(fa, fb, &FP_STATUS);
    return float32_to_f(fr);
}

uint64_t helper_sqrtf (uint64_t t)
{
    float32 ft, fr;

    ft = f_to_float32(t);
    fr = float32_sqrt(ft, &FP_STATUS);
    return float32_to_f(fr);
}


/* G floating (VAX) */
static inline uint64_t float64_to_g(float64 fa)
{
    uint64_t r, exp, mant, sig;
    CPU_DoubleU a;

    a.d = fa;
    sig = a.ll & 0x8000000000000000ull;
    exp = (a.ll >> 52) & 0x7ff;
    mant = a.ll & 0x000fffffffffffffull;

    if (exp == 2047) {
        /* NaN or infinity */
        r = 1; /* VAX dirty zero */
    } else if (exp == 0) {
        if (mant == 0) {
            /* Zero */
            r = 0;
        } else {
            /* Denormalized */
            r = sig | ((exp + 1) << 52) | mant;
        }
    } else {
        if (exp >= 2045) {
            /* Overflow */
            r = 1; /* VAX dirty zero */
        } else {
            r = sig | ((exp + 2) << 52);
        }
    }

    return r;
}

static inline float64 g_to_float64(uint64_t a)
{
    uint64_t exp, mant_sig;
    CPU_DoubleU r;

    exp = (a >> 52) & 0x7ff;
    mant_sig = a & 0x800fffffffffffffull;

    if (!exp && mant_sig) {
        /* Reserved operands / Dirty zero */
        dynamic_excp(EXCP_OPCDEC, 0);
    }

    if (exp < 3) {
        /* Underflow */
        r.ll = 0;
    } else {
        r.ll = ((exp - 2) << 52) | mant_sig;
    }

    return r.d;
}

uint64_t helper_g_to_memory (uint64_t a)
{
    uint64_t r;
    r =  (a & 0x000000000000ffffull) << 48;
    r |= (a & 0x00000000ffff0000ull) << 16;
    r |= (a & 0x0000ffff00000000ull) >> 16;
    r |= (a & 0xffff000000000000ull) >> 48;
    return r;
}

uint64_t helper_memory_to_g (uint64_t a)
{
    uint64_t r;
    r =  (a & 0x000000000000ffffull) << 48;
    r |= (a & 0x00000000ffff0000ull) << 16;
    r |= (a & 0x0000ffff00000000ull) >> 16;
    r |= (a & 0xffff000000000000ull) >> 48;
    return r;
}

uint64_t helper_addg (uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = g_to_float64(a);
    fb = g_to_float64(b);
    fr = float64_add(fa, fb, &FP_STATUS);
    return float64_to_g(fr);
}

uint64_t helper_subg (uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = g_to_float64(a);
    fb = g_to_float64(b);
    fr = float64_sub(fa, fb, &FP_STATUS);
    return float64_to_g(fr);
}

uint64_t helper_mulg (uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = g_to_float64(a);
    fb = g_to_float64(b);
    fr = float64_mul(fa, fb, &FP_STATUS);
    return float64_to_g(fr);
}

uint64_t helper_divg (uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = g_to_float64(a);
    fb = g_to_float64(b);
    fr = float64_div(fa, fb, &FP_STATUS);
    return float64_to_g(fr);
}

uint64_t helper_sqrtg (uint64_t a)
{
    float64 fa, fr;

    fa = g_to_float64(a);
    fr = float64_sqrt(fa, &FP_STATUS);
    return float64_to_g(fr);
}


/* S floating (single) */

/* Taken from linux/arch/alpha/kernel/traps.c, s_mem_to_reg.  */
static inline uint64_t float32_to_s_int(uint32_t fi)
{
    uint32_t frac = fi & 0x7fffff;
    uint32_t sign = fi >> 31;
    uint32_t exp_msb = (fi >> 30) & 1;
    uint32_t exp_low = (fi >> 23) & 0x7f;
    uint32_t exp;

    exp = (exp_msb << 10) | exp_low;
    if (exp_msb) {
        if (exp_low == 0x7f)
            exp = 0x7ff;
    } else {
        if (exp_low != 0x00)
            exp |= 0x380;
    }

    return (((uint64_t)sign << 63)
            | ((uint64_t)exp << 52)
            | ((uint64_t)frac << 29));
}

static inline uint64_t float32_to_s(float32 fa)
{
    CPU_FloatU a;
    a.f = fa;
    return float32_to_s_int(a.l);
}

static inline uint32_t s_to_float32_int(uint64_t a)
{
    return ((a >> 32) & 0xc0000000) | ((a >> 29) & 0x3fffffff);
}

static inline float32 s_to_float32(uint64_t a)
{
    CPU_FloatU r;
    r.l = s_to_float32_int(a);
    return r.f;
}

uint32_t helper_s_to_memory (uint64_t a)
{
    return s_to_float32_int(a);
}

uint64_t helper_memory_to_s (uint32_t a)
{
    return float32_to_s_int(a);
}

uint64_t helper_adds (uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = s_to_float32(a);
    fb = s_to_float32(b);
    fr = float32_add(fa, fb, &FP_STATUS);
    return float32_to_s(fr);
}

uint64_t helper_subs (uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = s_to_float32(a);
    fb = s_to_float32(b);
    fr = float32_sub(fa, fb, &FP_STATUS);
    return float32_to_s(fr);
}

uint64_t helper_muls (uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = s_to_float32(a);
    fb = s_to_float32(b);
    fr = float32_mul(fa, fb, &FP_STATUS);
    return float32_to_s(fr);
}

uint64_t helper_divs (uint64_t a, uint64_t b)
{
    float32 fa, fb, fr;

    fa = s_to_float32(a);
    fb = s_to_float32(b);
    fr = float32_div(fa, fb, &FP_STATUS);
    return float32_to_s(fr);
}

uint64_t helper_sqrts (uint64_t a)
{
    float32 fa, fr;

    fa = s_to_float32(a);
    fr = float32_sqrt(fa, &FP_STATUS);
    return float32_to_s(fr);
}


/* T floating (double) */
static inline float64 t_to_float64(uint64_t a)
{
    /* Memory format is the same as float64 */
    CPU_DoubleU r;
    r.ll = a;
    return r.d;
}

static inline uint64_t float64_to_t(float64 fa)
{
    /* Memory format is the same as float64 */
    CPU_DoubleU r;
    r.d = fa;
    return r.ll;
}

uint64_t helper_addt (uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = t_to_float64(a);
    fb = t_to_float64(b);
    fr = float64_add(fa, fb, &FP_STATUS);
    return float64_to_t(fr);
}

uint64_t helper_subt (uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = t_to_float64(a);
    fb = t_to_float64(b);
    fr = float64_sub(fa, fb, &FP_STATUS);
    return float64_to_t(fr);
}

uint64_t helper_mult (uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = t_to_float64(a);
    fb = t_to_float64(b);
    fr = float64_mul(fa, fb, &FP_STATUS);
    return float64_to_t(fr);
}

uint64_t helper_divt (uint64_t a, uint64_t b)
{
    float64 fa, fb, fr;

    fa = t_to_float64(a);
    fb = t_to_float64(b);
    fr = float64_div(fa, fb, &FP_STATUS);
    return float64_to_t(fr);
}

uint64_t helper_sqrtt (uint64_t a)
{
    float64 fa, fr;

    fa = t_to_float64(a);
    fr = float64_sqrt(fa, &FP_STATUS);
    return float64_to_t(fr);
}

/* Comparisons */
uint64_t helper_cmptun (uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = t_to_float64(a);
    fb = t_to_float64(b);

    if (float64_unordered_quiet(fa, fb, &FP_STATUS)) {
        return 0x4000000000000000ULL;
    } else {
        return 0;
    }
}

uint64_t helper_cmpteq(uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = t_to_float64(a);
    fb = t_to_float64(b);

    if (float64_eq_quiet(fa, fb, &FP_STATUS))
        return 0x4000000000000000ULL;
    else
        return 0;
}

uint64_t helper_cmptle(uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = t_to_float64(a);
    fb = t_to_float64(b);

    if (float64_le(fa, fb, &FP_STATUS))
        return 0x4000000000000000ULL;
    else
        return 0;
}

uint64_t helper_cmptlt(uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = t_to_float64(a);
    fb = t_to_float64(b);

    if (float64_lt(fa, fb, &FP_STATUS))
        return 0x4000000000000000ULL;
    else
        return 0;
}

uint64_t helper_cmpgeq(uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = g_to_float64(a);
    fb = g_to_float64(b);

    if (float64_eq_quiet(fa, fb, &FP_STATUS))
        return 0x4000000000000000ULL;
    else
        return 0;
}

uint64_t helper_cmpgle(uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = g_to_float64(a);
    fb = g_to_float64(b);

    if (float64_le(fa, fb, &FP_STATUS))
        return 0x4000000000000000ULL;
    else
        return 0;
}

uint64_t helper_cmpglt(uint64_t a, uint64_t b)
{
    float64 fa, fb;

    fa = g_to_float64(a);
    fb = g_to_float64(b);

    if (float64_lt(fa, fb, &FP_STATUS))
        return 0x4000000000000000ULL;
    else
        return 0;
}

/* Floating point format conversion */
uint64_t helper_cvtts (uint64_t a)
{
    float64 fa;
    float32 fr;

    fa = t_to_float64(a);
    fr = float64_to_float32(fa, &FP_STATUS);
    return float32_to_s(fr);
}

uint64_t helper_cvtst (uint64_t a)
{
    float32 fa;
    float64 fr;

    fa = s_to_float32(a);
    fr = float32_to_float64(fa, &FP_STATUS);
    return float64_to_t(fr);
}

uint64_t helper_cvtqs (uint64_t a)
{
    float32 fr = int64_to_float32(a, &FP_STATUS);
    return float32_to_s(fr);
}

/* Implement float64 to uint64 conversion without saturation -- we must
   supply the truncated result.  This behaviour is used by the compiler
   to get unsigned conversion for free with the same instruction.

   The VI flag is set when overflow or inexact exceptions should be raised.  */

static inline uint64_t helper_cvttq_internal(uint64_t a, int roundmode, int VI)
{
    uint64_t frac, ret = 0;
    uint32_t exp, sign, exc = 0;
    int shift;

    sign = (a >> 63);
    exp = (uint32_t)(a >> 52) & 0x7ff;
    frac = a & 0xfffffffffffffull;

    if (exp == 0) {
        if (unlikely(frac != 0)) {
            goto do_underflow;
        }
    } else if (exp == 0x7ff) {
        exc = (frac ? float_flag_invalid : VI ? float_flag_overflow : 0);
    } else {
        /* Restore implicit bit.  */
        frac |= 0x10000000000000ull;

        shift = exp - 1023 - 52;
        if (shift >= 0) {
            /* In this case the number is so large that we must shift
               the fraction left.  There is no rounding to do.  */
            if (shift < 63) {
                ret = frac << shift;
                if (VI && (ret >> shift) != frac) {
                    exc = float_flag_overflow;
                }
            }
        } else {
            uint64_t round;

            /* In this case the number is smaller than the fraction as
               represented by the 52 bit number.  Here we must think
               about rounding the result.  Handle this by shifting the
               fractional part of the number into the high bits of ROUND.
               This will let us efficiently handle round-to-nearest.  */
            shift = -shift;
            if (shift < 63) {
                ret = frac >> shift;
                round = frac << (64 - shift);
            } else {
                /* The exponent is so small we shift out everything.
                   Leave a sticky bit for proper rounding below.  */
            do_underflow:
                round = 1;
            }

            if (round) {
                exc = (VI ? float_flag_inexact : 0);
                switch (roundmode) {
                case float_round_nearest_even:
                    if (round == (1ull << 63)) {
                        /* Fraction is exactly 0.5; round to even.  */
                        ret += (ret & 1);
                    } else if (round > (1ull << 63)) {
                        ret += 1;
                    }
                    break;
                case float_round_to_zero:
                    break;
                case float_round_up:
                    ret += 1 - sign;
                    break;
                case float_round_down:
                    ret += sign;
                    break;
                }
            }
        }
        if (sign) {
            ret = -ret;
        }
    }
    if (unlikely(exc)) {
        float_raise(exc, &FP_STATUS);
    }

    return ret;
}

uint64_t helper_cvttq(uint64_t a)
{
    return helper_cvttq_internal(a, FP_STATUS.float_rounding_mode, 1);
}

uint64_t helper_cvttq_c(uint64_t a)
{
    return helper_cvttq_internal(a, float_round_to_zero, 0);
}

uint64_t helper_cvttq_svic(uint64_t a)
{
    return helper_cvttq_internal(a, float_round_to_zero, 1);
}

uint64_t helper_cvtqt (uint64_t a)
{
    float64 fr = int64_to_float64(a, &FP_STATUS);
    return float64_to_t(fr);
}

uint64_t helper_cvtqf (uint64_t a)
{
    float32 fr = int64_to_float32(a, &FP_STATUS);
    return float32_to_f(fr);
}

uint64_t helper_cvtgf (uint64_t a)
{
    float64 fa;
    float32 fr;

    fa = g_to_float64(a);
    fr = float64_to_float32(fa, &FP_STATUS);
    return float32_to_f(fr);
}

uint64_t helper_cvtgq (uint64_t a)
{
    float64 fa = g_to_float64(a);
    return float64_to_int64_round_to_zero(fa, &FP_STATUS);
}

uint64_t helper_cvtqg (uint64_t a)
{
    float64 fr;
    fr = int64_to_float64(a, &FP_STATUS);
    return float64_to_g(fr);
}

/* PALcode support special instructions */
#if !defined (CONFIG_USER_ONLY)
void helper_hw_ret (uint64_t a)
{
    env->pc = a & ~3;
    env->intr_flag = 0;
    env->lock_addr = -1;
    if ((a & 1) == 0) {
        env->pal_mode = 0;
        swap_shadow_regs(env);
    }
}

void helper_tbia(void)
{
    tlb_flush(env, 1);
}

void helper_tbis(uint64_t p)
{
    tlb_flush_page(env, p);
}
#endif

/*****************************************************************************/
/* Softmmu support */
#if !defined (CONFIG_USER_ONLY)
uint64_t helper_ldl_phys(uint64_t p)
{
    return (int32_t)ldl_phys(p);
}

uint64_t helper_ldq_phys(uint64_t p)
{
    return ldq_phys(p);
}

uint64_t helper_ldl_l_phys(uint64_t p)
{
    env->lock_addr = p;
    return env->lock_value = (int32_t)ldl_phys(p);
}

uint64_t helper_ldq_l_phys(uint64_t p)
{
    env->lock_addr = p;
    return env->lock_value = ldl_phys(p);
}

void helper_stl_phys(uint64_t p, uint64_t v)
{
    stl_phys(p, v);
}

void helper_stq_phys(uint64_t p, uint64_t v)
{
    stq_phys(p, v);
}

uint64_t helper_stl_c_phys(uint64_t p, uint64_t v)
{
    uint64_t ret = 0;

    if (p == env->lock_addr) {
        int32_t old = ldl_phys(p);
        if (old == (int32_t)env->lock_value) {
            stl_phys(p, v);
            ret = 1;
        }
    }
    env->lock_addr = -1;

    return ret;
}

uint64_t helper_stq_c_phys(uint64_t p, uint64_t v)
{
    uint64_t ret = 0;

    if (p == env->lock_addr) {
        uint64_t old = ldq_phys(p);
        if (old == env->lock_value) {
            stq_phys(p, v);
            ret = 1;
        }
    }
    env->lock_addr = -1;

    return ret;
}

static void QEMU_NORETURN do_unaligned_access(target_ulong addr, int is_write,
                                              int is_user, void *retaddr)
{
    uint64_t pc;
    uint32_t insn;

    do_restore_state(retaddr);

    pc = env->pc;
    insn = ldl_code(pc);

    env->trap_arg0 = addr;
    env->trap_arg1 = insn >> 26;                /* opcode */
    env->trap_arg2 = (insn >> 21) & 31;         /* dest regno */
    helper_excp(EXCP_UNALIGN, 0);
}

void QEMU_NORETURN do_unassigned_access(target_phys_addr_t addr, int is_write,
                                        int is_exec, int unused, int size)
{
    env->trap_arg0 = addr;
    env->trap_arg1 = is_write;
    dynamic_excp(EXCP_MCHK, 0);
}

#define MMUSUFFIX _mmu
#define ALIGNED_ONLY

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

/* try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
/* XXX: fix it to restore all registers */
void tlb_fill (target_ulong addr, int is_write, int mmu_idx, void *retaddr)
{
    CPUState *saved_env;
    int ret;

    /* XXX: hack to restore env in all cases, even if not called from
       generated code */
    saved_env = env;
    env = cpu_single_env;
    ret = cpu_alpha_handle_mmu_fault(env, addr, is_write, mmu_idx, 1);
    if (unlikely(ret != 0)) {
        do_restore_state(retaddr);
        /* Exception index and error code are already set */
        cpu_loop_exit(env);
    }
    env = saved_env;
}
#endif
