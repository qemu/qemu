/*
 * PA-RISC cpu parameters for qemu.
 *
 * Copyright (c) 2016 Richard Henderson <rth@twiddle.net>
 * SPDX-License-Identifier: LGPL-2.0+
 */

#ifndef HPPA_CPU_PARAM_H
#define HPPA_CPU_PARAM_H

#ifdef TARGET_HPPA64
# define TARGET_LONG_BITS             64
# define TARGET_REGISTER_BITS         64
# define TARGET_VIRT_ADDR_SPACE_BITS  64
# define TARGET_PHYS_ADDR_SPACE_BITS  64
#elif defined(CONFIG_USER_ONLY)
# define TARGET_LONG_BITS             32
# define TARGET_REGISTER_BITS         32
# define TARGET_VIRT_ADDR_SPACE_BITS  32
# define TARGET_PHYS_ADDR_SPACE_BITS  32
#else
/*
 * In order to form the GVA from space:offset,
 * we need a 64-bit virtual address space.
 */
# define TARGET_LONG_BITS             64
# define TARGET_REGISTER_BITS         32
# define TARGET_VIRT_ADDR_SPACE_BITS  64
# define TARGET_PHYS_ADDR_SPACE_BITS  32
#endif
#define TARGET_PAGE_BITS 12
#define NB_MMU_MODES 5

#endif
