/*
 * arch/nios2/include/asm/processor.h:
 * TASK_UNMAPPED_BASE         PAGE_ALIGN(TASK_SIZE / 3)
 * TASK_SIZE                  0x7FFF0000UL
 */
#define TASK_UNMAPPED_BASE    TARGET_PAGE_ALIGN(0x7FFF0000 / 3)

#include "../generic/target_mman.h"
