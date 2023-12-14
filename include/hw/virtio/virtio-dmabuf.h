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

#include "qemu/uuid.h"
#include "vhost.h"

typedef enum SharedObjectType {
    TYPE_INVALID = 0,
    TYPE_DMABUF,
    TYPE_VHOST_DEV,
} SharedObjectType;

typedef struct VirtioSharedObject {
    SharedObjectType type;
    gpointer value;
} VirtioSharedObject;

/**
 * virtio_add_dmabuf() - Add a new dma-buf resource to the lookup table
 * @uuid: new resource's UUID
 * @dmabuf_fd: the dma-buf descriptor that will be stored and shared with
 *             other virtio devices. The caller retains ownership over the
 *             descriptor and its lifecycle.
 *
 * Note: @dmabuf_fd must be a valid (non-negative) file descriptor.
 *
 * Return: true if the UUID did not exist and the resource has been added,
 * false if another resource with the same UUID already existed.
 * Note that if it finds a repeated UUID, the resource is not inserted in
 * the lookup table.
 */
bool virtio_add_dmabuf(QemuUUID *uuid, int dmabuf_fd);

/**
 * virtio_add_vhost_device() - Add a new exporter vhost device that holds the
 * resource with the associated UUID
 * @uuid: new resource's UUID
 * @dev: the pointer to the vhost device that holds the resource. The caller
 *       retains ownership over the device struct and its lifecycle.
 *
 * Return: true if the UUID did not exist and the device has been tracked,
 * false if another resource with the same UUID already existed.
 * Note that if it finds a repeated UUID, the resource is not inserted in
 * the lookup table.
 */
bool virtio_add_vhost_device(QemuUUID *uuid, struct vhost_dev *dev);

/**
 * virtio_remove_resource() - Removes a resource from the lookup table
 * @uuid: resource's UUID
 *
 * Return: true if the UUID has been found and removed from the lookup table.
 */
bool virtio_remove_resource(const QemuUUID *uuid);

/**
 * virtio_lookup_dmabuf() - Looks for a dma-buf resource in the lookup table
 * @uuid: resource's UUID
 *
 * Return: the dma-buf file descriptor integer, or -1 if the key is not found.
 */
int virtio_lookup_dmabuf(const QemuUUID *uuid);

/**
 * virtio_lookup_vhost_device() - Looks for an exporter vhost device in the
 * lookup table
 * @uuid: resource's UUID
 *
 * Return: pointer to the vhost_dev struct, or NULL if the key is not found.
 */
struct vhost_dev *virtio_lookup_vhost_device(const QemuUUID *uuid);

/**
 * virtio_object_type() - Looks for the type of resource in the lookup table
 * @uuid: resource's UUID
 *
 * Return: the type of resource associated with the UUID, or TYPE_INVALID if
 * the key is not found.
 */
SharedObjectType virtio_object_type(const QemuUUID *uuid);

/**
 * virtio_free_resources() - Destroys all keys and values of the shared
 * resources lookup table, and frees them
 */
void virtio_free_resources(void);

#endif /* VIRTIO_DMABUF_H */
