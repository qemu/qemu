/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef PPC_TARGET_ELF_H
#define PPC_TARGET_ELF_H

#include "target_ptrace.h"

#define HAVE_ELF_HWCAP          1
#define HAVE_ELF_HWCAP2         1
#define HAVE_ELF_CORE_DUMP      1

/*
 * The size of 48 words is set in arch/powerpc/include/uapi/asm/elf.h.
 * However PPC_ELF_CORE_COPY_REGS in arch/powerpc/include/asm/elf.h
 * open-codes a memcpy from struct pt_regs, then zeros the rest.
 */
typedef struct target_elf_gregset_t {
    union {
        struct target_pt_regs pt;
        abi_ulong reserved[48];
    };
} target_elf_gregset_t;

#endif
