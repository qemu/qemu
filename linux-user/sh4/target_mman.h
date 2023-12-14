/* arch/sh/include/asm/processor_32.h */
#define TASK_UNMAPPED_BASE \
    TARGET_PAGE_ALIGN((1u << TARGET_VIRT_ADDR_SPACE_BITS) / 3)

/* arch/sh/include/asm/elf.h */
#define ELF_ET_DYN_BASE       (TASK_UNMAPPED_BASE * 2)

#include "../generic/target_mman.h"
