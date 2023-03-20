/*
 * Virtio Shared dma-buf
 *
 * Copyright Red Hat, Inc. 2023
 *
 * Authors:
 *     Albert Esteve <aesteve@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef VIRTIO_DMABUF_H
#define VIRTIO_DMABUF_H

#include "qemu/osdep.h"

#include <glib.h>
#include "qemu/uuid.h"


bool virtio_add_dmabuf(QemuUUID uuid, int dmabuf_fd);
bool virtio_add_resource(QemuUUID uuid, gpointer value);
bool virtio_remove_resource(QemuUUID uuid);
int virtio_lookup_dmabuf(QemuUUID uuid);
gpointer virtio_lookup_resource(QemuUUID uuid);

#endif /* VIRTIO_DMABUF_H */
