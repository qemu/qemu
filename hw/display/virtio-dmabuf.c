/*
 * Virtio Shared dma-buf
 *
 * Copyright Red Hat, Inc. 2023
 *
 * Authors:
 *     Albert Esteve <aesteve@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "hw/virtio/virtio-dmabuf.h"


static struct shared_resources {
    GHashTable *uuids;
    bool locked;
} res = {
    .uuids = NULL,
    .locked = false,
};

bool virtio_add_dmabuf(QemuUUID uuid, int udmabuf_fd)
{
    /*
     * Check that the fd is valid.
     */
    return virtio_add_resource(uuid, GINT_TO_POINTER(udmabuf_fd));
}

bool virtio_add_resource(QemuUUID uuid, gpointer value)
{
    if (res.uuids == NULL) {
        res.uuids = g_hash_table_new_full(NULL, NULL, NULL, g_free);
    }
    /*
     * Add the resource to the table. If the key already exists in the table,
     * do not replace its value and return false. Otherwise, return true.
     */
    return true;
}

bool virtio_remove_resource(QemuUUID uuid)
{
    if (res.uuids == NULL) {
        return false;
    }
    /* 
     * Remove the resource and return true if key was found and removed.
     * If all resources are gone and table is empty, destroy the table.
     */
    return true;
}

int virtio_lookup_dmabuf(QemuUUID uuid)
{
    int fd = GPOINTER_TO_INT(virtio_lookup_resource(uuid));
    if (!fd) {
        return -1;
    }
    return fd;
}

gpointer virtio_lookup_resource(QemuUUID uuid)
{
    if (res.uuids == NULL) {
        return (gpointer) NULL;
    }
    /* Lookup the resource in the table and return it (can be NULL). */
    return (gpointer) NULL;
}
