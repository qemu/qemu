/*
 * Alpha cpu parameters for qemu.
 *
 * Copyright (c) 2007 Jocelyn Mayer
 * SPDX-License-Identifier: LGPL-2.0+
 */

#ifndef ALPHA_CPU_PARAM_H
#define ALPHA_CPU_PARAM_H 1

#define TARGET_LONG_BITS 64
#define TARGET_PAGE_BITS 13
#ifdef CONFIG_USER_ONLY
/*
 * ??? The kernel likes to give addresses in high memory.  If the host has
 * more virtual address space than the guest, this can lead to impossible
 * allocations.  Honor the long-standing assumption that only kernel addrs
 * are negative, but otherwise allow allocations anywhere.  This could lead
 * to tricky emulation problems for programs doing tagged addressing, but
 * that's far fewer than encounter the impossible allocation problem.
 */
#define TARGET_PHYS_ADDR_SPACE_BITS  63
#define TARGET_VIRT_ADDR_SPACE_BITS  63
#else
/* ??? EV4 has 34 phys addr bits, EV5 has 40, EV6 has 44.  */
#define TARGET_PHYS_ADDR_SPACE_BITS  44
#define TARGET_VIRT_ADDR_SPACE_BITS  (30 + TARGET_PAGE_BITS)
#endif
#define NB_MMU_MODES 3

#endif
