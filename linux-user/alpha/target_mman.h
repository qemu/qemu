#ifndef ALPHA_TARGET_MMAN_H
#define ALPHA_TARGET_MMAN_H

#define TARGET_MAP_ANONYMOUS            0x10
#define TARGET_MAP_FIXED                0x100
#define TARGET_MAP_GROWSDOWN            0x01000
#define TARGET_MAP_DENYWRITE            0x02000
#define TARGET_MAP_EXECUTABLE           0x04000
#define TARGET_MAP_LOCKED               0x08000
#define TARGET_MAP_NORESERVE            0x10000
#define TARGET_MAP_POPULATE             0x20000
#define TARGET_MAP_NONBLOCK             0x40000
#define TARGET_MAP_STACK                0x80000
#define TARGET_MAP_HUGETLB              0x100000
#define TARGET_MAP_FIXED_NOREPLACE      0x200000

#define TARGET_MADV_DONTNEED 6

#define TARGET_MS_ASYNC 1
#define TARGET_MS_SYNC 2
#define TARGET_MS_INVALIDATE 4

/*
 * arch/alpha/include/asm/processor.h:
 *
 * TASK_UNMAPPED_BASE           TASK_SIZE / 2
 * TASK_SIZE                    0x40000000000UL
 */
#define TASK_UNMAPPED_BASE      0x20000000000ull

/* arch/alpha/include/asm/elf.h */
#define ELF_ET_DYN_BASE         (TASK_UNMAPPED_BASE + 0x1000000)

#include "../generic/target_mman.h"

#endif
