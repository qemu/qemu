/*
 * ARM cpu parameters for qemu.
 *
 * Copyright (c) 2003 Fabrice Bellard
 * SPDX-License-Identifier: LGPL-2.0+
 */

#ifndef ARM_CPU_PARAM_H
#define ARM_CPU_PARAM_H

#ifdef TARGET_AARCH64
# define TARGET_LONG_BITS             64
# define TARGET_PHYS_ADDR_SPACE_BITS  52
# define TARGET_VIRT_ADDR_SPACE_BITS  52
#else
# define TARGET_LONG_BITS             32
# define TARGET_PHYS_ADDR_SPACE_BITS  40
# define TARGET_VIRT_ADDR_SPACE_BITS  32
#endif

#ifdef CONFIG_USER_ONLY
#define TARGET_PAGE_BITS 12
# ifdef TARGET_AARCH64
#  define TARGET_TAGGED_ADDRESSES
# endif
#else
/*
 * ARMv7 and later CPUs have 4K pages minimum, but ARMv5 and v6
 * have to support 1K tiny pages.
 */
# define TARGET_PAGE_BITS_VARY
# define TARGET_PAGE_BITS_MIN  10

/*
 * Cache the attrs and shareability fields from the page table entry.
 *
 * For ARMMMUIdx_Stage2*, pte_attrs is the S2 descriptor bits [5:2].
 * Otherwise, pte_attrs is the same as the MAIR_EL1 8-bit format.
 * For shareability and guarded, as in the SH and GP fields respectively
 * of the VMSAv8-64 PTEs.
 */
# define TARGET_PAGE_ENTRY_EXTRA  \
    uint8_t pte_attrs;            \
    uint8_t shareability;         \
    bool guarded;
#endif

#endif
