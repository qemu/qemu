/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef M68K_TARGET_ELF_H
#define M68K_TARGET_ELF_H

#define ELF_CLASS               ELFCLASS32
#define ELF_MACHINE             EM_68K

#define HAVE_ELF_CORE_DUMP      1

/*
 * See linux kernel: arch/m68k/include/asm/elf.h, where
 * elf_gregset_t is mapped to struct user_regs_struct via sizeof.
 *
 * Note that user_regs_struct has
 *    short stkadj, sr;
 *    ...
 *    short fmtvec, __fill;
 * but ELF_CORE_COPY_REGS writes to unsigned longs.
 * Therefore adjust the sr and fmtvec fields to match.
 */
typedef struct target_elf_gregset_t {
    abi_ulong d1, d2, d3, d4, d5, d6, d7;
    abi_ulong a0, a1, a2, a3, a4, a5, a6;
    abi_ulong d0;
    abi_ulong usp;
    abi_ulong orig_d0;
    abi_ulong sr;
    abi_ulong pc;
    abi_ulong fmtvec;
} target_elf_gregset_t;

#endif
