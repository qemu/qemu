#ifndef AARCH64_TARGET_MMAN_H
#define AARCH64_TARGET_MMAN_H

#define TARGET_PROT_BTI         0x10
#define TARGET_PROT_MTE         0x20

/*
 * arch/arm64/include/asm/processor.h:
 *
 * TASK_UNMAPPED_BASE     DEFAULT_MAP_WINDOW / 4
 * DEFAULT_MAP_WINDOW     DEFAULT_MAP_WINDOW_64
 * DEFAULT_MAP_WINDOW_64  UL(1) << VA_BITS_MIN
 * VA_BITS_MIN            48 (unless explicitly configured smaller)
 */
#define TASK_UNMAPPED_BASE      (1ull << (48 - 2))

/* arch/arm64/include/asm/elf.h */
#define ELF_ET_DYN_BASE         TARGET_PAGE_ALIGN((1ull << 48) / 3 * 2)

#include "../generic/target_mman.h"

#endif
