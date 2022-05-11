/*
 * MicroBlaze cpu parameters for qemu.
 *
 * Copyright (c) 2009 Edgar E. Iglesias
 * SPDX-License-Identifier: LGPL-2.0+
 */

#ifndef MICROBLAZE_CPU_PARAM_H
#define MICROBLAZE_CPU_PARAM_H

/*
 * While system mode can address up to 64 bits of address space,
 * this is done via the lea/sea instructions, which are system-only
 * (as they also bypass the mmu).
 *
 * We can improve the user-only experience by only exposing 32 bits
 * of address space.
 */
#ifdef CONFIG_USER_ONLY
#define TARGET_LONG_BITS 32
#define TARGET_PHYS_ADDR_SPACE_BITS 32
#define TARGET_VIRT_ADDR_SPACE_BITS 32
#else
#define TARGET_LONG_BITS 64
#define TARGET_PHYS_ADDR_SPACE_BITS 64
#define TARGET_VIRT_ADDR_SPACE_BITS 64
#endif

/* FIXME: MB uses variable pages down to 1K but linux only uses 4k.  */
#define TARGET_PAGE_BITS 12
#define NB_MMU_MODES 3

#endif
