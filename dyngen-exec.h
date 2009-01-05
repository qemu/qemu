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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
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

#ifdef __OpenBSD__
#include <sys/types.h>
#else
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
// Linux/Sparc64 defines uint64_t
#if !(defined (__sparc_v9__) && defined(__linux__)) && !(defined(__APPLE__) && defined(__x86_64__))
/* XXX may be done for all 64 bits targets ? */
#if defined (__x86_64__) || defined(__ia64) || defined(__s390x__) || defined(__alpha__) || defined(__powerpc64__)
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
#if !(defined (__sparc_v9__) && defined(__linux__)) && !(defined(__APPLE__) && defined(__x86_64__))
#if defined (__x86_64__) || defined(__ia64) || defined(__s390x__) || defined(__alpha__) || defined(__powerpc64__)
typedef signed long int64_t;
#else
typedef signed long long int64_t;
#endif
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
#elif defined(__arm__)
#define AREG0 "r7"
#define AREG1 "r4"
#define AREG2 "r5"
#define AREG3 "r6"
#elif defined(__hppa__)
#define AREG0 "r17"
#define AREG1 "r14"
#define AREG2 "r15"
#define AREG3 "r16"
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
#define AREG0 "g5"
#define AREG1 "g6"
#define AREG2 "g7"
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

/* The return address may point to the start of the next instruction.
   Subtracting one gets us the call instruction itself.  */
#if defined(__s390__)
# define GETPC() ((void*)(((unsigned long)__builtin_return_address(0) & 0x7fffffffUL) - 1))
#elif defined(__arm__)
/* Thumb return addresses have the low bit set, so we need to subtract two.
   This is still safe in ARM mode because instructions are 4 bytes.  */
# define GETPC() ((void *)((unsigned long)__builtin_return_address(0) - 2))
#else
# define GETPC() ((void *)((unsigned long)__builtin_return_address(0) - 1))
#endif

#endif /* !defined(__DYNGEN_EXEC_H__) */
