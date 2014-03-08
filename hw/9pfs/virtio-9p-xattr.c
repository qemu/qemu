/*
 * Virtio 9p  xattr callback
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

#include "hw/virtio/virtio.h"
#include "virtio-9p.h"
#include "fsdev/file-op-9p.h"
#include "virtio-9p-xattr.h"


static XattrOperations *get_xattr_operations(XattrOperations **h,
                                             const char *name)
{
    XattrOperations *xops;
    for (xops = *(h)++; xops != NULL; xops = *(h)++) {
        if (!strncmp(name, xops->name, strlen(xops->name))) {
            return xops;
        }
    }
    return NULL;
}

ssize_t v9fs_get_xattr(FsContext *ctx, const char *path,
                       const char *name, void *value, size_t size)
{
    XattrOperations *xops = get_xattr_operations(ctx->xops, name);
    if (xops) {
        return xops->getxattr(ctx, path, name, value, size);
    }
    errno = EOPNOTSUPP;
    return -1;
}

ssize_t pt_listxattr(FsContext *ctx, const char *path,
                     char *name, void *value, size_t size)
{
    int name_size = strlen(name) + 1;
    if (!value) {
        return name_size;
    }

    if (size < name_size) {
        errno = ERANGE;
        return -1;
    }

    /* no need for strncpy: name_size is strlen(name)+1 */
    memcpy(value, name, name_size);
    return name_size;
}


/*
 * Get the list and pass to each layer to find out whether
 * to send the data or not
 */
ssize_t v9fs_list_xattr(FsContext *ctx, const char *path,
                        void *value, size_t vsize)
{
    ssize_t size = 0;
    char *buffer;
    void *ovalue = value;
    XattrOperations *xops;
    char *orig_value, *orig_value_start;
    ssize_t xattr_len, parsed_len = 0, attr_len;

    /* Get the actual len */
    buffer = rpath(ctx, path);
    xattr_len = llistxattr(buffer, value, 0);
    if (xattr_len <= 0) {
        g_free(buffer);
        return xattr_len;
    }

    /* Now fetch the xattr and find the actual size */
    orig_value = g_malloc(xattr_len);
    xattr_len = llistxattr(buffer, orig_value, xattr_len);
    g_free(buffer);

    /* store the orig pointer */
    orig_value_start = orig_value;
    while (xattr_len > parsed_len) {
        xops = get_xattr_operations(ctx->xops, orig_value);
        if (!xops) {
            goto next_entry;
        }

        if (!value) {
            size += xops->listxattr(ctx, path, orig_value, value, vsize);
        } else {
            size = xops->listxattr(ctx, path, orig_value, value, vsize);
            if (size < 0) {
                goto err_out;
            }
            value += size;
            vsize -= size;
        }
next_entry:
        /* Got the next entry */
        attr_len = strlen(orig_value) + 1;
        parsed_len += attr_len;
        orig_value += attr_len;
    }
    if (value) {
        size = value - ovalue;
    }

err_out:
    g_free(orig_value_start);
    return size;
}

int v9fs_set_xattr(FsContext *ctx, const char *path, const char *name,
                   void *value, size_t size, int flags)
{
    XattrOperations *xops = get_xattr_operations(ctx->xops, name);
    if (xops) {
        return xops->setxattr(ctx, path, name, value, size, flags);
    }
    errno = EOPNOTSUPP;
    return -1;

}

int v9fs_remove_xattr(FsContext *ctx,
                      const char *path, const char *name)
{
    XattrOperations *xops = get_xattr_operations(ctx->xops, name);
    if (xops) {
        return xops->removexattr(ctx, path, name);
    }
    errno = EOPNOTSUPP;
    return -1;

}

XattrOperations *mapped_xattr_ops[] = {
    &mapped_user_xattr,
    &mapped_pacl_xattr,
    &mapped_dacl_xattr,
    NULL,
};

XattrOperations *passthrough_xattr_ops[] = {
    &passthrough_user_xattr,
    &passthrough_acl_xattr,
    NULL,
};

/* for .user none model should be same as passthrough */
XattrOperations *none_xattr_ops[] = {
    &passthrough_user_xattr,
    &none_acl_xattr,
    NULL,
};
