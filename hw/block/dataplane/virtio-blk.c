/*
 * Dedicated thread for virtio-blk I/O processing
 *
 * Copyright 2012 IBM, Corp.
 * Copyright 2012 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *   Stefan Hajnoczi <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "trace.h"
#include "qemu/iov.h"
#include "qemu/thread.h"
#include "qemu/error-report.h"
#include "hw/virtio/virtio-access.h"
#include "sysemu/block-backend.h"
#include "hw/virtio/virtio-blk.h"
#include "virtio-blk.h"
#include "block/aio.h"
#include "hw/virtio/virtio-bus.h"
#include "qom/object_interfaces.h"

struct VirtIOBlockDataPlane {
    bool starting;
    bool stopping;

    VirtIOBlkConf *conf;

    VirtIODevice *vdev;
    VirtQueue *vq;                  /* virtqueue vring */
    EventNotifier *guest_notifier;  /* irq */
    QEMUBH *bh;                     /* bh for guest notification */

    /* Note that these EventNotifiers are assigned by value.  This is
     * fine as long as you do not call event_notifier_cleanup on them
     * (because you don't own the file descriptor or handle; you just
     * use it).
     */
    IOThread *iothread;
    AioContext *ctx;
};

/* Raise an interrupt to signal guest, if necessary */
void virtio_blk_data_plane_notify(VirtIOBlockDataPlane *s)
{
    qemu_bh_schedule(s->bh);
}

static void notify_guest_bh(void *opaque)
{
    VirtIOBlockDataPlane *s = opaque;

    if (!virtio_should_notify(s->vdev, s->vq)) {
        return;
    }

    event_notifier_set(s->guest_notifier);
}

/* Context: QEMU global mutex held */
void virtio_blk_data_plane_create(VirtIODevice *vdev, VirtIOBlkConf *conf,
                                  VirtIOBlockDataPlane **dataplane,
                                  Error **errp)
{
    VirtIOBlockDataPlane *s;
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);

    *dataplane = NULL;

    if (!conf->iothread) {
        return;
    }

    /* Don't try if transport does not support notifiers. */
    if (!k->set_guest_notifiers || !k->set_host_notifier) {
        error_setg(errp,
                   "device is incompatible with dataplane "
                   "(transport does not support notifiers)");
        return;
    }

    /* If dataplane is (re-)enabled while the guest is running there could be
     * block jobs that can conflict.
     */
    if (blk_op_is_blocked(conf->conf.blk, BLOCK_OP_TYPE_DATAPLANE, errp)) {
        error_prepend(errp, "cannot start dataplane thread: ");
        return;
    }

    s = g_new0(VirtIOBlockDataPlane, 1);
    s->vdev = vdev;
    s->conf = conf;

    if (conf->iothread) {
        s->iothread = conf->iothread;
        object_ref(OBJECT(s->iothread));
    }
    s->ctx = iothread_get_aio_context(s->iothread);
    s->bh = aio_bh_new(s->ctx, notify_guest_bh, s);

    *dataplane = s;
}

/* Context: QEMU global mutex held */
void virtio_blk_data_plane_destroy(VirtIOBlockDataPlane *s)
{
    if (!s) {
        return;
    }

    virtio_blk_data_plane_stop(s);
    qemu_bh_delete(s->bh);
    object_unref(OBJECT(s->iothread));
    g_free(s);
}

static void virtio_blk_data_plane_handle_output(VirtIODevice *vdev,
                                                VirtQueue *vq)
{
    VirtIOBlock *s = (VirtIOBlock *)vdev;

    assert(s->dataplane);
    assert(s->dataplane_started);

    virtio_blk_handle_vq(s, vq);
}

/* Context: QEMU global mutex held */
void virtio_blk_data_plane_start(VirtIOBlockDataPlane *s)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(s->vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    VirtIOBlock *vblk = VIRTIO_BLK(s->vdev);
    int r;

    if (vblk->dataplane_started || s->starting) {
        return;
    }

    s->starting = true;
    s->vq = virtio_get_queue(s->vdev, 0);

    /* Set up guest notifier (irq) */
    r = k->set_guest_notifiers(qbus->parent, 1, true);
    if (r != 0) {
        fprintf(stderr, "virtio-blk failed to set guest notifier (%d), "
                "ensure -enable-kvm is set\n", r);
        goto fail_guest_notifiers;
    }
    s->guest_notifier = virtio_queue_get_guest_notifier(s->vq);

    /* Set up virtqueue notify */
    r = k->set_host_notifier(qbus->parent, 0, true);
    if (r != 0) {
        fprintf(stderr, "virtio-blk failed to set host notifier (%d)\n", r);
        goto fail_host_notifier;
    }

    s->starting = false;
    vblk->dataplane_started = true;
    trace_virtio_blk_data_plane_start(s);

    blk_set_aio_context(s->conf->conf.blk, s->ctx);

    /* Kick right away to begin processing requests already in vring */
    event_notifier_set(virtio_queue_get_host_notifier(s->vq));

    /* Get this show started by hooking up our callbacks */
    aio_context_acquire(s->ctx);
    virtio_queue_aio_set_host_notifier_handler(s->vq, s->ctx,
                                               virtio_blk_data_plane_handle_output);
    aio_context_release(s->ctx);
    return;

  fail_host_notifier:
    k->set_guest_notifiers(qbus->parent, 1, false);
  fail_guest_notifiers:
    vblk->dataplane_disabled = true;
    s->starting = false;
    vblk->dataplane_started = true;
}

/* Context: QEMU global mutex held */
void virtio_blk_data_plane_stop(VirtIOBlockDataPlane *s)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(s->vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    VirtIOBlock *vblk = VIRTIO_BLK(s->vdev);

    if (!vblk->dataplane_started || s->stopping) {
        return;
    }

    /* Better luck next time. */
    if (vblk->dataplane_disabled) {
        vblk->dataplane_disabled = false;
        vblk->dataplane_started = false;
        return;
    }
    s->stopping = true;
    trace_virtio_blk_data_plane_stop(s);

    aio_context_acquire(s->ctx);

    /* Stop notifications for new requests from guest */
    virtio_queue_aio_set_host_notifier_handler(s->vq, s->ctx, NULL);

    /* Drain and switch bs back to the QEMU main loop */
    blk_set_aio_context(s->conf->conf.blk, qemu_get_aio_context());

    aio_context_release(s->ctx);

    k->set_host_notifier(qbus->parent, 0, false);

    /* Clean up guest notifier (irq) */
    k->set_guest_notifiers(qbus->parent, 1, false);

    vblk->dataplane_started = false;
    s->stopping = false;
}
