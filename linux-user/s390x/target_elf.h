/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef S390X_TARGET_ELF_H
#define S390X_TARGET_ELF_H

#define HAVE_ELF_HWCAP          1
#define HAVE_ELF_CORE_DUMP      1

typedef abi_ulong target_elf_greg_t;

/* See linux kernel: arch/s390/include/uapi/asm/ptrace.h (s390_regs).  */
#define ELF_NREG                27
typedef struct target_elf_gregset_t {
    target_elf_greg_t regs[ELF_NREG];
} target_elf_gregset_t;

#endif
