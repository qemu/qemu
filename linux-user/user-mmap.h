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

#if HOST_LONG_BITS == 64 && TARGET_ABI_BITS == 64
#ifdef TARGET_AARCH64
# define TASK_UNMAPPED_BASE  0x5500000000
#else
# define TASK_UNMAPPED_BASE  (1ul << 38)
#endif
#else
#ifdef TARGET_HPPA
# define TASK_UNMAPPED_BASE  0xfa000000
#else
# define TASK_UNMAPPED_BASE  0x40000000
#endif
#endif

/*
 * Guest parameters for the ADDR_COMPAT_LAYOUT personality
 * (at present this is the only layout supported by QEMU).
 *
 * TASK_UNMAPPED_BASE: For mmap without hint (addr != 0), the search
 * for unused virtual memory begins at TASK_UNMAPPED_BASE.
 *
 * task_unmapped_base: When the guest address space is limited via -R,
 * the value of TASK_UNMAPPED_BASE is adjusted to fit.
 */
extern abi_ulong task_unmapped_base;

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

#endif /* LINUX_USER_USER_MMAP_H */
