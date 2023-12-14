/*
 * arch/x86/include/asm/processor.h:
 * TASK_UNMAPPED_BASE         __TASK_UNMAPPED_BASE(TASK_SIZE_LOW)
 * __TASK_UNMAPPED_BASE(S)    PAGE_ALIGN(S / 3)
 *
 * arch/x86/include/asm/page_32_types.h:
 * TASK_SIZE_LOW              TASK_SIZE
 * TASK_SIZE                  __PAGE_OFFSET
 * __PAGE_OFFSET              CONFIG_PAGE_OFFSET
 * CONFIG_PAGE_OFFSET         0xc0000000 (default in Kconfig)
 */
#define TASK_UNMAPPED_BASE    0x40000000

/* arch/x86/include/asm/elf.h */
#define ELF_ET_DYN_BASE       0x00400000

#include "../generic/target_mman.h"
