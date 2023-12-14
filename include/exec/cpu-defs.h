/*
 * common defines for all CPUs
 *
 * Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef CPU_DEFS_H
#define CPU_DEFS_H

#ifndef NEED_CPU_H
#error cpu.h included from common code
#endif

#include "qemu/host-utils.h"
#include "qemu/thread.h"
#ifndef CONFIG_USER_ONLY
#include "exec/hwaddr.h"
#endif
#include "exec/memattrs.h"
#include "hw/core/cpu.h"

#include "cpu-param.h"

#ifndef TARGET_LONG_BITS
# error TARGET_LONG_BITS must be defined in cpu-param.h
#endif
#ifndef TARGET_PHYS_ADDR_SPACE_BITS
# error TARGET_PHYS_ADDR_SPACE_BITS must be defined in cpu-param.h
#endif
#ifndef TARGET_VIRT_ADDR_SPACE_BITS
# error TARGET_VIRT_ADDR_SPACE_BITS must be defined in cpu-param.h
#endif
#ifndef TARGET_PAGE_BITS
# ifdef TARGET_PAGE_BITS_VARY
#  ifndef TARGET_PAGE_BITS_MIN
#   error TARGET_PAGE_BITS_MIN must be defined in cpu-param.h
#  endif
# else
#  error TARGET_PAGE_BITS must be defined in cpu-param.h
# endif
#endif

#include "exec/target_long.h"

#if defined(CONFIG_SOFTMMU) && defined(CONFIG_TCG)
#define CPU_TLB_DYN_MIN_BITS 6
#define CPU_TLB_DYN_DEFAULT_BITS 8

# if HOST_LONG_BITS == 32
/* Make sure we do not require a double-word shift for the TLB load */
#  define CPU_TLB_DYN_MAX_BITS (32 - TARGET_PAGE_BITS)
# else /* HOST_LONG_BITS == 64 */
/*
 * Assuming TARGET_PAGE_BITS==12, with 2**22 entries we can cover 2**(22+12) ==
 * 2**34 == 16G of address space. This is roughly what one would expect a
 * TLB to cover in a modern (as of 2018) x86_64 CPU. For instance, Intel
 * Skylake's Level-2 STLB has 16 1G entries.
 * Also, make sure we do not size the TLB past the guest's address space.
 */
#  ifdef TARGET_PAGE_BITS_VARY
#   define CPU_TLB_DYN_MAX_BITS                                  \
    MIN(22, TARGET_VIRT_ADDR_SPACE_BITS - TARGET_PAGE_BITS)
#  else
#   define CPU_TLB_DYN_MAX_BITS                                  \
    MIN_CONST(22, TARGET_VIRT_ADDR_SPACE_BITS - TARGET_PAGE_BITS)
#  endif
# endif

#endif /* CONFIG_SOFTMMU && CONFIG_TCG */

#endif
