/*
 * Virtio 9p
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */
#ifndef _FILEOP_H
#define _FILEOP_H
#include <sys/types.h>
#include <dirent.h>
#include <sys/time.h>
#include <utime.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/vfs.h>
#define SM_LOCAL_MODE_BITS    0600
#define SM_LOCAL_DIR_MODE_BITS    0700

typedef enum
{
    /*
     * Server will try to set uid/gid.
     * On failure ignore the error.
     */
    SM_NONE = 0,
    /*
     * uid/gid set on fileserver files
     */
    SM_PASSTHROUGH = 1,
    /*
     * uid/gid part of xattr
     */
    SM_MAPPED,
} SecModel;

typedef struct FsCred
{
    uid_t   fc_uid;
    gid_t   fc_gid;
    mode_t  fc_mode;
    dev_t   fc_rdev;
} FsCred;

struct xattr_operations;

typedef struct FsContext
{
    char *fs_root;
    SecModel fs_sm;
    uid_t uid;
    struct xattr_operations **xops;
} FsContext;

void cred_init(FsCred *);

typedef struct FileOperations
{
    int (*lstat)(FsContext *, const char *, struct stat *);
    ssize_t (*readlink)(FsContext *, const char *, char *, size_t);
    int (*chmod)(FsContext *, const char *, FsCred *);
    int (*chown)(FsContext *, const char *, FsCred *);
    int (*mknod)(FsContext *, const char *, FsCred *);
    int (*utimensat)(FsContext *, const char *, const struct timespec *);
    int (*remove)(FsContext *, const char *);
    int (*symlink)(FsContext *, const char *, const char *, FsCred *);
    int (*link)(FsContext *, const char *, const char *);
    int (*setuid)(FsContext *, uid_t);
    int (*close)(FsContext *, int);
    int (*closedir)(FsContext *, DIR *);
    DIR *(*opendir)(FsContext *, const char *);
    int (*open)(FsContext *, const char *, int);
    int (*open2)(FsContext *, const char *, int, FsCred *);
    void (*rewinddir)(FsContext *, DIR *);
    off_t (*telldir)(FsContext *, DIR *);
    int (*readdir_r)(FsContext *, DIR *, struct dirent *, struct dirent **);
    void (*seekdir)(FsContext *, DIR *, off_t);
    ssize_t (*preadv)(FsContext *, int, const struct iovec *, int, off_t);
    ssize_t (*pwritev)(FsContext *, int, const struct iovec *, int, off_t);
    int (*mkdir)(FsContext *, const char *, FsCred *);
    int (*fstat)(FsContext *, int, struct stat *);
    int (*rename)(FsContext *, const char *, const char *);
    int (*truncate)(FsContext *, const char *, off_t);
    int (*fsync)(FsContext *, int, int);
    int (*statfs)(FsContext *s, const char *path, struct statfs *stbuf);
    ssize_t (*lgetxattr)(FsContext *, const char *,
                         const char *, void *, size_t);
    ssize_t (*llistxattr)(FsContext *, const char *, void *, size_t);
    int (*lsetxattr)(FsContext *, const char *,
                     const char *, void *, size_t, int);
    int (*lremovexattr)(FsContext *, const char *, const char *);
    void *opaque;
} FileOperations;

#endif
