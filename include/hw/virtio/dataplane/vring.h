/* Copyright 2012 Red Hat, Inc. and/or its affiliates
 * Copyright IBM, Corp. 2012
 *
 * Based on Linux 2.6.39 vhost code:
 * Copyright (C) 2009 Red Hat, Inc.
 * Copyright (C) 2006 Rusty Russell IBM Corporation
 *
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *         Stefan Hajnoczi <stefanha@redhat.com>
 *
 * Inspiration, some code, and most witty comments come from
 * Documentation/virtual/lguest/lguest.c, by Rusty Russell
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 */

#ifndef VRING_H
#define VRING_H

#include <linux/virtio_ring.h>
#include "qemu-common.h"
#include "hw/virtio/virtio.h"

typedef struct {
    MemoryRegion *mr;               /* memory region containing the vring */
    struct vring vr;                /* virtqueue vring mapped to host memory */
    uint16_t last_avail_idx;        /* last processed avail ring index */
    uint16_t last_used_idx;         /* last processed used ring index */
    uint16_t signalled_used;        /* EVENT_IDX state */
    bool signalled_used_valid;
    bool broken;                    /* was there a fatal error? */
} Vring;

static inline unsigned int vring_get_num(Vring *vring)
{
    return vring->vr.num;
}

/* Are there more descriptors available? */
static inline bool vring_more_avail(Vring *vring)
{
    return vring->vr.avail->idx != vring->last_avail_idx;
}

/* Fail future vring_pop() and vring_push() calls until reset */
static inline void vring_set_broken(Vring *vring)
{
    vring->broken = true;
}

bool vring_setup(Vring *vring, VirtIODevice *vdev, int n);
void vring_teardown(Vring *vring, VirtIODevice *vdev, int n);
void vring_disable_notification(VirtIODevice *vdev, Vring *vring);
bool vring_enable_notification(VirtIODevice *vdev, Vring *vring);
bool vring_should_notify(VirtIODevice *vdev, Vring *vring);
int vring_pop(VirtIODevice *vdev, Vring *vring, VirtQueueElement **elem);
void vring_push(Vring *vring, VirtQueueElement *elem, int len);

#endif /* VRING_H */
