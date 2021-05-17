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
#include "qemu/main-loop.h"
#include "qemu/thread.h"
#include "qemu/error-report.h"
#include "hw/virtio/virtio-access.h"
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
    QEMUBH *bh;                     /* bh for guest notification */
    unsigned long *batch_notify_vqs;
    bool batch_notifications;

    /* Note that these EventNotifiers are assigned by value.  This is
     * fine as long as you do not call event_notifier_cleanup on them
     * (because you don't own the file descriptor or handle; you just
     * use it).
     */
    IOThread *iothread;
    AioContext *ctx;
};

/* Raise an interrupt to signal guest, if necessary */
void virtio_blk_data_plane_notify(VirtIOBlockDataPlane *s, VirtQueue *vq)
{
    if (s->batch_notifications) {
        set_bit(virtio_get_queue_index(vq), s->batch_notify_vqs);
        qemu_bh_schedule(s->bh);
    } else {
        virtio_notify_irqfd(s->vdev, vq);
    }
}

static void notify_guest_bh(void *opaque)
{
    VirtIOBlockDataPlane *s = opaque;
    unsigned nvqs = s->conf->num_queues;
    unsigned long bitmap[BITS_TO_LONGS(nvqs)];
    unsigned j;

    memcpy(bitmap, s->batch_notify_vqs, sizeof(bitmap));
    memset(s->batch_notify_vqs, 0, sizeof(bitmap));

    for (j = 0; j < nvqs; j += BITS_PER_LONG) {
        unsigned long bits = bitmap[j / BITS_PER_LONG];

        while (bits != 0) {
            unsigned i = j + ctzl(bits);
            VirtQueue *vq = virtio_get_queue(s->vdev, i);

            virtio_notify_irqfd(s->vdev, vq);

            bits &= bits - 1; /* clear right-most bit */
        }
    }
}

/* Context: QEMU global mutex held */
bool virtio_blk_data_plane_create(VirtIODevice *vdev, VirtIOBlkConf *conf,
                                  VirtIOBlockDataPlane **dataplane,
                                  Error **errp)
{
    VirtIOBlockDataPlane *s;
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);

    *dataplane = NULL;

    if (conf->iothread) {
        if (!k->set_guest_notifiers || !k->ioeventfd_assign) {
            error_setg(errp,
                       "device is incompatible with iothread "
                       "(transport does not support notifiers)");
            return false;
        }
        if (!virtio_device_ioeventfd_enabled(vdev)) {
            error_setg(errp, "ioeventfd is required for iothread");
            return false;
        }

        /* If dataplane is (re-)enabled while the guest is running there could
         * be block jobs that can conflict.
         */
        if (blk_op_is_blocked(conf->conf.blk, BLOCK_OP_TYPE_DATAPLANE, errp)) {
            error_prepend(errp, "cannot start virtio-blk dataplane: ");
            return false;
        }
    }
    /* Don't try if transport does not support notifiers. */
    if (!virtio_device_ioeventfd_enabled(vdev)) {
        return false;
    }

    s = g_new0(VirtIOBlockDataPlane, 1);
    s->vdev = vdev;
    s->conf = conf;

    if (conf->iothread) {
        s->iothread = conf->iothread;
        object_ref(OBJECT(s->iothread));
        s->ctx = iothread_get_aio_context(s->iothread);
    } else {
        s->ctx = qemu_get_aio_context();
    }
    s->bh = aio_bh_new(s->ctx, notify_guest_bh, s);
    s->batch_notify_vqs = bitmap_new(conf->num_queues);

    *dataplane = s;

    return true;
}

/* Context: QEMU global mutex held */
void virtio_blk_data_plane_destroy(VirtIOBlockDataPlane *s)
{
    VirtIOBlock *vblk;

    if (!s) {
        return;
    }

    vblk = VIRTIO_BLK(s->vdev);
    assert(!vblk->dataplane_started);
    g_free(s->batch_notify_vqs);
    qemu_bh_delete(s->bh);
    if (s->iothread) {
        object_unref(OBJECT(s->iothread));
    }
    g_free(s);
}

static bool virtio_blk_data_plane_handle_output(VirtIODevice *vdev,
                                                VirtQueue *vq)
{
    VirtIOBlock *s = (VirtIOBlock *)vdev;

    assert(s->dataplane);
    assert(s->dataplane_started);

    return virtio_blk_handle_vq(s, vq);
}

/* Context: QEMU global mutex held */
int virtio_blk_data_plane_start(VirtIODevice *vdev)
{
    VirtIOBlock *vblk = VIRTIO_BLK(vdev);
    VirtIOBlockDataPlane *s = vblk->dataplane;
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vblk)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    AioContext *old_context;
    unsigned i;
    unsigned nvqs = s->conf->num_queues;
    Error *local_err = NULL;
    int r;

    if (vblk->dataplane_started || s->starting) {
        return 0;
    }

    s->starting = true;

    if (!virtio_vdev_has_feature(vdev, VIRTIO_RING_F_EVENT_IDX)) {
        s->batch_notifications = true;
    } else {
        s->batch_notifications = false;
    }

    /* Set up guest notifier (irq) */
    r = k->set_guest_notifiers(qbus->parent, nvqs, true);
    if (r != 0) {
        error_report("virtio-blk failed to set guest notifier (%d), "
                     "ensure -accel kvm is set.", r);
        goto fail_guest_notifiers;
    }

    /*
     * Batch all the host notifiers in a single transaction to avoid
     * quadratic time complexity in address_space_update_ioeventfds().
     */
    memory_region_transaction_begin();

    /* Set up virtqueue notify */
    for (i = 0; i < nvqs; i++) {
        r = virtio_bus_set_host_notifier(VIRTIO_BUS(qbus), i, true);
        if (r != 0) {
            int j = i;

            fprintf(stderr, "virtio-blk failed to set host notifier (%d)\n", r);
            while (i--) {
                virtio_bus_set_host_notifier(VIRTIO_BUS(qbus), i, false);
            }

            /*
             * The transaction expects the ioeventfds to be open when it
             * commits. Do it now, before the cleanup loop.
             */
            memory_region_transaction_commit();

            while (j--) {
                virtio_bus_cleanup_host_notifier(VIRTIO_BUS(qbus), i);
            }
            goto fail_host_notifiers;
        }
    }

    memory_region_transaction_commit();

    s->starting = false;
    vblk->dataplane_started = true;
    trace_virtio_blk_data_plane_start(s);

    old_context = blk_get_aio_context(s->conf->conf.blk);
    aio_context_acquire(old_context);
    r = blk_set_aio_context(s->conf->conf.blk, s->ctx, &local_err);
    aio_context_release(old_context);
    if (r < 0) {
        error_report_err(local_err);
        goto fail_aio_context;
    }

    /* Process queued requests before the ones in vring */
    virtio_blk_process_queued_requests(vblk, false);

    /* Kick right away to begin processing requests already in vring */
    for (i = 0; i < nvqs; i++) {
        VirtQueue *vq = virtio_get_queue(s->vdev, i);

        event_notifier_set(virtio_queue_get_host_notifier(vq));
    }

    /* Get this show started by hooking up our callbacks */
    aio_context_acquire(s->ctx);
    for (i = 0; i < nvqs; i++) {
        VirtQueue *vq = virtio_get_queue(s->vdev, i);

        virtio_queue_aio_set_host_notifier_handler(vq, s->ctx,
                virtio_blk_data_plane_handle_output);
    }
    aio_context_release(s->ctx);
    return 0;

  fail_aio_context:
    memory_region_transaction_begin();

    for (i = 0; i < nvqs; i++) {
        virtio_bus_set_host_notifier(VIRTIO_BUS(qbus), i, false);
    }

    memory_region_transaction_commit();

    for (i = 0; i < nvqs; i++) {
        virtio_bus_cleanup_host_notifier(VIRTIO_BUS(qbus), i);
    }
  fail_host_notifiers:
    k->set_guest_notifiers(qbus->parent, nvqs, false);
  fail_guest_notifiers:
    /*
     * If we failed to set up the guest notifiers queued requests will be
     * processed on the main context.
     */
    virtio_blk_process_queued_requests(vblk, false);
    vblk->dataplane_disabled = true;
    s->starting = false;
    vblk->dataplane_started = true;
    return -ENOSYS;
}

/* Stop notifications for new requests from guest.
 *
 * Context: BH in IOThread
 */
static void virtio_blk_data_plane_stop_bh(void *opaque)
{
    VirtIOBlockDataPlane *s = opaque;
    unsigned i;

    for (i = 0; i < s->conf->num_queues; i++) {
        VirtQueue *vq = virtio_get_queue(s->vdev, i);

        virtio_queue_aio_set_host_notifier_handler(vq, s->ctx, NULL);
    }
}

/* Context: QEMU global mutex held */
void virtio_blk_data_plane_stop(VirtIODevice *vdev)
{
    VirtIOBlock *vblk = VIRTIO_BLK(vdev);
    VirtIOBlockDataPlane *s = vblk->dataplane;
    BusState *qbus = qdev_get_parent_bus(DEVICE(vblk));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    unsigned i;
    unsigned nvqs = s->conf->num_queues;

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
    aio_wait_bh_oneshot(s->ctx, virtio_blk_data_plane_stop_bh, s);

    /* Drain and try to switch bs back to the QEMU main loop. If other users
     * keep the BlockBackend in the iothread, that's ok */
    blk_set_aio_context(s->conf->conf.blk, qemu_get_aio_context(), NULL);

    aio_context_release(s->ctx);

    /*
     * Batch all the host notifiers in a single transaction to avoid
     * quadratic time complexity in address_space_update_ioeventfds().
     */
    memory_region_transaction_begin();

    for (i = 0; i < nvqs; i++) {
        virtio_bus_set_host_notifier(VIRTIO_BUS(qbus), i, false);
    }

    /*
     * The transaction expects the ioeventfds to be open when it
     * commits. Do it now, before the cleanup loop.
     */
    memory_region_transaction_commit();

    for (i = 0; i < nvqs; i++) {
        virtio_bus_cleanup_host_notifier(VIRTIO_BUS(qbus), i);
    }

    qemu_bh_cancel(s->bh);
    notify_guest_bh(s); /* final chance to notify guest */

    /* Clean up guest notifier (irq) */
    k->set_guest_notifiers(qbus->parent, nvqs, false);

    vblk->dataplane_started = false;
    s->stopping = false;
}
