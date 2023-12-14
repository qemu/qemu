/*
 * arch/hexgon/include/asm/processor.h
 * TASK_UNMAPPED_BASE        PAGE_ALIGN(TASK_SIZE / 3)
 *
 * arch/hexagon/include/asm/mem-layout.h
 * TASK_SIZE                 PAGE_OFFSET
 * PAGE_OFFSET               0xc0000000
 */
#define TASK_UNMAPPED_BASE   0x40000000

/* arch/hexagon/include/asm/elf.h */
#define ELF_ET_DYN_BASE      0x08000000

#include "../generic/target_mman.h"
