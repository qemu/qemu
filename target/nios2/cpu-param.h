/*
 * Altera Nios II cpu parameters for qemu.
 *
 * Copyright (c) 2012 Chris Wulff <crwulff@gmail.com>
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef NIOS2_CPU_PARAM_H
#define NIOS2_CPU_PARAM_H

#define TARGET_LONG_BITS 32
#define TARGET_PAGE_BITS 12
#define TARGET_PHYS_ADDR_SPACE_BITS 32
#ifdef CONFIG_USER_ONLY
# define TARGET_VIRT_ADDR_SPACE_BITS 31
#else
# define TARGET_VIRT_ADDR_SPACE_BITS 32
#endif

#endif
