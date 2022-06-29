/*
 * 9p user. xattr callback
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

/*
 * Not so fast! You might want to read the 9p developer docs first:
 * https://wiki.qemu.org/Documentation/9p
 */

#include "qemu/osdep.h"
#include "9p.h"
#include "fsdev/file-op-9p.h"
#include "9p-xattr.h"


static ssize_t mp_user_getxattr(FsContext *ctx, const char *path,
                                const char *name, void *value, size_t size)
{
    if (strncmp(name, "user.virtfs.", 12) == 0) {
        /*
         * Don't allow fetch of user.virtfs namespace
         * in case of mapped security
         */
        errno = ENOATTR;
        return -1;
    }
    return local_getxattr_nofollow(ctx, path, name, value, size);
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
             * Don't allow fetch of user.virtfs namespace
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

    /* name_size includes the trailing NUL. */
    memcpy(value, name, name_size);
    return name_size;
}

static int mp_user_setxattr(FsContext *ctx, const char *path, const char *name,
                            void *value, size_t size, int flags)
{
    if (strncmp(name, "user.virtfs.", 12) == 0) {
        /*
         * Don't allow fetch of user.virtfs namespace
         * in case of mapped security
         */
        errno = EACCES;
        return -1;
    }
    return local_setxattr_nofollow(ctx, path, name, value, size, flags);
}

static int mp_user_removexattr(FsContext *ctx,
                               const char *path, const char *name)
{
    if (strncmp(name, "user.virtfs.", 12) == 0) {
        /*
         * Don't allow fetch of user.virtfs namespace
         * in case of mapped security
         */
        errno = EACCES;
        return -1;
    }
    return local_removexattr_nofollow(ctx, path, name);
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
