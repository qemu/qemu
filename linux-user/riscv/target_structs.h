/*
 * RISC-V specific structures for linux-user
 *
 * This is a copy of ../aarch64/target_structs.h atm.
 *
 */
#ifndef RISCV_TARGET_STRUCTS_H
#define RISCV_TARGET_STRUCTS_H

struct target_ipc_perm {
    abi_int __key;                      /* Key.  */
    abi_uint uid;                       /* Owner's user ID.  */
    abi_uint gid;                       /* Owner's group ID.  */
    abi_uint cuid;                      /* Creator's user ID.  */
    abi_uint cgid;                      /* Creator's group ID.  */
    abi_ushort mode;                    /* Read/write permission.  */
    abi_ushort __pad1;
    abi_ushort __seq;                   /* Sequence number.  */
    abi_ushort __pad2;
    abi_ulong __unused1;
    abi_ulong __unused2;
};

struct target_shmid_ds {
    struct target_ipc_perm shm_perm;    /* operation permission struct */
    abi_long shm_segsz;                 /* size of segment in bytes */
    abi_ulong shm_atime;                /* time of last shmat() */
#if TARGET_ABI_BITS == 32
    abi_ulong __unused1;
#endif
    abi_ulong shm_dtime;                /* time of last shmdt() */
#if TARGET_ABI_BITS == 32
    abi_ulong __unused2;
#endif
    abi_ulong shm_ctime;                /* time of last change by shmctl() */
#if TARGET_ABI_BITS == 32
    abi_ulong __unused3;
#endif
    abi_int shm_cpid;                   /* pid of creator */
    abi_int shm_lpid;                   /* pid of last shmop */
    abi_ulong shm_nattch;               /* number of current attaches */
    abi_ulong __unused4;
    abi_ulong __unused5;
};

#endif
