/*
 * QEMU Crypto Device Common Vhost Implement
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Gonglei <arei.gonglei@huawei.com>
 *    Jay Zhou <jianjay.zhou@huawei.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */
#ifndef CRYPTODEV_VHOST_H
#define CRYPTODEV_VHOST_H

#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-backend.h"
#include "chardev/char.h"

#include "sysemu/cryptodev.h"


typedef struct CryptoDevBackendVhostOptions {
    VhostBackendType backend_type;
    void *opaque;
    int total_queues;
    CryptoDevBackendClient *cc;
} CryptoDevBackendVhostOptions;

typedef struct CryptoDevBackendVhost {
    struct vhost_dev dev;
    struct vhost_virtqueue vqs[1];
    int backend;
    CryptoDevBackendClient *cc;
} CryptoDevBackendVhost;

/**
 * cryptodev_vhost_get_max_queues:
 * @crypto: the cryptodev backend common vhost object
 *
 * Get the maximum queue number of @crypto.
 *
 *
 * Returns: the maximum queue number
 */
uint64_t
cryptodev_vhost_get_max_queues(
                        CryptoDevBackendVhost *crypto);


/**
 * cryptodev_vhost_init:
 * @options: the common vhost object's option
 *
 * Creates a new cryptodev backend common vhost object
 *
 ** The returned object must be released with
 * cryptodev_vhost_cleanup() when no
 * longer required
 *
 * Returns: the cryptodev backend common vhost object
 */
struct CryptoDevBackendVhost *
cryptodev_vhost_init(
             CryptoDevBackendVhostOptions *options);

/**
 * cryptodev_vhost_cleanup:
 * @crypto: the cryptodev backend common vhost object
 *
 * Clean the resource associated with @crypto that realizaed
 * by cryptodev_vhost_init()
 *
 */
void cryptodev_vhost_cleanup(
                        CryptoDevBackendVhost *crypto);

/**
 * cryptodev_get_vhost:
 * @cc: the client object for each queue
 * @b: the cryptodev backend common vhost object
 * @queue: the cryptodev backend queue index
 *
 * Gets a new cryptodev backend common vhost object based on
 * @b and @queue
 *
 * Returns: the cryptodev backend common vhost object
 */
CryptoDevBackendVhost *
cryptodev_get_vhost(CryptoDevBackendClient *cc,
                            CryptoDevBackend *b,
                            uint16_t queue);
/**
 * cryptodev_vhost_start:
 * @dev: the virtio crypto object
 * @total_queues: the total count of queue
 *
 * Starts the vhost crypto logic
 *
 * Returns: 0 for success, negative for errors
 */
int cryptodev_vhost_start(VirtIODevice *dev, int total_queues);

/**
 * cryptodev_vhost_stop:
 * @dev: the virtio crypto object
 * @total_queues: the total count of queue
 *
 * Stops the vhost crypto logic
 *
 */
void cryptodev_vhost_stop(VirtIODevice *dev, int total_queues);

/**
 * cryptodev_vhost_virtqueue_mask:
 * @dev: the virtio crypto object
 * @queue: the cryptodev backend queue index
 * @idx: the virtqueue index
 * @mask: mask or not (true or false)
 *
 * Mask/unmask events for @idx virtqueue on @dev device
 *
 */
void cryptodev_vhost_virtqueue_mask(VirtIODevice *dev,
                                           int queue,
                                           int idx, bool mask);

/**
 * cryptodev_vhost_virtqueue_pending:
 * @dev: the virtio crypto object
 * @queue: the cryptodev backend queue index
 * @idx: the virtqueue index
 *
 * Test and clear event pending status for @idx virtqueue on @dev device.
 * Should be called after unmask to avoid losing events.
 *
 * Returns: true for success, false for errors
 */
bool cryptodev_vhost_virtqueue_pending(VirtIODevice *dev,
                                              int queue, int idx);

#endif /* CRYPTODEV_VHOST_H */
