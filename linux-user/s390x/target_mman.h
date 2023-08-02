/*
 * arch/s390/include/asm/processor.h:
 * TASK_UNMAPPED_BASE           (... : (_REGION2_SIZE >> 1))
 *
 * arch/s390/include/asm/pgtable.h:
 * _REGION2_SIZE                (1UL << _REGION2_SHIFT)
 * _REGION2_SHIFT               42
 */
#define TASK_UNMAPPED_BASE      (1ull << 41)

#include "../generic/target_mman.h"
