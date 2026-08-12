/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef MIPS_TARGET_ELF_H
#define MIPS_TARGET_ELF_H

#include "target_ptrace.h"

#define ELF_CLASS               ELFCLASS32
#define ELF_MACHINE             EM_MIPS
#define EXSTACK_DEFAULT         true

#define HAVE_ELF_HWCAP          1
#define HAVE_ELF_BASE_PLATFORM  1
#define HAVE_ELF_CORE_DUMP      1

/* See linux kernel: arch/mips/include/asm/elf.h.  */
typedef struct target_elf_gregset_t {
    union {
        abi_ulong reserved[45];
        struct target_pt_regs pt;
    };
} target_elf_gregset_t;

#define HAVE_ELF_CORE_FPREGS    1

/*
 * Matches the kernel's elf_fpregset_t (ELF_NFPREG = 33):
 *   fpr[0..31] hold f0-f31; fcsr occupies the low 32 bits of slot 32.
 *   pad rounds the struct to 33 × 8 bytes = 264 bytes.
 */
typedef struct target_elf_fpregset_t {
    uint64_t fpr[32];
    uint32_t fcsr;
    uint32_t pad;
} target_elf_fpregset_t;

#endif
