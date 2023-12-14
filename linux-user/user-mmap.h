/*
 * user-mmap.h: prototypes for linux-user guest binary loader
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

#ifndef LINUX_USER_USER_MMAP_H
#define LINUX_USER_USER_MMAP_H

/*
 * Guest parameters for the ADDR_COMPAT_LAYOUT personality
 * (at present this is the only layout supported by QEMU).
 *
 * TASK_UNMAPPED_BASE: For mmap without hint (addr != 0), the search
 * for unused virtual memory begins at TASK_UNMAPPED_BASE.
 *
 * ELF_ET_DYN_BASE: When the executable is ET_DYN (i.e. PIE), and requires
 * an interpreter (i.e. not -static-pie), use ELF_ET_DYN_BASE instead of
 * TASK_UNMAPPED_BASE for selecting the address of the executable.
 * This provides some distance between the executable and the interpreter,
 * which allows the initial brk to be placed immediately after the
 * executable and also have room to grow.
 *
 * task_unmapped_base, elf_et_dyn_base: When the guest address space is
 * limited via -R, the values of TASK_UNMAPPED_BASE and ELF_ET_DYN_BASE
 * must be adjusted to fit.
 */
extern abi_ulong task_unmapped_base;
extern abi_ulong elf_et_dyn_base;

/*
 * mmap_next_start: The base address for the next mmap without hint,
 * increased after each successful map, starting at task_unmapped_base.
 * This is an optimization within QEMU and not part of ADDR_COMPAT_LAYOUT.
 */
extern abi_ulong mmap_next_start;

int target_mprotect(abi_ulong start, abi_ulong len, int prot);
abi_long target_mmap(abi_ulong start, abi_ulong len, int prot,
                     int flags, int fd, off_t offset);
int target_munmap(abi_ulong start, abi_ulong len);
abi_long target_mremap(abi_ulong old_addr, abi_ulong old_size,
                       abi_ulong new_size, unsigned long flags,
                       abi_ulong new_addr);
abi_long target_madvise(abi_ulong start, abi_ulong len_in, int advice);
abi_ulong mmap_find_vma(abi_ulong, abi_ulong, abi_ulong);
void mmap_fork_start(void);
void mmap_fork_end(int child);

abi_ulong target_shmat(CPUArchState *cpu_env, int shmid,
                       abi_ulong shmaddr, int shmflg);
abi_long target_shmdt(abi_ulong shmaddr);

#endif /* LINUX_USER_USER_MMAP_H */
