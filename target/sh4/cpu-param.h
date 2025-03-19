/*
 * SH4 cpu parameters for qemu.
 *
 * Copyright (c) 2005 Samuel Tardieu
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef SH4_CPU_PARAM_H
#define SH4_CPU_PARAM_H

#define TARGET_PAGE_BITS 12  /* 4k */
#define TARGET_PHYS_ADDR_SPACE_BITS  32
#ifdef CONFIG_USER_ONLY
# define TARGET_VIRT_ADDR_SPACE_BITS 31
#else
# define TARGET_VIRT_ADDR_SPACE_BITS 32
#endif

#define TARGET_INSN_START_EXTRA_WORDS 1

#endif
