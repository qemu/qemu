/*
 * PowerPC cpu parameters for qemu.
 *
 * Copyright (c) 2007 Jocelyn Mayer
 * SPDX-License-Identifier: LGPL-2.0+
 */

#ifndef PPC_CPU_PARAM_H
#define PPC_CPU_PARAM_H

#ifdef TARGET_PPC64
# define TARGET_LONG_BITS 64
/*
 * Note that the official physical address space bits is 62-M where M
 * is implementation dependent.  I've not looked up M for the set of
 * cpus we emulate at the system level.
 */
#define TARGET_PHYS_ADDR_SPACE_BITS 62
/*
 * Note that the PPC environment architecture talks about 80 bit virtual
 * addresses, with segmentation.  Obviously that's not all visible to a
 * single process, which is all we're concerned with here.
 */
# ifdef TARGET_ABI32
#  define TARGET_VIRT_ADDR_SPACE_BITS 32
# else
#  define TARGET_VIRT_ADDR_SPACE_BITS 64
# endif
#else
# define TARGET_LONG_BITS 32
# define TARGET_PHYS_ADDR_SPACE_BITS 36
# define TARGET_VIRT_ADDR_SPACE_BITS 32
#endif

#ifdef CONFIG_USER_ONLY
/* Allow user-only to vary page size from 4k */
# define TARGET_PAGE_BITS_VARY
# define TARGET_PAGE_BITS_MIN 12
#else
# define TARGET_PAGE_BITS 12
#endif

#endif
