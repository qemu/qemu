/* i386 execution defines */

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

#ifdef __i386__
register unsigned int T0 asm("ebx");
register unsigned int T1 asm("esi");
register unsigned int A0 asm("edi");
register struct CPUX86State *env asm("ebp");
#endif
#ifdef __powerpc__
register unsigned int T0 asm("r24");
register unsigned int T1 asm("r25");
register unsigned int A0 asm("r26");
register struct CPUX86State *env asm("r27");
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
register unsigned int T0 asm("l0");
register unsigned int T1 asm("l1");
register unsigned int A0 asm("l2");
register struct CPUX86State *env asm("l3");
#endif

/* force GCC to generate only one epilog at the end of the function */
#define FORCE_RET() asm volatile ("");

#ifndef OPPROTO
#define OPPROTO
#endif

#define xglue(x, y) x ## y
#define glue(x, y) xglue(x, y)

#define EAX (env->regs[R_EAX])
#define ECX (env->regs[R_ECX])
#define EDX (env->regs[R_EDX])
#define EBX (env->regs[R_EBX])
#define ESP (env->regs[R_ESP])
#define EBP (env->regs[R_EBP])
#define ESI (env->regs[R_ESI])
#define EDI (env->regs[R_EDI])
#define PC  (env->pc)
#define DF  (env->df)

#define CC_SRC (env->cc_src)
#define CC_DST (env->cc_dst)
#define CC_OP  (env->cc_op)

/* float macros */
#define FT0    (env->ft0)
#define ST0    (env->fpregs[env->fpstt])
#define ST(n)  (env->fpregs[(env->fpstt + (n)) & 7])
#define ST1    ST(1)

extern int __op_param1, __op_param2, __op_param3;
#define PARAM1 ((long)(&__op_param1))
#define PARAM2 ((long)(&__op_param2))
#define PARAM3 ((long)(&__op_param3))

#include "cpu-i386.h"

typedef struct CCTable {
    int (*compute_all)(void); /* return all the flags */
    int (*compute_c)(void);  /* return the C flag */
} CCTable;

extern CCTable cc_table[];
