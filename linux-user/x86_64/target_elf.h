/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef X86_64_TARGET_ELF_H
#define X86_64_TARGET_ELF_H

#include "target_ptrace.h"

#define ELF_CLASS               ELFCLASS64
#define ELF_MACHINE             EM_X86_64
#define VDSO_HEADER             "vdso.c.inc"

#define HAVE_ELF_HWCAP          1
#define HAVE_ELF_PLATFORM       1
#define HAVE_ELF_CORE_DUMP      1
#define HAVE_GUEST_COMMPAGE     1

/*
 * See linux kernel: arch/x86/include/asm/elf.h, where
 * elf_gregset_t is mapped to struct user_regs_struct via sizeof.
 */
typedef struct target_elf_gregset_t {
    struct target_user_regs_struct pt;
} target_elf_gregset_t;

#endif
