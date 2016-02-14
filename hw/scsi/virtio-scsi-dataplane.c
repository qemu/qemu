/*
 * Virtio SCSI dataplane
 *
 * Copyright Red Hat, Inc. 2014
 *
 * Authors:
 *   Fam Zheng <famz@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/virtio/virtio-scsi.h"
#include "qemu/error-report.h"
#include "sysemu/block-backend.h"
#include <hw/scsi/scsi.h>
#include <block/scsi.h>
#include <hw/virtio/virtio-bus.h>
#include "hw/virtio/virtio-access.h"
#include "stdio.h"

/* Context: QEMU global mutex held */
void virtio_scsi_set_iothread(VirtIOSCSI *s, IOThread *iothread)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(s)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(s);

    assert(!s->ctx);
    s->ctx = iothread_get_aio_context(vs->conf.iothread);

    /* Don't try if transport does not support notifiers. */
    if (!k->set_guest_notifiers || !k->set_host_notifier) {
        fprintf(stderr, "virtio-scsi: Failed to set iothread "
                   "(transport does not support notifiers)");
        exit(1);
    }
}

static int virtio_scsi_vring_init(VirtIOSCSI *s, VirtQueue *vq, int n)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(s)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int rc;

    /* Set up virtqueue notify */
    rc = k->set_host_notifier(qbus->parent, n, true);
    if (rc != 0) {
        fprintf(stderr, "virtio-scsi: Failed to set host notifier (%d)\n",
                rc);
        s->dataplane_fenced = true;
        return rc;
    }

    virtio_queue_aio_set_host_notifier_handler(vq, s->ctx, true, true);
    return 0;
}

void virtio_scsi_dataplane_notify(VirtIODevice *vdev, VirtIOSCSIReq *req)
{
    if (virtio_should_notify(vdev, req->vq)) {
        event_notifier_set(virtio_queue_get_guest_notifier(req->vq));
    }
}

/* assumes s->ctx held */
static void virtio_scsi_clear_aio(VirtIOSCSI *s)
{
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(s);
    int i;

    virtio_queue_aio_set_host_notifier_handler(vs->ctrl_vq, s->ctx, false, false);
    virtio_queue_aio_set_host_notifier_handler(vs->event_vq, s->ctx, false, false);
    for (i = 0; i < vs->conf.num_queues; i++) {
        virtio_queue_aio_set_host_notifier_handler(vs->cmd_vqs[i], s->ctx, false, false);
    }
}

/* Context: QEMU global mutex held */
void virtio_scsi_dataplane_start(VirtIOSCSI *s)
{
    int i;
    int rc;
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(s)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(s);

    if (s->dataplane_started ||
        s->dataplane_starting ||
        s->dataplane_fenced ||
        s->ctx != iothread_get_aio_context(vs->conf.iothread)) {
        return;
    }

    s->dataplane_starting = true;

    /* Set up guest notifier (irq) */
    rc = k->set_guest_notifiers(qbus->parent, vs->conf.num_queues + 2, true);
    if (rc != 0) {
        fprintf(stderr, "virtio-scsi: Failed to set guest notifiers (%d), "
                "ensure -enable-kvm is set\n", rc);
        goto fail_guest_notifiers;
    }

    aio_context_acquire(s->ctx);
    rc = virtio_scsi_vring_init(s, vs->ctrl_vq, 0);
    if (rc) {
        goto fail_vrings;
    }
    rc = virtio_scsi_vring_init(s, vs->event_vq, 1);
    if (rc) {
        goto fail_vrings;
    }
    for (i = 0; i < vs->conf.num_queues; i++) {
        rc = virtio_scsi_vring_init(s, vs->cmd_vqs[i], i + 2);
        if (rc) {
            goto fail_vrings;
        }
    }

    s->dataplane_starting = false;
    s->dataplane_started = true;
    aio_context_release(s->ctx);
    return;

fail_vrings:
    virtio_scsi_clear_aio(s);
    aio_context_release(s->ctx);
    for (i = 0; i < vs->conf.num_queues + 2; i++) {
        k->set_host_notifier(qbus->parent, i, false);
    }
    k->set_guest_notifiers(qbus->parent, vs->conf.num_queues + 2, false);
fail_guest_notifiers:
    s->dataplane_fenced = true;
    s->dataplane_starting = false;
    s->dataplane_started = true;
}

/* Context: QEMU global mutex held */
void virtio_scsi_dataplane_stop(VirtIOSCSI *s)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(s)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(s);
    int i;

    if (!s->dataplane_started || s->dataplane_stopping) {
        return;
    }

    /* Better luck next time. */
    if (s->dataplane_fenced) {
        s->dataplane_fenced = false;
        s->dataplane_started = false;
        return;
    }
    s->dataplane_stopping = true;
    assert(s->ctx == iothread_get_aio_context(vs->conf.iothread));

    aio_context_acquire(s->ctx);

    virtio_scsi_clear_aio(s);

    blk_drain_all(); /* ensure there are no in-flight requests */

    aio_context_release(s->ctx);

    for (i = 0; i < vs->conf.num_queues + 2; i++) {
        k->set_host_notifier(qbus->parent, i, false);
    }

    /* Clean up guest notifier (irq) */
    k->set_guest_notifiers(qbus->parent, vs->conf.num_queues + 2, false);
    s->dataplane_stopping = false;
    s->dataplane_started = false;
}
