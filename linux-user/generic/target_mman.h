#ifndef LINUX_USER_TARGET_MMAN_H
#define LINUX_USER_TARGET_MMAN_H

/* These are defined in linux/mmap.h */
#define TARGET_MAP_SHARED               0x01
#define TARGET_MAP_PRIVATE              0x02
#define TARGET_MAP_SHARED_VALIDATE      0x03

/* 0x0100 - 0x4000 flags are defined in asm-generic/mman.h */
#ifndef TARGET_MAP_GROWSDOWN
#define TARGET_MAP_GROWSDOWN            0x0100
#endif
#ifndef TARGET_MAP_DENYWRITE
#define TARGET_MAP_DENYWRITE            0x0800
#endif
#ifndef TARGET_MAP_EXECUTABLE
#define TARGET_MAP_EXECUTABLE           0x1000
#endif
#ifndef TARGET_MAP_LOCKED
#define TARGET_MAP_LOCKED               0x2000
#endif
#ifndef TARGET_MAP_NORESERVE
#define TARGET_MAP_NORESERVE            0x4000
#endif

/* Defined in asm-generic/mman-common.h */
#ifndef TARGET_PROT_SEM
#define TARGET_PROT_SEM                 0x08
#endif

#ifndef TARGET_MAP_TYPE
#define TARGET_MAP_TYPE                 0x0f
#endif
#ifndef TARGET_MAP_FIXED
#define TARGET_MAP_FIXED                0x10
#endif
#ifndef TARGET_MAP_ANONYMOUS
#define TARGET_MAP_ANONYMOUS            0x20
#endif
#ifndef TARGET_MAP_POPULATE
#define TARGET_MAP_POPULATE             0x008000
#endif
#ifndef TARGET_MAP_NONBLOCK
#define TARGET_MAP_NONBLOCK             0x010000
#endif
#ifndef TARGET_MAP_STACK
#define TARGET_MAP_STACK                0x020000
#endif
#ifndef TARGET_MAP_HUGETLB
#define TARGET_MAP_HUGETLB              0x040000
#endif
#ifndef TARGET_MAP_SYNC
#define TARGET_MAP_SYNC                 0x080000
#endif
#ifndef TARGET_MAP_FIXED_NOREPLACE
#define TARGET_MAP_FIXED_NOREPLACE      0x100000
#endif
#ifndef TARGET_MAP_UNINITIALIZED
#define TARGET_MAP_UNINITIALIZED        0x4000000
#endif

#ifndef TARGET_MADV_NORMAL
#define TARGET_MADV_NORMAL 0
#endif

#ifndef TARGET_MADV_RANDOM
#define TARGET_MADV_RANDOM 1
#endif

#ifndef TARGET_MADV_SEQUENTIAL
#define TARGET_MADV_SEQUENTIAL 2
#endif

#ifndef TARGET_MADV_WILLNEED
#define TARGET_MADV_WILLNEED 3
#endif

#ifndef TARGET_MADV_DONTNEED
#define TARGET_MADV_DONTNEED 4
#endif

#ifndef TARGET_MADV_FREE
#define TARGET_MADV_FREE 8
#endif

#ifndef TARGET_MADV_REMOVE
#define TARGET_MADV_REMOVE 9
#endif

#ifndef TARGET_MADV_DONTFORK
#define TARGET_MADV_DONTFORK 10
#endif

#ifndef TARGET_MADV_DOFORK
#define TARGET_MADV_DOFORK 11
#endif

#ifndef TARGET_MADV_MERGEABLE
#define TARGET_MADV_MERGEABLE 12
#endif

#ifndef TARGET_MADV_UNMERGEABLE
#define TARGET_MADV_UNMERGEABLE 13
#endif

#ifndef TARGET_MADV_HUGEPAGE
#define TARGET_MADV_HUGEPAGE 14
#endif

#ifndef TARGET_MADV_NOHUGEPAGE
#define TARGET_MADV_NOHUGEPAGE 15
#endif

#ifndef TARGET_MADV_DONTDUMP
#define TARGET_MADV_DONTDUMP 16
#endif

#ifndef TARGET_MADV_DODUMP
#define TARGET_MADV_DODUMP 17
#endif

#ifndef TARGET_MADV_WIPEONFORK
#define TARGET_MADV_WIPEONFORK 18
#endif

#ifndef TARGET_MADV_KEEPONFORK
#define TARGET_MADV_KEEPONFORK 19
#endif

#ifndef TARGET_MADV_COLD
#define TARGET_MADV_COLD 20
#endif

#ifndef TARGET_MADV_PAGEOUT
#define TARGET_MADV_PAGEOUT 21
#endif

#ifndef TARGET_MADV_POPULATE_READ
#define TARGET_MADV_POPULATE_READ 22
#endif

#ifndef TARGET_MADV_POPULATE_WRITE
#define TARGET_MADV_POPULATE_WRITE 23
#endif

#ifndef TARGET_MADV_DONTNEED_LOCKED
#define TARGET_MADV_DONTNEED_LOCKED 24
#endif


#ifndef TARGET_MS_ASYNC
#define TARGET_MS_ASYNC 1
#endif

#ifndef TARGET_MS_INVALIDATE
#define TARGET_MS_INVALIDATE 2
#endif

#ifndef TARGET_MS_SYNC
#define TARGET_MS_SYNC 4
#endif

#endif
