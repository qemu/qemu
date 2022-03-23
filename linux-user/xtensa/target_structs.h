#ifndef XTENSA_TARGET_STRUCTS_H
#define XTENSA_TARGET_STRUCTS_H

struct target_ipc_perm {
    abi_int __key;                      /* Key.  */
    abi_uint uid;                       /* Owner's user ID.  */
    abi_uint gid;                       /* Owner's group ID.  */
    abi_uint cuid;                      /* Creator's user ID.  */
    abi_uint cgid;                      /* Creator's group ID.  */
    abi_uint mode;                      /* Read/write permission.  */
    abi_ulong __seq;                    /* Sequence number.  */
    abi_ulong __unused1;
    abi_ulong __unused2;
};

struct target_semid64_ds {
  struct target_ipc_perm sem_perm;
#if TARGET_BIG_ENDIAN
  abi_ulong __unused1;
  abi_ulong sem_otime;
  abi_ulong __unused2;
  abi_ulong sem_ctime;
#else
  abi_ulong sem_otime;
  abi_ulong __unused1;
  abi_ulong sem_ctime;
  abi_ulong __unused2;
#endif
  abi_ulong sem_nsems;
  abi_ulong __unused3;
  abi_ulong __unused4;
};
#define TARGET_SEMID64_DS

struct target_shmid_ds {
    struct target_ipc_perm shm_perm;    /* operation permission struct */
    abi_long shm_segsz;                 /* size of segment in bytes */
    abi_long shm_atime;                 /* time of last shmat() */
    abi_ulong __unused1;
    abi_long shm_dtime;                 /* time of last shmdt() */
    abi_ulong __unused2;
    abi_long shm_ctime;                 /* time of last change by shmctl() */
    abi_ulong __unused3;
    abi_uint shm_cpid;                  /* pid of creator */
    abi_uint shm_lpid;                  /* pid of last shmop */
    abi_ulong shm_nattch;               /* number of current attaches */
    abi_ulong __unused4;
    abi_ulong __unused5;
};

#endif
