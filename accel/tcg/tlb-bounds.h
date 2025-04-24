/*
 * softmmu size bounds
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef ACCEL_TCG_TLB_BOUNDS_H
#define ACCEL_TCG_TLB_BOUNDS_H

#define CPU_TLB_DYN_MIN_BITS 6
#define CPU_TLB_DYN_DEFAULT_BITS 8

# if HOST_LONG_BITS == 32
/* Make sure we do not require a double-word shift for the TLB load */
#  define CPU_TLB_DYN_MAX_BITS (32 - TARGET_PAGE_BITS)
# else /* HOST_LONG_BITS == 64 */
/*
 * Assuming TARGET_PAGE_BITS==12, with 2**22 entries we can cover 2**(22+12) ==
 * 2**34 == 16G of address space. This is roughly what one would expect a
 * TLB to cover in a modern (as of 2018) x86_64 CPU. For instance, Intel
 * Skylake's Level-2 STLB has 16 1G entries.
 * Also, make sure we do not size the TLB past the guest's address space.
 */
#  ifdef TARGET_PAGE_BITS_VARY
#   define CPU_TLB_DYN_MAX_BITS                                  \
    MIN(22, TARGET_VIRT_ADDR_SPACE_BITS - TARGET_PAGE_BITS)
#  else
#   define CPU_TLB_DYN_MAX_BITS                                  \
    MIN_CONST(22, TARGET_VIRT_ADDR_SPACE_BITS - TARGET_PAGE_BITS)
#  endif
# endif

#endif /* ACCEL_TCG_TLB_BOUNDS_H */
