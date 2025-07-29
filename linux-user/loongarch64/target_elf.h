/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#ifndef LOONGARCH_TARGET_ELF_H
#define LOONGARCH_TARGET_ELF_H

#define HAVE_ELF_HWCAP          1
#define HAVE_ELF_PLATFORM       1
#define HAVE_ELF_CORE_DUMP      1

typedef abi_ulong target_elf_greg_t;

/* See linux kernel: arch/loongarch/include/asm/elf.h */
#define ELF_NREG                45
typedef struct target_elf_gregset_t {
    target_elf_greg_t regs[ELF_NREG];
} target_elf_gregset_t;

#endif
