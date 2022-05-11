/*
 * Alpha cpu parameters for qemu.
 *
 * Copyright (c) 2007 Jocelyn Mayer
 * SPDX-License-Identifier: LGPL-2.0+
 */

#ifndef ALPHA_CPU_PARAM_H
#define ALPHA_CPU_PARAM_H

#define TARGET_LONG_BITS 64
#define TARGET_PAGE_BITS 13

/* ??? EV4 has 34 phys addr bits, EV5 has 40, EV6 has 44.  */
#define TARGET_PHYS_ADDR_SPACE_BITS  44
#define TARGET_VIRT_ADDR_SPACE_BITS  (30 + TARGET_PAGE_BITS)

#define NB_MMU_MODES 3

#endif
