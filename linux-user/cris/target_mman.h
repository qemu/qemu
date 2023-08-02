/*
 * arch/cris/include/asm/processor.h:
 * TASK_UNMAPPED_BASE      (PAGE_ALIGN(TASK_SIZE / 3))
 *
 * arch/cris/include/arch-v32/arch/processor.h
 * TASK_SIZE               0xb0000000
 */
#define TASK_UNMAPPED_BASE TARGET_PAGE_ALIGN(0xb0000000 / 3)

#include "../generic/target_mman.h"
