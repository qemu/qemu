/*
 * arch/arm/include/asm/memory.h
 * TASK_UNMAPPED_BASE        ALIGN(TASK_SIZE / 3, SZ_16M)
 * TASK_SIZE                 CONFIG_PAGE_OFFSET
 * CONFIG_PAGE_OFFSET        0xC0000000 (default in Kconfig)
 */
#define TASK_UNMAPPED_BASE   0x40000000

/* arch/arm/include/asm/elf.h */
#define ELF_ET_DYN_BASE      0x00400000

#include "../generic/target_mman.h"
