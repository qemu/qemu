/*
 * MIPS cpu parameters for qemu.
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef MIPS_CPU_PARAM_H
#define MIPS_CPU_PARAM_H

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
#define TARGET_PAGE_BITS 12

#define TARGET_INSN_START_EXTRA_WORDS 2

#endif
