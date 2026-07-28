/*
 * SH4 cpu parameters for qemu.
 *
 * Copyright (c) 2005 Samuel Tardieu
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef SH4_CPU_PARAM_H
#define SH4_CPU_PARAM_H

#define TARGET_PAGE_BITS 12  /* 4k */

/* qemu-user does not emulate the MMU, so no need to limit to 31 bits. */
#define TARGET_VIRT_ADDR_SPACE_BITS 32

#endif
