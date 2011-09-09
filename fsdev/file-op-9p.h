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

typedef struct V9fsPath {
    int16_t size;
    char *data;
} V9fsPath;

void cred_init(FsCred *);

typedef struct FileOperations
{
    int (*lstat)(FsContext *, V9fsPath *, struct stat *);
    ssize_t (*readlink)(FsContext *, V9fsPath *, char *, size_t);
    int (*chmod)(FsContext *, V9fsPath *, FsCred *);
    int (*chown)(FsContext *, V9fsPath *, FsCred *);
    int (*mknod)(FsContext *, V9fsPath *, const char *, FsCred *);
    int (*utimensat)(FsContext *, V9fsPath *, const struct timespec *);
    int (*remove)(FsContext *, const char *);
    int (*symlink)(FsContext *, const char *, V9fsPath *,
                   const char *, FsCred *);
    int (*link)(FsContext *, V9fsPath *, V9fsPath *, const char *);
    int (*setuid)(FsContext *, uid_t);
    int (*close)(FsContext *, int);
    int (*closedir)(FsContext *, DIR *);
    DIR *(*opendir)(FsContext *, V9fsPath *);
    int (*open)(FsContext *, V9fsPath *, int);
    int (*open2)(FsContext *, V9fsPath *, const char *, int, FsCred *);
    void (*rewinddir)(FsContext *, DIR *);
    off_t (*telldir)(FsContext *, DIR *);
    int (*readdir_r)(FsContext *, DIR *, struct dirent *, struct dirent **);
    void (*seekdir)(FsContext *, DIR *, off_t);
    ssize_t (*preadv)(FsContext *, int, const struct iovec *, int, off_t);
    ssize_t (*pwritev)(FsContext *, int, const struct iovec *, int, off_t);
    int (*mkdir)(FsContext *, V9fsPath *, const char *, FsCred *);
    int (*fstat)(FsContext *, int, struct stat *);
    int (*rename)(FsContext *, const char *, const char *);
    int (*truncate)(FsContext *, V9fsPath *, off_t);
    int (*fsync)(FsContext *, int, int);
    int (*statfs)(FsContext *s, V9fsPath *path, struct statfs *stbuf);
    ssize_t (*lgetxattr)(FsContext *, V9fsPath *,
                         const char *, void *, size_t);
    ssize_t (*llistxattr)(FsContext *, V9fsPath *, void *, size_t);
    int (*lsetxattr)(FsContext *, V9fsPath *,
                     const char *, void *, size_t, int);
    int (*lremovexattr)(FsContext *, V9fsPath *, const char *);
    int (*name_to_path)(FsContext *, V9fsPath *, const char *, V9fsPath *);
    int (*renameat)(FsContext *ctx, V9fsPath *olddir, const char *old_name,
                    V9fsPath *newdir, const char *new_name);
    int (*unlinkat)(FsContext *ctx, V9fsPath *dir, const char *name, int flags);
    void *opaque;
} FileOperations;

#endif
