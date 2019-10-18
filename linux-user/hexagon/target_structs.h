/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

/*
 * Hexagon specific structures for linux-user
 */
#ifndef TARGET_STRUCTS_H
#define TARGET_STRUCTS_H

struct target_ipc_perm {
    abi_int __key;
    abi_int uid;
    abi_int gid;
    abi_int cuid;
    abi_int cgid;
    abi_ushort mode;
    abi_ushort __pad1;
    abi_ushort __seq;
};

struct target_shmid_ds {
    struct target_ipc_perm shm_perm;
    abi_long shm_segsz;
    abi_ulong shm_atime;
    abi_ulong shm_dtime;
    abi_ulong shm_ctime;
    abi_int shm_cpid;
    abi_int shm_lpid;
    abi_ulong shm_nattch;
};

#endif
