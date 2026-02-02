/*
 * miscellaneous BSD system call shims
 *
 * Copyright (c) 2013 Stacey D. Son
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BSD_MISC_H
#define BSD_MISC_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/uuid.h>

#include "qemu-bsd.h"

/* quotactl(2) */
static inline abi_long do_bsd_quotactl(abi_ulong path, abi_long cmd,
        __unused abi_ulong target_addr)
{
    qemu_log("qemu: Unsupported syscall quotactl()\n");
    return -TARGET_ENOSYS;
}

/* reboot(2) */
static inline abi_long do_bsd_reboot(abi_long how)
{
    qemu_log("qemu: Unsupported syscall reboot()\n");
    return -TARGET_ENOSYS;
}

/* uuidgen(2) */
static inline abi_long do_bsd_uuidgen(abi_ulong target_addr, int count)
{
    int i;
    abi_long ret;
    g_autofree struct uuid *host_uuid = NULL;

    /*
     * 2048 is the kernel limit, but there's no #define for it, nor any sysctl
     * to query it.
     */
    if (count < 1 || count > 2048) {
        return -TARGET_EINVAL;
    }

    host_uuid = g_malloc(count * sizeof(struct uuid));

    ret = get_errno(uuidgen(host_uuid, count));
    if (is_error(ret)) {
        goto out;
    }
    for (i = 0; i < count; i++) {
        ret = host_to_target_uuid(target_addr +
            (abi_ulong)(sizeof(struct target_uuid) * i), &host_uuid[i]);
        if (is_error(ret)) {
            break;
        }
    }

out:
    return ret;
}

/*
 * System V Semaphores
 */

/* semget(2) */
static inline abi_long do_bsd_semget(abi_long key, int nsems,
        int target_flags)
{
    return get_errno(semget(key, nsems,
                target_to_host_bitmask(target_flags, ipc_flags_tbl)));
}

/* semop(2) */
static inline abi_long do_bsd_semop(int semid, abi_long ptr, unsigned nsops)
{
    g_autofree struct sembuf *sops = g_malloc(nsops * sizeof(struct sembuf));
    struct target_sembuf *target_sembuf;
    int i;

    target_sembuf = lock_user(VERIFY_READ, ptr,
            nsops * sizeof(struct target_sembuf), 1);
    if (target_sembuf == NULL) {
        return -TARGET_EFAULT;
    }
    for (i = 0; i < nsops; i++) {
        __get_user(sops[i].sem_num, &target_sembuf[i].sem_num);
        __get_user(sops[i].sem_op, &target_sembuf[i].sem_op);
        __get_user(sops[i].sem_flg, &target_sembuf[i].sem_flg);
    }
    unlock_user(target_sembuf, ptr, 0);

    return semop(semid, sops, nsops);
}

/* getdtablesize(2) */
static inline abi_long do_bsd_getdtablesize(void)
{
    return get_errno(getdtablesize());
}

#endif /* BSD_MISC_H */
