/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#ifndef LOONGARCH_TARGET_ELF_H
#define LOONGARCH_TARGET_ELF_H

#include "target_ptrace.h"

#define ELF_CLASS               ELFCLASS64
#define ELF_MACHINE             EM_LOONGARCH
#define EXSTACK_DEFAULT         true
#define VDSO_HEADER             "vdso.c.inc"

#define HAVE_ELF_HWCAP          1
#define HAVE_ELF_PLATFORM       1
#define HAVE_ELF_CORE_DUMP      1

/* See linux kernel: arch/loongarch/include/asm/elf.h */
typedef struct target_elf_gregset_t {
    struct target_user_pt_regs pt;
} target_elf_gregset_t;

#endif
