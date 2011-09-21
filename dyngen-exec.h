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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#if !defined(__DYNGEN_EXEC_H__)
#define __DYNGEN_EXEC_H__

#if defined(__i386__)
#define AREG0 "ebp"
#elif defined(__x86_64__)
#define AREG0 "r14"
#elif defined(_ARCH_PPC)
#define AREG0 "r27"
#elif defined(__arm__)
#define AREG0 "r7"
#elif defined(__hppa__)
#define AREG0 "r17"
#elif defined(__mips__)
#define AREG0 "s0"
#elif defined(__sparc__)
#ifdef CONFIG_SOLARIS
#define AREG0 "g2"
#else
#ifdef __sparc_v9__
#define AREG0 "g5"
#else
#define AREG0 "g6"
#endif
#endif
#elif defined(__s390__)
#define AREG0 "r10"
#elif defined(__alpha__)
/* Note $15 is the frame pointer, so anything in op-i386.c that would
   require a frame pointer, like alloca, would probably loose.  */
#define AREG0 "$15"
#elif defined(__mc68000)
#define AREG0 "%a5"
#elif defined(__ia64__)
#define AREG0 "r7"
#else
#error unsupported CPU
#endif

register CPUState *env asm(AREG0);

#endif /* !defined(__DYNGEN_EXEC_H__) */
