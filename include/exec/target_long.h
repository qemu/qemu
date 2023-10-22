/*
 * Target Long Definitions
 *
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2023 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _TARGET_LONG_H_
#define _TARGET_LONG_H_

/*
 * Usually this should only be included via cpu-defs.h however for
 * certain cases where we want to build only two versions of a binary
 * object we can include directly. However the build-system must
 * ensure TARGET_LONG_BITS is defined directly.
 */
#ifndef TARGET_LONG_BITS
#error TARGET_LONG_BITS not defined
#endif

#define TARGET_LONG_SIZE (TARGET_LONG_BITS / 8)

/* target_ulong is the type of a virtual address */
#if TARGET_LONG_SIZE == 4
typedef int32_t target_long;
typedef uint32_t target_ulong;
#define TARGET_FMT_lx "%08x"
#define TARGET_FMT_ld "%d"
#define TARGET_FMT_lu "%u"
#define MO_TL MO_32
#elif TARGET_LONG_SIZE == 8
typedef int64_t target_long;
typedef uint64_t target_ulong;
#define TARGET_FMT_lx "%016" PRIx64
#define TARGET_FMT_ld "%" PRId64
#define TARGET_FMT_lu "%" PRIu64
#define MO_TL MO_64
#else
#error TARGET_LONG_SIZE undefined
#endif

#endif /* _TARGET_LONG_H_ */
