/*
 * VDUSE (vDPA Device in Userspace) library
 *
 * Copyright (C) 2022 Bytedance Inc. and/or its affiliates. All rights reserved.
 *
 * Author:
 *   Xie Yongji <xieyongji@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef LIBVDUSE_H
#define LIBVDUSE_H

#include <stdint.h>
#include <sys/uio.h>

#define VIRTQUEUE_MAX_SIZE 1024

/* VDUSE device structure */
typedef struct VduseDev VduseDev;

/* Virtqueue structure */
typedef struct VduseVirtq VduseVirtq;

/* Some operation of VDUSE backend */
typedef struct VduseOps {
    /* Called when virtqueue can be processed */
    void (*enable_queue)(VduseDev *dev, VduseVirtq *vq);
    /* Called when virtqueue processing should be stopped */
    void (*disable_queue)(VduseDev *dev, VduseVirtq *vq);
} VduseOps;

/* Describing elements of the I/O buffer */
typedef struct VduseVirtqElement {
    /* Descriptor table index */
    unsigned int index;
    /* Number of physically-contiguous device-readable descriptors */
    unsigned int out_num;
    /* Number of physically-contiguous device-writable descriptors */
    unsigned int in_num;
    /* Array to store physically-contiguous device-writable descriptors */
    struct iovec *in_sg;
    /* Array to store physically-contiguous device-readable descriptors */
    struct iovec *out_sg;
} VduseVirtqElement;


/**
 * vduse_get_virtio_features:
 *
 * Get supported virtio features
 *
 * Returns: supported feature bits
 */
uint64_t vduse_get_virtio_features(void);

/**
 * vduse_queue_get_dev:
 * @vq: specified virtqueue
 *
 * Get corresponding VDUSE device from the virtqueue.
 *
 * Returns: a pointer to VDUSE device on success, NULL on failure.
 */
VduseDev *vduse_queue_get_dev(VduseVirtq *vq);

/**
 * vduse_queue_get_fd:
 * @vq: specified virtqueue
 *
 * Get the kick fd for the virtqueue.
 *
 * Returns: file descriptor on success, -1 on failure.
 */
int vduse_queue_get_fd(VduseVirtq *vq);

/**
 * vduse_queue_pop:
 * @vq: specified virtqueue
 * @sz: the size of struct to return (must be >= VduseVirtqElement)
 *
 * Pop an element from virtqueue available ring.
 *
 * Returns: a pointer to a structure containing VduseVirtqElement on success,
 * NULL on failure.
 */
void *vduse_queue_pop(VduseVirtq *vq, size_t sz);

/**
 * vduse_queue_push:
 * @vq: specified virtqueue
 * @elem: pointer to VduseVirtqElement returned by vduse_queue_pop()
 * @len: length in bytes to write
 *
 * Push an element to virtqueue used ring.
 */
void vduse_queue_push(VduseVirtq *vq, const VduseVirtqElement *elem,
                      unsigned int len);
/**
 * vduse_queue_notify:
 * @vq: specified virtqueue
 *
 * Request to notify the queue.
 */
void vduse_queue_notify(VduseVirtq *vq);

/**
 * vduse_dev_get_priv:
 * @dev: VDUSE device
 *
 * Get the private pointer passed to vduse_dev_create().
 *
 * Returns: private pointer on success, NULL on failure.
 */
void *vduse_dev_get_priv(VduseDev *dev);

/**
 * vduse_dev_get_queue:
 * @dev: VDUSE device
 * @index: virtqueue index
 *
 * Get the specified virtqueue.
 *
 * Returns: a pointer to the virtqueue on success, NULL on failure.
 */
VduseVirtq *vduse_dev_get_queue(VduseDev *dev, int index);

/**
 * vduse_dev_get_fd:
 * @dev: VDUSE device
 *
 * Get the control message fd for the VDUSE device.
 *
 * Returns: file descriptor on success, -1 on failure.
 */
int vduse_dev_get_fd(VduseDev *dev);

/**
 * vduse_dev_handler:
 * @dev: VDUSE device
 *
 * Used to process the control message.
 *
 * Returns: file descriptor on success, -errno on failure.
 */
int vduse_dev_handler(VduseDev *dev);

/**
 * vduse_dev_update_config:
 * @dev: VDUSE device
 * @size: the size to write to configuration space
 * @offset: the offset from the beginning of configuration space
 * @buffer: the buffer used to write from
 *
 * Update device configuration space and inject a config interrupt.
 *
 * Returns: 0 on success, -errno on failure.
 */
int vduse_dev_update_config(VduseDev *dev, uint32_t size,
                            uint32_t offset, char *buffer);

/**
 * vduse_dev_setup_queue:
 * @dev: VDUSE device
 * @index: virtqueue index
 * @max_size: the max size of virtqueue
 *
 * Setup the specified virtqueue.
 *
 * Returns: 0 on success, -errno on failure.
 */
int vduse_dev_setup_queue(VduseDev *dev, int index, int max_size);

/**
 * vduse_set_reconnect_log_file:
 * @dev: VDUSE device
 * @file: filename of reconnect log
 *
 * Specify the file to store log for reconnecting. It should
 * be called before vduse_dev_setup_queue().
 *
 * Returns: 0 on success, -errno on failure.
 */
int vduse_set_reconnect_log_file(VduseDev *dev, const char *filename);

/**
 * vduse_dev_create_by_fd:
 * @fd: passed file descriptor
 * @num_queues: the number of virtqueues
 * @ops: the operation of VDUSE backend
 * @priv: private pointer
 *
 * Create VDUSE device from a passed file descriptor.
 *
 * Returns: pointer to VDUSE device on success, NULL on failure.
 */
VduseDev *vduse_dev_create_by_fd(int fd, uint16_t num_queues,
                                 const VduseOps *ops, void *priv);

/**
 * vduse_dev_create_by_name:
 * @name: VDUSE device name
 * @num_queues: the number of virtqueues
 * @ops: the operation of VDUSE backend
 * @priv: private pointer
 *
 * Create VDUSE device on /dev/vduse/$NAME.
 *
 * Returns: pointer to VDUSE device on success, NULL on failure.
 */
VduseDev *vduse_dev_create_by_name(const char *name, uint16_t num_queues,
                                   const VduseOps *ops, void *priv);

/**
 * vduse_dev_create:
 * @name: VDUSE device name
 * @device_id: virtio device id
 * @vendor_id: virtio vendor id
 * @features: virtio features
 * @num_queues: the number of virtqueues
 * @config_size: the size of the configuration space
 * @config: the buffer of the configuration space
 * @ops: the operation of VDUSE backend
 * @priv: private pointer
 *
 * Create VDUSE device.
 *
 * Returns: pointer to VDUSE device on success, NULL on failure.
 */
VduseDev *vduse_dev_create(const char *name, uint32_t device_id,
                           uint32_t vendor_id, uint64_t features,
                           uint16_t num_queues, uint32_t config_size,
                           char *config, const VduseOps *ops, void *priv);

/**
 * vduse_dev_destroy:
 * @dev: VDUSE device
 *
 * Destroy the VDUSE device.
 *
 * Returns: 0 on success, -errno on failure.
 */
int vduse_dev_destroy(VduseDev *dev);

#endif
