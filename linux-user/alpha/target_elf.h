/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef ALPHA_TARGET_ELF_H
#define ALPHA_TARGET_ELF_H

#define ELF_CLASS               ELFCLASS64
#define ELF_MACHINE             EM_ALPHA

#define HAVE_ELF_CORE_DUMP      1
#define HAVE_ELF_HWCAP          1

/*
 * Matches the kernel's elf_gregset_t (ELF_NGREG = 33):
 *   r0-r30 at indices 0-30, pc at 31, ps at 32.
 * r31 (hardwired zero) is not stored; pc occupies index 31.
 */
/*
 * The floating-point note holds $f0 through $f30 and then the control
 * register in the slot $f31 would occupy; $f31 reads as zero.
 */
#define HAVE_ELF_CORE_FPREGS    1

typedef struct target_elf_fpregset_t {
    uint64_t fpr[31];    /* $f0-$f30 */
    uint64_t fpcr;       /* the slot for $f31 */
} target_elf_fpregset_t;

typedef struct target_elf_gregset_t {
    abi_ulong regs[31];  /* integer registers r0-r30  [0..30] */
    abi_ulong pc;        /* program counter           [31]    */
    abi_ulong unique;    /* thread's UNIQUE field     [32]    */
} target_elf_gregset_t;

#endif
