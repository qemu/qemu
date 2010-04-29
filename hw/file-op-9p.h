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

typedef struct FsContext
{
    char *fs_root;
    uid_t uid;
} FsContext;

typedef struct FileOperations
{
    int (*lstat)(FsContext *, const char *, struct stat *);
    ssize_t (*readlink)(FsContext *, const char *, char *, size_t);
    int (*setuid)(FsContext *, uid_t);
    int (*close)(FsContext *, int);
    int (*closedir)(FsContext *, DIR *);
    DIR *(*opendir)(FsContext *, const char *);
    int (*open)(FsContext *, const char *, int);
    void (*rewinddir)(FsContext *, DIR *);
    off_t (*telldir)(FsContext *, DIR *);
    struct dirent *(*readdir)(FsContext *, DIR *);
    void (*seekdir)(FsContext *, DIR *, off_t);
    ssize_t (*readv)(FsContext *, int, const struct iovec *, int);
    off_t (*lseek)(FsContext *, int, off_t, int);
    void *opaque;
} FileOperations;
#endif
