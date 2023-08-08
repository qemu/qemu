#ifndef HPPA_TARGET_MMAN_H
#define HPPA_TARGET_MMAN_H

#define TARGET_MAP_TYPE                 0x2b
#define TARGET_MAP_FIXED                0x04
#define TARGET_MAP_ANONYMOUS            0x10
#define TARGET_MAP_GROWSDOWN            0x8000
#define TARGET_MAP_POPULATE             0x10000
#define TARGET_MAP_NONBLOCK             0x20000
#define TARGET_MAP_STACK                0x40000
#define TARGET_MAP_HUGETLB              0x80000
#define TARGET_MAP_UNINITIALIZED        0

#define TARGET_MADV_MERGEABLE 65
#define TARGET_MADV_UNMERGEABLE 66
#define TARGET_MADV_HUGEPAGE 67
#define TARGET_MADV_NOHUGEPAGE 68
#define TARGET_MADV_DONTDUMP 69
#define TARGET_MADV_DODUMP 70
#define TARGET_MADV_WIPEONFORK 71
#define TARGET_MADV_KEEPONFORK 72

#define TARGET_MS_SYNC 1
#define TARGET_MS_ASYNC 2
#define TARGET_MS_INVALIDATE 4

/* arch/parisc/include/asm/processor.h: DEFAULT_MAP_BASE32 */
#define TASK_UNMAPPED_BASE      0x40000000

/* arch/parisc/include/asm/elf.h */
#define ELF_ET_DYN_BASE         (TASK_UNMAPPED_BASE + 0x01000000)

#include "../generic/target_mman.h"

#endif
