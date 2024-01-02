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

    /*
     * The AioContext for each virtqueue. The BlockDriverState will use the
     * first element as its AioContext.
     */
    AioContext **vq_aio_context;
};

/* Raise an interrupt to signal guest, if necessary */
void virtio_blk_data_plane_notify(VirtIOBlockDataPlane *s, VirtQueue *vq)
{
    virtio_notify_irqfd(s->vdev, vq);
}

/* Generate vq:AioContext mappings from a validated iothread-vq-mapping list */
static void
apply_vq_mapping(IOThreadVirtQueueMappingList *iothread_vq_mapping_list,
                 AioContext **vq_aio_context, uint16_t num_queues)
{
    IOThreadVirtQueueMappingList *node;
    size_t num_iothreads = 0;
    size_t cur_iothread = 0;

    for (node = iothread_vq_mapping_list; node; node = node->next) {
        num_iothreads++;
    }

    for (node = iothread_vq_mapping_list; node; node = node->next) {
        IOThread *iothread = iothread_by_id(node->value->iothread);
        AioContext *ctx = iothread_get_aio_context(iothread);

        /* Released in virtio_blk_data_plane_destroy() */
        object_ref(OBJECT(iothread));

        if (node->value->vqs) {
            uint16List *vq;

            /* Explicit vq:IOThread assignment */
            for (vq = node->value->vqs; vq; vq = vq->next) {
                vq_aio_context[vq->value] = ctx;
            }
        } else {
            /* Round-robin vq:IOThread assignment */
            for (unsigned i = cur_iothread; i < num_queues;
                 i += num_iothreads) {
                vq_aio_context[i] = ctx;
            }
        }

        cur_iothread++;
    }
}

/* Context: BQL held */
bool virtio_blk_data_plane_create(VirtIODevice *vdev, VirtIOBlkConf *conf,
                                  VirtIOBlockDataPlane **dataplane,
                                  Error **errp)
{
    VirtIOBlockDataPlane *s;
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);

    *dataplane = NULL;

    if (conf->iothread || conf->iothread_vq_mapping_list) {
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
    s->vq_aio_context = g_new(AioContext *, conf->num_queues);

    if (conf->iothread_vq_mapping_list) {
        apply_vq_mapping(conf->iothread_vq_mapping_list, s->vq_aio_context,
                         conf->num_queues);
    } else if (conf->iothread) {
        AioContext *ctx = iothread_get_aio_context(conf->iothread);
        for (unsigned i = 0; i < conf->num_queues; i++) {
            s->vq_aio_context[i] = ctx;
        }

        /* Released in virtio_blk_data_plane_destroy() */
        object_ref(OBJECT(conf->iothread));
    } else {
        AioContext *ctx = qemu_get_aio_context();
        for (unsigned i = 0; i < conf->num_queues; i++) {
            s->vq_aio_context[i] = ctx;
        }
    }

    *dataplane = s;

    return true;
}

/* Context: BQL held */
void virtio_blk_data_plane_destroy(VirtIOBlockDataPlane *s)
{
    VirtIOBlock *vblk;
    VirtIOBlkConf *conf;

    if (!s) {
        return;
    }

    vblk = VIRTIO_BLK(s->vdev);
    assert(!vblk->dataplane_started);
    conf = s->conf;

    if (conf->iothread_vq_mapping_list) {
        IOThreadVirtQueueMappingList *node;

        for (node = conf->iothread_vq_mapping_list; node; node = node->next) {
            IOThread *iothread = iothread_by_id(node->value->iothread);
            object_unref(OBJECT(iothread));
        }
    }

    if (conf->iothread) {
        object_unref(OBJECT(conf->iothread));
    }

    g_free(s->vq_aio_context);
    g_free(s);
}

/* Context: BQL held */
int virtio_blk_data_plane_start(VirtIODevice *vdev)
{
    VirtIOBlock *vblk = VIRTIO_BLK(vdev);
    VirtIOBlockDataPlane *s = vblk->dataplane;
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vblk)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    unsigned i;
    unsigned nvqs = s->conf->num_queues;
    Error *local_err = NULL;
    int r;

    if (vblk->dataplane_started || s->starting) {
        return 0;
    }

    s->starting = true;

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
                virtio_bus_cleanup_host_notifier(VIRTIO_BUS(qbus), j);
            }
            goto fail_host_notifiers;
        }
    }

    memory_region_transaction_commit();

    trace_virtio_blk_data_plane_start(s);

    r = blk_set_aio_context(s->conf->conf.blk, s->vq_aio_context[0],
                            &local_err);
    if (r < 0) {
        error_report_err(local_err);
        goto fail_aio_context;
    }

    /*
     * These fields must be visible to the IOThread when it processes the
     * virtqueue, otherwise it will think dataplane has not started yet.
     *
     * Make sure ->dataplane_started is false when blk_set_aio_context() is
     * called above so that draining does not cause the host notifier to be
     * detached/attached prematurely.
     */
    s->starting = false;
    vblk->dataplane_started = true;
    smp_wmb(); /* paired with aio_notify_accept() on the read side */

    /* Get this show started by hooking up our callbacks */
    if (!blk_in_drain(s->conf->conf.blk)) {
        for (i = 0; i < nvqs; i++) {
            VirtQueue *vq = virtio_get_queue(s->vdev, i);
            AioContext *ctx = s->vq_aio_context[i];

            /* Kick right away to begin processing requests already in vring */
            event_notifier_set(virtio_queue_get_host_notifier(vq));

            virtio_queue_aio_attach_host_notifier(vq, ctx);
        }
    }
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
    vblk->dataplane_disabled = true;
    s->starting = false;
    return -ENOSYS;
}

/* Stop notifications for new requests from guest.
 *
 * Context: BH in IOThread
 */
static void virtio_blk_data_plane_stop_vq_bh(void *opaque)
{
    VirtQueue *vq = opaque;
    EventNotifier *host_notifier = virtio_queue_get_host_notifier(vq);

    virtio_queue_aio_detach_host_notifier(vq, qemu_get_current_aio_context());

    /*
     * Test and clear notifier after disabling event, in case poll callback
     * didn't have time to run.
     */
    virtio_queue_host_notifier_read(host_notifier);
}

/* Context: BQL held */
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

    if (!blk_in_drain(s->conf->conf.blk)) {
        for (i = 0; i < nvqs; i++) {
            VirtQueue *vq = virtio_get_queue(s->vdev, i);
            AioContext *ctx = s->vq_aio_context[i];

            aio_wait_bh_oneshot(ctx, virtio_blk_data_plane_stop_vq_bh, vq);
        }
    }

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

    /*
     * Set ->dataplane_started to false before draining so that host notifiers
     * are not detached/attached anymore.
     */
    vblk->dataplane_started = false;

    /* Wait for virtio_blk_dma_restart_bh() and in flight I/O to complete */
    blk_drain(s->conf->conf.blk);

    /*
     * Try to switch bs back to the QEMU main loop. If other users keep the
     * BlockBackend in the iothread, that's ok
     */
    blk_set_aio_context(s->conf->conf.blk, qemu_get_aio_context(), NULL);

    /* Clean up guest notifier (irq) */
    k->set_guest_notifiers(qbus->parent, nvqs, false);

    s->stopping = false;
}

void virtio_blk_data_plane_detach(VirtIOBlockDataPlane *s)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(s->vdev);

    for (uint16_t i = 0; i < s->conf->num_queues; i++) {
        VirtQueue *vq = virtio_get_queue(vdev, i);
        virtio_queue_aio_detach_host_notifier(vq, s->vq_aio_context[i]);
    }
}

void virtio_blk_data_plane_attach(VirtIOBlockDataPlane *s)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(s->vdev);

    for (uint16_t i = 0; i < s->conf->num_queues; i++) {
        VirtQueue *vq = virtio_get_queue(vdev, i);
        virtio_queue_aio_attach_host_notifier(vq, s->vq_aio_context[i]);
    }
}
