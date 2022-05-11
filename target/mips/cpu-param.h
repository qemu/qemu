/*
 * MIPS cpu parameters for qemu.
 *
 * SPDX-License-Identifier: LGPL-2.0+
 */

#ifndef MIPS_CPU_PARAM_H
#define MIPS_CPU_PARAM_H

#ifdef TARGET_MIPS64
# define TARGET_LONG_BITS 64
#else
# define TARGET_LONG_BITS 32
#endif
#ifdef TARGET_ABI_MIPSN64
#define TARGET_PHYS_ADDR_SPACE_BITS 48
#define TARGET_VIRT_ADDR_SPACE_BITS 48
#else
#define TARGET_PHYS_ADDR_SPACE_BITS 40
# ifdef CONFIG_USER_ONLY
#  define TARGET_VIRT_ADDR_SPACE_BITS 31
# else
#  define TARGET_VIRT_ADDR_SPACE_BITS 32
#endif
#endif
#ifdef CONFIG_USER_ONLY
#define TARGET_PAGE_BITS 12
#else
#define TARGET_PAGE_BITS_VARY
#define TARGET_PAGE_BITS_MIN 12
#endif
#define NB_MMU_MODES 4

#endif
