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

#include "trace.h"
#include "qemu/iov.h"
#include "qemu/thread.h"
#include "qemu/error-report.h"
#include "hw/virtio/dataplane/vring.h"
#include "block/block.h"
#include "hw/virtio/virtio-blk.h"
#include "virtio-blk.h"
#include "block/aio.h"
#include "hw/virtio/virtio-bus.h"
#include "qom/object_interfaces.h"

struct VirtIOBlockDataPlane {
    bool started;
    bool starting;
    bool stopping;

    VirtIOBlkConf *blk;

    VirtIODevice *vdev;
    Vring vring;                    /* virtqueue vring */
    EventNotifier *guest_notifier;  /* irq */

    /* Note that these EventNotifiers are assigned by value.  This is
     * fine as long as you do not call event_notifier_cleanup on them
     * (because you don't own the file descriptor or handle; you just
     * use it).
     */
    IOThread *iothread;
    IOThread internal_iothread_obj;
    AioContext *ctx;
    EventNotifier host_notifier;    /* doorbell */

    /* Operation blocker on BDS */
    Error *blocker;
    void (*saved_complete_request)(struct VirtIOBlockReq *req,
                                   unsigned char status);
};

/* Raise an interrupt to signal guest, if necessary */
static void notify_guest(VirtIOBlockDataPlane *s)
{
    if (!vring_should_notify(s->vdev, &s->vring)) {
        return;
    }

    event_notifier_set(s->guest_notifier);
}

static void complete_request_vring(VirtIOBlockReq *req, unsigned char status)
{
    stb_p(&req->in->status, status);

    vring_push(&req->dev->dataplane->vring, req->elem,
               req->qiov.size + sizeof(*req->in));
    notify_guest(req->dev->dataplane);
}

static void handle_notify(EventNotifier *e)
{
    VirtIOBlockDataPlane *s = container_of(e, VirtIOBlockDataPlane,
                                           host_notifier);

    VirtQueueElement *elem;
    VirtIOBlockReq *req;
    int ret;
    MultiReqBuffer mrb = {
        .num_writes = 0,
    };

    event_notifier_test_and_clear(&s->host_notifier);
    bdrv_io_plug(s->blk->conf.bs);
    for (;;) {
        /* Disable guest->host notifies to avoid unnecessary vmexits */
        vring_disable_notification(s->vdev, &s->vring);

        for (;;) {
            ret = vring_pop(s->vdev, &s->vring, &elem);
            if (ret < 0) {
                assert(elem == NULL);
                break; /* no more requests */
            }

            trace_virtio_blk_data_plane_process_request(s, elem->out_num,
                                                        elem->in_num, elem->index);

            req = g_slice_new(VirtIOBlockReq);
            req->dev = VIRTIO_BLK(s->vdev);
            req->elem = elem;
            virtio_blk_handle_request(req, &mrb);
        }

        virtio_submit_multiwrite(s->blk->conf.bs, &mrb);

        if (likely(ret == -EAGAIN)) { /* vring emptied */
            /* Re-enable guest->host notifies and stop processing the vring.
             * But if the guest has snuck in more descriptors, keep processing.
             */
            if (vring_enable_notification(s->vdev, &s->vring)) {
                break;
            }
        } else { /* fatal error */
            break;
        }
    }
    bdrv_io_unplug(s->blk->conf.bs);
}

/* Context: QEMU global mutex held */
void virtio_blk_data_plane_create(VirtIODevice *vdev, VirtIOBlkConf *blk,
                                  VirtIOBlockDataPlane **dataplane,
                                  Error **errp)
{
    VirtIOBlockDataPlane *s;
    VirtIOBlock *vblk = VIRTIO_BLK(vdev);
    Error *local_err = NULL;
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);

    *dataplane = NULL;

    if (!blk->data_plane && !blk->iothread) {
        return;
    }

    /* Don't try if transport does not support notifiers. */
    if (!k->set_guest_notifiers || !k->set_host_notifier) {
        error_setg(errp,
                   "device is incompatible with x-data-plane "
                   "(transport does not support notifiers)");
        return;
    }

    /* If dataplane is (re-)enabled while the guest is running there could be
     * block jobs that can conflict.
     */
    if (bdrv_op_is_blocked(blk->conf.bs, BLOCK_OP_TYPE_DATAPLANE, &local_err)) {
        error_report("cannot start dataplane thread: %s",
                      error_get_pretty(local_err));
        error_free(local_err);
        return;
    }

    s = g_new0(VirtIOBlockDataPlane, 1);
    s->vdev = vdev;
    s->blk = blk;

    if (blk->iothread) {
        s->iothread = blk->iothread;
        object_ref(OBJECT(s->iothread));
    } else {
        /* Create per-device IOThread if none specified.  This is for
         * x-data-plane option compatibility.  If x-data-plane is removed we
         * can drop this.
         */
        object_initialize(&s->internal_iothread_obj,
                          sizeof(s->internal_iothread_obj),
                          TYPE_IOTHREAD);
        user_creatable_complete(OBJECT(&s->internal_iothread_obj), &error_abort);
        s->iothread = &s->internal_iothread_obj;
    }
    s->ctx = iothread_get_aio_context(s->iothread);

    error_setg(&s->blocker, "block device is in use by data plane");
    bdrv_op_block_all(blk->conf.bs, s->blocker);

    *dataplane = s;
    s->saved_complete_request = vblk->complete_request;
    vblk->complete_request = complete_request_vring;
}

/* Context: QEMU global mutex held */
void virtio_blk_data_plane_destroy(VirtIOBlockDataPlane *s)
{
    if (!s) {
        return;
    }

    virtio_blk_data_plane_stop(s);
    bdrv_op_unblock_all(s->blk->conf.bs, s->blocker);
    error_free(s->blocker);
    object_unref(OBJECT(s->iothread));
    g_free(s);
}

/* Context: QEMU global mutex held */
void virtio_blk_data_plane_start(VirtIOBlockDataPlane *s)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(s->vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    VirtQueue *vq;

    if (s->started) {
        return;
    }

    if (s->starting) {
        return;
    }

    s->starting = true;

    vq = virtio_get_queue(s->vdev, 0);
    if (!vring_setup(&s->vring, s->vdev, 0)) {
        s->starting = false;
        return;
    }

    /* Set up guest notifier (irq) */
    if (k->set_guest_notifiers(qbus->parent, 1, true) != 0) {
        fprintf(stderr, "virtio-blk failed to set guest notifier, "
                "ensure -enable-kvm is set\n");
        exit(1);
    }
    s->guest_notifier = virtio_queue_get_guest_notifier(vq);

    /* Set up virtqueue notify */
    if (k->set_host_notifier(qbus->parent, 0, true) != 0) {
        fprintf(stderr, "virtio-blk failed to set host notifier\n");
        exit(1);
    }
    s->host_notifier = *virtio_queue_get_host_notifier(vq);

    s->starting = false;
    s->started = true;
    trace_virtio_blk_data_plane_start(s);

    bdrv_set_aio_context(s->blk->conf.bs, s->ctx);

    /* Kick right away to begin processing requests already in vring */
    event_notifier_set(virtio_queue_get_host_notifier(vq));

    /* Get this show started by hooking up our callbacks */
    aio_context_acquire(s->ctx);
    aio_set_event_notifier(s->ctx, &s->host_notifier, handle_notify);
    aio_context_release(s->ctx);
}

/* Context: QEMU global mutex held */
void virtio_blk_data_plane_stop(VirtIOBlockDataPlane *s)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(s->vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    VirtIOBlock *vblk = VIRTIO_BLK(s->vdev);
    if (!s->started || s->stopping) {
        return;
    }
    s->stopping = true;
    vblk->complete_request = s->saved_complete_request;
    trace_virtio_blk_data_plane_stop(s);

    aio_context_acquire(s->ctx);

    /* Stop notifications for new requests from guest */
    aio_set_event_notifier(s->ctx, &s->host_notifier, NULL);

    /* Drain and switch bs back to the QEMU main loop */
    bdrv_set_aio_context(s->blk->conf.bs, qemu_get_aio_context());

    aio_context_release(s->ctx);

    /* Sync vring state back to virtqueue so that non-dataplane request
     * processing can continue when we disable the host notifier below.
     */
    vring_teardown(&s->vring, s->vdev, 0);

    k->set_host_notifier(qbus->parent, 0, false);

    /* Clean up guest notifier (irq) */
    k->set_guest_notifiers(qbus->parent, 1, false);

    s->started = false;
    s->stopping = false;
}
