/*
 * i386 cpu parameters for qemu.
 *
 * Copyright (c) 2003 Fabrice Bellard
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef I386_CPU_PARAM_H
#define I386_CPU_PARAM_H

#ifdef TARGET_X86_64
# define TARGET_LONG_BITS             64
# define TARGET_PHYS_ADDR_SPACE_BITS  52
/*
 * ??? This is really 48 bits, sign-extended, but the only thing
 * accessible to userland with bit 48 set is the VSYSCALL, and that
 * is handled via other mechanisms.
 */
# define TARGET_VIRT_ADDR_SPACE_BITS  47
#else
# define TARGET_LONG_BITS             32
# define TARGET_PHYS_ADDR_SPACE_BITS  36
# define TARGET_VIRT_ADDR_SPACE_BITS  32
#endif
#define TARGET_PAGE_BITS 12

/* The x86 has a strong memory model with some store-after-load re-ordering */
#define TCG_GUEST_DEFAULT_MO      (TCG_MO_ALL & ~TCG_MO_ST_LD)

#endif
