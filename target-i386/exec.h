/*
 *  i386 execution defines
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
#include "config.h"
#include "dyngen-exec.h"

/* XXX: factorize this mess */
#ifdef TARGET_X86_64
#define TARGET_LONG_BITS 64
#else
#define TARGET_LONG_BITS 32
#endif

#include "cpu-defs.h"

register struct CPUX86State *env asm(AREG0);

#include "qemu-common.h"
#include "qemu-log.h"

#undef EAX
#define EAX (env->regs[R_EAX])
#undef ECX
#define ECX (env->regs[R_ECX])
#undef EDX
#define EDX (env->regs[R_EDX])
#undef EBX
#define EBX (env->regs[R_EBX])
#undef ESP
#define ESP (env->regs[R_ESP])
#undef EBP
#define EBP (env->regs[R_EBP])
#undef ESI
#define ESI (env->regs[R_ESI])
#undef EDI
#define EDI (env->regs[R_EDI])
#undef EIP
#define EIP (env->eip)
#define DF  (env->df)

#define CC_SRC (env->cc_src)
#define CC_DST (env->cc_dst)
#define CC_OP  (env->cc_op)

/* float macros */
#define FT0    (env->ft0)
#define ST0    (env->fpregs[env->fpstt].d)
#define ST(n)  (env->fpregs[(env->fpstt + (n)) & 7].d)
#define ST1    ST(1)

#include "cpu.h"
#include "exec-all.h"

/* op_helper.c */
void do_interrupt(int intno, int is_int, int error_code,
                  target_ulong next_eip, int is_hw);
void do_interrupt_user(int intno, int is_int, int error_code,
                       target_ulong next_eip);
void QEMU_NORETURN raise_exception_err(int exception_index, int error_code);
void QEMU_NORETURN raise_exception(int exception_index);
void QEMU_NORETURN raise_exception_env(int exception_index, CPUState *nenv);
void do_smm_enter(void);

/* n must be a constant to be efficient */
static inline target_long lshift(target_long x, int n)
{
    if (n >= 0)
        return x << n;
    else
        return x >> (-n);
}

#include "helper.h"

static inline void svm_check_intercept(uint32_t type)
{
    helper_svm_check_intercept_param(type, 0);
}

#if !defined(CONFIG_USER_ONLY)

#include "softmmu_exec.h"

#endif /* !defined(CONFIG_USER_ONLY) */

#ifdef USE_X86LDOUBLE
/* use long double functions */
#define floatx_to_int32 floatx80_to_int32
#define floatx_to_int64 floatx80_to_int64
#define floatx_to_int32_round_to_zero floatx80_to_int32_round_to_zero
#define floatx_to_int64_round_to_zero floatx80_to_int64_round_to_zero
#define int32_to_floatx int32_to_floatx80
#define int64_to_floatx int64_to_floatx80
#define float32_to_floatx float32_to_floatx80
#define float64_to_floatx float64_to_floatx80
#define floatx_to_float32 floatx80_to_float32
#define floatx_to_float64 floatx80_to_float64
#define floatx_add floatx80_add
#define floatx_div floatx80_div
#define floatx_mul floatx80_mul
#define floatx_sub floatx80_sub
#define floatx_abs floatx80_abs
#define floatx_chs floatx80_chs
#define floatx_scalbn floatx80_scalbn
#define floatx_round_to_int floatx80_round_to_int
#define floatx_compare floatx80_compare
#define floatx_compare_quiet floatx80_compare_quiet
#define floatx_is_any_nan floatx80_is_any_nan
#define floatx_is_zero floatx80_is_zero
#else
#define floatx_to_int32 float64_to_int32
#define floatx_to_int64 float64_to_int64
#define floatx_to_int32_round_to_zero float64_to_int32_round_to_zero
#define floatx_to_int64_round_to_zero float64_to_int64_round_to_zero
#define int32_to_floatx int32_to_float64
#define int64_to_floatx int64_to_float64
#define float32_to_floatx float32_to_float64
#define float64_to_floatx(x, e) (x)
#define floatx_to_float32 float64_to_float32
#define floatx_to_float64(x, e) (x)
#define floatx_add float64_add
#define floatx_div float64_div
#define floatx_mul float64_mul
#define floatx_sub float64_sub
#define floatx_abs float64_abs
#define floatx_chs float64_chs
#define floatx_scalbn float64_scalbn
#define floatx_round_to_int float64_round_to_int
#define floatx_compare float64_compare
#define floatx_compare_quiet float64_compare_quiet
#define floatx_is_any_nan float64_is_any_nan
#define floatx_is_zero float64_is_zero
#endif

#define RC_MASK         0xc00
#define RC_NEAR		0x000
#define RC_DOWN		0x400
#define RC_UP		0x800
#define RC_CHOP		0xc00

#define MAXTAN 9223372036854775808.0

#ifdef USE_X86LDOUBLE

/* only for x86 */
typedef CPU_LDoubleU CPU86_LDoubleU;

/* the following deal with x86 long double-precision numbers */
#define MAXEXPD 0x7fff
#define EXPBIAS 16383
#define EXPD(fp)	(fp.l.upper & 0x7fff)
#define SIGND(fp)	((fp.l.upper) & 0x8000)
#define MANTD(fp)       (fp.l.lower)
#define BIASEXPONENT(fp) fp.l.upper = (fp.l.upper & ~(0x7fff)) | EXPBIAS

#else

typedef CPU_DoubleU CPU86_LDoubleU;

/* the following deal with IEEE double-precision numbers */
#define MAXEXPD 0x7ff
#define EXPBIAS 1023
#define EXPD(fp)	(((fp.l.upper) >> 20) & 0x7FF)
#define SIGND(fp)	((fp.l.upper) & 0x80000000)
#ifdef __arm__
#define MANTD(fp)	(fp.l.lower | ((uint64_t)(fp.l.upper & ((1 << 20) - 1)) << 32))
#else
#define MANTD(fp)	(fp.ll & ((1LL << 52) - 1))
#endif
#define BIASEXPONENT(fp) fp.l.upper = (fp.l.upper & ~(0x7ff << 20)) | (EXPBIAS << 20)
#endif

static inline void fpush(void)
{
    env->fpstt = (env->fpstt - 1) & 7;
    env->fptags[env->fpstt] = 0; /* validate stack entry */
}

static inline void fpop(void)
{
    env->fptags[env->fpstt] = 1; /* invvalidate stack entry */
    env->fpstt = (env->fpstt + 1) & 7;
}

#ifndef USE_X86LDOUBLE
static inline CPU86_LDouble helper_fldt(target_ulong ptr)
{
    CPU86_LDoubleU temp;
    int upper, e;
    uint64_t ll;

    /* mantissa */
    upper = lduw(ptr + 8);
    /* XXX: handle overflow ? */
    e = (upper & 0x7fff) - 16383 + EXPBIAS; /* exponent */
    e |= (upper >> 4) & 0x800; /* sign */
    ll = (ldq(ptr) >> 11) & ((1LL << 52) - 1);
#ifdef __arm__
    temp.l.upper = (e << 20) | (ll >> 32);
    temp.l.lower = ll;
#else
    temp.ll = ll | ((uint64_t)e << 52);
#endif
    return temp.d;
}

static inline void helper_fstt(CPU86_LDouble f, target_ulong ptr)
{
    CPU86_LDoubleU temp;
    int e;

    temp.d = f;
    /* mantissa */
    stq(ptr, (MANTD(temp) << 11) | (1LL << 63));
    /* exponent + sign */
    e = EXPD(temp) - EXPBIAS + 16383;
    e |= SIGND(temp) >> 16;
    stw(ptr + 8, e);
}
#else

/* we use memory access macros */

static inline CPU86_LDouble helper_fldt(target_ulong ptr)
{
    CPU86_LDoubleU temp;

    temp.l.lower = ldq(ptr);
    temp.l.upper = lduw(ptr + 8);
    return temp.d;
}

static inline void helper_fstt(CPU86_LDouble f, target_ulong ptr)
{
    CPU86_LDoubleU temp;

    temp.d = f;
    stq(ptr, temp.l.lower);
    stw(ptr + 8, temp.l.upper);
}

#endif /* USE_X86LDOUBLE */

#define FPUS_IE (1 << 0)
#define FPUS_DE (1 << 1)
#define FPUS_ZE (1 << 2)
#define FPUS_OE (1 << 3)
#define FPUS_UE (1 << 4)
#define FPUS_PE (1 << 5)
#define FPUS_SF (1 << 6)
#define FPUS_SE (1 << 7)
#define FPUS_B  (1 << 15)

#define FPUC_EM 0x3f

static inline uint32_t compute_eflags(void)
{
    return env->eflags | helper_cc_compute_all(CC_OP) | (DF & DF_MASK);
}

/* NOTE: CC_OP must be modified manually to CC_OP_EFLAGS */
static inline void load_eflags(int eflags, int update_mask)
{
    CC_SRC = eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    DF = 1 - (2 * ((eflags >> 10) & 1));
    env->eflags = (env->eflags & ~update_mask) |
        (eflags & update_mask) | 0x2;
}

static inline int cpu_has_work(CPUState *env)
{
    return ((env->interrupt_request & CPU_INTERRUPT_HARD) &&
            (env->eflags & IF_MASK)) ||
           (env->interrupt_request & (CPU_INTERRUPT_NMI |
                                      CPU_INTERRUPT_INIT |
                                      CPU_INTERRUPT_SIPI |
                                      CPU_INTERRUPT_MCE));
}

/* load efer and update the corresponding hflags. XXX: do consistency
   checks with cpuid bits ? */
static inline void cpu_load_efer(CPUState *env, uint64_t val)
{
    env->efer = val;
    env->hflags &= ~(HF_LMA_MASK | HF_SVME_MASK);
    if (env->efer & MSR_EFER_LMA)
        env->hflags |= HF_LMA_MASK;
    if (env->efer & MSR_EFER_SVME)
        env->hflags |= HF_SVME_MASK;
}

static inline void cpu_pc_from_tb(CPUState *env, TranslationBlock *tb)
{
    env->eip = tb->pc - tb->cs_base;
}

