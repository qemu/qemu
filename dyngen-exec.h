/*
 *  dyngen defines for micro operation code
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

typedef struct FILE FILE;
extern int fprintf(FILE *, const char *, ...);
extern int printf(const char *, ...);
#define NULL 0
#include <fenv.h>

#ifdef __i386__
#define AREG0 "ebp"
#define AREG1 "ebx"
#define AREG2 "esi"
#define AREG3 "edi"
#endif
#ifdef __powerpc__
#define AREG0 "r27"
#define AREG1 "r24"
#define AREG2 "r25"
#define AREG3 "r26"
#define AREG4 "r16"
#define AREG5 "r17"
#define AREG6 "r18"
#define AREG7 "r19"
#define AREG8 "r20"
#define AREG9 "r21"
#define AREG10 "r22"
#define AREG11 "r23"
#define USE_INT_TO_FLOAT_HELPERS
#define BUGGY_GCC_DIV64
#endif
#ifdef __arm__
#define AREG0 "r7"
#define AREG1 "r4"
#define AREG2 "r5"
#define AREG3 "r6"
#endif
#ifdef __mips__
#define AREG0 "s3"
#define AREG1 "s0"
#define AREG2 "s1"
#define AREG3 "s2"
#endif
#ifdef __sparc__
#define AREG0 "g6"
#define AREG1 "g1"
#define AREG2 "g2"
#define AREG3 "g3"
#define AREG4 "l0"
#define AREG5 "l1"
#define AREG6 "l2"
#define AREG7 "l3"
#define AREG8 "l4"
#define AREG9 "l5"
#define AREG10 "l6"
#define AREG11 "l7"
#define USE_FP_CONVERT
#endif
#ifdef __s390__
#define AREG0 "r10"
#define AREG1 "r7"
#define AREG2 "r8"
#define AREG3 "r9"
#endif
#ifdef __alpha__
/* Note $15 is the frame pointer, so anything in op-i386.c that would
   require a frame pointer, like alloca, would probably loose.  */
#define AREG0 "$15"
#define AREG1 "$9"
#define AREG2 "$10"
#define AREG3 "$11"
#define AREG4 "$12"
#define AREG5 "$13"
#define AREG6 "$14"
#endif
#ifdef __ia64__
#define AREG0 "r27"
#define AREG1 "r24"
#define AREG2 "r25"
#define AREG3 "r26"
#endif

/* force GCC to generate only one epilog at the end of the function */
#define FORCE_RET() asm volatile ("");

#ifndef OPPROTO
#define OPPROTO
#endif

#define xglue(x, y) x ## y
#define glue(x, y) xglue(x, y)
#define stringify(s)	tostring(s)
#define tostring(s)	#s

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

#ifdef __i386__
#define EXIT_TB() asm volatile ("ret")
#endif
#ifdef __powerpc__
#define EXIT_TB() asm volatile ("blr")
#endif
#ifdef __s390__
#define EXIT_TB() asm volatile ("br %r14")
#endif
#ifdef __alpha__
#define EXIT_TB() asm volatile ("ret")
#endif
#ifdef __ia64__
#define EXIT_TB() asm volatile ("br.ret.sptk.many b0;;")
#endif
#ifdef __sparc__
#define EXIT_TB() asm volatile ("jmpl %i0 + 8, %g0\n"
                                "nop")
#endif
#ifdef __arm__
#define EXIT_TB() asm volatile ("b exec_loop")
#endif

