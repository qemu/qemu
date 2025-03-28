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

#ifndef COMPILING_PER_TARGET
#error cpu.h included from common code
#endif

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
#if !defined(TARGET_PAGE_BITS) && !defined(TARGET_PAGE_BITS_VARY)
# error TARGET_PAGE_BITS must be defined in cpu-param.h
#endif

#include "exec/target_long.h"

#endif
