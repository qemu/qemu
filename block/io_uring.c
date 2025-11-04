/*
 * Linux io_uring support.
 *
 * Copyright (C) 2009 IBM, Corp.
 * Copyright (C) 2009 Red Hat, Inc.
 * Copyright (C) 2019 Aarushi Mehta
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include <liburing.h>
#include "block/aio.h"
#include "block/block.h"
#include "block/raw-aio.h"
#include "qemu/coroutine.h"
#include "system/block-backend.h"
#include "trace.h"

typedef struct {
    Coroutine *co;
    QEMUIOVector *qiov;
    uint64_t offset;
    ssize_t ret;
    int type;
    int fd;
    BdrvRequestFlags flags;

    /*
     * Buffered reads may require resubmission, see
     * luring_resubmit_short_read().
     */
    int total_read;
    QEMUIOVector resubmit_qiov;

    CqeHandler cqe_handler;
} LuringRequest;

static void luring_prep_sqe(struct io_uring_sqe *sqe, void *opaque)
{
    LuringRequest *req = opaque;
    QEMUIOVector *qiov = req->qiov;
    uint64_t offset = req->offset;
    int fd = req->fd;
    BdrvRequestFlags flags = req->flags;

    switch (req->type) {
    case QEMU_AIO_WRITE:
    {
        int luring_flags = (flags & BDRV_REQ_FUA) ? RWF_DSYNC : 0;
        if (luring_flags != 0 || qiov->niov > 1) {
#ifdef HAVE_IO_URING_PREP_WRITEV2
            io_uring_prep_writev2(sqe, fd, qiov->iov,
                                  qiov->niov, offset, luring_flags);
#else
            /*
             * FUA should only be enabled with HAVE_IO_URING_PREP_WRITEV2, see
             * luring_has_fua().
             */
            assert(luring_flags == 0);

            io_uring_prep_writev(sqe, fd, qiov->iov, qiov->niov, offset);
#endif
        } else {
            /* The man page says non-vectored is faster than vectored */
            struct iovec *iov = qiov->iov;
            io_uring_prep_write(sqe, fd, iov->iov_base, iov->iov_len, offset);
        }
        break;
    }
    case QEMU_AIO_ZONE_APPEND:
        io_uring_prep_writev(sqe, fd, qiov->iov, qiov->niov, offset);
        break;
    case QEMU_AIO_READ:
    {
        if (req->resubmit_qiov.iov != NULL) {
            qiov = &req->resubmit_qiov;
        }
        if (qiov->niov > 1) {
            io_uring_prep_readv(sqe, fd, qiov->iov, qiov->niov,
                                offset + req->total_read);
        } else {
            /* The man page says non-vectored is faster than vectored */
            struct iovec *iov = qiov->iov;
            io_uring_prep_read(sqe, fd, iov->iov_base, iov->iov_len,
                               offset + req->total_read);
        }
        break;
    }
    case QEMU_AIO_FLUSH:
        io_uring_prep_fsync(sqe, fd, IORING_FSYNC_DATASYNC);
        break;
    default:
        fprintf(stderr, "%s: invalid AIO request type, aborting 0x%x.\n",
                        __func__, req->type);
        abort();
    }
}

/**
 * luring_resubmit_short_read:
 *
 * Short reads are rare but may occur. The remaining read request needs to be
 * resubmitted.
 */
static void luring_resubmit_short_read(LuringRequest *req, int nread)
{
    QEMUIOVector *resubmit_qiov;
    size_t remaining;

    trace_luring_resubmit_short_read(req, nread);

    /* Update read position */
    req->total_read += nread;
    remaining = req->qiov->size - req->total_read;

    /* Shorten qiov */
    resubmit_qiov = &req->resubmit_qiov;
    if (resubmit_qiov->iov == NULL) {
        qemu_iovec_init(resubmit_qiov, req->qiov->niov);
    } else {
        qemu_iovec_reset(resubmit_qiov);
    }
    qemu_iovec_concat(resubmit_qiov, req->qiov, req->total_read, remaining);

    aio_add_sqe(luring_prep_sqe, req, &req->cqe_handler);
}

static void luring_cqe_handler(CqeHandler *cqe_handler)
{
    LuringRequest *req = container_of(cqe_handler, LuringRequest, cqe_handler);
    int ret = cqe_handler->cqe.res;

    trace_luring_cqe_handler(req, ret);

    if (ret < 0) {
        /*
         * Only writev/readv/fsync requests on regular files or host block
         * devices are submitted. Therefore -EAGAIN is not expected but it's
         * known to happen sometimes with Linux SCSI. Submit again and hope
         * the request completes successfully.
         *
         * For more information, see:
         * https://lore.kernel.org/io-uring/20210727165811.284510-3-axboe@kernel.dk/T/#u
         *
         * If the code is changed to submit other types of requests in the
         * future, then this workaround may need to be extended to deal with
         * genuine -EAGAIN results that should not be resubmitted
         * immediately.
         */
        if (ret == -EINTR || ret == -EAGAIN) {
            aio_add_sqe(luring_prep_sqe, req, &req->cqe_handler);
            return;
        }
    } else if (req->qiov) {
        /* total_read is non-zero only for resubmitted read requests */
        int total_bytes = ret + req->total_read;

        if (total_bytes == req->qiov->size) {
            ret = 0;
        } else {
            /* Short Read/Write */
            if (req->type == QEMU_AIO_READ) {
                if (ret > 0) {
                    luring_resubmit_short_read(req, ret);
                    return;
                }

                /* Pad with zeroes */
                qemu_iovec_memset(req->qiov, total_bytes, 0,
                                  req->qiov->size - total_bytes);
                ret = 0;
            } else {
                ret = -ENOSPC;
            }
        }
    }

    req->ret = ret;
    qemu_iovec_destroy(&req->resubmit_qiov);

    /*
     * If the coroutine is already entered it must be in luring_co_submit() and
     * will notice req->ret has been filled in when it eventually runs later.
     * Coroutines cannot be entered recursively so avoid doing that!
     */
    if (!qemu_coroutine_entered(req->co)) {
        aio_co_wake(req->co);
    }
}

int coroutine_fn luring_co_submit(BlockDriverState *bs, int fd,
                                  uint64_t offset, QEMUIOVector *qiov,
                                  int type, BdrvRequestFlags flags)
{
    LuringRequest req = {
        .co         = qemu_coroutine_self(),
        .qiov       = qiov,
        .ret        = -EINPROGRESS,
        .type       = type,
        .fd         = fd,
        .offset     = offset,
        .flags      = flags,
    };

    req.cqe_handler.cb = luring_cqe_handler;

    trace_luring_co_submit(bs, &req, fd, offset, qiov ? qiov->size : 0, type);
    aio_add_sqe(luring_prep_sqe, &req, &req.cqe_handler);

    if (req.ret == -EINPROGRESS) {
        qemu_coroutine_yield();
    }
    return req.ret;
}

bool luring_has_fua(void)
{
#ifdef HAVE_IO_URING_PREP_WRITEV2
    return true;
#else
    return false;
#endif
}
