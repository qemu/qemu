/*
 * 9p
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

#ifndef QEMU_9P_XATTR_H
#define QEMU_9P_XATTR_H

#include "qemu/xattr.h"

typedef struct xattr_operations
{
    const char *name;
    ssize_t (*getxattr)(FsContext *ctx, const char *path,
                        const char *name, void *value, size_t size);
    ssize_t (*listxattr)(FsContext *ctx, const char *path,
                         char *name, void *value, size_t size);
    int (*setxattr)(FsContext *ctx, const char *path, const char *name,
                    void *value, size_t size, int flags);
    int (*removexattr)(FsContext *ctx,
                       const char *path, const char *name);
} XattrOperations;

ssize_t local_getxattr_nofollow(FsContext *ctx, const char *path,
                                const char *name, void *value, size_t size);
ssize_t local_setxattr_nofollow(FsContext *ctx, const char *path,
                                const char *name, void *value, size_t size,
                                int flags);
ssize_t local_removexattr_nofollow(FsContext *ctx, const char *path,
                                   const char *name);

extern XattrOperations mapped_user_xattr;
extern XattrOperations passthrough_user_xattr;

extern XattrOperations mapped_pacl_xattr;
extern XattrOperations mapped_dacl_xattr;
extern XattrOperations passthrough_acl_xattr;
extern XattrOperations none_acl_xattr;

extern XattrOperations *mapped_xattr_ops[];
extern XattrOperations *passthrough_xattr_ops[];
extern XattrOperations *none_xattr_ops[];

ssize_t v9fs_get_xattr(FsContext *ctx, const char *path, const char *name,
                       void *value, size_t size);
ssize_t v9fs_list_xattr(FsContext *ctx, const char *path, void *value,
                        size_t vsize);
int v9fs_set_xattr(FsContext *ctx, const char *path, const char *name,
                          void *value, size_t size, int flags);
int v9fs_remove_xattr(FsContext *ctx, const char *path, const char *name);

ssize_t pt_listxattr(FsContext *ctx, const char *path, char *name, void *value,
                     size_t size);
ssize_t pt_getxattr(FsContext *ctx, const char *path, const char *name,
                    void *value, size_t size);
int pt_setxattr(FsContext *ctx, const char *path, const char *name, void *value,
                size_t size, int flags);
int pt_removexattr(FsContext *ctx, const char *path, const char *name);

ssize_t notsup_getxattr(FsContext *ctx, const char *path, const char *name,
                        void *value, size_t size);
int notsup_setxattr(FsContext *ctx, const char *path, const char *name,
                    void *value, size_t size, int flags);
ssize_t notsup_listxattr(FsContext *ctx, const char *path, char *name,
                         void *value, size_t size);
int notsup_removexattr(FsContext *ctx, const char *path, const char *name);

#endif
