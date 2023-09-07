/*
 * arch/s390/include/asm/processor.h:
 * TASK_UNMAPPED_BASE           (... : (_REGION2_SIZE >> 1))
 *
 * arch/s390/include/asm/pgtable.h:
 * _REGION2_SIZE                (1UL << _REGION2_SHIFT)
 * _REGION2_SHIFT               42
 */
#define TASK_UNMAPPED_BASE      (1ull << 41)

/*
 * arch/s390/include/asm/elf.h:
 * ELF_ET_DYN_BASE              (STACK_TOP / 3 * 2) & ~((1UL << 32) - 1)
 *
 * arch/s390/include/asm/processor.h:
 * STACK_TOP                    VDSO_LIMIT - VDSO_SIZE - PAGE_SIZE
 * VDSO_LIMIT                   _REGION2_SIZE
 */
#define ELF_ET_DYN_BASE         (((1ull << 42) / 3 * 2) & ~0xffffffffull)

#include "../generic/target_mman.h"
