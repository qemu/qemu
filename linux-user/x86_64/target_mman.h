/*
 * arch/x86/include/asm/processor.h:
 * TASK_UNMAPPED_BASE         __TASK_UNMAPPED_BASE(TASK_SIZE_LOW)
 * __TASK_UNMAPPED_BASE(S)    PAGE_ALIGN(S / 3)
 *
 * arch/x86/include/asm/page_64_types.h:
 * TASK_SIZE_LOW              DEFAULT_MAP_WINDOW
 * DEFAULT_MAP_WINDOW         ((1UL << 47) - PAGE_SIZE)
 */
#define TASK_UNMAPPED_BASE \
    TARGET_PAGE_ALIGN((1ull << TARGET_VIRT_ADDR_SPACE_BITS) / 3)

/* arch/x86/include/asm/elf.h */
#define ELF_ET_DYN_BASE       (TASK_UNMAPPED_BASE * 2)

#include "../generic/target_mman.h"
