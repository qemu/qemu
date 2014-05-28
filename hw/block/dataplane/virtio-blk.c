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
#include "ioq.h"
#include "block/block.h"
#include "hw/virtio/virtio-blk.h"
#include "virtio-blk.h"
#include "block/aio.h"
#include "hw/virtio/virtio-bus.h"
#include "qom/object_interfaces.h"

enum {
    SEG_MAX = 126,                  /* maximum number of I/O segments */
    VRING_MAX = SEG_MAX + 2,        /* maximum number of vring descriptors */
    REQ_MAX = VRING_MAX,            /* maximum number of requests in the vring,
                                     * is VRING_MAX / 2 with traditional and
                                     * VRING_MAX with indirect descriptors */
};

typedef struct {
    struct iocb iocb;               /* Linux AIO control block */
    QEMUIOVector *inhdr;            /* iovecs for virtio_blk_inhdr */
    VirtQueueElement *elem;         /* saved data from the virtqueue */
    struct iovec *bounce_iov;       /* used if guest buffers are unaligned */
    QEMUIOVector *read_qiov;        /* for read completion /w bounce buffer */
} VirtIOBlockRequest;

struct VirtIOBlockDataPlane {
    bool started;
    bool starting;
    bool stopping;

    VirtIOBlkConf *blk;
    int fd;                         /* image file descriptor */

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
    EventNotifier io_notifier;      /* Linux AIO completion */
    EventNotifier host_notifier;    /* doorbell */

    IOQueue ioqueue;                /* Linux AIO queue (should really be per
                                       IOThread) */
    VirtIOBlockRequest requests[REQ_MAX]; /* pool of requests, managed by the
                                             queue */

    unsigned int num_reqs;

    /* Operation blocker on BDS */
    Error *blocker;
};

/* Raise an interrupt to signal guest, if necessary */
static void notify_guest(VirtIOBlockDataPlane *s)
{
    if (!vring_should_notify(s->vdev, &s->vring)) {
        return;
    }

    event_notifier_set(s->guest_notifier);
}

static void complete_request(struct iocb *iocb, ssize_t ret, void *opaque)
{
    VirtIOBlockDataPlane *s = opaque;
    VirtIOBlockRequest *req = container_of(iocb, VirtIOBlockRequest, iocb);
    struct virtio_blk_inhdr hdr;
    int len;

    if (likely(ret >= 0)) {
        hdr.status = VIRTIO_BLK_S_OK;
        len = ret;
    } else {
        hdr.status = VIRTIO_BLK_S_IOERR;
        len = 0;
    }

    trace_virtio_blk_data_plane_complete_request(s, req->elem->index, ret);

    if (req->read_qiov) {
        assert(req->bounce_iov);
        qemu_iovec_from_buf(req->read_qiov, 0, req->bounce_iov->iov_base, len);
        qemu_iovec_destroy(req->read_qiov);
        g_slice_free(QEMUIOVector, req->read_qiov);
    }

    if (req->bounce_iov) {
        qemu_vfree(req->bounce_iov->iov_base);
        g_slice_free(struct iovec, req->bounce_iov);
    }

    qemu_iovec_from_buf(req->inhdr, 0, &hdr, sizeof(hdr));
    qemu_iovec_destroy(req->inhdr);
    g_slice_free(QEMUIOVector, req->inhdr);

    /* According to the virtio specification len should be the number of bytes
     * written to, but for virtio-blk it seems to be the number of bytes
     * transferred plus the status bytes.
     */
    vring_push(&s->vring, req->elem, len + sizeof(hdr));
    req->elem = NULL;
    s->num_reqs--;
}

static void complete_request_early(VirtIOBlockDataPlane *s, VirtQueueElement *elem,
                                   QEMUIOVector *inhdr, unsigned char status)
{
    struct virtio_blk_inhdr hdr = {
        .status = status,
    };

    qemu_iovec_from_buf(inhdr, 0, &hdr, sizeof(hdr));
    qemu_iovec_destroy(inhdr);
    g_slice_free(QEMUIOVector, inhdr);

    vring_push(&s->vring, elem, sizeof(hdr));
    notify_guest(s);
}

/* Get disk serial number */
static void do_get_id_cmd(VirtIOBlockDataPlane *s,
                          struct iovec *iov, unsigned int iov_cnt,
                          VirtQueueElement *elem, QEMUIOVector *inhdr)
{
    char id[VIRTIO_BLK_ID_BYTES];

    /* Serial number not NUL-terminated when longer than buffer */
    strncpy(id, s->blk->serial ? s->blk->serial : "", sizeof(id));
    iov_from_buf(iov, iov_cnt, 0, id, sizeof(id));
    complete_request_early(s, elem, inhdr, VIRTIO_BLK_S_OK);
}

static int do_rdwr_cmd(VirtIOBlockDataPlane *s, bool read,
                       struct iovec *iov, unsigned iov_cnt,
                       long long offset, VirtQueueElement *elem,
                       QEMUIOVector *inhdr)
{
    struct iocb *iocb;
    QEMUIOVector qiov;
    struct iovec *bounce_iov = NULL;
    QEMUIOVector *read_qiov = NULL;

    qemu_iovec_init_external(&qiov, iov, iov_cnt);
    if (!bdrv_qiov_is_aligned(s->blk->conf.bs, &qiov)) {
        void *bounce_buffer = qemu_blockalign(s->blk->conf.bs, qiov.size);

        if (read) {
            /* Need to copy back from bounce buffer on completion */
            read_qiov = g_slice_new(QEMUIOVector);
            qemu_iovec_init(read_qiov, iov_cnt);
            qemu_iovec_concat_iov(read_qiov, iov, iov_cnt, 0, qiov.size);
        } else {
            qemu_iovec_to_buf(&qiov, 0, bounce_buffer, qiov.size);
        }

        /* Redirect I/O to aligned bounce buffer */
        bounce_iov = g_slice_new(struct iovec);
        bounce_iov->iov_base = bounce_buffer;
        bounce_iov->iov_len = qiov.size;
        iov = bounce_iov;
        iov_cnt = 1;
    }

    iocb = ioq_rdwr(&s->ioqueue, read, iov, iov_cnt, offset);

    /* Fill in virtio block metadata needed for completion */
    VirtIOBlockRequest *req = container_of(iocb, VirtIOBlockRequest, iocb);
    req->elem = elem;
    req->inhdr = inhdr;
    req->bounce_iov = bounce_iov;
    req->read_qiov = read_qiov;
    return 0;
}

static int process_request(IOQueue *ioq, VirtQueueElement *elem)
{
    VirtIOBlockDataPlane *s = container_of(ioq, VirtIOBlockDataPlane, ioqueue);
    struct iovec *iov = elem->out_sg;
    struct iovec *in_iov = elem->in_sg;
    unsigned out_num = elem->out_num;
    unsigned in_num = elem->in_num;
    struct virtio_blk_outhdr outhdr;
    QEMUIOVector *inhdr;
    size_t in_size;

    /* Copy in outhdr */
    if (unlikely(iov_to_buf(iov, out_num, 0, &outhdr,
                            sizeof(outhdr)) != sizeof(outhdr))) {
        error_report("virtio-blk request outhdr too short");
        return -EFAULT;
    }
    iov_discard_front(&iov, &out_num, sizeof(outhdr));

    /* Grab inhdr for later */
    in_size = iov_size(in_iov, in_num);
    if (in_size < sizeof(struct virtio_blk_inhdr)) {
        error_report("virtio_blk request inhdr too short");
        return -EFAULT;
    }
    inhdr = g_slice_new(QEMUIOVector);
    qemu_iovec_init(inhdr, 1);
    qemu_iovec_concat_iov(inhdr, in_iov, in_num,
            in_size - sizeof(struct virtio_blk_inhdr),
            sizeof(struct virtio_blk_inhdr));
    iov_discard_back(in_iov, &in_num, sizeof(struct virtio_blk_inhdr));

    /* TODO Linux sets the barrier bit even when not advertised! */
    outhdr.type &= ~VIRTIO_BLK_T_BARRIER;

    switch (outhdr.type) {
    case VIRTIO_BLK_T_IN:
        do_rdwr_cmd(s, true, in_iov, in_num, outhdr.sector * 512, elem, inhdr);
        return 0;

    case VIRTIO_BLK_T_OUT:
        do_rdwr_cmd(s, false, iov, out_num, outhdr.sector * 512, elem, inhdr);
        return 0;

    case VIRTIO_BLK_T_SCSI_CMD:
        /* TODO support SCSI commands */
        complete_request_early(s, elem, inhdr, VIRTIO_BLK_S_UNSUPP);
        return 0;

    case VIRTIO_BLK_T_FLUSH:
        /* TODO fdsync not supported by Linux AIO, do it synchronously here! */
        if (qemu_fdatasync(s->fd) < 0) {
            complete_request_early(s, elem, inhdr, VIRTIO_BLK_S_IOERR);
        } else {
            complete_request_early(s, elem, inhdr, VIRTIO_BLK_S_OK);
        }
        return 0;

    case VIRTIO_BLK_T_GET_ID:
        do_get_id_cmd(s, in_iov, in_num, elem, inhdr);
        return 0;

    default:
        error_report("virtio-blk unsupported request type %#x", outhdr.type);
        qemu_iovec_destroy(inhdr);
        g_slice_free(QEMUIOVector, inhdr);
        return -EFAULT;
    }
}

static void handle_notify(EventNotifier *e)
{
    VirtIOBlockDataPlane *s = container_of(e, VirtIOBlockDataPlane,
                                           host_notifier);

    VirtQueueElement *elem;
    int ret;
    unsigned int num_queued;

    event_notifier_test_and_clear(&s->host_notifier);
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

            if (process_request(&s->ioqueue, elem) < 0) {
                vring_set_broken(&s->vring);
                vring_free_element(elem);
                ret = -EFAULT;
                break;
            }
        }

        if (likely(ret == -EAGAIN)) { /* vring emptied */
            /* Re-enable guest->host notifies and stop processing the vring.
             * But if the guest has snuck in more descriptors, keep processing.
             */
            if (vring_enable_notification(s->vdev, &s->vring)) {
                break;
            }
        } else { /* ret == -ENOBUFS or fatal error, iovecs[] is depleted */
            /* Since there are no iovecs[] left, stop processing for now.  Do
             * not re-enable guest->host notifies since the I/O completion
             * handler knows to check for more vring descriptors anyway.
             */
            break;
        }
    }

    num_queued = ioq_num_queued(&s->ioqueue);
    if (num_queued > 0) {
        s->num_reqs += num_queued;

        int rc = ioq_submit(&s->ioqueue);
        if (unlikely(rc < 0)) {
            fprintf(stderr, "ioq_submit failed %d\n", rc);
            exit(1);
        }
    }
}

static void handle_io(EventNotifier *e)
{
    VirtIOBlockDataPlane *s = container_of(e, VirtIOBlockDataPlane,
                                           io_notifier);

    event_notifier_test_and_clear(&s->io_notifier);
    if (ioq_run_completion(&s->ioqueue, complete_request, s) > 0) {
        notify_guest(s);
    }

    /* If there were more requests than iovecs, the vring will not be empty yet
     * so check again.  There should now be enough resources to process more
     * requests.
     */
    if (unlikely(vring_more_avail(&s->vring))) {
        handle_notify(&s->host_notifier);
    }
}

/* Context: QEMU global mutex held */
void virtio_blk_data_plane_create(VirtIODevice *vdev, VirtIOBlkConf *blk,
                                  VirtIOBlockDataPlane **dataplane,
                                  Error **errp)
{
    VirtIOBlockDataPlane *s;
    int fd;
    Error *local_err = NULL;

    *dataplane = NULL;

    if (!blk->data_plane) {
        return;
    }

    if (blk->scsi) {
        error_setg(errp,
                   "device is incompatible with x-data-plane, use scsi=off");
        return;
    }

    if (blk->config_wce) {
        error_setg(errp, "device is incompatible with x-data-plane, "
                         "use config-wce=off");
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

    fd = raw_get_aio_fd(blk->conf.bs);
    if (fd < 0) {
        error_setg(errp, "drive is incompatible with x-data-plane, "
                         "use format=raw,cache=none,aio=native");
        return;
    }

    s = g_new0(VirtIOBlockDataPlane, 1);
    s->vdev = vdev;
    s->fd = fd;
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
    int i;

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

    /* Set up ioqueue */
    ioq_init(&s->ioqueue, s->fd, REQ_MAX);
    for (i = 0; i < ARRAY_SIZE(s->requests); i++) {
        ioq_put_iocb(&s->ioqueue, &s->requests[i].iocb);
    }
    s->io_notifier = *ioq_get_notifier(&s->ioqueue);

    s->starting = false;
    s->started = true;
    trace_virtio_blk_data_plane_start(s);

    /* Kick right away to begin processing requests already in vring */
    event_notifier_set(virtio_queue_get_host_notifier(vq));

    /* Get this show started by hooking up our callbacks */
    aio_context_acquire(s->ctx);
    aio_set_event_notifier(s->ctx, &s->host_notifier, handle_notify);
    aio_set_event_notifier(s->ctx, &s->io_notifier, handle_io);
    aio_context_release(s->ctx);
}

/* Context: QEMU global mutex held */
void virtio_blk_data_plane_stop(VirtIOBlockDataPlane *s)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(s->vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    if (!s->started || s->stopping) {
        return;
    }
    s->stopping = true;
    trace_virtio_blk_data_plane_stop(s);

    aio_context_acquire(s->ctx);

    /* Stop notifications for new requests from guest */
    aio_set_event_notifier(s->ctx, &s->host_notifier, NULL);

    /* Complete pending requests */
    while (s->num_reqs > 0) {
        aio_poll(s->ctx, true);
    }

    /* Stop ioq callbacks (there are no pending requests left) */
    aio_set_event_notifier(s->ctx, &s->io_notifier, NULL);

    aio_context_release(s->ctx);

    /* Sync vring state back to virtqueue so that non-dataplane request
     * processing can continue when we disable the host notifier below.
     */
    vring_teardown(&s->vring, s->vdev, 0);

    ioq_cleanup(&s->ioqueue);
    k->set_host_notifier(qbus->parent, 0, false);

    /* Clean up guest notifier (irq) */
    k->set_guest_notifiers(qbus->parent, 1, false);

    s->started = false;
    s->stopping = false;
}
