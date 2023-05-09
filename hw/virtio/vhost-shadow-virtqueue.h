/*
 * vhost shadow virtqueue
 *
 * SPDX-FileCopyrightText: Red Hat, Inc. 2021
 * SPDX-FileContributor: Author: Eugenio Pérez <eperezma@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef VHOST_SHADOW_VIRTQUEUE_H
#define VHOST_SHADOW_VIRTQUEUE_H

#include "qemu/event_notifier.h"
#include "hw/virtio/virtio.h"
#include "standard-headers/linux/vhost_types.h"
#include "hw/virtio/vhost-iova-tree.h"

typedef struct SVQDescState {
    VirtQueueElement *elem;

    /*
     * Number of descriptors exposed to the device. May or may not match
     * guest's
     */
    unsigned int ndescs;
} SVQDescState;

typedef struct VhostShadowVirtqueue VhostShadowVirtqueue;

/**
 * Callback to handle an avail buffer.
 *
 * @svq:  Shadow virtqueue
 * @elem:  Element placed in the queue by the guest
 * @vq_callback_opaque:  Opaque
 *
 * Returns 0 if the vq is running as expected.
 *
 * Note that ownership of elem is transferred to the callback.
 */
typedef int (*VirtQueueAvailCallback)(VhostShadowVirtqueue *svq,
                                      VirtQueueElement *elem,
                                      void *vq_callback_opaque);

typedef struct VhostShadowVirtqueueOps {
    VirtQueueAvailCallback avail_handler;
} VhostShadowVirtqueueOps;

/* Shadow virtqueue to relay notifications */
typedef struct VhostShadowVirtqueue {
    /* Shadow vring */
    struct vring vring;

    /* Shadow kick notifier, sent to vhost */
    EventNotifier hdev_kick;
    /* Shadow call notifier, sent to vhost */
    EventNotifier hdev_call;

    /*
     * Borrowed virtqueue's guest to host notifier. To borrow it in this event
     * notifier allows to recover the VhostShadowVirtqueue from the event loop
     * easily. If we use the VirtQueue's one, we don't have an easy way to
     * retrieve VhostShadowVirtqueue.
     *
     * So shadow virtqueue must not clean it, or we would lose VirtQueue one.
     */
    EventNotifier svq_kick;

    /* Guest's call notifier, where the SVQ calls guest. */
    EventNotifier svq_call;

    /* Virtio queue shadowing */
    VirtQueue *vq;

    /* Virtio device */
    VirtIODevice *vdev;

    /* IOVA mapping */
    VhostIOVATree *iova_tree;

    /* SVQ vring descriptors state */
    SVQDescState *desc_state;

    /* Next VirtQueue element that guest made available */
    VirtQueueElement *next_guest_avail_elem;

    /*
     * Backup next field for each descriptor so we can recover securely, not
     * needing to trust the device access.
     */
    uint16_t *desc_next;

    /* Caller callbacks */
    const VhostShadowVirtqueueOps *ops;

    /* Caller callbacks opaque */
    void *ops_opaque;

    /* Next head to expose to the device */
    uint16_t shadow_avail_idx;

    /* Next free descriptor */
    uint16_t free_head;

    /* Last seen used idx */
    uint16_t shadow_used_idx;

    /* Next head to consume from the device */
    uint16_t last_used_idx;

    /* Size of SVQ vring free descriptors */
    uint16_t num_free;
} VhostShadowVirtqueue;

bool vhost_svq_valid_features(uint64_t features, Error **errp);

void vhost_svq_push_elem(VhostShadowVirtqueue *svq,
                         const VirtQueueElement *elem, uint32_t len);
int vhost_svq_add(VhostShadowVirtqueue *svq, const struct iovec *out_sg,
                  size_t out_num, const struct iovec *in_sg, size_t in_num,
                  VirtQueueElement *elem);
size_t vhost_svq_poll(VhostShadowVirtqueue *svq);

void vhost_svq_set_svq_kick_fd(VhostShadowVirtqueue *svq, int svq_kick_fd);
void vhost_svq_set_svq_call_fd(VhostShadowVirtqueue *svq, int call_fd);
void vhost_svq_get_vring_addr(const VhostShadowVirtqueue *svq,
                              struct vhost_vring_addr *addr);
size_t vhost_svq_driver_area_size(const VhostShadowVirtqueue *svq);
size_t vhost_svq_device_area_size(const VhostShadowVirtqueue *svq);

void vhost_svq_start(VhostShadowVirtqueue *svq, VirtIODevice *vdev,
                     VirtQueue *vq);
void vhost_svq_stop(VhostShadowVirtqueue *svq);

VhostShadowVirtqueue *vhost_svq_new(VhostIOVATree *iova_tree,
                                    const VhostShadowVirtqueueOps *ops,
                                    void *ops_opaque);

void vhost_svq_free(gpointer vq);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(VhostShadowVirtqueue, vhost_svq_free);

#endif
