/* arch/sh/include/asm/processor_32.h */
#define TASK_UNMAPPED_BASE \
    TARGET_PAGE_ALIGN((1u << TARGET_VIRT_ADDR_SPACE_BITS) / 3)

#include "../generic/target_mman.h"
