/*
 *  memory management system call shims and definitions
 *
 *  Copyright (c) 2013-15 Stacey D. Son
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

/*
 * Copyright (c) 1982, 1986, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef BSD_USER_BSD_MEM_H
#define BSD_USER_BSD_MEM_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <fcntl.h>

#include "qemu-bsd.h"
#include "exec/mmap-lock.h"
#include "exec/page-protection.h"
#include "user/page-protection.h"

extern struct bsd_shm_regions bsd_shm_regions[];
extern abi_ulong target_brk;
extern abi_ulong initial_target_brk;

/* mmap(2) */
static inline abi_long do_bsd_mmap(void *cpu_env, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5, abi_long arg6, abi_long arg7,
    abi_long arg8)
{
    if (regpairs_aligned(cpu_env) != 0) {
        arg6 = arg7;
        arg7 = arg8;
    }
    return get_errno(target_mmap(arg1, arg2, arg3,
                                 target_to_host_bitmask(arg4, mmap_flags_tbl),
                                 arg5, target_arg64(arg6, arg7)));
}

/* munmap(2) */
static inline abi_long do_bsd_munmap(abi_long arg1, abi_long arg2)
{
    return get_errno(target_munmap(arg1, arg2));
}

/* mprotect(2) */
static inline abi_long do_bsd_mprotect(abi_long arg1, abi_long arg2,
        abi_long arg3)
{
    return get_errno(target_mprotect(arg1, arg2, arg3));
}

/* msync(2) */
static inline abi_long do_bsd_msync(abi_long addr, abi_long len, abi_long flags)
{
    if (!guest_range_valid_untagged(addr, len)) {
        /* It seems odd, but POSIX wants this to be ENOMEM */
        return -TARGET_ENOMEM;
    }

    return get_errno(msync(g2h_untagged(addr), len, flags));
}

/* mlock(2) */
static inline abi_long do_bsd_mlock(abi_long arg1, abi_long arg2)
{
    if (!guest_range_valid_untagged(arg1, arg2)) {
        return -TARGET_EINVAL;
    }
    return get_errno(mlock(g2h_untagged(arg1), arg2));
}

/* munlock(2) */
static inline abi_long do_bsd_munlock(abi_long arg1, abi_long arg2)
{
    if (!guest_range_valid_untagged(arg1, arg2)) {
        return -TARGET_EINVAL;
    }
    return get_errno(munlock(g2h_untagged(arg1), arg2));
}

/* mlockall(2) */
static inline abi_long do_bsd_mlockall(abi_long arg1)
{
    return get_errno(mlockall(arg1));
}

/* munlockall(2) */
static inline abi_long do_bsd_munlockall(void)
{
    return get_errno(munlockall());
}

/* madvise(2) */
static inline abi_long do_bsd_madvise(abi_long arg1, abi_long arg2,
        abi_long arg3)
{
    abi_ulong len;
    int ret = 0;
    abi_long start = arg1;
    abi_long len_in = arg2;
    abi_long advice = arg3;

    if (start & ~TARGET_PAGE_MASK) {
        return -TARGET_EINVAL;
    }
    if (len_in == 0) {
        return 0;
    }
    len = TARGET_PAGE_ALIGN(len_in);
    if (len == 0 || !guest_range_valid_untagged(start, len)) {
        return -TARGET_EINVAL;
    }

    /*
     * Most advice values are hints, so ignoring and returning success is ok.
     *
     * However, some advice values such as MADV_DONTNEED, are not hints and
     * need to be emulated.
     *
     * A straight passthrough for those may not be safe because qemu sometimes
     * turns private file-backed mappings into anonymous mappings.
     * If all guest pages have PAGE_PASSTHROUGH set, mappings have the
     * same semantics for the host as for the guest.
     *
     * MADV_DONTNEED is passed through, if possible.
     * If passthrough isn't possible, we nevertheless (wrongly!) return
     * success, which is broken but some userspace programs fail to work
     * otherwise. Completely implementing such emulation is quite complicated
     * though.
     */
    mmap_lock();
    switch (advice) {
    case MADV_DONTNEED:
        if (page_check_range(start, len, PAGE_PASSTHROUGH)) {
            ret = get_errno(madvise(g2h_untagged(start), len, advice));
            if (ret == 0) {
                page_reset_target_data(start, start + len - 1);
            }
        }
    }
    mmap_unlock();

    return ret;
}

/* minherit(2) */
static inline abi_long do_bsd_minherit(abi_long addr, abi_long len,
        abi_long inherit)
{
    return get_errno(minherit(g2h_untagged(addr), len, inherit));
}

/* mincore(2) */
static inline abi_long do_bsd_mincore(abi_ulong target_addr, abi_ulong len,
        abi_ulong target_vec)
{
    abi_long ret;
    void *p;
    abi_ulong vec_len = DIV_ROUND_UP(len, TARGET_PAGE_SIZE);

    if (!guest_range_valid_untagged(target_addr, len)
        || !page_check_range(target_addr, len, PAGE_VALID)) {
        return -TARGET_EFAULT;
    }

    p = lock_user(VERIFY_WRITE, target_vec, vec_len, 0);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(mincore(g2h_untagged(target_addr), len, p));
    unlock_user(p, target_vec, vec_len);

    return ret;
}

/* do_brk() must return target values and target errnos. */
static inline abi_long do_obreak(abi_ulong brk_val)
{
    abi_long mapped_addr;
    abi_ulong new_brk;
    abi_ulong old_brk;

    /* brk pointers are always untagged */

    /* do not allow to shrink below initial brk value */
    if (brk_val < initial_target_brk) {
        return target_brk;
    }

    new_brk = TARGET_PAGE_ALIGN(brk_val);
    old_brk = TARGET_PAGE_ALIGN(target_brk);

    /* new and old target_brk might be on the same page */
    if (new_brk == old_brk) {
        target_brk = brk_val;
        return target_brk;
    }

    /* Release heap if necessary */
    if (new_brk < old_brk) {
        target_munmap(new_brk, old_brk - new_brk);

        target_brk = brk_val;
        return target_brk;
    }

    mapped_addr = target_mmap(old_brk, new_brk - old_brk,
                              PROT_READ | PROT_WRITE,
                              MAP_FIXED | MAP_EXCL | MAP_ANON | MAP_PRIVATE,
                              -1, 0);

    if (mapped_addr == old_brk) {
        target_brk = brk_val;
        return target_brk;
    }

    /* For everything else, return the previous break. */
    return target_brk;
}

/* shm_open(2) */
static inline abi_long do_bsd_shm_open(abi_ulong arg1, abi_long arg2,
        abi_long arg3)
{
    int ret;
    void *p;

    if (arg1 == (uintptr_t)SHM_ANON) {
        p = SHM_ANON;
    } else {
        p = lock_user_string(arg1);
        if (p == NULL) {
            return -TARGET_EFAULT;
        }
    }
    ret = get_errno(shm_open(p, target_to_host_bitmask(arg2, fcntl_flags_tbl),
                             arg3));

    if (p != SHM_ANON) {
        unlock_user(p, arg1, 0);
    }

    return ret;
}

/* shm_unlink(2) */
static inline abi_long do_bsd_shm_unlink(abi_ulong arg1)
{
    int ret;
    void *p;

    p = lock_user_string(arg1);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(shm_unlink(p)); /* XXX path(p)? */
    unlock_user(p, arg1, 0);

    return ret;
}

/* shmget(2) */
static inline abi_long do_bsd_shmget(abi_long arg1, abi_ulong arg2,
        abi_long arg3)
{
    return get_errno(shmget(arg1, arg2, arg3));
}

/* shmctl(2) */
static inline abi_long do_bsd_shmctl(abi_long shmid, abi_long cmd,
        abi_ulong buff)
{
    struct shmid_ds dsarg;
    abi_long ret = -TARGET_EINVAL;

    cmd &= 0xff;

    switch (cmd) {
    case IPC_STAT:
        if (target_to_host_shmid_ds(&dsarg, buff)) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(shmctl(shmid, cmd, &dsarg));
        if (host_to_target_shmid_ds(buff, &dsarg)) {
            return -TARGET_EFAULT;
        }
        break;

    case IPC_SET:
        if (target_to_host_shmid_ds(&dsarg, buff)) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(shmctl(shmid, cmd, &dsarg));
        break;

    case IPC_RMID:
        ret = get_errno(shmctl(shmid, cmd, NULL));
        break;

    default:
        ret = -TARGET_EINVAL;
        break;
    }

    return ret;
}

/* shmat(2) */
static inline abi_long do_bsd_shmat(int shmid, abi_ulong shmaddr, int shmflg)
{
    abi_ulong raddr;
    abi_long ret;
    struct shmid_ds shm_info;

    /* Find out the length of the shared memory segment. */
    ret = get_errno(shmctl(shmid, IPC_STAT, &shm_info));
    if (is_error(ret)) {
        /* Can't get the length */
        return ret;
    }

    if (!guest_range_valid_untagged(shmaddr, shm_info.shm_segsz)) {
        return -TARGET_EINVAL;
    }

    WITH_MMAP_LOCK_GUARD() {
        void *host_raddr;

        if (shmaddr) {
            host_raddr = shmat(shmid, (void *)g2h_untagged(shmaddr), shmflg);
        } else {
            abi_ulong alignment;
            abi_ulong mmap_start;

            alignment = 0; /* alignment above page size not required */
            mmap_start = mmap_find_vma(0, shm_info.shm_segsz, alignment);

            if (mmap_start == -1) {
                return -TARGET_ENOMEM;
            }
            host_raddr = shmat(shmid, g2h_untagged(mmap_start),
                               shmflg | SHM_REMAP);
        }

        if (host_raddr == (void *)-1) {
            return get_errno(-1);
        }
        raddr = h2g(host_raddr);

        page_set_flags(raddr, raddr + shm_info.shm_segsz - 1,
                       PAGE_VALID | PAGE_RESET | PAGE_READ |
                       (shmflg & SHM_RDONLY ? 0 : PAGE_WRITE));

        for (int i = 0; i < N_BSD_SHM_REGIONS; i++) {
            if (bsd_shm_regions[i].start == 0) {
                bsd_shm_regions[i].start = raddr;
                bsd_shm_regions[i].size = shm_info.shm_segsz;
                break;
            }
        }
    }

    return raddr;
}

/* shmdt(2) */
static inline abi_long do_bsd_shmdt(abi_ulong shmaddr)
{
    abi_long ret;

    WITH_MMAP_LOCK_GUARD() {
        int i;

        for (i = 0; i < N_BSD_SHM_REGIONS; ++i) {
            if (bsd_shm_regions[i].start == shmaddr) {
                break;
            }
        }

        if (i == N_BSD_SHM_REGIONS) {
            return -TARGET_EINVAL;
        }

        ret = get_errno(shmdt(g2h_untagged(shmaddr)));
        if (ret == 0) {
            abi_ulong size = bsd_shm_regions[i].size;

            bsd_shm_regions[i].start = 0;
            page_set_flags(shmaddr, shmaddr + size - 1, 0);
            mmap_reserve(shmaddr, size);
        }
    }

    return ret;
}

static inline abi_long do_bsd_vadvise(void)
{
    /* See sys_ovadvise() in vm_unix.c */
    return -TARGET_EINVAL;
}

static inline abi_long do_bsd_sbrk(void)
{
    /* see sys_sbrk() in vm_mmap.c */
    return -TARGET_EOPNOTSUPP;
}

static inline abi_long do_bsd_sstk(void)
{
    /* see sys_sstk() in vm_mmap.c */
    return -TARGET_EOPNOTSUPP;
}

#endif /* BSD_USER_BSD_MEM_H */
