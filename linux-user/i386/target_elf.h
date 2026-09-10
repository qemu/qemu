/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef I386_TARGET_ELF_H
#define I386_TARGET_ELF_H

#include "target_ptrace.h"

#define ELF_CLASS               ELFCLASS32
#define ELF_MACHINE             EM_386
#define EXSTACK_DEFAULT         true
#define VDSO_HEADER             "vdso.c.inc"

#define HAVE_ELF_HWCAP          1
#define HAVE_ELF_PLATFORM       1
#define HAVE_ELF_CORE_DUMP      1

/*
 * See linux kernel: arch/x86/include/asm/elf.h, where elf_gregset_t
 * is mapped to struct user_regs_struct via sizeof.
 */
typedef struct target_elf_gregset_t {
    struct target_user_regs_struct pt;
} target_elf_gregset_t;

/*
 * Matches the kernel's elf_fpregset_t, i.e. struct user_i387_struct from
 * arch/x86/include/asm/user_32.h.  This is the legacy FSAVE image without
 * the trailing software status word, which FSAVE does not write either.
 */
#define HAVE_ELF_CORE_FPREGS    1

typedef struct target_elf_fpregset_t {
    uint32_t cwd;             /* FPU control word                 */
    uint32_t swd;             /* FPU status word                  */
    uint32_t twd;             /* FPU tag word                     */
    uint32_t fip;             /* FPU IP offset                    */
    uint32_t fcs;             /* FPU IP selector                  */
    uint32_t foo;             /* FPU operand pointer offset       */
    uint32_t fos;             /* FPU operand pointer selector     */
    uint32_t st_space[20];    /* 8 * 10 bytes for st0-st7         */
} target_elf_fpregset_t;

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_machine(x)    ((x) == EM_386 || (x) == EM_486)

/*
 * i386 is the only target which supplies AT_SYSINFO for the vdso.
 * All others only supply AT_SYSINFO_EHDR.
 */
#define DLINFO_ARCH_ITEMS (vdso_info != NULL)
#define ARCH_DLINFO                                     \
    do {                                                \
        if (vdso_info) {                                \
            NEW_AUX_ENT(AT_SYSINFO, vdso_info->entry);  \
        }                                               \
    } while (0)

#endif
