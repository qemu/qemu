/*
 * GDB Syscall support
 *
 * Copyright (c) 2023 Linaro Ltd
 *
 * SPDX-License-Identifier: LGPL-2.0+
 */

#ifndef _SYSCALLS_H_
#define _SYSCALLS_H_

/* For gdb file i/o remote protocol open flags. */
#define GDB_O_RDONLY  0
#define GDB_O_WRONLY  1
#define GDB_O_RDWR    2
#define GDB_O_APPEND  8
#define GDB_O_CREAT   0x200
#define GDB_O_TRUNC   0x400
#define GDB_O_EXCL    0x800

/* For gdb file i/o remote protocol errno values */
#define GDB_EPERM           1
#define GDB_ENOENT          2
#define GDB_EINTR           4
#define GDB_EBADF           9
#define GDB_EACCES         13
#define GDB_EFAULT         14
#define GDB_EBUSY          16
#define GDB_EEXIST         17
#define GDB_ENODEV         19
#define GDB_ENOTDIR        20
#define GDB_EISDIR         21
#define GDB_EINVAL         22
#define GDB_ENFILE         23
#define GDB_EMFILE         24
#define GDB_EFBIG          27
#define GDB_ENOSPC         28
#define GDB_ESPIPE         29
#define GDB_EROFS          30
#define GDB_ENAMETOOLONG   91
#define GDB_EUNKNOWN       9999

/* For gdb file i/o remote protocol lseek whence. */
#define GDB_SEEK_SET  0
#define GDB_SEEK_CUR  1
#define GDB_SEEK_END  2

/* For gdb file i/o stat/fstat. */
typedef uint32_t gdb_mode_t;
typedef uint32_t gdb_time_t;

struct gdb_stat {
  uint32_t    gdb_st_dev;     /* device */
  uint32_t    gdb_st_ino;     /* inode */
  gdb_mode_t  gdb_st_mode;    /* protection */
  uint32_t    gdb_st_nlink;   /* number of hard links */
  uint32_t    gdb_st_uid;     /* user ID of owner */
  uint32_t    gdb_st_gid;     /* group ID of owner */
  uint32_t    gdb_st_rdev;    /* device type (if inode device) */
  uint64_t    gdb_st_size;    /* total size, in bytes */
  uint64_t    gdb_st_blksize; /* blocksize for filesystem I/O */
  uint64_t    gdb_st_blocks;  /* number of blocks allocated */
  gdb_time_t  gdb_st_atime;   /* time of last access */
  gdb_time_t  gdb_st_mtime;   /* time of last modification */
  gdb_time_t  gdb_st_ctime;   /* time of last change */
} QEMU_PACKED;

struct gdb_timeval {
  gdb_time_t tv_sec;  /* second */
  uint64_t tv_usec;   /* microsecond */
} QEMU_PACKED;

typedef void (*gdb_syscall_complete_cb)(CPUState *cpu, uint64_t ret, int err);

/**
 * gdb_do_syscall:
 * @cb: function to call when the system call has completed
 * @fmt: gdb syscall format string
 * ...: list of arguments to interpolate into @fmt
 *
 * Send a GDB syscall request. This function will return immediately;
 * the callback function will be called later when the remote system
 * call has completed.
 *
 * @fmt should be in the 'call-id,parameter,parameter...' format documented
 * for the F request packet in the GDB remote protocol. A limited set of
 * printf-style format specifiers is supported:
 *   %x  - target_ulong argument printed in hex
 *   %lx - 64-bit argument printed in hex
 *   %s  - string pointer (target_ulong) and length (int) pair
 */
void gdb_do_syscall(gdb_syscall_complete_cb cb, const char *fmt, ...);

/**
 * use_gdb_syscalls() - report if GDB should be used for syscalls
 *
 * This is mostly driven by the semihosting mode the user configures
 * but assuming GDB is allowed by that we report true if GDB is
 * connected to the stub.
 */
int use_gdb_syscalls(void);

/**
 * gdb_exit: exit gdb session, reporting inferior status
 * @code: exit code reported
 *
 * This closes the session and sends a final packet to GDB reporting
 * the exit status of the program. It also cleans up any connections
 * detritus before returning.
 */
void gdb_exit(int code);

/**
 * gdb_qemu_exit: ask qemu to exit
 * @code: exit code reported
 *
 * This requests qemu to exit. This function is allowed to return as
 * the exit request might be processed asynchronously by qemu backend.
 */
void gdb_qemu_exit(int code);

#endif /* _SYSCALLS_H_ */
