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

#define LO_COMMPAGE             0
#define STACK_GROWS_DOWN        0
#define STACK_ALIGNMENT         64
#define VDSO_HEADER             "vdso.c.inc"

#endif
