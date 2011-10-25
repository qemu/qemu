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

typedef struct FsCred
{
    uid_t   fc_uid;
    gid_t   fc_gid;
    mode_t  fc_mode;
    dev_t   fc_rdev;
} FsCred;

struct xattr_operations;
struct FsContext;
struct V9fsPath;

typedef struct extended_ops {
    int (*get_st_gen)(struct FsContext *, struct V9fsPath *,
                      mode_t, uint64_t *);
} extended_ops;

/* export flags */
#define V9FS_IMMEDIATE_WRITEOUT     0x00000001
#define V9FS_PATHNAME_FSCONTEXT     0x00000002
/*
 * uid/gid set on fileserver files
 */
#define V9FS_SM_PASSTHROUGH         0x00000004
/*
 * uid/gid part of xattr
 */
#define V9FS_SM_MAPPED              0x00000008
/*
 * Server will try to set uid/gid.
 * On failure ignore the error.
 */
#define V9FS_SM_NONE                0x00000010
#define V9FS_RDONLY                 0x00000020

#define V9FS_SEC_MASK               0x0000001C



typedef struct FsContext
{
    uid_t uid;
    char *fs_root;
    int export_flags;
    struct xattr_operations **xops;
    struct extended_ops exops;
    /* fs driver specific data */
    void *private;
} FsContext;

typedef struct V9fsPath {
    int16_t size;
    char *data;
} V9fsPath;

typedef union V9fsFidOpenState V9fsFidOpenState;

void cred_init(FsCred *);

typedef struct FileOperations
{
    int (*init)(struct FsContext *);
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
    int (*close)(FsContext *, V9fsFidOpenState *);
    int (*closedir)(FsContext *, V9fsFidOpenState *);
    int (*opendir)(FsContext *, V9fsPath *, V9fsFidOpenState *);
    int (*open)(FsContext *, V9fsPath *, int, V9fsFidOpenState *);
    int (*open2)(FsContext *, V9fsPath *, const char *,
                 int, FsCred *, V9fsFidOpenState *);
    void (*rewinddir)(FsContext *, V9fsFidOpenState *);
    off_t (*telldir)(FsContext *, V9fsFidOpenState *);
    int (*readdir_r)(FsContext *, V9fsFidOpenState *,
                     struct dirent *, struct dirent **);
    void (*seekdir)(FsContext *, V9fsFidOpenState *, off_t);
    ssize_t (*preadv)(FsContext *, V9fsFidOpenState *,
                      const struct iovec *, int, off_t);
    ssize_t (*pwritev)(FsContext *, V9fsFidOpenState *,
                       const struct iovec *, int, off_t);
    int (*mkdir)(FsContext *, V9fsPath *, const char *, FsCred *);
    int (*fstat)(FsContext *, V9fsFidOpenState *, struct stat *);
    int (*rename)(FsContext *, const char *, const char *);
    int (*truncate)(FsContext *, V9fsPath *, off_t);
    int (*fsync)(FsContext *, V9fsFidOpenState *, int);
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
