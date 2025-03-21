/*
 * Alpha cpu parameters for qemu.
 *
 * Copyright (c) 2007 Jocelyn Mayer
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef ALPHA_CPU_PARAM_H
#define ALPHA_CPU_PARAM_H

/* ??? EV4 has 34 phys addr bits, EV5 has 40, EV6 has 44.  */
#define TARGET_PHYS_ADDR_SPACE_BITS  44

#ifdef CONFIG_USER_ONLY
/*
 * Allow user-only to vary page size.  Real hardware allows only 8k and 64k,
 * but since any variance means guests cannot assume a fixed value, allow
 * a 4k minimum to match x86 host, which can minimize emulation issues.
 */
# define TARGET_PAGE_BITS_VARY
# define TARGET_VIRT_ADDR_SPACE_BITS  63
#else
# define TARGET_PAGE_BITS 13
# define TARGET_VIRT_ADDR_SPACE_BITS  (30 + TARGET_PAGE_BITS)
#endif

#define TARGET_INSN_START_EXTRA_WORDS 0

#endif
