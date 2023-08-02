#ifndef MIPS_TARGET_MMAN_H
#define MIPS_TARGET_MMAN_H

#define TARGET_PROT_SEM                 0x10

#define TARGET_MAP_NORESERVE            0x0400
#define TARGET_MAP_ANONYMOUS            0x0800
#define TARGET_MAP_GROWSDOWN            0x1000
#define TARGET_MAP_DENYWRITE            0x2000
#define TARGET_MAP_EXECUTABLE           0x4000
#define TARGET_MAP_LOCKED               0x8000
#define TARGET_MAP_POPULATE             0x10000
#define TARGET_MAP_NONBLOCK             0x20000
#define TARGET_MAP_STACK                0x40000
#define TARGET_MAP_HUGETLB              0x80000

/*
 * arch/mips/include/asm/processor.h:
 * TASK_UNMAPPED_BASE         PAGE_ALIGN(TASK_SIZE / 3)
 */
#define TASK_UNMAPPED_BASE \
    TARGET_PAGE_ALIGN((1ull << TARGET_VIRT_ADDR_SPACE_BITS) / 3)

/* arch/mips/include/asm/elf.h */
#define ELF_ET_DYN_BASE       (TASK_UNMAPPED_BASE * 2)

#include "../generic/target_mman.h"

#endif
