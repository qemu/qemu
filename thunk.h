/*
 *  Generic thunking code to convert data between host and target CPU
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
#ifndef THUNK_H
#define THUNK_H

#include <inttypes.h>
#include "config.h"

#ifdef HAVE_BYTESWAP_H
#include <byteswap.h>
#else

#define bswap_16(x) \
({ \
	uint16_t __x = (x); \
	((uint16_t)( \
		(((uint16_t)(__x) & (uint16_t)0x00ffU) << 8) | \
		(((uint16_t)(__x) & (uint16_t)0xff00U) >> 8) )); \
})

#define bswap_32(x) \
({ \
	uint32_t __x = (x); \
	((uint32_t)( \
		(((uint32_t)(__x) & (uint32_t)0x000000ffUL) << 24) | \
		(((uint32_t)(__x) & (uint32_t)0x0000ff00UL) <<  8) | \
		(((uint32_t)(__x) & (uint32_t)0x00ff0000UL) >>  8) | \
		(((uint32_t)(__x) & (uint32_t)0xff000000UL) >> 24) )); \
})

#define bswap_64(x) \
({ \
	uint64_t __x = (x); \
	((uint64_t)( \
		(uint64_t)(((uint64_t)(__x) & (uint64_t)0x00000000000000ffULL) << 56) | \
		(uint64_t)(((uint64_t)(__x) & (uint64_t)0x000000000000ff00ULL) << 40) | \
		(uint64_t)(((uint64_t)(__x) & (uint64_t)0x0000000000ff0000ULL) << 24) | \
		(uint64_t)(((uint64_t)(__x) & (uint64_t)0x00000000ff000000ULL) <<  8) | \
	        (uint64_t)(((uint64_t)(__x) & (uint64_t)0x000000ff00000000ULL) >>  8) | \
		(uint64_t)(((uint64_t)(__x) & (uint64_t)0x0000ff0000000000ULL) >> 24) | \
		(uint64_t)(((uint64_t)(__x) & (uint64_t)0x00ff000000000000ULL) >> 40) | \
		(uint64_t)(((uint64_t)(__x) & (uint64_t)0xff00000000000000ULL) >> 56) )); \
})

#endif

#ifdef WORDS_BIGENDIAN
#define BSWAP_NEEDED
#endif

/* XXX: autoconf */
#define TARGET_I386
#define TARGET_LONG_BITS 32


#if defined(__alpha__)
#define HOST_LONG_BITS 64
#else
#define HOST_LONG_BITS 32
#endif

#define TARGET_LONG_SIZE (TARGET_LONG_BITS / 8)
#define HOST_LONG_SIZE (TARGET_LONG_BITS / 8)

static inline uint16_t bswap16(uint16_t x)
{
    return bswap_16(x);
}

static inline uint32_t bswap32(uint32_t x) 
{
    return bswap_32(x);
}

static inline uint64_t bswap64(uint64_t x) 
{
    return bswap_64(x);
}

static void inline bswap16s(uint16_t *s)
{
    *s = bswap16(*s);
}

static void inline bswap32s(uint32_t *s)
{
    *s = bswap32(*s);
}

static void inline bswap64s(uint64_t *s)
{
    *s = bswap64(*s);
}

#ifdef BSWAP_NEEDED

static inline uint16_t tswap16(uint16_t s)
{
    return bswap16(s);
}

static inline uint32_t tswap32(uint32_t s)
{
    return bswap32(s);
}

static inline uint64_t tswap64(uint64_t s)
{
    return bswap64(s);
}

static void inline tswap16s(uint16_t *s)
{
    *s = bswap16(*s);
}

static void inline tswap32s(uint32_t *s)
{
    *s = bswap32(*s);
}

static void inline tswap64s(uint64_t *s)
{
    *s = bswap64(*s);
}

#else

static inline uint16_t tswap16(uint16_t s)
{
    return s;
}

static inline uint32_t tswap32(uint32_t s)
{
    return s;
}

static inline uint64_t tswap64(uint64_t s)
{
    return s;
}

static void inline tswap16s(uint16_t *s)
{
}

static void inline tswap32s(uint32_t *s)
{
}

static void inline tswap64s(uint64_t *s)
{
}

#endif

#if TARGET_LONG_SIZE == 4
#define tswapl(s) tswap32(s)
#define tswapls(s) tswap32s((uint32_t *)(s))
#else
#define tswapl(s) tswap64(s)
#define tswapls(s) tswap64s((uint64_t *)(s))
#endif

#if TARGET_LONG_SIZE == 4
typedef int32_t target_long;
typedef uint32_t target_ulong;
#elif TARGET_LONG_SIZE == 8
typedef int64_t target_long;
typedef uint64_t target_ulong;
#else
#error TARGET_LONG_SIZE undefined
#endif

/* types enums definitions */

typedef enum argtype {
    TYPE_NULL,
    TYPE_CHAR,
    TYPE_SHORT,
    TYPE_INT,
    TYPE_LONG,
    TYPE_ULONG,
    TYPE_PTRVOID, /* pointer on unknown data */
    TYPE_LONGLONG,
    TYPE_ULONGLONG,
    TYPE_PTR,
    TYPE_ARRAY,
    TYPE_STRUCT,
} argtype;

#define MK_PTR(type) TYPE_PTR, type
#define MK_ARRAY(type, size) TYPE_ARRAY, size, type
#define MK_STRUCT(id) TYPE_STRUCT, id

#define THUNK_TARGET 0
#define THUNK_HOST   1

typedef struct {
    /* standard struct handling */
    const argtype *field_types;
    int nb_fields;
    int *field_offsets[2];
    /* special handling */
    void (*convert[2])(void *dst, const void *src);
    int size[2];
    int align[2];
    const char *name;
} StructEntry;

/* Translation table for bitmasks... */
typedef struct bitmask_transtbl {
	unsigned int	x86_mask;
	unsigned int	x86_bits;
	unsigned int	alpha_mask;
	unsigned int	alpha_bits;
} bitmask_transtbl;

void thunk_register_struct(int id, const char *name, const argtype *types);
void thunk_register_struct_direct(int id, const char *name, StructEntry *se1);
const argtype *thunk_convert(void *dst, const void *src, 
                             const argtype *type_ptr, int to_host);

unsigned int target_to_host_bitmask(unsigned int x86_mask, 
                                    bitmask_transtbl * trans_tbl);
unsigned int host_to_target_bitmask(unsigned int alpha_mask, 
                                    bitmask_transtbl * trans_tbl);

#endif
