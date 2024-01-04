/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright IBM Corp. 2008
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

#ifndef __LIBCFLAT_H
#define __LIBCFLAT_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define __unused __attribute__((__unused__))

#define xstr(s) xxstr(s)
#define xxstr(s) #s

#define __ALIGN_MASK(x, mask)	(((x) + (mask)) & ~(mask))
#define __ALIGN(x, a)		__ALIGN_MASK(x, (typeof(x))(a) - 1)
#define ALIGN(x, a)		__ALIGN((x), (a))

typedef uint8_t		u8;
typedef int8_t		s8;
typedef uint16_t	u16;
typedef int16_t		s16;
typedef uint32_t	u32;
typedef int32_t		s32;
typedef uint64_t	u64;
typedef int64_t		s64;
typedef unsigned long	ulong;

typedef _Bool		bool;
#define false 0
#define true  1

extern void puts(const char *s);
extern void exit(int code);
extern void abort(void);

extern int printf(const char *fmt, ...);
extern int snprintf(char *buf, int size, const char *fmt, ...);
extern int vsnprintf(char *buf, int size, const char *fmt, va_list va);
extern long atol(const char *ptr);

void report_prefix_push(const char *prefix);
void report_prefix_pop(void);
void report(const char *msg_fmt, bool pass, ...);
void report_xfail(const char *msg_fmt, bool xfail, bool pass, ...);
int report_summary(void);

#define ARRAY_SIZE(_a) (sizeof(_a)/sizeof((_a)[0]))

#define container_of(ptr, type, member) ({				\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);		\
	(type *)( (char *)__mptr - offsetof(type,member) );})

#define assert(cond)							\
do {									\
	if (!(cond))							\
		printf("%s:%d: assert failed\n", __FILE__, __LINE__),	\
		abort();						\
} while (0)

#endif
