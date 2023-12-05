/*
 * S/390 cpu parameters for qemu.
 *
 * Copyright (c) 2009 Ulrich Hecht
 * SPDX-License-Identifier: GPL-2.0+
 */

#ifndef S390_CPU_PARAM_H
#define S390_CPU_PARAM_H

#define TARGET_LONG_BITS 64
#define TARGET_PAGE_BITS 12
#define TARGET_PHYS_ADDR_SPACE_BITS 64
#define TARGET_VIRT_ADDR_SPACE_BITS 64

/*
 * The z/Architecture has a strong memory model with some
 * store-after-load re-ordering.
 */
#define TCG_GUEST_DEFAULT_MO      (TCG_MO_ALL & ~TCG_MO_ST_LD)

#endif
