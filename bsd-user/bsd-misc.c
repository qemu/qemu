/*
 * BSD misc system call conversions routines
 *
 * Copyright (c) 2013 Stacey D. Son
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"

#define _WANT_SEMUN
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/uuid.h>

#include "qemu.h"
#include "qemu-bsd.h"

/*
 * BSD uuidgen(2) struct uuid conversion
 */
abi_long host_to_target_uuid(abi_ulong target_addr, struct uuid *host_uuid)
{
    struct target_uuid *target_uuid;

    if (!lock_user_struct(VERIFY_WRITE, target_uuid, target_addr, 0)) {
        return -TARGET_EFAULT;
    }
    __put_user(host_uuid->time_low, &target_uuid->time_low);
    __put_user(host_uuid->time_mid, &target_uuid->time_mid);
    __put_user(host_uuid->time_hi_and_version,
        &target_uuid->time_hi_and_version);
    host_uuid->clock_seq_hi_and_reserved =
        target_uuid->clock_seq_hi_and_reserved;
    host_uuid->clock_seq_low = target_uuid->clock_seq_low;
    memcpy(host_uuid->node, target_uuid->node, TARGET_UUID_NODE_LEN);
    unlock_user_struct(target_uuid, target_addr, 1);
    return 0;
}

abi_long target_to_host_semarray(int semid, unsigned short **host_array,
        abi_ulong target_addr)
{
    abi_long ret;
    int nsems, i;
    unsigned short *array;
    union semun semun;
    struct semid_ds semid_ds;

    semun.buf = &semid_ds;
    ret = semctl(semid, 0, IPC_STAT, semun);
    if (ret == -1) {
        return get_errno(ret);
    }
    nsems = semid_ds.sem_nsems;
    *host_array = g_new(unsigned short, nsems);
    array = lock_user(VERIFY_READ, target_addr,
        nsems * sizeof(unsigned short), 1);
    if (array == NULL) {
        free(*host_array);
        return -TARGET_EFAULT;
    }
    for (i = 0; i < nsems; i++) {
        __get_user((*host_array)[i], array + i);
    }
    unlock_user(array, target_addr, 0);

    return 0;
}

abi_long host_to_target_semarray(int semid, abi_ulong target_addr,
        unsigned short **host_arrayp)
{
    g_autofree unsigned short *host_array = *host_arrayp;
    abi_long ret;
    int nsems, i;
    unsigned short *array;
    union semun semun;
    struct semid_ds semid_ds;

    semun.buf = &semid_ds;

    ret = semctl(semid, 0, IPC_STAT, semun);
    if (ret == -1) {
        return get_errno(ret);
    }

    nsems = semid_ds.sem_nsems;
    array = (unsigned short *)lock_user(VERIFY_WRITE, target_addr,
        nsems * sizeof(unsigned short), 0);
    if (array == NULL) {
        return -TARGET_EFAULT;
    }
    for (i = 0; i < nsems; i++) {
        __put_user(array[i], host_array + i);
    }
    unlock_user(array, target_addr, 1);
    return 0;
}

abi_long target_to_host_semid_ds(struct semid_ds *host_sd,
        abi_ulong target_addr)
{
    struct target_semid_ds *target_sd;

    if (!lock_user_struct(VERIFY_READ, target_sd, target_addr, 1)) {
        return -TARGET_EFAULT;
    }
    target_to_host_ipc_perm__locked(&host_sd->sem_perm, &target_sd->sem_perm);
    /* sem_base is not used by kernel for IPC_STAT/IPC_SET */
    /* host_sd->sem_base  = g2h_untagged(target_sd->sem_base); */
    __get_user(host_sd->sem_nsems, &target_sd->sem_nsems);
    __get_user(host_sd->sem_otime, &target_sd->sem_otime);
    __get_user(host_sd->sem_ctime, &target_sd->sem_ctime);
    unlock_user_struct(target_sd, target_addr, 0);
    return 0;
}

abi_long host_to_target_semid_ds(abi_ulong target_addr,
        struct semid_ds *host_sd)
{
    struct target_semid_ds *target_sd;

    if (!lock_user_struct(VERIFY_WRITE, target_sd, target_addr, 0)) {
        return -TARGET_EFAULT;
    }
    host_to_target_ipc_perm__locked(&target_sd->sem_perm,
                                    &host_sd->sem_perm);
    /* sem_base is not used by kernel for IPC_STAT/IPC_SET */
    /* target_sd->sem_base = h2g((void *)host_sd->sem_base); */
    __put_user(target_sd->sem_nsems, &host_sd->sem_nsems);
    __put_user(target_sd->sem_otime, &host_sd->sem_otime);
    __put_user(target_sd->sem_ctime, &host_sd->sem_ctime);
    unlock_user_struct(target_sd, target_addr, 1);

    return 0;
}

abi_long target_to_host_msqid_ds(struct msqid_ds *host_md,
        abi_ulong target_addr)
{
    struct target_msqid_ds *target_md;

    if (!lock_user_struct(VERIFY_READ, target_md, target_addr, 1)) {
        return -TARGET_EFAULT;
    }

    memset(host_md, 0, sizeof(struct msqid_ds));
    target_to_host_ipc_perm__locked(&host_md->msg_perm,
                                    &target_md->msg_perm);

    /* msg_first and msg_last are not used by IPC_SET/IPC_STAT in kernel. */
    __get_user(host_md->msg_cbytes, &target_md->msg_cbytes);
    __get_user(host_md->msg_qnum, &target_md->msg_qnum);
    __get_user(host_md->msg_qbytes, &target_md->msg_qbytes);
    __get_user(host_md->msg_lspid, &target_md->msg_lspid);
    __get_user(host_md->msg_lrpid, &target_md->msg_lrpid);
    __get_user(host_md->msg_stime, &target_md->msg_stime);
    __get_user(host_md->msg_rtime, &target_md->msg_rtime);
    __get_user(host_md->msg_ctime, &target_md->msg_ctime);
    unlock_user_struct(target_md, target_addr, 0);

    return 0;
}
