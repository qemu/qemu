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
#include "qapi/error.h"
#include "hw/virtio/virtio-scsi.h"
#include "qemu/error-report.h"
#include "system/block-backend.h"
#include "hw/scsi/scsi.h"
#include "scsi/constants.h"
#include "hw/virtio/virtio-bus.h"

/* Context: BQL held */
void virtio_scsi_dataplane_setup(VirtIOSCSI *s, Error **errp)
{
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(s);
    VirtIODevice *vdev = VIRTIO_DEVICE(s);
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);

    if (vs->conf.iothread) {
        if (!k->set_guest_notifiers || !k->ioeventfd_assign) {
            error_setg(errp,
                       "device is incompatible with iothread "
                       "(transport does not support notifiers)");
            return;
        }
        if (!virtio_device_ioeventfd_enabled(vdev)) {
            error_setg(errp, "ioeventfd is required for iothread");
            return;
        }
        s->ctx = iothread_get_aio_context(vs->conf.iothread);
    } else {
        if (!virtio_device_ioeventfd_enabled(vdev)) {
            return;
        }
        s->ctx = qemu_get_aio_context();
    }
}

static int virtio_scsi_set_host_notifier(VirtIOSCSI *s, VirtQueue *vq, int n)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(s)));
    int rc;

    /* Set up virtqueue notify */
    rc = virtio_bus_set_host_notifier(VIRTIO_BUS(qbus), n, true);
    if (rc != 0) {
        fprintf(stderr, "virtio-scsi: Failed to set host notifier (%d)\n",
                rc);
        s->dataplane_fenced = true;
        return rc;
    }

    return 0;
}

/* Context: BH in IOThread */
static void virtio_scsi_dataplane_stop_bh(void *opaque)
{
    VirtIOSCSI *s = opaque;
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(s);
    EventNotifier *host_notifier;
    int i;

    virtio_queue_aio_detach_host_notifier(vs->ctrl_vq, s->ctx);
    host_notifier = virtio_queue_get_host_notifier(vs->ctrl_vq);

    /*
     * Test and clear notifier after disabling event, in case poll callback
     * didn't have time to run.
     */
    virtio_queue_host_notifier_read(host_notifier);

    virtio_queue_aio_detach_host_notifier(vs->event_vq, s->ctx);
    host_notifier = virtio_queue_get_host_notifier(vs->event_vq);
    virtio_queue_host_notifier_read(host_notifier);

    for (i = 0; i < vs->conf.num_queues; i++) {
        virtio_queue_aio_detach_host_notifier(vs->cmd_vqs[i], s->ctx);
        host_notifier = virtio_queue_get_host_notifier(vs->cmd_vqs[i]);
        virtio_queue_host_notifier_read(host_notifier);
    }
}

/* Context: BQL held */
int virtio_scsi_dataplane_start(VirtIODevice *vdev)
{
    int i;
    int rc;
    int vq_init_count = 0;
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(vdev);
    VirtIOSCSI *s = VIRTIO_SCSI(vdev);

    if (s->dataplane_started ||
        s->dataplane_starting ||
        s->dataplane_fenced) {
        return 0;
    }

    s->dataplane_starting = true;

    /* Set up guest notifier (irq) */
    rc = k->set_guest_notifiers(qbus->parent, vs->conf.num_queues + 2, true);
    if (rc != 0) {
        error_report("virtio-scsi: Failed to set guest notifiers (%d), "
                     "ensure -accel kvm is set.", rc);
        goto fail_guest_notifiers;
    }

    /*
     * Batch all the host notifiers in a single transaction to avoid
     * quadratic time complexity in address_space_update_ioeventfds().
     */
    memory_region_transaction_begin();

    rc = virtio_scsi_set_host_notifier(s, vs->ctrl_vq, 0);
    if (rc != 0) {
        goto fail_host_notifiers;
    }

    vq_init_count++;
    rc = virtio_scsi_set_host_notifier(s, vs->event_vq, 1);
    if (rc != 0) {
        goto fail_host_notifiers;
    }

    vq_init_count++;

    for (i = 0; i < vs->conf.num_queues; i++) {
        rc = virtio_scsi_set_host_notifier(s, vs->cmd_vqs[i], i + 2);
        if (rc) {
            goto fail_host_notifiers;
        }
        vq_init_count++;
    }

    memory_region_transaction_commit();

    s->dataplane_starting = false;
    s->dataplane_started = true;
    smp_wmb(); /* paired with aio_notify_accept() */

    if (s->bus.drain_count == 0) {
        virtio_queue_aio_attach_host_notifier(vs->ctrl_vq, s->ctx);
        virtio_queue_aio_attach_host_notifier_no_poll(vs->event_vq, s->ctx);

        for (i = 0; i < vs->conf.num_queues; i++) {
            virtio_queue_aio_attach_host_notifier(vs->cmd_vqs[i], s->ctx);
        }
    }
    return 0;

fail_host_notifiers:
    for (i = 0; i < vq_init_count; i++) {
        virtio_bus_set_host_notifier(VIRTIO_BUS(qbus), i, false);
    }

    /*
     * The transaction expects the ioeventfds to be open when it
     * commits. Do it now, before the cleanup loop.
     */
    memory_region_transaction_commit();

    for (i = 0; i < vq_init_count; i++) {
        virtio_bus_cleanup_host_notifier(VIRTIO_BUS(qbus), i);
    }
    k->set_guest_notifiers(qbus->parent, vs->conf.num_queues + 2, false);
fail_guest_notifiers:
    s->dataplane_fenced = true;
    s->dataplane_starting = false;
    s->dataplane_started = true;
    return -ENOSYS;
}

/* Context: BQL held */
void virtio_scsi_dataplane_stop(VirtIODevice *vdev)
{
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(vdev);
    VirtIOSCSI *s = VIRTIO_SCSI(vdev);
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

    if (s->bus.drain_count == 0) {
        aio_wait_bh_oneshot(s->ctx, virtio_scsi_dataplane_stop_bh, s);
    }

    blk_drain_all(); /* ensure there are no in-flight requests */

    /*
     * Batch all the host notifiers in a single transaction to avoid
     * quadratic time complexity in address_space_update_ioeventfds().
     */
    memory_region_transaction_begin();

    for (i = 0; i < vs->conf.num_queues + 2; i++) {
        virtio_bus_set_host_notifier(VIRTIO_BUS(qbus), i, false);
    }

    /*
     * The transaction expects the ioeventfds to be open when it
     * commits. Do it now, before the cleanup loop.
     */
    memory_region_transaction_commit();

    for (i = 0; i < vs->conf.num_queues + 2; i++) {
        virtio_bus_cleanup_host_notifier(VIRTIO_BUS(qbus), i);
    }

    /* Clean up guest notifier (irq) */
    k->set_guest_notifiers(qbus->parent, vs->conf.num_queues + 2, false);
    s->dataplane_stopping = false;
    s->dataplane_started = false;
}
