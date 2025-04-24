/*
 * m68k cpu parameters for qemu.
 *
 * Copyright (c) 2005-2007 CodeSourcery
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef M68K_CPU_PARAM_H
#define M68K_CPU_PARAM_H

/*
 * Coldfire Linux uses 8k pages
 * and m68k linux uses 4k pages
 * use the smallest one
 */
#define TARGET_PAGE_BITS 12
#define TARGET_PHYS_ADDR_SPACE_BITS 32
#define TARGET_VIRT_ADDR_SPACE_BITS 32

#define TARGET_INSN_START_EXTRA_WORDS 1

#endif
