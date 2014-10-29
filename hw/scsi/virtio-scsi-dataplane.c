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

static VirtIOSCSIVring *virtio_scsi_vring_init(VirtIOSCSI *s,
                                               VirtQueue *vq,
                                               EventNotifierHandler *handler,
                                               int n)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(s)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    VirtIOSCSIVring *r = g_slice_new(VirtIOSCSIVring);

    /* Set up virtqueue notify */
    if (k->set_host_notifier(qbus->parent, n, true) != 0) {
        fprintf(stderr, "virtio-scsi: Failed to set host notifier\n");
        exit(1);
    }
    r->host_notifier = *virtio_queue_get_host_notifier(vq);
    r->guest_notifier = *virtio_queue_get_guest_notifier(vq);
    aio_set_event_notifier(s->ctx, &r->host_notifier, handler);

    r->parent = s;

    if (!vring_setup(&r->vring, VIRTIO_DEVICE(s), n)) {
        fprintf(stderr, "virtio-scsi: VRing setup failed\n");
        exit(1);
    }
    return r;
}

VirtIOSCSIReq *virtio_scsi_pop_req_vring(VirtIOSCSI *s,
                                         VirtIOSCSIVring *vring)
{
    VirtIOSCSIReq *req = virtio_scsi_init_req(s, NULL);
    int r;

    req->vring = vring;
    r = vring_pop((VirtIODevice *)s, &vring->vring, &req->elem);
    if (r < 0) {
        virtio_scsi_free_req(req);
        req = NULL;
    }
    return req;
}

void virtio_scsi_vring_push_notify(VirtIOSCSIReq *req)
{
    vring_push(&req->vring->vring, &req->elem,
               req->qsgl.size + req->resp_iov.size);
    event_notifier_set(&req->vring->guest_notifier);
}

static void virtio_scsi_iothread_handle_ctrl(EventNotifier *notifier)
{
    VirtIOSCSIVring *vring = container_of(notifier,
                                          VirtIOSCSIVring, host_notifier);
    VirtIOSCSI *s = VIRTIO_SCSI(vring->parent);
    VirtIOSCSIReq *req;

    event_notifier_test_and_clear(notifier);
    while ((req = virtio_scsi_pop_req_vring(s, vring))) {
        virtio_scsi_handle_ctrl_req(s, req);
    }
}

static void virtio_scsi_iothread_handle_event(EventNotifier *notifier)
{
    VirtIOSCSIVring *vring = container_of(notifier,
                                          VirtIOSCSIVring, host_notifier);
    VirtIOSCSI *s = vring->parent;
    VirtIODevice *vdev = VIRTIO_DEVICE(s);

    event_notifier_test_and_clear(notifier);

    if (!(vdev->status & VIRTIO_CONFIG_S_DRIVER_OK)) {
        return;
    }

    if (s->events_dropped) {
        virtio_scsi_push_event(s, NULL, VIRTIO_SCSI_T_NO_EVENT, 0);
    }
}

static void virtio_scsi_iothread_handle_cmd(EventNotifier *notifier)
{
    VirtIOSCSIVring *vring = container_of(notifier,
                                          VirtIOSCSIVring, host_notifier);
    VirtIOSCSI *s = (VirtIOSCSI *)vring->parent;
    VirtIOSCSIReq *req, *next;
    QTAILQ_HEAD(, VirtIOSCSIReq) reqs = QTAILQ_HEAD_INITIALIZER(reqs);

    event_notifier_test_and_clear(notifier);
    while ((req = virtio_scsi_pop_req_vring(s, vring))) {
        if (virtio_scsi_handle_cmd_req_prepare(s, req)) {
            QTAILQ_INSERT_TAIL(&reqs, req, next);
        }
    }

    QTAILQ_FOREACH_SAFE(req, &reqs, next, next) {
        virtio_scsi_handle_cmd_req_submit(s, req);
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
        s->ctx != iothread_get_aio_context(vs->conf.iothread)) {
        return;
    }

    s->dataplane_starting = true;

    /* Set up guest notifier (irq) */
    rc = k->set_guest_notifiers(qbus->parent, vs->conf.num_queues + 2, true);
    if (rc != 0) {
        fprintf(stderr, "virtio-scsi: Failed to set guest notifiers, "
                "ensure -enable-kvm is set\n");
        exit(1);
    }

    aio_context_acquire(s->ctx);
    s->ctrl_vring = virtio_scsi_vring_init(s, vs->ctrl_vq,
                                           virtio_scsi_iothread_handle_ctrl,
                                           0);
    s->event_vring = virtio_scsi_vring_init(s, vs->event_vq,
                                            virtio_scsi_iothread_handle_event,
                                            1);
    s->cmd_vrings = g_malloc0(sizeof(VirtIOSCSIVring) * vs->conf.num_queues);
    for (i = 0; i < vs->conf.num_queues; i++) {
        s->cmd_vrings[i] =
            virtio_scsi_vring_init(s, vs->cmd_vqs[i],
                                   virtio_scsi_iothread_handle_cmd,
                                   i + 2);
    }

    aio_context_release(s->ctx);
    s->dataplane_starting = false;
    s->dataplane_started = true;
}

/* Context: QEMU global mutex held */
void virtio_scsi_dataplane_stop(VirtIOSCSI *s)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(s)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    VirtIODevice *vdev = VIRTIO_DEVICE(s);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(s);
    int i;

    if (!s->dataplane_started || s->dataplane_stopping) {
        return;
    }
    s->dataplane_stopping = true;
    assert(s->ctx == iothread_get_aio_context(vs->conf.iothread));

    aio_context_acquire(s->ctx);

    aio_set_event_notifier(s->ctx, &s->ctrl_vring->host_notifier, NULL);
    aio_set_event_notifier(s->ctx, &s->event_vring->host_notifier, NULL);
    for (i = 0; i < vs->conf.num_queues; i++) {
        aio_set_event_notifier(s->ctx, &s->cmd_vrings[i]->host_notifier, NULL);
    }

    blk_drain_all(); /* ensure there are no in-flight requests */

    aio_context_release(s->ctx);

    /* Sync vring state back to virtqueue so that non-dataplane request
     * processing can continue when we disable the host notifier below.
     */
    vring_teardown(&s->ctrl_vring->vring, vdev, 0);
    vring_teardown(&s->event_vring->vring, vdev, 1);
    for (i = 0; i < vs->conf.num_queues; i++) {
        vring_teardown(&s->cmd_vrings[i]->vring, vdev, 2 + i);
    }

    for (i = 0; i < vs->conf.num_queues + 2; i++) {
        k->set_host_notifier(qbus->parent, i, false);
    }

    /* Clean up guest notifier (irq) */
    k->set_guest_notifiers(qbus->parent, vs->conf.num_queues + 2, false);
    s->dataplane_stopping = false;
    s->dataplane_started = false;
}
