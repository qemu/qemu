/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef MICROBLAZE_TARGET_ELF_H
#define MICROBLAZE_TARGET_ELF_H

#define HAVE_ELF_CORE_DUMP      1

typedef abi_ulong target_elf_greg_t;

#define ELF_NREG                38
typedef struct target_elf_gregset_t {
    target_elf_greg_t regs[ELF_NREG];
} target_elf_gregset_t;

#endif
