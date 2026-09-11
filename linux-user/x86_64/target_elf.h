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

/*
 * Matches the kernel's elf_fpregset_t, i.e. struct user_i387_struct from
 * arch/x86/include/asm/user_64.h, which is the 512 byte FXSAVE image.
 */
#define HAVE_ELF_CORE_FPREGS    1

typedef struct target_elf_fpregset_t {
    uint16_t cwd;             /* FPU control word                   */
    uint16_t swd;             /* FPU status word                    */
    uint16_t twd;             /* abridged tag word, not the x87 one */
    uint16_t fop;             /* last instruction opcode            */
    uint64_t rip;             /* instruction pointer                */
    uint64_t rdp;             /* data pointer                       */
    uint32_t mxcsr;
    uint32_t mxcsr_mask;
    uint32_t st_space[32];    /*  8 * 16 bytes for st0-st7          */
    uint32_t xmm_space[64];   /* 16 * 16 bytes for xmm0-xmm15       */
    uint32_t padding[24];
} target_elf_fpregset_t;

#endif
