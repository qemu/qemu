/*
 *  RISC-V ELF definitions
 *
 *  Copyright (c) 2019 Mark Corbin
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

#ifndef TARGET_ARCH_ELF_H
#define TARGET_ARCH_ELF_H

#define elf_check_arch(x) ((x) == EM_RISCV)
#define ELF_START_MMAP 0x80000000
#define ELF_ET_DYN_LOAD_ADDR    0x100000
#define ELF_CLASS   ELFCLASS64

#define ELF_DATA    ELFDATA2LSB
#define ELF_ARCH    EM_RISCV

#define ELF_HWCAP get_elf_hwcap()
static uint32_t get_elf_hwcap(void)
{
    RISCVCPU *cpu = RISCV_CPU(thread_cpu);

    return cpu->env.misa_ext_mask;
}

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE        4096

#endif /* TARGET_ARCH_ELF_H */
