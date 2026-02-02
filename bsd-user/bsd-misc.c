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
