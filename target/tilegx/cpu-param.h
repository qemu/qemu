/*
 * TILE-Gx cpu parameters for qemu.
 *
 * Copyright (c) 2015 Chen Gang
 * SPDX-License-Identifier: LGPL-2.0+
 */

#ifndef TILEGX_CPU_PARAM_H
#define TILEGX_CPU_PARAM_H 1

#define TARGET_LONG_BITS 64
#define TARGET_PAGE_BITS 16  /* TILE-Gx uses 64KB page size */
#define TARGET_PHYS_ADDR_SPACE_BITS 42
#define TARGET_VIRT_ADDR_SPACE_BITS 64
#define NB_MMU_MODES 1

#endif
