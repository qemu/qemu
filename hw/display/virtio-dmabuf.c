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

#include "qemu/osdep.h"

#include "hw/virtio/virtio-dmabuf.h"


static GMutex lock;
static GHashTable *resource_uuids;

/*
 * uuid_equal_func: wrapper for UUID is_equal function to
 * satisfy g_hash_table_new expected parameters signatures.
 */
static int uuid_equal_func(const void *lhv, const void *rhv)
{
    return qemu_uuid_is_equal(lhv, rhv);
}

static bool virtio_add_resource(QemuUUID *uuid, VirtioSharedObject *value)
{
    bool result = true;

    g_mutex_lock(&lock);
    if (resource_uuids == NULL) {
        resource_uuids = g_hash_table_new_full(qemu_uuid_hash,
                                               uuid_equal_func,
                                               NULL,
                                               g_free);
    }
    if (g_hash_table_lookup(resource_uuids, uuid) == NULL) {
        g_hash_table_insert(resource_uuids, uuid, value);
    } else {
        result = false;
    }
    g_mutex_unlock(&lock);

    return result;
}

bool virtio_add_dmabuf(QemuUUID *uuid, int udmabuf_fd)
{
    bool result;
    VirtioSharedObject *vso;
    if (udmabuf_fd < 0) {
        return false;
    }
    vso = g_new(VirtioSharedObject, 1);
    vso->type = TYPE_DMABUF;
    vso->value = GINT_TO_POINTER(udmabuf_fd);
    result = virtio_add_resource(uuid, vso);
    if (!result) {
        g_free(vso);
    }

    return result;
}

bool virtio_add_vhost_device(QemuUUID *uuid, struct vhost_dev *dev)
{
    bool result;
    VirtioSharedObject *vso;
    if (dev == NULL) {
        return false;
    }
    vso = g_new(VirtioSharedObject, 1);
    vso->type = TYPE_VHOST_DEV;
    vso->value = dev;
    result = virtio_add_resource(uuid, vso);
    if (!result) {
        g_free(vso);
    }

    return result;
}

bool virtio_remove_resource(const QemuUUID *uuid)
{
    bool result;
    g_mutex_lock(&lock);
    result = g_hash_table_remove(resource_uuids, uuid);
    g_mutex_unlock(&lock);

    return result;
}

static VirtioSharedObject *get_shared_object(const QemuUUID *uuid)
{
    gpointer lookup_res = NULL;

    g_mutex_lock(&lock);
    if (resource_uuids != NULL) {
        lookup_res = g_hash_table_lookup(resource_uuids, uuid);
    }
    g_mutex_unlock(&lock);

    return (VirtioSharedObject *) lookup_res;
}

int virtio_lookup_dmabuf(const QemuUUID *uuid)
{
    VirtioSharedObject *vso = get_shared_object(uuid);
    if (vso == NULL) {
        return -1;
    }
    assert(vso->type == TYPE_DMABUF);
    return GPOINTER_TO_INT(vso->value);
}

struct vhost_dev *virtio_lookup_vhost_device(const QemuUUID *uuid)
{
    VirtioSharedObject *vso = get_shared_object(uuid);
    if (vso == NULL) {
        return NULL;
    }
    assert(vso->type == TYPE_VHOST_DEV);
    return (struct vhost_dev *) vso->value;
}

SharedObjectType virtio_object_type(const QemuUUID *uuid)
{
    VirtioSharedObject *vso = get_shared_object(uuid);
    if (vso == NULL) {
        return TYPE_INVALID;
    }
    return vso->type;
}

void virtio_free_resources(void)
{
    g_mutex_lock(&lock);
    g_hash_table_destroy(resource_uuids);
    /* Reference count shall be 0 after the implicit unref on destroy */
    resource_uuids = NULL;
    g_mutex_unlock(&lock);
}
