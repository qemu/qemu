/*
 * Virtio 9p user. xattr callback
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 * Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include <sys/types.h>
#include "hw/virtio.h"
#include "virtio-9p.h"
#include "fsdev/file-op-9p.h"
#include "virtio-9p-xattr.h"


static ssize_t mp_user_getxattr(FsContext *ctx, const char *path,
                                const char *name, void *value, size_t size)
{
    char buffer[PATH_MAX];
    if (strncmp(name, "user.virtfs.", 12) == 0) {
        /*
         * Don't allow fetch of user.virtfs namesapce
         * in case of mapped security
         */
        errno = ENOATTR;
        return -1;
    }
    return lgetxattr(rpath(ctx, path, buffer), name, value, size);
}

static ssize_t mp_user_listxattr(FsContext *ctx, const char *path,
                                 char *name, void *value, size_t size)
{
    int name_size = strlen(name) + 1;
    if (strncmp(name, "user.virtfs.", 12) == 0) {

        /*  check if it is a mapped posix acl */
        if (strncmp(name, "user.virtfs.system.posix_acl_", 29) == 0) {
            /* adjust the name and size */
            name += 12;
            name_size -= 12;
        } else {
            /*
             * Don't allow fetch of user.virtfs namesapce
             * in case of mapped security
             */
            return 0;
        }
    }
    if (!value) {
        return name_size;
    }

    if (size < name_size) {
        errno = ERANGE;
        return -1;
    }

    strncpy(value, name, name_size);
    return name_size;
}

static int mp_user_setxattr(FsContext *ctx, const char *path, const char *name,
                            void *value, size_t size, int flags)
{
    char buffer[PATH_MAX];
    if (strncmp(name, "user.virtfs.", 12) == 0) {
        /*
         * Don't allow fetch of user.virtfs namesapce
         * in case of mapped security
         */
        errno = EACCES;
        return -1;
    }
    return lsetxattr(rpath(ctx, path, buffer), name, value, size, flags);
}

static int mp_user_removexattr(FsContext *ctx,
                               const char *path, const char *name)
{
    char buffer[PATH_MAX];
    if (strncmp(name, "user.virtfs.", 12) == 0) {
        /*
         * Don't allow fetch of user.virtfs namesapce
         * in case of mapped security
         */
        errno = EACCES;
        return -1;
    }
    return lremovexattr(rpath(ctx, path, buffer), name);
}

XattrOperations mapped_user_xattr = {
    .name = "user.",
    .getxattr = mp_user_getxattr,
    .setxattr = mp_user_setxattr,
    .listxattr = mp_user_listxattr,
    .removexattr = mp_user_removexattr,
};

XattrOperations passthrough_user_xattr = {
    .name = "user.",
    .getxattr = pt_getxattr,
    .setxattr = pt_setxattr,
    .listxattr = pt_listxattr,
    .removexattr = pt_removexattr,
};
