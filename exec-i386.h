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
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long long int64_t;

#define bswap32(x) \
({ \
	uint32_t __x = (x); \
	((uint32_t)( \
		(((uint32_t)(__x) & (uint32_t)0x000000ffUL) << 24) | \
		(((uint32_t)(__x) & (uint32_t)0x0000ff00UL) <<  8) | \
		(((uint32_t)(__x) & (uint32_t)0x00ff0000UL) >>  8) | \
		(((uint32_t)(__x) & (uint32_t)0xff000000UL) >> 24) )); \
})

#define NULL 0
#include <fenv.h>

typedef struct FILE FILE;
extern FILE *logfile;
extern int loglevel;
extern int fprintf(FILE *, const char *, ...);
extern int printf(const char *, ...);

#ifdef __i386__
register unsigned int T0 asm("ebx");
register unsigned int T1 asm("esi");
register unsigned int A0 asm("edi");
register struct CPUX86State *env asm("ebp");
#endif
#ifdef __powerpc__
register unsigned int EAX asm("r16");
register unsigned int ECX asm("r17");
register unsigned int EDX asm("r18");
register unsigned int EBX asm("r19");
register unsigned int ESP asm("r20");
register unsigned int EBP asm("r21");
register unsigned int ESI asm("r22");
register unsigned int EDI asm("r23");
register unsigned int T0 asm("r24");
register unsigned int T1 asm("r25");
register unsigned int A0 asm("r26");
register struct CPUX86State *env asm("r27");
#define USE_INT_TO_FLOAT_HELPERS
#define BUGGY_GCC_DIV64
#define reg_EAX
#define reg_ECX
#define reg_EDX
#define reg_EBX
#define reg_ESP
#define reg_EBP
#define reg_ESI
#define reg_EDI
#endif
#ifdef __arm__
register unsigned int T0 asm("r4");
register unsigned int T1 asm("r5");
register unsigned int A0 asm("r6");
register struct CPUX86State *env asm("r7");
#endif
#ifdef __mips__
register unsigned int T0 asm("s0");
register unsigned int T1 asm("s1");
register unsigned int A0 asm("s2");
register struct CPUX86State *env asm("s3");
#endif
#ifdef __sparc__
register unsigned int EAX asm("l0");
register unsigned int ECX asm("l1");
register unsigned int EDX asm("l2");
register unsigned int EBX asm("l3");
register unsigned int ESP asm("l4");
register unsigned int EBP asm("l5");
register unsigned int ESI asm("l6");
register unsigned int EDI asm("l7");
register unsigned int T0 asm("g1");
register unsigned int T1 asm("g2");
register unsigned int A0 asm("g3");
register struct CPUX86State *env asm("g6");
#define USE_FP_CONVERT
#define reg_EAX
#define reg_ECX
#define reg_EDX
#define reg_EBX
#define reg_ESP
#define reg_EBP
#define reg_ESI
#define reg_EDI
#endif
#ifdef __s390__
register unsigned int T0 asm("r7");
register unsigned int T1 asm("r8");
register unsigned int A0 asm("r9");
register struct CPUX86State *env asm("r10");
#endif
#ifdef __alpha__
register unsigned int T0 asm("$9");
register unsigned int T1 asm("$10");
register unsigned int A0 asm("$11");
register unsigned int EAX asm("$12");
register unsigned int ESP asm("$13");
register unsigned int EBP asm("$14");
/* Note $15 is the frame pointer, so anything in op-i386.c that would
   require a frame pointer, like alloca, would probably loose.  */
register struct CPUX86State *env asm("$15");
#define reg_EAX
#define reg_ESP
#define reg_EBP
#endif
#ifdef __ia64__
register unsigned int T0 asm("r24");
register unsigned int T1 asm("r25");
register unsigned int A0 asm("r26");
register struct CPUX86State *env asm("r27");
#endif

/* force GCC to generate only one epilog at the end of the function */
#define FORCE_RET() asm volatile ("");

#ifndef OPPROTO
#define OPPROTO
#endif

#define xglue(x, y) x ## y
#define glue(x, y) xglue(x, y)

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

#ifdef __alpha__
/* the symbols are considered non exported so a br immediate is generated */
#define __hidden __attribute__((visibility("hidden")))
#else
#define __hidden 
#endif

#ifdef __alpha__
/* Suggested by Richard Henderson. This will result in code like
        ldah $0,__op_param1($29)        !gprelhigh
        lda $0,__op_param1($0)          !gprellow
   We can then conveniently change $29 to $31 and adapt the offsets to
   emit the appropriate constant.  */
extern int __op_param1 __hidden;
extern int __op_param2 __hidden;
extern int __op_param3 __hidden;
#define PARAM1 ({ int _r; asm("" : "=r"(_r) : "0" (&__op_param1)); _r; })
#define PARAM2 ({ int _r; asm("" : "=r"(_r) : "0" (&__op_param2)); _r; })
#define PARAM3 ({ int _r; asm("" : "=r"(_r) : "0" (&__op_param3)); _r; })
#else
extern int __op_param1, __op_param2, __op_param3;
#define PARAM1 ((long)(&__op_param1))
#define PARAM2 ((long)(&__op_param2))
#define PARAM3 ((long)(&__op_param3))
#endif
extern int __op_jmp0, __op_jmp1;

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
void helper_cpuid(void);
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

typedef union {
    double d;
#ifndef WORDS_BIGENDIAN
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
    int64_t ll;
} CPU86_LDoubleU;

/* the following deal with IEEE double-precision numbers */
#define MAXEXPD 0x7ff
#define EXPBIAS 1023
#define EXPD(fp)	(((fp.l.upper) >> 20) & 0x7FF)
#define SIGND(fp)	((fp.l.upper) & 0x80000000)
#define MANTD(fp)	(fp.ll & ((1LL << 52) - 1))
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
    /* mantissa */
    upper = lduw(ptr + 8);
    /* XXX: handle overflow ? */
    e = (upper & 0x7fff) - 16383 + EXPBIAS; /* exponent */
    e |= (upper >> 4) & 0x800; /* sign */
    temp.ll = ((ldq(ptr) >> 11) & ((1LL << 52) - 1)) | ((uint64_t)e << 52);
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

