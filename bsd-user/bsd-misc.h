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

/* __semctl(2) */
static inline abi_long do_bsd___semctl(int semid, int semnum, int target_cmd,
                                       abi_ptr un_ptr)
{
    void *target_un;
    union semun arg;
    struct semid_ds dsarg;
    unsigned short *array = NULL;
    int host_cmd;
    abi_long ret = 0;
    abi_ulong target_array, target_buffer;

    switch (target_cmd) {
    case TARGET_GETVAL:
        host_cmd = GETVAL;
        break;

    case TARGET_SETVAL:
        host_cmd = SETVAL;
        break;

    case TARGET_GETALL:
        host_cmd = GETALL;
        break;

    case TARGET_SETALL:
        host_cmd = SETALL;
        break;

    case TARGET_IPC_STAT:
        host_cmd = IPC_STAT;
        break;

    case TARGET_IPC_SET:
        host_cmd = IPC_SET;
        break;

    case TARGET_IPC_RMID:
        host_cmd = IPC_RMID;
        break;

    case TARGET_GETPID:
        host_cmd = GETPID;
        break;

    case TARGET_GETNCNT:
        host_cmd = GETNCNT;
        break;

    case TARGET_GETZCNT:
        host_cmd = GETZCNT;
        break;

    default:
        return -TARGET_EINVAL;
    }

    /*
     * Unlike Linux and the semctl system call, we take a pointer
     * to the union arg here.
     */
    target_un = lock_user(VERIFY_READ, un_ptr, sizeof(union target_semun), 1);

    switch (host_cmd) {
    case GETVAL:
    case SETVAL:
        __get_user(arg.val, (abi_int *)target_un);
        ret = get_errno(semctl(semid, semnum, host_cmd, arg));
        break;

    case GETALL:
    case SETALL:
        __get_user(target_array, (abi_ulong *)target_un);
        ret = target_to_host_semarray(semid, &array, target_array);
        if (is_error(ret)) {
            goto out;
        }
        arg.array = array;
        ret = get_errno(semctl(semid, semnum, host_cmd, arg));
        if (!is_error(ret)) {
            ret = host_to_target_semarray(semid, target_array, &array);
        }
        break;

    case IPC_STAT:
    case IPC_SET:
        __get_user(target_buffer, (abi_ulong *)target_un);
        ret = target_to_host_semid_ds(&dsarg, target_buffer);
        if (is_error(ret)) {
            goto out;
        }
        arg.buf = &dsarg;
        ret = get_errno(semctl(semid, semnum, host_cmd, arg));
        if (!is_error(ret)) {
            ret = host_to_target_semid_ds(target_buffer, &dsarg);
        }
        break;

    case IPC_RMID:
    case GETPID:
    case GETNCNT:
    case GETZCNT:
        ret = get_errno(semctl(semid, semnum, host_cmd, NULL));
        break;

    default:
        ret = -TARGET_EINVAL;
        break;
    }
out:
    unlock_user(target_un, un_ptr, 1);
    return ret;
}

/* msgctl(2) */
static inline abi_long do_bsd_msgctl(int msgid, int target_cmd, abi_long ptr)
{
    struct msqid_ds dsarg;
    abi_long ret = -TARGET_EINVAL;
    int host_cmd;

    switch (target_cmd) {
    case TARGET_IPC_STAT:
        host_cmd = IPC_STAT;
        break;

    case TARGET_IPC_SET:
        host_cmd = IPC_SET;
        break;

    case TARGET_IPC_RMID:
        host_cmd = IPC_RMID;
        break;

    default:
        return -TARGET_EINVAL;
    }

    switch (host_cmd) {
    case IPC_STAT:
    case IPC_SET:
        if (target_to_host_msqid_ds(&dsarg, ptr)) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(msgctl(msgid, host_cmd, &dsarg));
        if (host_to_target_msqid_ds(ptr, &dsarg)) {
            return -TARGET_EFAULT;
        }
        break;

    case IPC_RMID:
        ret = get_errno(msgctl(msgid, host_cmd, NULL));
        break;

    default:
        ret = -TARGET_EINVAL;
        break;
    }
    return ret;
}

/* getdtablesize(2) */
static inline abi_long do_bsd_getdtablesize(void)
{
    return get_errno(getdtablesize());
}

#endif /* BSD_MISC_H */
