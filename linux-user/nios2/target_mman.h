/*
 * arch/nios2/include/asm/processor.h:
 * TASK_UNMAPPED_BASE         PAGE_ALIGN(TASK_SIZE / 3)
 * TASK_SIZE                  0x7FFF0000UL
 */
#define TASK_UNMAPPED_BASE    TARGET_PAGE_ALIGN(0x7FFF0000 / 3)

/* arch/nios2/include/asm/elf.h */
#define ELF_ET_DYN_BASE       0xD0000000

#include "../generic/target_mman.h"
