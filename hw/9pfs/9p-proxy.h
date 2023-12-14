/*
 * 9p Proxy callback
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 * M. Mohan Kumar <mohan@in.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

/*
 * NOTE: The 9p 'proxy' backend is deprecated (since QEMU 8.1) and will be
 * removed in a future version of QEMU!
 */

#ifndef QEMU_9P_PROXY_H
#define QEMU_9P_PROXY_H

#define PROXY_MAX_IO_SZ (64 * 1024)
#define V9FS_FD_VALID INT_MAX

/*
 * proxy iovec only support one element and
 * marsha/unmarshal doesn't do little endian conversion.
 */
#define proxy_unmarshal(in_sg, offset, fmt, args...) \
    v9fs_iov_unmarshal(in_sg, 1, offset, 0, fmt, ##args)
#define proxy_marshal(out_sg, offset, fmt, args...) \
    v9fs_iov_marshal(out_sg, 1, offset, 0, fmt, ##args)

union MsgControl {
    struct cmsghdr cmsg;
    char control[CMSG_SPACE(sizeof(int))];
};

typedef struct {
    uint32_t type;
    uint32_t size;
} ProxyHeader;

#define PROXY_HDR_SZ (sizeof(ProxyHeader))

enum {
    T_SUCCESS = 0,
    T_ERROR,
    T_OPEN,
    T_CREATE,
    T_MKNOD,
    T_MKDIR,
    T_SYMLINK,
    T_LINK,
    T_LSTAT,
    T_READLINK,
    T_STATFS,
    T_CHMOD,
    T_CHOWN,
    T_TRUNCATE,
    T_UTIME,
    T_RENAME,
    T_REMOVE,
    T_LGETXATTR,
    T_LLISTXATTR,
    T_LSETXATTR,
    T_LREMOVEXATTR,
    T_GETVERSION,
};

typedef struct {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    uint64_t st_size;
    uint64_t st_blksize;
    uint64_t st_blocks;
    uint64_t st_atim_sec;
    uint64_t st_atim_nsec;
    uint64_t st_mtim_sec;
    uint64_t st_mtim_nsec;
    uint64_t st_ctim_sec;
    uint64_t st_ctim_nsec;
} ProxyStat;

typedef struct {
    uint64_t f_type;
    uint64_t f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    uint64_t f_fsid[2];
    uint64_t f_namelen;
    uint64_t f_frsize;
} ProxyStatFS;
#endif
