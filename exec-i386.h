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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "dyngen-exec.h"

/* at least 4 register variables are defines */
register struct CPUX86State *env asm(AREG0);
register uint32_t T0 asm(AREG1);
register uint32_t T1 asm(AREG2);
register uint32_t T2 asm(AREG3);

#define A0 T2

/* if more registers are available, we define some registers too */
#ifdef AREG4
register uint32_t EAX asm(AREG4);
#define reg_EAX
#endif

#ifdef AREG5
register uint32_t ESP asm(AREG5);
#define reg_ESP
#endif

#ifdef AREG6
register uint32_t EBP asm(AREG6);
#define reg_EBP
#endif

#ifdef AREG7
register uint32_t ECX asm(AREG7);
#define reg_ECX
#endif

#ifdef AREG8
register uint32_t EDX asm(AREG8);
#define reg_EDX
#endif

#ifdef AREG9
register uint32_t EBX asm(AREG9);
#define reg_EBX
#endif

#ifdef AREG10
register uint32_t ESI asm(AREG10);
#define reg_ESI
#endif

#ifdef AREG11
register uint32_t EDI asm(AREG11);
#define reg_EDI
#endif

extern FILE *logfile;
extern int loglevel;

#ifndef reg_EAX
#define EAX (env->regs[R_EAX])
#endif
#ifndef reg_ECX
#define ECX (env->regs[R_ECX])
#endif
#ifndef reg_EDX
#define EDX (env->regs[R_EDX])
#endif
#ifndef reg_EBX
#define EBX (env->regs[R_EBX])
#endif
#ifndef reg_ESP
#define ESP (env->regs[R_ESP])
#endif
#ifndef reg_EBP
#define EBP (env->regs[R_EBP])
#endif
#ifndef reg_ESI
#define ESI (env->regs[R_ESI])
#endif
#ifndef reg_EDI
#define EDI (env->regs[R_EDI])
#endif
#define EIP  (env->eip)
#define DF  (env->df)

#define CC_SRC (env->cc_src)
#define CC_DST (env->cc_dst)
#define CC_OP  (env->cc_op)

/* float macros */
#define FT0    (env->ft0)
#define ST0    (env->fpregs[env->fpstt])
#define ST(n)  (env->fpregs[(env->fpstt + (n)) & 7])
#define ST1    ST(1)

#ifdef USE_FP_CONVERT
#define FP_CONVERT  (env->fp_convert)
#endif

#include "cpu-i386.h"
#include "exec.h"

typedef struct CCTable {
    int (*compute_all)(void); /* return all the flags */
    int (*compute_c)(void);  /* return the C flag */
} CCTable;

extern CCTable cc_table[];

void load_seg(int seg_reg, int selector, unsigned cur_eip);
void __hidden cpu_lock(void);
void __hidden cpu_unlock(void);
void raise_interrupt(int intno, int is_int, int error_code, 
                     unsigned int next_eip);
void raise_exception_err(int exception_index, int error_code);
void raise_exception(int exception_index);
void __hidden cpu_loop_exit(void);
void helper_fsave(uint8_t *ptr, int data32);
void helper_frstor(uint8_t *ptr, int data32);

void OPPROTO op_movl_eflags_T0(void);
void OPPROTO op_movl_T0_eflags(void);
void raise_interrupt(int intno, int is_int, int error_code, 
                     unsigned int next_eip);
void raise_exception_err(int exception_index, int error_code);
void raise_exception(int exception_index);
void helper_divl_EAX_T0(uint32_t eip);
void helper_idivl_EAX_T0(uint32_t eip);
void helper_cmpxchg8b(void);
void helper_cpuid(void);
void helper_rdtsc(void);
void helper_lsl(void);
void helper_lar(void);

#ifdef USE_X86LDOUBLE
/* use long double functions */
#define lrint lrintl
#define llrint llrintl
#define fabs fabsl
#define sin sinl
#define cos cosl
#define sqrt sqrtl
#define pow powl
#define log logl
#define tan tanl
#define atan2 atan2l
#define floor floorl
#define ceil ceill
#define rint rintl
#endif

extern int lrint(CPU86_LDouble x);
extern int64_t llrint(CPU86_LDouble x);
extern CPU86_LDouble fabs(CPU86_LDouble x);
extern CPU86_LDouble sin(CPU86_LDouble x);
extern CPU86_LDouble cos(CPU86_LDouble x);
extern CPU86_LDouble sqrt(CPU86_LDouble x);
extern CPU86_LDouble pow(CPU86_LDouble, CPU86_LDouble);
extern CPU86_LDouble log(CPU86_LDouble x);
extern CPU86_LDouble tan(CPU86_LDouble x);
extern CPU86_LDouble atan2(CPU86_LDouble, CPU86_LDouble);
extern CPU86_LDouble floor(CPU86_LDouble x);
extern CPU86_LDouble ceil(CPU86_LDouble x);
extern CPU86_LDouble rint(CPU86_LDouble x);

#define RC_MASK         0xc00
#define RC_NEAR		0x000
#define RC_DOWN		0x400
#define RC_UP		0x800
#define RC_CHOP		0xc00

#define MAXTAN 9223372036854775808.0

#ifdef __arm__
/* we have no way to do correct rounding - a FPU emulator is needed */
#define FE_DOWNWARD   FE_TONEAREST
#define FE_UPWARD     FE_TONEAREST
#define FE_TOWARDZERO FE_TONEAREST
#endif

#ifdef USE_X86LDOUBLE

/* only for x86 */
typedef union {
    long double d;
    struct {
        unsigned long long lower;
        unsigned short upper;
    } l;
} CPU86_LDoubleU;

/* the following deal with x86 long double-precision numbers */
#define MAXEXPD 0x7fff
#define EXPBIAS 16383
#define EXPD(fp)	(fp.l.upper & 0x7fff)
#define SIGND(fp)	((fp.l.upper) & 0x8000)
#define MANTD(fp)       (fp.l.lower)
#define BIASEXPONENT(fp) fp.l.upper = (fp.l.upper & ~(0x7fff)) | EXPBIAS

#else

/* NOTE: arm is horrible as double 32 bit words are stored in big endian ! */
typedef union {
    double d;
#if !defined(WORDS_BIGENDIAN) && !defined(__arm__)
    struct {
        uint32_t lower;
        int32_t upper;
    } l;
#else
    struct {
        int32_t upper;
        uint32_t lower;
    } l;
#endif
#ifndef __arm__
    int64_t ll;
#endif
} CPU86_LDoubleU;

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
static inline CPU86_LDouble helper_fldt(uint8_t *ptr)
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

static inline void helper_fstt(CPU86_LDouble f, uint8_t *ptr)
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
#endif

const CPU86_LDouble f15rk[7];

void helper_fldt_ST0_A0(void);
void helper_fstt_ST0_A0(void);
void helper_fbld_ST0_A0(void);
void helper_fbst_ST0_A0(void);
void helper_f2xm1(void);
void helper_fyl2x(void);
void helper_fptan(void);
void helper_fpatan(void);
void helper_fxtract(void);
void helper_fprem1(void);
void helper_fprem(void);
void helper_fyl2xp1(void);
void helper_fsqrt(void);
void helper_fsincos(void);
void helper_frndint(void);
void helper_fscale(void);
void helper_fsin(void);
void helper_fcos(void);
void helper_fxam_ST0(void);
void helper_fstenv(uint8_t *ptr, int data32);
void helper_fldenv(uint8_t *ptr, int data32);
void helper_fsave(uint8_t *ptr, int data32);
void helper_frstor(uint8_t *ptr, int data32);

const uint8_t parity_table[256];
const uint8_t rclw_table[32];
const uint8_t rclb_table[32];
