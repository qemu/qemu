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

/*
 * Matches the kernel's elf_gregset_t (ELF_NGREG = 33):
 *   r0-r30 at indices 0-30, pc at 31, ps at 32.
 * r31 (hardwired zero) is not stored; pc occupies index 31.
 */
typedef struct target_elf_gregset_t {
    abi_ulong regs[31];  /* integer registers r0-r30  [0..30] */
    abi_ulong pc;        /* program counter           [31]    */
    abi_ulong unique;    /* thread's UNIQUE field     [32]    */
} target_elf_gregset_t;

#endif
