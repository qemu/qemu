/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef ARM_TARGET_ELF_H
#define ARM_TARGET_ELF_H

#include "target_ptrace.h"

#define ELF_MACHINE             EM_ARM
#define ELF_CLASS               ELFCLASS32
#define EXSTACK_DEFAULT         true

#define HAVE_ELF_HWCAP          1
#define HAVE_ELF_HWCAP2         1
#define HAVE_ELF_PLATFORM       1
#define HAVE_ELF_CORE_DUMP      1
#define HAVE_VDSO_IMAGE_INFO    1

#define HI_COMMPAGE             ((intptr_t)0xffff0f00u)

/*
 * See linux kernel: arch/arm/include/asm/elf.h, where
 * elf_gregset_t is mapped to struct pt_regs via sizeof.
 */
typedef struct target_elf_gregset_t {
    struct target_pt_regs pt;
} target_elf_gregset_t;

#endif
