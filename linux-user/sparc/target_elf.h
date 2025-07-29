/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef SPARC_TARGET_ELF_H
#define SPARC_TARGET_ELF_H

#ifndef TARGET_SPARC64
# define ELF_CLASS              ELFCLASS32
# define ELF_MACHINE            EM_SPARC
#elif defined(TARGET_ABI32)
# define ELF_CLASS              ELFCLASS32
# define elf_check_machine(x)   ((x) == EM_SPARC32PLUS || (x) == EM_SPARC)
#else
# define ELF_CLASS              ELFCLASS64
# define ELF_MACHINE            EM_SPARCV9
#endif

#define HAVE_ELF_HWCAP          1

#endif
