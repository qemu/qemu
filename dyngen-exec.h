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
#if !defined(__DYNGEN_EXEC_H__)
#define __DYNGEN_EXEC_H__

/* prevent Solaris from trying to typedef FILE in gcc's
   include/floatingpoint.h which will conflict with the
   definition down below */
#ifdef __sun__
#define _FILEDEFED
#endif

/* NOTE: standard headers should be used with special care at this
   point because host CPU registers are used as global variables. Some
   host headers do not allow that. */
#include <stddef.h>

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
// Linux/Sparc64 defines uint64_t
#if !(defined (__sparc_v9__) && defined(__linux__))
/* XXX may be done for all 64 bits targets ? */
#if defined (__x86_64__) || defined(__ia64) || defined(__s390x__) || defined(__alpha__) 
typedef unsigned long uint64_t;
#else
typedef unsigned long long uint64_t;
#endif
#endif

/* if Solaris/__sun__, don't typedef int8_t, as it will be typedef'd
   prior to this and will cause an error in compliation, conflicting
   with /usr/include/sys/int_types.h, line 75 */
#ifndef __sun__
typedef signed char int8_t;
#endif
typedef signed short int16_t;
typedef signed int int32_t;
// Linux/Sparc64 defines int64_t
#if !(defined (__sparc_v9__) && defined(__linux__))
#if defined (__x86_64__) || defined(__ia64) || defined(__s390x__) || defined(__alpha__)
typedef signed long int64_t;
#else
typedef signed long long int64_t;
#endif
#endif

/* XXX: This may be wrong for 64-bit ILP32 hosts.  */
typedef void * host_reg_t;

#define INT8_MIN		(-128)
#define INT16_MIN		(-32767-1)
#define INT32_MIN		(-2147483647-1)
#define INT64_MIN		(-(int64_t)(9223372036854775807)-1)
#define INT8_MAX		(127)
#define INT16_MAX		(32767)
#define INT32_MAX		(2147483647)
#define INT64_MAX		((int64_t)(9223372036854775807))
#define UINT8_MAX		(255)
#define UINT16_MAX		(65535)
#define UINT32_MAX		(4294967295U)
#define UINT64_MAX		((uint64_t)(18446744073709551615))

#ifdef _BSD
typedef struct __sFILE FILE;
#else
typedef struct FILE FILE;
#endif
extern int fprintf(FILE *, const char *, ...);
extern int fputs(const char *, FILE *);
extern int printf(const char *, ...);
#undef NULL
#define NULL 0

#if defined(__i386__)
#define AREG0 "ebp"
#define AREG1 "ebx"
#define AREG2 "esi"
#define AREG3 "edi"
#elif defined(__x86_64__)
#define AREG0 "r14"
#define AREG1 "r15"
#define AREG2 "r12"
#define AREG3 "r13"
//#define AREG4 "rbp"
//#define AREG5 "rbx"
#elif defined(__powerpc__)
#define AREG0 "r27"
#define AREG1 "r24"
#define AREG2 "r25"
#define AREG3 "r26"
/* XXX: suppress this hack */
#if defined(CONFIG_USER_ONLY)
#define AREG4 "r16"
#define AREG5 "r17"
#define AREG6 "r18"
#define AREG7 "r19"
#define AREG8 "r20"
#define AREG9 "r21"
#define AREG10 "r22"
#define AREG11 "r23"
#endif
#define USE_INT_TO_FLOAT_HELPERS
#define BUGGY_GCC_DIV64
#elif defined(__arm__)
#define AREG0 "r7"
#define AREG1 "r4"
#define AREG2 "r5"
#define AREG3 "r6"
#elif defined(__mips__)
#define AREG0 "fp"
#define AREG1 "s0"
#define AREG2 "s1"
#define AREG3 "s2"
#define AREG4 "s3"
#define AREG5 "s4"
#define AREG6 "s5"
#define AREG7 "s6"
#define AREG8 "s7"
#elif defined(__sparc__)
#ifdef HOST_SOLARIS
#define AREG0 "g2"
#define AREG1 "g3"
#define AREG2 "g4"
#define AREG3 "g5"
#define AREG4 "g6"
#else
#ifdef __sparc_v9__
#define AREG0 "g1"
#define AREG1 "g4"
#define AREG2 "g5"
#define AREG3 "g7"
#else
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
#endif
#endif
#define USE_FP_CONVERT
#elif defined(__s390__)
#define AREG0 "r10"
#define AREG1 "r7"
#define AREG2 "r8"
#define AREG3 "r9"
#elif defined(__alpha__)
/* Note $15 is the frame pointer, so anything in op-i386.c that would
   require a frame pointer, like alloca, would probably loose.  */
#define AREG0 "$15"
#define AREG1 "$9"
#define AREG2 "$10"
#define AREG3 "$11"
#define AREG4 "$12"
#define AREG5 "$13"
#define AREG6 "$14"
#elif defined(__mc68000)
#define AREG0 "%a5"
#define AREG1 "%a4"
#define AREG2 "%d7"
#define AREG3 "%d6"
#define AREG4 "%d5"
#elif defined(__ia64__)
#define AREG0 "r7"
#define AREG1 "r4"
#define AREG2 "r5"
#define AREG3 "r6"
#else
#error unsupported CPU
#endif

/* force GCC to generate only one epilog at the end of the function */
#define FORCE_RET() __asm__ __volatile__("" : : : "memory");

#ifndef OPPROTO
#define OPPROTO
#endif

#define xglue(x, y) x ## y
#define glue(x, y) xglue(x, y)
#define stringify(s)	tostring(s)
#define tostring(s)	#s

#if defined(__alpha__) || defined(__s390__)
/* the symbols are considered non exported so a br immediate is generated */
#define __hidden __attribute__((visibility("hidden")))
#else
#define __hidden
#endif

#if defined(__alpha__)
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
#elif defined(__s390__)
extern int __op_param1 __hidden;
extern int __op_param2 __hidden;
extern int __op_param3 __hidden;
#define PARAM1 ({ int _r; asm("bras %0,8; .long " ASM_NAME(__op_param1) "; l %0,0(%0)" : "=r"(_r) : ); _r; })
#define PARAM2 ({ int _r; asm("bras %0,8; .long " ASM_NAME(__op_param2) "; l %0,0(%0)" : "=r"(_r) : ); _r; })
#define PARAM3 ({ int _r; asm("bras %0,8; .long " ASM_NAME(__op_param3) "; l %0,0(%0)" : "=r"(_r) : ); _r; })
#else
#if defined(__APPLE__)
static int __op_param1, __op_param2, __op_param3;
#else
extern int __op_param1, __op_param2, __op_param3;
#endif
#define PARAM1 ((long)(&__op_param1))
#define PARAM2 ((long)(&__op_param2))
#define PARAM3 ((long)(&__op_param3))
#endif /* !defined(__alpha__) */

extern int __op_jmp0, __op_jmp1, __op_jmp2, __op_jmp3;

#if defined(_WIN32) || defined(__APPLE__)
#define ASM_NAME(x) "_" #x
#else
#define ASM_NAME(x) #x
#endif

#if defined(__i386__)
#define EXIT_TB() asm volatile ("ret")
#define GOTO_LABEL_PARAM(n) asm volatile ("jmp " ASM_NAME(__op_gen_label) #n)
#elif defined(__x86_64__)
#define EXIT_TB() asm volatile ("ret")
#define GOTO_LABEL_PARAM(n) asm volatile ("jmp " ASM_NAME(__op_gen_label) #n)
#elif defined(__powerpc__)
#define EXIT_TB() asm volatile ("blr")
#define GOTO_LABEL_PARAM(n) asm volatile ("b " ASM_NAME(__op_gen_label) #n)
#elif defined(__s390__)
#define EXIT_TB() asm volatile ("br %r14")
#define GOTO_LABEL_PARAM(n) asm volatile ("larl %r7,12; l %r7,0(%r7); br %r7; .long " ASM_NAME(__op_gen_label) #n)
#elif defined(__alpha__)
#define EXIT_TB() asm volatile ("ret")
#elif defined(__ia64__)
#define EXIT_TB() asm volatile ("br.ret.sptk.many b0;;")
#define GOTO_LABEL_PARAM(n) asm volatile ("br.sptk.many " \
					  ASM_NAME(__op_gen_label) #n)
#elif defined(__sparc__)
#define EXIT_TB() asm volatile ("jmpl %i0 + 8, %g0; nop")
#define GOTO_LABEL_PARAM(n) asm volatile ("ba " ASM_NAME(__op_gen_label) #n ";nop")
#elif defined(__arm__)
#define EXIT_TB() asm volatile ("b exec_loop")
#define GOTO_LABEL_PARAM(n) asm volatile ("b " ASM_NAME(__op_gen_label) #n)
#elif defined(__mc68000)
#define EXIT_TB() asm volatile ("rts")
#elif defined(__mips__)
#define EXIT_TB() asm volatile ("jr $ra")
#define GOTO_LABEL_PARAM(n) asm volatile (".set noat; la $1, " ASM_NAME(__op_gen_label) #n "; jr $1; .set at")
#else
#error unsupported CPU
#endif

#endif /* !defined(__DYNGEN_EXEC_H__) */
