#ifndef PPC_TARGET_MMAN_H
#define PPC_TARGET_MMAN_H

#define TARGET_MAP_NORESERVE            0x40
#define TARGET_MAP_LOCKED               0x80

/*
 * arch/powerpc/include/asm/task_size_64.h
 * TASK_UNMAPPED_BASE_USER32    (PAGE_ALIGN(TASK_SIZE_USER32 / 4))
 * TASK_UNMAPPED_BASE_USER64    (PAGE_ALIGN(DEFAULT_MAP_WINDOW_USER64 / 4))
 * TASK_SIZE_USER32             (0x0000000100000000UL - (1 * PAGE_SIZE))
 * DEFAULT_MAP_WINDOW_USER64    TASK_SIZE_64TB (with 4k pages)
 */
#ifdef TARGET_PPC64
#define TASK_UNMAPPED_BASE      0x0000100000000000ull
#else
#define TASK_UNMAPPED_BASE      0x40000000
#endif

/* arch/powerpc/include/asm/elf.h */
#ifdef TARGET_PPC64
#define ELF_ET_DYN_BASE         0x100000000ull
#else
#define ELF_ET_DYN_BASE         0x000400000
#endif

#include "../generic/target_mman.h"

#endif
