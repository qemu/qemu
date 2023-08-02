/*
 * arch/openrisc/include/asm/processor.h:
 * TASK_UNMAPPED_BASE      (TASK_SIZE / 8 * 3)
 * TASK_SIZE               (0x80000000UL)
 */
#define TASK_UNMAPPED_BASE      0x30000000

/* arch/openrisc/include/asm/elf.h */
#define ELF_ET_DYN_BASE         0x08000000

#include "../generic/target_mman.h"
