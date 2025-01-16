/*
 * MMAP declarations for QEMU user emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef USER_MMAP_H
#define USER_MMAP_H

#include "user/abitypes.h"

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

abi_ulong mmap_find_vma(abi_ulong start, abi_ulong size, abi_ulong alignment);

void TSA_NO_TSA mmap_fork_start(void);
void TSA_NO_TSA mmap_fork_end(int child);

#endif
