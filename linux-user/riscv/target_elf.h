/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef RISCV_TARGET_ELF_H
#define RISCV_TARGET_ELF_H

#define ELF_MACHINE             EM_RISCV

#ifdef TARGET_RISCV32
#define ELF_CLASS               ELFCLASS32
#define VDSO_HEADER             "vdso-32.c.inc"
#else
#define ELF_CLASS               ELFCLASS64
#define VDSO_HEADER             "vdso-64.c.inc"
#endif

#define HAVE_ELF_HWCAP          1

#endif
