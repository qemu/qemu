/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef HPPA_TARGET_ELF_H
#define HPPA_TARGET_ELF_H

#define ELF_CLASS               ELFCLASS32
#define ELF_MACHINE             EM_PARISC

#define HAVE_ELF_PLATFORM       1
#define HAVE_ELF_CORE_DUMP      1

/*
 * Matches struct user_regs_struct from arch/parisc/include/uapi/asm/ptrace.h.
 * ELF_NGREG = 80; register indices match those used by libunwind and gdb.
 */
typedef struct target_elf_gregset_t {
    abi_ulong gr[32];      /* gr[0..31]; PSW in gr[0]            [0..31] */
    abi_ulong sr[8];       /* space registers                    [32..39] */
    abi_ulong iaoq[2];     /* instruction address offset         [40..41] */
    abi_ulong iasq[2];     /* instruction address space          [42..43] */
    abi_ulong sar;         /* shift amount register (CR11)       [44] */
    abi_ulong iir;         /* interrupt instruction register     [45] */
    abi_ulong isr;         /* interrupt space register           [46] */
    abi_ulong ior;         /* interrupt offset register          [47] */
    abi_ulong ipsw;        /* interrupt PSW (CR22)               [48] */
    abi_ulong cr0;         /* recovery counter                   [49] */
    abi_ulong cr24_31[8];  /* cr24..cr31                         [50..57] */
    abi_ulong cr8_15[6];   /* cr8, cr9, cr12, cr13, cr10, cr15  [58..63] */
    abi_ulong pad[16];     /* pad to 80 elements                 [64..79] */
} target_elf_gregset_t;

#define LO_COMMPAGE             0
#define STACK_GROWS_DOWN        0
#define STACK_ALIGNMENT         64
#define VDSO_HEADER             "vdso.c.inc"

#endif
