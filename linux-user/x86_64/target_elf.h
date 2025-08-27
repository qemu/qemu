/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef X86_64_TARGET_ELF_H
#define X86_64_TARGET_ELF_H

#define HAVE_ELF_HWCAP          1
#define HAVE_ELF_PLATFORM       1
#define HAVE_ELF_CORE_DUMP      1

/*
 * Note that ELF_NREG should be 29 as there should be place for
 * TRAPNO and ERR "registers" as well but linux doesn't dump those.
 *
 * See linux kernel: arch/x86/include/asm/elf.h
 */
#define ELF_NREG                27

typedef abi_ulong target_elf_greg_t;

typedef struct target_elf_gregset_t {
    target_elf_greg_t regs[ELF_NREG];
} target_elf_gregset_t;

#endif
