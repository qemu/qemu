/*
 *  x86_64 ELF definitions
 *
 *  Copyright (c) 2013 Stacey D. Son
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _TARGET_ARCH_ELF_H_
#define _TARGET_ARCH_ELF_H_

#define ELF_PLATFORM get_elf_platform()

static const char *get_elf_platform(void)
{
    static char elf_platform[] = "i386";
    int family = object_property_get_int(OBJECT(thread_cpu), "family", NULL);
    if (family > 6) {
        family = 6;
    }
    if (family >= 3) {
        elf_platform[1] = '0' + family;
    }
    return elf_platform;
}

#define ELF_HWCAP get_elf_hwcap()

static uint32_t get_elf_hwcap(void)
{
    X86CPU *cpu = X86_CPU(thread_cpu);

    return cpu->env.features[FEAT_1_EDX];
}

#define ELF_START_MMAP 0x2aaaaab000ULL
#define elf_check_arch(x) (((x) == ELF_ARCH))

#define ELF_CLASS      ELFCLASS64
#define ELF_DATA       ELFDATA2LSB
#define ELF_ARCH       EM_X86_64

static inline void init_thread(struct target_pt_regs *regs,
                               struct image_info *infop)
{
    regs->rax = 0;
    regs->rsp = infop->start_stack;
    regs->rip = infop->entry;
    if (bsd_type == target_freebsd) {
        regs->rdi = infop->start_stack;
    }
}

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE       4096

#endif /* _TARGET_ARCH_ELF_H_ */
