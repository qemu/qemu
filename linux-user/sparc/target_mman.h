#ifndef SPARC_TARGET_MMAN_H
#define SPARC_TARGET_MMAN_H

#define TARGET_MAP_NORESERVE           0x40
#define TARGET_MAP_LOCKED              0x100
#define TARGET_MAP_GROWSDOWN           0x0200

/*
 * arch/sparc/include/asm/page_64.h:
 * TASK_UNMAPPED_BASE      (test_thread_flag(TIF_32BIT) ? \
 *                          _AC(0x0000000070000000,UL) : \
 *                          VA_EXCLUDE_END)
 * But VA_EXCLUDE_END is > 0xffff800000000000UL which doesn't work
 * in userland emulation.
 */
#ifdef TARGET_ABI32
#define TASK_UNMAPPED_BASE      0x70000000
#else
#define TASK_UNMAPPED_BASE      (1ull << (TARGET_VIRT_ADDR_SPACE_BITS - 2))
#endif

/*
 * arch/sparc/include/asm/elf_64.h
 * Except that COMPAT_ELF_ET_DYN_BASE exactly matches TASK_UNMAPPED_BASE,
 * so move it up a bit.
 */
#ifdef TARGET_ABI32
#define ELF_ET_DYN_BASE         0x78000000
#else
#define ELF_ET_DYN_BASE         0x0000010000000000ull
#endif

#include "../generic/target_mman.h"

#endif
