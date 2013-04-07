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
#include "vring.h"
#include "ioq.h"
#include "migration/migration.h"
#include "block/block.h"
#include "hw/virtio-blk.h"
#include "hw/dataplane/virtio-blk.h"
#include "block/aio.h"

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
    unsigned int head;              /* vring descriptor index */
    struct iovec *bounce_iov;       /* used if guest buffers are unaligned */
    QEMUIOVector *read_qiov;        /* for read completion /w bounce buffer */
} VirtIOBlockRequest;

struct VirtIOBlockDataPlane {
    bool started;
    bool stopping;
    QEMUBH *start_bh;
    QemuThread thread;

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
    AioContext *ctx;
    EventNotifier io_notifier;      /* Linux AIO completion */
    EventNotifier host_notifier;    /* doorbell */

    IOQueue ioqueue;                /* Linux AIO queue (should really be per
                                       dataplane thread) */
    VirtIOBlockRequest requests[REQ_MAX]; /* pool of requests, managed by the
                                             queue */

    unsigned int num_reqs;

    Error *migration_blocker;
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

    trace_virtio_blk_data_plane_complete_request(s, req->head, ret);

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
    vring_push(&s->vring, req->head, len + sizeof(hdr));

    s->num_reqs--;
}

static void complete_request_early(VirtIOBlockDataPlane *s, unsigned int head,
                                   QEMUIOVector *inhdr, unsigned char status)
{
    struct virtio_blk_inhdr hdr = {
        .status = status,
    };

    qemu_iovec_from_buf(inhdr, 0, &hdr, sizeof(hdr));
    qemu_iovec_destroy(inhdr);
    g_slice_free(QEMUIOVector, inhdr);

    vring_push(&s->vring, head, sizeof(hdr));
    notify_guest(s);
}

/* Get disk serial number */
static void do_get_id_cmd(VirtIOBlockDataPlane *s,
                          struct iovec *iov, unsigned int iov_cnt,
                          unsigned int head, QEMUIOVector *inhdr)
{
    char id[VIRTIO_BLK_ID_BYTES];

    /* Serial number not NUL-terminated when shorter than buffer */
    strncpy(id, s->blk->serial ? s->blk->serial : "", sizeof(id));
    iov_from_buf(iov, iov_cnt, 0, id, sizeof(id));
    complete_request_early(s, head, inhdr, VIRTIO_BLK_S_OK);
}

static int do_rdwr_cmd(VirtIOBlockDataPlane *s, bool read,
                       struct iovec *iov, unsigned int iov_cnt,
                       long long offset, unsigned int head,
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
    req->head = head;
    req->inhdr = inhdr;
    req->bounce_iov = bounce_iov;
    req->read_qiov = read_qiov;
    return 0;
}

static int process_request(IOQueue *ioq, struct iovec iov[],
                           unsigned int out_num, unsigned int in_num,
                           unsigned int head)
{
    VirtIOBlockDataPlane *s = container_of(ioq, VirtIOBlockDataPlane, ioqueue);
    struct iovec *in_iov = &iov[out_num];
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
        do_rdwr_cmd(s, true, in_iov, in_num, outhdr.sector * 512, head, inhdr);
        return 0;

    case VIRTIO_BLK_T_OUT:
        do_rdwr_cmd(s, false, iov, out_num, outhdr.sector * 512, head, inhdr);
        return 0;

    case VIRTIO_BLK_T_SCSI_CMD:
        /* TODO support SCSI commands */
        complete_request_early(s, head, inhdr, VIRTIO_BLK_S_UNSUPP);
        return 0;

    case VIRTIO_BLK_T_FLUSH:
        /* TODO fdsync not supported by Linux AIO, do it synchronously here! */
        if (qemu_fdatasync(s->fd) < 0) {
            complete_request_early(s, head, inhdr, VIRTIO_BLK_S_IOERR);
        } else {
            complete_request_early(s, head, inhdr, VIRTIO_BLK_S_OK);
        }
        return 0;

    case VIRTIO_BLK_T_GET_ID:
        do_get_id_cmd(s, in_iov, in_num, head, inhdr);
        return 0;

    default:
        error_report("virtio-blk unsupported request type %#x", outhdr.type);
        qemu_iovec_destroy(inhdr);
        g_slice_free(QEMUIOVector, inhdr);
        return -EFAULT;
    }
}

static int flush_true(EventNotifier *e)
{
    return true;
}

static void handle_notify(EventNotifier *e)
{
    VirtIOBlockDataPlane *s = container_of(e, VirtIOBlockDataPlane,
                                           host_notifier);

    /* There is one array of iovecs into which all new requests are extracted
     * from the vring.  Requests are read from the vring and the translated
     * descriptors are written to the iovecs array.  The iovecs do not have to
     * persist across handle_notify() calls because the kernel copies the
     * iovecs on io_submit().
     *
     * Handling io_submit() EAGAIN may require storing the requests across
     * handle_notify() calls until the kernel has sufficient resources to
     * accept more I/O.  This is not implemented yet.
     */
    struct iovec iovec[VRING_MAX];
    struct iovec *end = &iovec[VRING_MAX];
    struct iovec *iov = iovec;

    /* When a request is read from the vring, the index of the first descriptor
     * (aka head) is returned so that the completed request can be pushed onto
     * the vring later.
     *
     * The number of hypervisor read-only iovecs is out_num.  The number of
     * hypervisor write-only iovecs is in_num.
     */
    int head;
    unsigned int out_num = 0, in_num = 0;
    unsigned int num_queued;

    event_notifier_test_and_clear(&s->host_notifier);
    for (;;) {
        /* Disable guest->host notifies to avoid unnecessary vmexits */
        vring_disable_notification(s->vdev, &s->vring);

        for (;;) {
            head = vring_pop(s->vdev, &s->vring, iov, end, &out_num, &in_num);
            if (head < 0) {
                break; /* no more requests */
            }

            trace_virtio_blk_data_plane_process_request(s, out_num, in_num,
                                                        head);

            if (process_request(&s->ioqueue, iov, out_num, in_num, head) < 0) {
                vring_set_broken(&s->vring);
                break;
            }
            iov += out_num + in_num;
        }

        if (likely(head == -EAGAIN)) { /* vring emptied */
            /* Re-enable guest->host notifies and stop processing the vring.
             * But if the guest has snuck in more descriptors, keep processing.
             */
            if (vring_enable_notification(s->vdev, &s->vring)) {
                break;
            }
        } else { /* head == -ENOBUFS or fatal error, iovecs[] is depleted */
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

static int flush_io(EventNotifier *e)
{
    VirtIOBlockDataPlane *s = container_of(e, VirtIOBlockDataPlane,
                                           io_notifier);

    return s->num_reqs > 0;
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

static void *data_plane_thread(void *opaque)
{
    VirtIOBlockDataPlane *s = opaque;

    do {
        aio_poll(s->ctx, true);
    } while (!s->stopping || s->num_reqs > 0);
    return NULL;
}

static void start_data_plane_bh(void *opaque)
{
    VirtIOBlockDataPlane *s = opaque;

    qemu_bh_delete(s->start_bh);
    s->start_bh = NULL;
    qemu_thread_create(&s->thread, data_plane_thread,
                       s, QEMU_THREAD_JOINABLE);
}

bool virtio_blk_data_plane_create(VirtIODevice *vdev, VirtIOBlkConf *blk,
                                  VirtIOBlockDataPlane **dataplane)
{
    VirtIOBlockDataPlane *s;
    int fd;

    *dataplane = NULL;

    if (!blk->data_plane) {
        return true;
    }

    if (blk->scsi) {
        error_report("device is incompatible with x-data-plane, use scsi=off");
        return false;
    }

    if (blk->config_wce) {
        error_report("device is incompatible with x-data-plane, "
                     "use config-wce=off");
        return false;
    }

    fd = raw_get_aio_fd(blk->conf.bs);
    if (fd < 0) {
        error_report("drive is incompatible with x-data-plane, "
                     "use format=raw,cache=none,aio=native");
        return false;
    }

    s = g_new0(VirtIOBlockDataPlane, 1);
    s->vdev = vdev;
    s->fd = fd;
    s->blk = blk;

    /* Prevent block operations that conflict with data plane thread */
    bdrv_set_in_use(blk->conf.bs, 1);

    error_setg(&s->migration_blocker,
            "x-data-plane does not support migration");
    migrate_add_blocker(s->migration_blocker);

    *dataplane = s;
    return true;
}

void virtio_blk_data_plane_destroy(VirtIOBlockDataPlane *s)
{
    if (!s) {
        return;
    }

    virtio_blk_data_plane_stop(s);
    migrate_del_blocker(s->migration_blocker);
    error_free(s->migration_blocker);
    bdrv_set_in_use(s->blk->conf.bs, 0);
    g_free(s);
}

void virtio_blk_data_plane_start(VirtIOBlockDataPlane *s)
{
    VirtQueue *vq;
    int i;

    if (s->started) {
        return;
    }

    vq = virtio_get_queue(s->vdev, 0);
    if (!vring_setup(&s->vring, s->vdev, 0)) {
        return;
    }

    s->ctx = aio_context_new();

    /* Set up guest notifier (irq) */
    if (s->vdev->binding->set_guest_notifiers(s->vdev->binding_opaque, 1,
                                              true) != 0) {
        fprintf(stderr, "virtio-blk failed to set guest notifier, "
                "ensure -enable-kvm is set\n");
        exit(1);
    }
    s->guest_notifier = virtio_queue_get_guest_notifier(vq);

    /* Set up virtqueue notify */
    if (s->vdev->binding->set_host_notifier(s->vdev->binding_opaque,
                                            0, true) != 0) {
        fprintf(stderr, "virtio-blk failed to set host notifier\n");
        exit(1);
    }
    s->host_notifier = *virtio_queue_get_host_notifier(vq);
    aio_set_event_notifier(s->ctx, &s->host_notifier, handle_notify, flush_true);

    /* Set up ioqueue */
    ioq_init(&s->ioqueue, s->fd, REQ_MAX);
    for (i = 0; i < ARRAY_SIZE(s->requests); i++) {
        ioq_put_iocb(&s->ioqueue, &s->requests[i].iocb);
    }
    s->io_notifier = *ioq_get_notifier(&s->ioqueue);
    aio_set_event_notifier(s->ctx, &s->io_notifier, handle_io, flush_io);

    s->started = true;
    trace_virtio_blk_data_plane_start(s);

    /* Kick right away to begin processing requests already in vring */
    event_notifier_set(virtio_queue_get_host_notifier(vq));

    /* Spawn thread in BH so it inherits iothread cpusets */
    s->start_bh = qemu_bh_new(start_data_plane_bh, s);
    qemu_bh_schedule(s->start_bh);
}

void virtio_blk_data_plane_stop(VirtIOBlockDataPlane *s)
{
    if (!s->started || s->stopping) {
        return;
    }
    s->stopping = true;
    trace_virtio_blk_data_plane_stop(s);

    /* Stop thread or cancel pending thread creation BH */
    if (s->start_bh) {
        qemu_bh_delete(s->start_bh);
        s->start_bh = NULL;
    } else {
        aio_notify(s->ctx);
        qemu_thread_join(&s->thread);
    }

    aio_set_event_notifier(s->ctx, &s->io_notifier, NULL, NULL);
    ioq_cleanup(&s->ioqueue);

    aio_set_event_notifier(s->ctx, &s->host_notifier, NULL, NULL);
    s->vdev->binding->set_host_notifier(s->vdev->binding_opaque, 0, false);

    aio_context_unref(s->ctx);

    /* Clean up guest notifier (irq) */
    s->vdev->binding->set_guest_notifiers(s->vdev->binding_opaque, 1, false);

    vring_teardown(&s->vring);
    s->started = false;
    s->stopping = false;
}
