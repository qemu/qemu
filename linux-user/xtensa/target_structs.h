#ifndef XTENSA_TARGET_STRUCTS_T
#define XTENSA_TARGET_STRUCTS_T

struct target_ipc_perm {
    abi_int __key;                      /* Key.  */
    abi_uint uid;                       /* Owner's user ID.  */
    abi_uint gid;                       /* Owner's group ID.  */
    abi_uint cuid;                      /* Creator's user ID.  */
    abi_uint cgid;                      /* Creator's group ID.  */
    abi_uint mode;                      /* Read/write permission.  */
    abi_ushort __seq;                   /* Sequence number.  */
};

struct target_shmid_ds {
    struct target_ipc_perm shm_perm;    /* operation permission struct */
    abi_int shm_segsz;                  /* size of segment in bytes */
    abi_long shm_atime;                 /* time of last shmat() */
    abi_long shm_dtime;                 /* time of last shmdt() */
    abi_long shm_ctime;                 /* time of last change by shmctl() */
    abi_ushort shm_cpid;                /* pid of creator */
    abi_ushort shm_lpid;                /* pid of last shmop */
    abi_ushort shm_nattch;              /* number of current attaches */
    abi_ushort shm_unused;              /* compatibility */
    abi_ulong __unused2;
    abi_ulong __unused3;
};

#endif
