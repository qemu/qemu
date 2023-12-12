/*
 * target-specific swap() definitions
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef USER_TSWAP_H
#define USER_TSWAP_H

#include "exec/cpu-defs.h"
#include "exec/tswap.h"

#if TARGET_LONG_SIZE == 4
#define tswapl(s) tswap32(s)
#define bswaptls(s) bswap32s(s)
#else
#define tswapl(s) tswap64(s)
#define bswaptls(s) bswap64s(s)
#endif

#endif
