/*
 * MicroBlaze cpu parameters for qemu.
 *
 * Copyright (c) 2009 Edgar E. Iglesias
 * SPDX-License-Identifier: LGPL-2.0+
 */

#ifndef MICROBLAZE_CPU_PARAM_H
#define MICROBLAZE_CPU_PARAM_H 1

#define TARGET_LONG_BITS 64
#define TARGET_PHYS_ADDR_SPACE_BITS 64
#define TARGET_VIRT_ADDR_SPACE_BITS 64
/* FIXME: MB uses variable pages down to 1K but linux only uses 4k.  */
#define TARGET_PAGE_BITS 12
#define NB_MMU_MODES 3

#endif
