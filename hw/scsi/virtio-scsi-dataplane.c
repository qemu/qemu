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
    VirtIOSCSIVring *r;
    int rc;

    /* Set up virtqueue notify */
    rc = k->set_host_notifier(qbus->parent, n, true);
    if (rc != 0) {
        fprintf(stderr, "virtio-scsi: Failed to set host notifier (%d)\n",
                rc);
        s->dataplane_fenced = true;
        return NULL;
    }

    r = g_slice_new(VirtIOSCSIVring);
    r->host_notifier = *virtio_queue_get_host_notifier(vq);
    r->guest_notifier = *virtio_queue_get_guest_notifier(vq);
    aio_set_event_notifier(s->ctx, &r->host_notifier, handler);

    r->parent = s;

    if (!vring_setup(&r->vring, VIRTIO_DEVICE(s), n)) {
        fprintf(stderr, "virtio-scsi: VRing setup failed\n");
        goto fail_vring;
    }
    return r;

fail_vring:
    aio_set_event_notifier(s->ctx, &r->host_notifier, NULL);
    k->set_host_notifier(qbus->parent, n, false);
    g_slice_free(VirtIOSCSIVring, r);
    return NULL;
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
    VirtIODevice *vdev = VIRTIO_DEVICE(req->vring->parent);

    vring_push(vdev, &req->vring->vring, &req->elem,
               req->qsgl.size + req->resp_iov.size);

    if (vring_should_notify(vdev, &req->vring->vring)) {
        event_notifier_set(&req->vring->guest_notifier);
    }
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

/* assumes s->ctx held */
static void virtio_scsi_clear_aio(VirtIOSCSI *s)
{
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(s);
    int i;

    if (s->ctrl_vring) {
        aio_set_event_notifier(s->ctx, &s->ctrl_vring->host_notifier, NULL);
    }
    if (s->event_vring) {
        aio_set_event_notifier(s->ctx, &s->event_vring->host_notifier, NULL);
    }
    if (s->cmd_vrings) {
        for (i = 0; i < vs->conf.num_queues && s->cmd_vrings[i]; i++) {
            aio_set_event_notifier(s->ctx, &s->cmd_vrings[i]->host_notifier, NULL);
        }
    }
}

static void virtio_scsi_vring_teardown(VirtIOSCSI *s)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(s);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(s);
    int i;

    if (s->ctrl_vring) {
        vring_teardown(&s->ctrl_vring->vring, vdev, 0);
        g_slice_free(VirtIOSCSIVring, s->ctrl_vring);
        s->ctrl_vring = NULL;
    }
    if (s->event_vring) {
        vring_teardown(&s->event_vring->vring, vdev, 1);
        g_slice_free(VirtIOSCSIVring, s->event_vring);
        s->event_vring = NULL;
    }
    if (s->cmd_vrings) {
        for (i = 0; i < vs->conf.num_queues && s->cmd_vrings[i]; i++) {
            vring_teardown(&s->cmd_vrings[i]->vring, vdev, 2 + i);
            g_slice_free(VirtIOSCSIVring, s->cmd_vrings[i]);
            s->cmd_vrings[i] = NULL;
        }
        free(s->cmd_vrings);
        s->cmd_vrings = NULL;
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
        s->dataplane_fenced = true;
        goto fail_guest_notifiers;
    }

    aio_context_acquire(s->ctx);
    s->ctrl_vring = virtio_scsi_vring_init(s, vs->ctrl_vq,
                                           virtio_scsi_iothread_handle_ctrl,
                                           0);
    if (!s->ctrl_vring) {
        goto fail_vrings;
    }
    s->event_vring = virtio_scsi_vring_init(s, vs->event_vq,
                                            virtio_scsi_iothread_handle_event,
                                            1);
    if (!s->event_vring) {
        goto fail_vrings;
    }
    s->cmd_vrings = g_new(VirtIOSCSIVring *, vs->conf.num_queues);
    for (i = 0; i < vs->conf.num_queues; i++) {
        s->cmd_vrings[i] =
            virtio_scsi_vring_init(s, vs->cmd_vqs[i],
                                   virtio_scsi_iothread_handle_cmd,
                                   i + 2);
        if (!s->cmd_vrings[i]) {
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
    virtio_scsi_vring_teardown(s);
    for (i = 0; i < vs->conf.num_queues + 2; i++) {
        k->set_host_notifier(qbus->parent, i, false);
    }
    k->set_guest_notifiers(qbus->parent, vs->conf.num_queues + 2, false);
fail_guest_notifiers:
    s->dataplane_starting = false;
}

/* Context: QEMU global mutex held */
void virtio_scsi_dataplane_stop(VirtIOSCSI *s)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(s)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(s);
    int i;

    /* Better luck next time. */
    if (s->dataplane_fenced) {
        s->dataplane_fenced = false;
        return;
    }
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
    virtio_scsi_vring_teardown(s);

    for (i = 0; i < vs->conf.num_queues + 2; i++) {
        k->set_host_notifier(qbus->parent, i, false);
    }

    /* Clean up guest notifier (irq) */
    k->set_guest_notifiers(qbus->parent, vs->conf.num_queues + 2, false);
    s->dataplane_stopping = false;
    s->dataplane_started = false;
}
