/*
 * arch/microblaze/include/asm/processor.h:
 * TASK_UNMAPPED_BASE           (TASK_SIZE / 8 * 3)
 * TASK_SIZE                    CONFIG_KERNEL_START
 * CONFIG_KERNEL_START          0xc0000000 (default in Kconfig)
 */
#define TASK_UNMAPPED_BASE      0x48000000

/* arch/microblaze/include/uapi/asm/elf.h */
#define ELF_ET_DYN_BASE         0x08000000

#include "../generic/target_mman.h"
