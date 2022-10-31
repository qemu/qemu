/*
 * QEMU Block driver for iSCSI images
 *
 * Copyright (c) 2010-2011 Ronnie Sahlberg <ronniesahlberg@gmail.com>
 * Copyright (c) 2012-2017 Peter Lieven <pl@kamp.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"

#include <poll.h>
#include <math.h>
#include <arpa/inet.h>
#include "sysemu/sysemu.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qemu/bitops.h"
#include "qemu/bitmap.h"
#include "block/block_int.h"
#include "block/qdict.h"
#include "scsi/constants.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/uuid.h"
#include "sysemu/replay.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-machine.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "crypto/secret.h"
#include "scsi/utils.h"
#include "trace.h"

/* Conflict between scsi/utils.h and libiscsi! :( */
#define SCSI_XFER_NONE ISCSI_XFER_NONE
#include <iscsi/iscsi.h>
#define inline __attribute__((gnu_inline))  /* required for libiscsi v1.9.0 */
#include <iscsi/scsi-lowlevel.h>
#undef inline
#undef SCSI_XFER_NONE
QEMU_BUILD_BUG_ON((int)SCSI_XFER_NONE != (int)ISCSI_XFER_NONE);

#ifdef __linux__
#include <scsi/sg.h>
#endif

typedef struct IscsiLun {
    struct iscsi_context *iscsi;
    AioContext *aio_context;
    int lun;
    enum scsi_inquiry_peripheral_device_type type;
    int block_size;
    uint64_t num_blocks;
    int events;
    QEMUTimer *nop_timer;
    QEMUTimer *event_timer;
    QemuMutex mutex;
    struct scsi_inquiry_logical_block_provisioning lbp;
    struct scsi_inquiry_block_limits bl;
    struct scsi_inquiry_device_designator *dd;
    unsigned char *zeroblock;
    /* The allocmap tracks which clusters (pages) on the iSCSI target are
     * allocated and which are not. In case a target returns zeros for
     * unallocated pages (iscsilun->lprz) we can directly return zeros instead
     * of reading zeros over the wire if a read request falls within an
     * unallocated block. As there are 3 possible states we need 2 bitmaps to
     * track. allocmap_valid keeps track if QEMU's information about a page is
     * valid. allocmap tracks if a page is allocated or not. In case QEMU has no
     * valid information about a page the corresponding allocmap entry should be
     * switched to unallocated as well to force a new lookup of the allocation
     * status as lookups are generally skipped if a page is suspect to be
     * allocated. If a iSCSI target is opened with cache.direct = on the
     * allocmap_valid does not exist turning all cached information invalid so
     * that a fresh lookup is made for any page even if allocmap entry returns
     * it's unallocated. */
    unsigned long *allocmap;
    unsigned long *allocmap_valid;
    long allocmap_size;
    int cluster_size;
    bool use_16_for_rw;
    bool write_protected;
    bool lbpme;
    bool lbprz;
    bool dpofua;
    bool has_write_same;
    bool request_timed_out;
} IscsiLun;

typedef struct IscsiTask {
    int status;
    int complete;
    int retries;
    int do_retry;
    struct scsi_task *task;
    Coroutine *co;
    IscsiLun *iscsilun;
    QEMUTimer retry_timer;
    int err_code;
    char *err_str;
} IscsiTask;

typedef struct IscsiAIOCB {
    BlockAIOCB common;
    QEMUBH *bh;
    IscsiLun *iscsilun;
    struct scsi_task *task;
    int status;
    int64_t sector_num;
    int nb_sectors;
    int ret;
#ifdef __linux__
    sg_io_hdr_t *ioh;
#endif
    bool cancelled;
} IscsiAIOCB;

/* libiscsi uses time_t so its enough to process events every second */
#define EVENT_INTERVAL 1000
#define NOP_INTERVAL 5000
#define MAX_NOP_FAILURES 3
#define ISCSI_CMD_RETRIES ARRAY_SIZE(iscsi_retry_times)
static const unsigned iscsi_retry_times[] = {8, 32, 128, 512, 2048, 8192, 32768};

/* this threshold is a trade-off knob to choose between
 * the potential additional overhead of an extra GET_LBA_STATUS request
 * vs. unnecessarily reading a lot of zero sectors over the wire.
 * If a read request is greater or equal than ISCSI_CHECKALLOC_THRES
 * sectors we check the allocation status of the area covered by the
 * request first if the allocationmap indicates that the area might be
 * unallocated. */
#define ISCSI_CHECKALLOC_THRES 64

#ifdef __linux__

static void
iscsi_bh_cb(void *p)
{
    IscsiAIOCB *acb = p;

    qemu_bh_delete(acb->bh);

    acb->common.cb(acb->common.opaque, acb->status);

    if (acb->task != NULL) {
        scsi_free_scsi_task(acb->task);
        acb->task = NULL;
    }

    qemu_aio_unref(acb);
}

static void
iscsi_schedule_bh(IscsiAIOCB *acb)
{
    if (acb->bh) {
        return;
    }
    acb->bh = aio_bh_new(acb->iscsilun->aio_context, iscsi_bh_cb, acb);
    qemu_bh_schedule(acb->bh);
}

#endif

static void iscsi_co_generic_bh_cb(void *opaque)
{
    struct IscsiTask *iTask = opaque;

    iTask->complete = 1;
    aio_co_wake(iTask->co);
}

static void iscsi_retry_timer_expired(void *opaque)
{
    struct IscsiTask *iTask = opaque;
    iTask->complete = 1;
    if (iTask->co) {
        aio_co_wake(iTask->co);
    }
}

static inline unsigned exp_random(double mean)
{
    return -mean * log((double)rand() / RAND_MAX);
}

/* SCSI_SENSE_ASCQ_INVALID_FIELD_IN_PARAMETER_LIST was introduced in
 * libiscsi 1.10.0, together with other constants we need.  Use it as
 * a hint that we have to define them ourselves if needed, to keep the
 * minimum required libiscsi version at 1.9.0.  We use an ASCQ macro for
 * the test because SCSI_STATUS_* is an enum.
 *
 * To guard against future changes where SCSI_SENSE_ASCQ_* also becomes
 * an enum, check against the LIBISCSI_API_VERSION macro, which was
 * introduced in 1.11.0.  If it is present, there is no need to define
 * anything.
 */
#if !defined(SCSI_SENSE_ASCQ_INVALID_FIELD_IN_PARAMETER_LIST) && \
    !defined(LIBISCSI_API_VERSION)
#define SCSI_STATUS_TASK_SET_FULL                          0x28
#define SCSI_STATUS_TIMEOUT                                0x0f000002
#define SCSI_SENSE_ASCQ_INVALID_FIELD_IN_PARAMETER_LIST    0x2600
#define SCSI_SENSE_ASCQ_PARAMETER_LIST_LENGTH_ERROR        0x1a00
#endif

#ifndef LIBISCSI_API_VERSION
#define LIBISCSI_API_VERSION 20130701
#endif

static int iscsi_translate_sense(struct scsi_sense *sense)
{
    return scsi_sense_to_errno(sense->key,
                               (sense->ascq & 0xFF00) >> 8,
                               sense->ascq & 0xFF);
}

/* Called (via iscsi_service) with QemuMutex held.  */
static void
iscsi_co_generic_cb(struct iscsi_context *iscsi, int status,
                        void *command_data, void *opaque)
{
    struct IscsiTask *iTask = opaque;
    struct scsi_task *task = command_data;

    iTask->status = status;
    iTask->do_retry = 0;
    iTask->err_code = 0;
    iTask->task = task;

    if (status != SCSI_STATUS_GOOD) {
        iTask->err_code = -EIO;
        if (iTask->retries++ < ISCSI_CMD_RETRIES) {
            if (status == SCSI_STATUS_BUSY ||
                status == SCSI_STATUS_TIMEOUT ||
                status == SCSI_STATUS_TASK_SET_FULL) {
                unsigned retry_time =
                    exp_random(iscsi_retry_times[iTask->retries - 1]);
                if (status == SCSI_STATUS_TIMEOUT) {
                    /* make sure the request is rescheduled AFTER the
                     * reconnect is initiated */
                    retry_time = EVENT_INTERVAL * 2;
                    iTask->iscsilun->request_timed_out = true;
                }
                error_report("iSCSI Busy/TaskSetFull/TimeOut"
                             " (retry #%u in %u ms): %s",
                             iTask->retries, retry_time,
                             iscsi_get_error(iscsi));
                aio_timer_init(iTask->iscsilun->aio_context,
                               &iTask->retry_timer, QEMU_CLOCK_REALTIME,
                               SCALE_MS, iscsi_retry_timer_expired, iTask);
                timer_mod(&iTask->retry_timer,
                          qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + retry_time);
                iTask->do_retry = 1;
            } else if (status == SCSI_STATUS_CHECK_CONDITION) {
                int error = iscsi_translate_sense(&task->sense);
                if (error == EAGAIN) {
                    error_report("iSCSI CheckCondition: %s",
                                 iscsi_get_error(iscsi));
                    iTask->do_retry = 1;
                } else {
                    iTask->err_code = -error;
                    iTask->err_str = g_strdup(iscsi_get_error(iscsi));
                }
            }
        }
    }

    if (iTask->co) {
        replay_bh_schedule_oneshot_event(iTask->iscsilun->aio_context,
                                         iscsi_co_generic_bh_cb, iTask);
    } else {
        iTask->complete = 1;
    }
}

static void coroutine_fn
iscsi_co_init_iscsitask(IscsiLun *iscsilun, struct IscsiTask *iTask)
{
    *iTask = (struct IscsiTask) {
        .co         = qemu_coroutine_self(),
        .iscsilun   = iscsilun,
    };
}

#ifdef __linux__

/* Called (via iscsi_service) with QemuMutex held. */
static void
iscsi_abort_task_cb(struct iscsi_context *iscsi, int status, void *command_data,
                    void *private_data)
{
    IscsiAIOCB *acb = private_data;

    /* If the command callback hasn't been called yet, drop the task */
    if (!acb->bh) {
        /* Call iscsi_aio_ioctl_cb() with SCSI_STATUS_CANCELLED */
        iscsi_scsi_cancel_task(iscsi, acb->task);
    }

    qemu_aio_unref(acb); /* acquired in iscsi_aio_cancel() */
}

static void
iscsi_aio_cancel(BlockAIOCB *blockacb)
{
    IscsiAIOCB *acb = (IscsiAIOCB *)blockacb;
    IscsiLun *iscsilun = acb->iscsilun;

    WITH_QEMU_LOCK_GUARD(&iscsilun->mutex) {

        /* If it was cancelled or completed already, our work is done here */
        if (acb->cancelled || acb->status != -EINPROGRESS) {
            return;
        }

        acb->cancelled = true;

        qemu_aio_ref(acb); /* released in iscsi_abort_task_cb() */

        /* send a task mgmt call to the target to cancel the task on the target */
        if (iscsi_task_mgmt_abort_task_async(iscsilun->iscsi, acb->task,
                                             iscsi_abort_task_cb, acb) < 0) {
            qemu_aio_unref(acb); /* since iscsi_abort_task_cb() won't be called */
        }
    }
}

static const AIOCBInfo iscsi_aiocb_info = {
    .aiocb_size         = sizeof(IscsiAIOCB),
    .cancel_async       = iscsi_aio_cancel,
};

#endif

static void iscsi_process_read(void *arg);
static void iscsi_process_write(void *arg);

/* Called with QemuMutex held.  */
static void
iscsi_set_events(IscsiLun *iscsilun)
{
    struct iscsi_context *iscsi = iscsilun->iscsi;
    int ev = iscsi_which_events(iscsi);

    if (ev != iscsilun->events) {
        aio_set_fd_handler(iscsilun->aio_context, iscsi_get_fd(iscsi),
                           false,
                           (ev & POLLIN) ? iscsi_process_read : NULL,
                           (ev & POLLOUT) ? iscsi_process_write : NULL,
                           NULL, NULL,
                           iscsilun);
        iscsilun->events = ev;
    }
}

static void iscsi_timed_check_events(void *opaque)
{
    IscsiLun *iscsilun = opaque;

    WITH_QEMU_LOCK_GUARD(&iscsilun->mutex) {
        /* check for timed out requests */
        iscsi_service(iscsilun->iscsi, 0);

        if (iscsilun->request_timed_out) {
            iscsilun->request_timed_out = false;
            iscsi_reconnect(iscsilun->iscsi);
        }

        /*
         * newer versions of libiscsi may return zero events. Ensure we are
         * able to return to service once this situation changes.
         */
        iscsi_set_events(iscsilun);
    }

    timer_mod(iscsilun->event_timer,
              qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + EVENT_INTERVAL);
}

static void
iscsi_process_read(void *arg)
{
    IscsiLun *iscsilun = arg;
    struct iscsi_context *iscsi = iscsilun->iscsi;

    qemu_mutex_lock(&iscsilun->mutex);
    iscsi_service(iscsi, POLLIN);
    iscsi_set_events(iscsilun);
    qemu_mutex_unlock(&iscsilun->mutex);
}

static void
iscsi_process_write(void *arg)
{
    IscsiLun *iscsilun = arg;
    struct iscsi_context *iscsi = iscsilun->iscsi;

    qemu_mutex_lock(&iscsilun->mutex);
    iscsi_service(iscsi, POLLOUT);
    iscsi_set_events(iscsilun);
    qemu_mutex_unlock(&iscsilun->mutex);
}

static int64_t sector_lun2qemu(int64_t sector, IscsiLun *iscsilun)
{
    return sector * iscsilun->block_size / BDRV_SECTOR_SIZE;
}

static int64_t sector_qemu2lun(int64_t sector, IscsiLun *iscsilun)
{
    return sector * BDRV_SECTOR_SIZE / iscsilun->block_size;
}

static bool is_byte_request_lun_aligned(int64_t offset, int64_t bytes,
                                        IscsiLun *iscsilun)
{
    if (offset % iscsilun->block_size || bytes % iscsilun->block_size) {
        error_report("iSCSI misaligned request: "
                     "iscsilun->block_size %u, offset %" PRIi64
                     ", bytes %" PRIi64,
                     iscsilun->block_size, offset, bytes);
        return false;
    }
    return true;
}

static bool is_sector_request_lun_aligned(int64_t sector_num, int nb_sectors,
                                          IscsiLun *iscsilun)
{
    assert(nb_sectors <= BDRV_REQUEST_MAX_SECTORS);
    return is_byte_request_lun_aligned(sector_num << BDRV_SECTOR_BITS,
                                       nb_sectors << BDRV_SECTOR_BITS,
                                       iscsilun);
}

static void iscsi_allocmap_free(IscsiLun *iscsilun)
{
    g_free(iscsilun->allocmap);
    g_free(iscsilun->allocmap_valid);
    iscsilun->allocmap = NULL;
    iscsilun->allocmap_valid = NULL;
}


static int iscsi_allocmap_init(IscsiLun *iscsilun, int open_flags)
{
    iscsi_allocmap_free(iscsilun);

    assert(iscsilun->cluster_size);
    iscsilun->allocmap_size =
        DIV_ROUND_UP(iscsilun->num_blocks * iscsilun->block_size,
                     iscsilun->cluster_size);

    iscsilun->allocmap = bitmap_try_new(iscsilun->allocmap_size);
    if (!iscsilun->allocmap) {
        return -ENOMEM;
    }

    if (open_flags & BDRV_O_NOCACHE) {
        /* when cache.direct = on all allocmap entries are
         * treated as invalid to force a relookup of the block
         * status on every read request */
        return 0;
    }

    iscsilun->allocmap_valid = bitmap_try_new(iscsilun->allocmap_size);
    if (!iscsilun->allocmap_valid) {
        /* if we are under memory pressure free the allocmap as well */
        iscsi_allocmap_free(iscsilun);
        return -ENOMEM;
    }

    return 0;
}

static void
iscsi_allocmap_update(IscsiLun *iscsilun, int64_t offset,
                      int64_t bytes, bool allocated, bool valid)
{
    int64_t cl_num_expanded, nb_cls_expanded, cl_num_shrunk, nb_cls_shrunk;

    if (iscsilun->allocmap == NULL) {
        return;
    }
    /* expand to entirely contain all affected clusters */
    assert(iscsilun->cluster_size);
    cl_num_expanded = offset / iscsilun->cluster_size;
    nb_cls_expanded = DIV_ROUND_UP(offset + bytes,
                                   iscsilun->cluster_size) - cl_num_expanded;
    /* shrink to touch only completely contained clusters */
    cl_num_shrunk = DIV_ROUND_UP(offset, iscsilun->cluster_size);
    nb_cls_shrunk = (offset + bytes) / iscsilun->cluster_size - cl_num_shrunk;
    if (allocated) {
        bitmap_set(iscsilun->allocmap, cl_num_expanded, nb_cls_expanded);
    } else {
        if (nb_cls_shrunk > 0) {
            bitmap_clear(iscsilun->allocmap, cl_num_shrunk, nb_cls_shrunk);
        }
    }

    if (iscsilun->allocmap_valid == NULL) {
        return;
    }
    if (valid) {
        if (nb_cls_shrunk > 0) {
            bitmap_set(iscsilun->allocmap_valid, cl_num_shrunk, nb_cls_shrunk);
        }
    } else {
        bitmap_clear(iscsilun->allocmap_valid, cl_num_expanded,
                     nb_cls_expanded);
    }
}

static void
iscsi_allocmap_set_allocated(IscsiLun *iscsilun, int64_t offset,
                             int64_t bytes)
{
    iscsi_allocmap_update(iscsilun, offset, bytes, true, true);
}

static void
iscsi_allocmap_set_unallocated(IscsiLun *iscsilun, int64_t offset,
                               int64_t bytes)
{
    /* Note: if cache.direct=on the fifth argument to iscsi_allocmap_update
     * is ignored, so this will in effect be an iscsi_allocmap_set_invalid.
     */
    iscsi_allocmap_update(iscsilun, offset, bytes, false, true);
}

static void iscsi_allocmap_set_invalid(IscsiLun *iscsilun, int64_t offset,
                                       int64_t bytes)
{
    iscsi_allocmap_update(iscsilun, offset, bytes, false, false);
}

static void iscsi_allocmap_invalidate(IscsiLun *iscsilun)
{
    if (iscsilun->allocmap) {
        bitmap_zero(iscsilun->allocmap, iscsilun->allocmap_size);
    }
    if (iscsilun->allocmap_valid) {
        bitmap_zero(iscsilun->allocmap_valid, iscsilun->allocmap_size);
    }
}

static inline bool
iscsi_allocmap_is_allocated(IscsiLun *iscsilun, int64_t offset,
                            int64_t bytes)
{
    unsigned long size;
    if (iscsilun->allocmap == NULL) {
        return true;
    }
    assert(iscsilun->cluster_size);
    size = DIV_ROUND_UP(offset + bytes, iscsilun->cluster_size);
    return !(find_next_bit(iscsilun->allocmap, size,
                           offset / iscsilun->cluster_size) == size);
}

static inline bool iscsi_allocmap_is_valid(IscsiLun *iscsilun,
                                           int64_t offset, int64_t bytes)
{
    unsigned long size;
    if (iscsilun->allocmap_valid == NULL) {
        return false;
    }
    assert(iscsilun->cluster_size);
    size = DIV_ROUND_UP(offset + bytes, iscsilun->cluster_size);
    return (find_next_zero_bit(iscsilun->allocmap_valid, size,
                               offset / iscsilun->cluster_size) == size);
}

static void coroutine_fn iscsi_co_wait_for_task(IscsiTask *iTask,
                                                IscsiLun *iscsilun)
{
    while (!iTask->complete) {
        iscsi_set_events(iscsilun);
        qemu_mutex_unlock(&iscsilun->mutex);
        qemu_coroutine_yield();
        qemu_mutex_lock(&iscsilun->mutex);
    }
}

static int coroutine_fn
iscsi_co_writev(BlockDriverState *bs, int64_t sector_num, int nb_sectors,
                QEMUIOVector *iov, int flags)
{
    IscsiLun *iscsilun = bs->opaque;
    struct IscsiTask iTask;
    uint64_t lba;
    uint32_t num_sectors;
    bool fua = flags & BDRV_REQ_FUA;
    int r = 0;

    if (fua) {
        assert(iscsilun->dpofua);
    }
    if (!is_sector_request_lun_aligned(sector_num, nb_sectors, iscsilun)) {
        return -EINVAL;
    }

    if (bs->bl.max_transfer) {
        assert(nb_sectors << BDRV_SECTOR_BITS <= bs->bl.max_transfer);
    }

    lba = sector_qemu2lun(sector_num, iscsilun);
    num_sectors = sector_qemu2lun(nb_sectors, iscsilun);
    iscsi_co_init_iscsitask(iscsilun, &iTask);
    qemu_mutex_lock(&iscsilun->mutex);
retry:
    if (iscsilun->use_16_for_rw) {
#if LIBISCSI_API_VERSION >= (20160603)
        iTask.task = iscsi_write16_iov_task(iscsilun->iscsi, iscsilun->lun, lba,
                                            NULL, num_sectors * iscsilun->block_size,
                                            iscsilun->block_size, 0, 0, fua, 0, 0,
                                            iscsi_co_generic_cb, &iTask,
                                            (struct scsi_iovec *)iov->iov, iov->niov);
    } else {
        iTask.task = iscsi_write10_iov_task(iscsilun->iscsi, iscsilun->lun, lba,
                                            NULL, num_sectors * iscsilun->block_size,
                                            iscsilun->block_size, 0, 0, fua, 0, 0,
                                            iscsi_co_generic_cb, &iTask,
                                            (struct scsi_iovec *)iov->iov, iov->niov);
    }
#else
        iTask.task = iscsi_write16_task(iscsilun->iscsi, iscsilun->lun, lba,
                                        NULL, num_sectors * iscsilun->block_size,
                                        iscsilun->block_size, 0, 0, fua, 0, 0,
                                        iscsi_co_generic_cb, &iTask);
    } else {
        iTask.task = iscsi_write10_task(iscsilun->iscsi, iscsilun->lun, lba,
                                        NULL, num_sectors * iscsilun->block_size,
                                        iscsilun->block_size, 0, 0, fua, 0, 0,
                                        iscsi_co_generic_cb, &iTask);
    }
#endif
    if (iTask.task == NULL) {
        qemu_mutex_unlock(&iscsilun->mutex);
        return -ENOMEM;
    }
#if LIBISCSI_API_VERSION < (20160603)
    scsi_task_set_iov_out(iTask.task, (struct scsi_iovec *) iov->iov,
                          iov->niov);
#endif
    iscsi_co_wait_for_task(&iTask, iscsilun);

    if (iTask.task != NULL) {
        scsi_free_scsi_task(iTask.task);
        iTask.task = NULL;
    }

    if (iTask.do_retry) {
        iTask.complete = 0;
        goto retry;
    }

    if (iTask.status != SCSI_STATUS_GOOD) {
        iscsi_allocmap_set_invalid(iscsilun, sector_num * BDRV_SECTOR_SIZE,
                                   nb_sectors * BDRV_SECTOR_SIZE);
        error_report("iSCSI WRITE10/16 failed at lba %" PRIu64 ": %s", lba,
                     iTask.err_str);
        r = iTask.err_code;
        goto out_unlock;
    }

    iscsi_allocmap_set_allocated(iscsilun, sector_num * BDRV_SECTOR_SIZE,
                                 nb_sectors * BDRV_SECTOR_SIZE);

out_unlock:
    qemu_mutex_unlock(&iscsilun->mutex);
    g_free(iTask.err_str);
    return r;
}



static int coroutine_fn iscsi_co_block_status(BlockDriverState *bs,
                                              bool want_zero, int64_t offset,
                                              int64_t bytes, int64_t *pnum,
                                              int64_t *map,
                                              BlockDriverState **file)
{
    IscsiLun *iscsilun = bs->opaque;
    struct scsi_get_lba_status *lbas = NULL;
    struct scsi_lba_status_descriptor *lbasd = NULL;
    struct IscsiTask iTask;
    uint64_t lba, max_bytes;
    int ret;

    iscsi_co_init_iscsitask(iscsilun, &iTask);

    assert(QEMU_IS_ALIGNED(offset | bytes, iscsilun->block_size));

    /* default to all sectors allocated */
    ret = BDRV_BLOCK_DATA | BDRV_BLOCK_OFFSET_VALID;
    if (map) {
        *map = offset;
    }
    *pnum = bytes;

    /* LUN does not support logical block provisioning */
    if (!iscsilun->lbpme) {
        goto out;
    }

    lba = offset / iscsilun->block_size;
    max_bytes = (iscsilun->num_blocks - lba) * iscsilun->block_size;

    qemu_mutex_lock(&iscsilun->mutex);
retry:
    if (iscsi_get_lba_status_task(iscsilun->iscsi, iscsilun->lun,
                                  lba, 8 + 16, iscsi_co_generic_cb,
                                  &iTask) == NULL) {
        ret = -ENOMEM;
        goto out_unlock;
    }
    iscsi_co_wait_for_task(&iTask, iscsilun);

    if (iTask.do_retry) {
        if (iTask.task != NULL) {
            scsi_free_scsi_task(iTask.task);
            iTask.task = NULL;
        }
        iTask.complete = 0;
        goto retry;
    }

    if (iTask.status != SCSI_STATUS_GOOD) {
        /* in case the get_lba_status_callout fails (i.e.
         * because the device is busy or the cmd is not
         * supported) we pretend all blocks are allocated
         * for backwards compatibility */
        error_report("iSCSI GET_LBA_STATUS failed at lba %" PRIu64 ": %s",
                     lba, iTask.err_str);
        goto out_unlock;
    }

    lbas = scsi_datain_unmarshall(iTask.task);
    if (lbas == NULL || lbas->num_descriptors == 0) {
        ret = -EIO;
        goto out_unlock;
    }

    lbasd = &lbas->descriptors[0];

    if (lba != lbasd->lba) {
        ret = -EIO;
        goto out_unlock;
    }

    *pnum = MIN((int64_t) lbasd->num_blocks * iscsilun->block_size, max_bytes);

    if (lbasd->provisioning == SCSI_PROVISIONING_TYPE_DEALLOCATED ||
        lbasd->provisioning == SCSI_PROVISIONING_TYPE_ANCHORED) {
        ret &= ~BDRV_BLOCK_DATA;
        if (iscsilun->lbprz) {
            ret |= BDRV_BLOCK_ZERO;
        }
    }

    if (ret & BDRV_BLOCK_ZERO) {
        iscsi_allocmap_set_unallocated(iscsilun, offset, *pnum);
    } else {
        iscsi_allocmap_set_allocated(iscsilun, offset, *pnum);
    }

out_unlock:
    qemu_mutex_unlock(&iscsilun->mutex);
    g_free(iTask.err_str);
out:
    if (iTask.task != NULL) {
        scsi_free_scsi_task(iTask.task);
    }
    if (ret > 0 && ret & BDRV_BLOCK_OFFSET_VALID && file) {
        *file = bs;
    }
    return ret;
}

static int coroutine_fn iscsi_co_readv(BlockDriverState *bs,
                                       int64_t sector_num, int nb_sectors,
                                       QEMUIOVector *iov)
{
    IscsiLun *iscsilun = bs->opaque;
    struct IscsiTask iTask;
    uint64_t lba;
    uint32_t num_sectors;
    int r = 0;

    if (!is_sector_request_lun_aligned(sector_num, nb_sectors, iscsilun)) {
        return -EINVAL;
    }

    if (bs->bl.max_transfer) {
        assert(nb_sectors << BDRV_SECTOR_BITS <= bs->bl.max_transfer);
    }

    /* if cache.direct is off and we have a valid entry in our allocation map
     * we can skip checking the block status and directly return zeroes if
     * the request falls within an unallocated area */
    if (iscsi_allocmap_is_valid(iscsilun, sector_num * BDRV_SECTOR_SIZE,
                                nb_sectors * BDRV_SECTOR_SIZE) &&
        !iscsi_allocmap_is_allocated(iscsilun, sector_num * BDRV_SECTOR_SIZE,
                                     nb_sectors * BDRV_SECTOR_SIZE)) {
            qemu_iovec_memset(iov, 0, 0x00, iov->size);
            return 0;
    }

    if (nb_sectors >= ISCSI_CHECKALLOC_THRES &&
        !iscsi_allocmap_is_valid(iscsilun, sector_num * BDRV_SECTOR_SIZE,
                                 nb_sectors * BDRV_SECTOR_SIZE) &&
        !iscsi_allocmap_is_allocated(iscsilun, sector_num * BDRV_SECTOR_SIZE,
                                     nb_sectors * BDRV_SECTOR_SIZE)) {
        int64_t pnum;
        /* check the block status from the beginning of the cluster
         * containing the start sector */
        int64_t head;
        int ret;

        assert(iscsilun->cluster_size);
        head = (sector_num * BDRV_SECTOR_SIZE) % iscsilun->cluster_size;
        ret = iscsi_co_block_status(bs, true,
                                    sector_num * BDRV_SECTOR_SIZE - head,
                                    BDRV_REQUEST_MAX_BYTES, &pnum, NULL, NULL);
        if (ret < 0) {
            return ret;
        }
        /* if the whole request falls into an unallocated area we can avoid
         * reading and directly return zeroes instead */
        if (ret & BDRV_BLOCK_ZERO &&
            pnum >= nb_sectors * BDRV_SECTOR_SIZE + head) {
            qemu_iovec_memset(iov, 0, 0x00, iov->size);
            return 0;
        }
    }

    lba = sector_qemu2lun(sector_num, iscsilun);
    num_sectors = sector_qemu2lun(nb_sectors, iscsilun);

    iscsi_co_init_iscsitask(iscsilun, &iTask);
    qemu_mutex_lock(&iscsilun->mutex);
retry:
    if (iscsilun->use_16_for_rw) {
#if LIBISCSI_API_VERSION >= (20160603)
        iTask.task = iscsi_read16_iov_task(iscsilun->iscsi, iscsilun->lun, lba,
                                           num_sectors * iscsilun->block_size,
                                           iscsilun->block_size, 0, 0, 0, 0, 0,
                                           iscsi_co_generic_cb, &iTask,
                                           (struct scsi_iovec *)iov->iov, iov->niov);
    } else {
        iTask.task = iscsi_read10_iov_task(iscsilun->iscsi, iscsilun->lun, lba,
                                           num_sectors * iscsilun->block_size,
                                           iscsilun->block_size,
                                           0, 0, 0, 0, 0,
                                           iscsi_co_generic_cb, &iTask,
                                           (struct scsi_iovec *)iov->iov, iov->niov);
    }
#else
        iTask.task = iscsi_read16_task(iscsilun->iscsi, iscsilun->lun, lba,
                                       num_sectors * iscsilun->block_size,
                                       iscsilun->block_size, 0, 0, 0, 0, 0,
                                       iscsi_co_generic_cb, &iTask);
    } else {
        iTask.task = iscsi_read10_task(iscsilun->iscsi, iscsilun->lun, lba,
                                       num_sectors * iscsilun->block_size,
                                       iscsilun->block_size,
                                       0, 0, 0, 0, 0,
                                       iscsi_co_generic_cb, &iTask);
    }
#endif
    if (iTask.task == NULL) {
        qemu_mutex_unlock(&iscsilun->mutex);
        return -ENOMEM;
    }
#if LIBISCSI_API_VERSION < (20160603)
    scsi_task_set_iov_in(iTask.task, (struct scsi_iovec *) iov->iov, iov->niov);
#endif

    iscsi_co_wait_for_task(&iTask, iscsilun);
    if (iTask.task != NULL) {
        scsi_free_scsi_task(iTask.task);
        iTask.task = NULL;
    }

    if (iTask.do_retry) {
        iTask.complete = 0;
        goto retry;
    }

    if (iTask.status != SCSI_STATUS_GOOD) {
        error_report("iSCSI READ10/16 failed at lba %" PRIu64 ": %s",
                     lba, iTask.err_str);
        r = iTask.err_code;
    }

    qemu_mutex_unlock(&iscsilun->mutex);
    g_free(iTask.err_str);
    return r;
}

static int coroutine_fn iscsi_co_flush(BlockDriverState *bs)
{
    IscsiLun *iscsilun = bs->opaque;
    struct IscsiTask iTask;
    int r = 0;

    iscsi_co_init_iscsitask(iscsilun, &iTask);
    qemu_mutex_lock(&iscsilun->mutex);
retry:
    if (iscsi_synchronizecache10_task(iscsilun->iscsi, iscsilun->lun, 0, 0, 0,
                                      0, iscsi_co_generic_cb, &iTask) == NULL) {
        qemu_mutex_unlock(&iscsilun->mutex);
        return -ENOMEM;
    }

    iscsi_co_wait_for_task(&iTask, iscsilun);

    if (iTask.task != NULL) {
        scsi_free_scsi_task(iTask.task);
        iTask.task = NULL;
    }

    if (iTask.do_retry) {
        iTask.complete = 0;
        goto retry;
    }

    if (iTask.status != SCSI_STATUS_GOOD) {
        error_report("iSCSI SYNCHRONIZECACHE10 failed: %s", iTask.err_str);
        r = iTask.err_code;
    }

    qemu_mutex_unlock(&iscsilun->mutex);
    g_free(iTask.err_str);
    return r;
}

#ifdef __linux__
/* Called (via iscsi_service) with QemuMutex held.  */
static void
iscsi_aio_ioctl_cb(struct iscsi_context *iscsi, int status,
                     void *command_data, void *opaque)
{
    IscsiAIOCB *acb = opaque;

    if (status == SCSI_STATUS_CANCELLED) {
        if (!acb->bh) {
            acb->status = -ECANCELED;
            iscsi_schedule_bh(acb);
        }
        return;
    }

    acb->status = 0;
    if (status < 0) {
        error_report("Failed to ioctl(SG_IO) to iSCSI lun. %s",
                     iscsi_get_error(iscsi));
        acb->status = -iscsi_translate_sense(&acb->task->sense);
    }

    acb->ioh->driver_status = 0;
    acb->ioh->host_status   = 0;
    acb->ioh->resid         = 0;
    acb->ioh->status        = status;

#define SG_ERR_DRIVER_SENSE    0x08

    if (status == SCSI_STATUS_CHECK_CONDITION && acb->task->datain.size >= 2) {
        int ss;

        acb->ioh->driver_status |= SG_ERR_DRIVER_SENSE;

        acb->ioh->sb_len_wr = acb->task->datain.size - 2;
        ss = MIN(acb->ioh->mx_sb_len, acb->ioh->sb_len_wr);
        memcpy(acb->ioh->sbp, &acb->task->datain.data[2], ss);
    }

    iscsi_schedule_bh(acb);
}

static void iscsi_ioctl_bh_completion(void *opaque)
{
    IscsiAIOCB *acb = opaque;

    qemu_bh_delete(acb->bh);
    acb->common.cb(acb->common.opaque, acb->ret);
    qemu_aio_unref(acb);
}

static void iscsi_ioctl_handle_emulated(IscsiAIOCB *acb, int req, void *buf)
{
    BlockDriverState *bs = acb->common.bs;
    IscsiLun *iscsilun = bs->opaque;
    int ret = 0;

    switch (req) {
    case SG_GET_VERSION_NUM:
        *(int *)buf = 30000;
        break;
    case SG_GET_SCSI_ID:
        ((struct sg_scsi_id *)buf)->scsi_type = iscsilun->type;
        break;
    default:
        ret = -EINVAL;
    }
    assert(!acb->bh);
    acb->bh = aio_bh_new(bdrv_get_aio_context(bs),
                         iscsi_ioctl_bh_completion, acb);
    acb->ret = ret;
    qemu_bh_schedule(acb->bh);
}

static BlockAIOCB *iscsi_aio_ioctl(BlockDriverState *bs,
        unsigned long int req, void *buf,
        BlockCompletionFunc *cb, void *opaque)
{
    IscsiLun *iscsilun = bs->opaque;
    struct iscsi_context *iscsi = iscsilun->iscsi;
    struct iscsi_data data;
    IscsiAIOCB *acb;

    acb = qemu_aio_get(&iscsi_aiocb_info, bs, cb, opaque);

    acb->iscsilun = iscsilun;
    acb->bh          = NULL;
    acb->status      = -EINPROGRESS;
    acb->ioh         = buf;
    acb->cancelled   = false;

    if (req != SG_IO) {
        iscsi_ioctl_handle_emulated(acb, req, buf);
        return &acb->common;
    }

    if (acb->ioh->cmd_len > SCSI_CDB_MAX_SIZE) {
        error_report("iSCSI: ioctl error CDB exceeds max size (%d > %d)",
                     acb->ioh->cmd_len, SCSI_CDB_MAX_SIZE);
        qemu_aio_unref(acb);
        return NULL;
    }

    acb->task = malloc(sizeof(struct scsi_task));
    if (acb->task == NULL) {
        error_report("iSCSI: Failed to allocate task for scsi command. %s",
                     iscsi_get_error(iscsi));
        qemu_aio_unref(acb);
        return NULL;
    }
    memset(acb->task, 0, sizeof(struct scsi_task));

    switch (acb->ioh->dxfer_direction) {
    case SG_DXFER_TO_DEV:
        acb->task->xfer_dir = SCSI_XFER_WRITE;
        break;
    case SG_DXFER_FROM_DEV:
        acb->task->xfer_dir = SCSI_XFER_READ;
        break;
    default:
        acb->task->xfer_dir = SCSI_XFER_NONE;
        break;
    }

    acb->task->cdb_size = acb->ioh->cmd_len;
    memcpy(&acb->task->cdb[0], acb->ioh->cmdp, acb->ioh->cmd_len);
    acb->task->expxferlen = acb->ioh->dxfer_len;

    data.size = 0;
    qemu_mutex_lock(&iscsilun->mutex);
    if (acb->task->xfer_dir == SCSI_XFER_WRITE) {
        if (acb->ioh->iovec_count == 0) {
            data.data = acb->ioh->dxferp;
            data.size = acb->ioh->dxfer_len;
        } else {
            scsi_task_set_iov_out(acb->task,
                                 (struct scsi_iovec *) acb->ioh->dxferp,
                                 acb->ioh->iovec_count);
        }
    }

    if (iscsi_scsi_command_async(iscsi, iscsilun->lun, acb->task,
                                 iscsi_aio_ioctl_cb,
                                 (data.size > 0) ? &data : NULL,
                                 acb) != 0) {
        qemu_mutex_unlock(&iscsilun->mutex);
        scsi_free_scsi_task(acb->task);
        qemu_aio_unref(acb);
        return NULL;
    }

    /* tell libiscsi to read straight into the buffer we got from ioctl */
    if (acb->task->xfer_dir == SCSI_XFER_READ) {
        if (acb->ioh->iovec_count == 0) {
            scsi_task_add_data_in_buffer(acb->task,
                                         acb->ioh->dxfer_len,
                                         acb->ioh->dxferp);
        } else {
            scsi_task_set_iov_in(acb->task,
                                 (struct scsi_iovec *) acb->ioh->dxferp,
                                 acb->ioh->iovec_count);
        }
    }

    iscsi_set_events(iscsilun);
    qemu_mutex_unlock(&iscsilun->mutex);

    return &acb->common;
}

#endif

static int64_t
iscsi_getlength(BlockDriverState *bs)
{
    IscsiLun *iscsilun = bs->opaque;
    int64_t len;

    len  = iscsilun->num_blocks;
    len *= iscsilun->block_size;

    return len;
}

static int
coroutine_fn iscsi_co_pdiscard(BlockDriverState *bs, int64_t offset,
                               int64_t bytes)
{
    IscsiLun *iscsilun = bs->opaque;
    struct IscsiTask iTask;
    struct unmap_list list;
    int r = 0;

    if (!is_byte_request_lun_aligned(offset, bytes, iscsilun)) {
        return -ENOTSUP;
    }

    if (!iscsilun->lbp.lbpu) {
        /* UNMAP is not supported by the target */
        return 0;
    }

    /*
     * We don't want to overflow list.num which is uint32_t.
     * We rely on our max_pdiscard.
     */
    assert(bytes / iscsilun->block_size <= UINT32_MAX);

    list.lba = offset / iscsilun->block_size;
    list.num = bytes / iscsilun->block_size;

    iscsi_co_init_iscsitask(iscsilun, &iTask);
    qemu_mutex_lock(&iscsilun->mutex);
retry:
    if (iscsi_unmap_task(iscsilun->iscsi, iscsilun->lun, 0, 0, &list, 1,
                         iscsi_co_generic_cb, &iTask) == NULL) {
        r = -ENOMEM;
        goto out_unlock;
    }

    iscsi_co_wait_for_task(&iTask, iscsilun);

    if (iTask.task != NULL) {
        scsi_free_scsi_task(iTask.task);
        iTask.task = NULL;
    }

    if (iTask.do_retry) {
        iTask.complete = 0;
        goto retry;
    }

    iscsi_allocmap_set_invalid(iscsilun, offset, bytes);

    if (iTask.status == SCSI_STATUS_CHECK_CONDITION) {
        /* the target might fail with a check condition if it
           is not happy with the alignment of the UNMAP request
           we silently fail in this case */
        goto out_unlock;
    }

    if (iTask.status != SCSI_STATUS_GOOD) {
        error_report("iSCSI UNMAP failed at lba %" PRIu64 ": %s",
                     list.lba, iTask.err_str);
        r = iTask.err_code;
        goto out_unlock;
    }

out_unlock:
    qemu_mutex_unlock(&iscsilun->mutex);
    g_free(iTask.err_str);
    return r;
}

static int
coroutine_fn iscsi_co_pwrite_zeroes(BlockDriverState *bs, int64_t offset,
                                    int64_t bytes, BdrvRequestFlags flags)
{
    IscsiLun *iscsilun = bs->opaque;
    struct IscsiTask iTask;
    uint64_t lba;
    uint64_t nb_blocks;
    bool use_16_for_ws = iscsilun->use_16_for_rw;
    int r = 0;

    if (!is_byte_request_lun_aligned(offset, bytes, iscsilun)) {
        return -ENOTSUP;
    }

    if (flags & BDRV_REQ_MAY_UNMAP) {
        if (!use_16_for_ws && !iscsilun->lbp.lbpws10) {
            /* WRITESAME10 with UNMAP is unsupported try WRITESAME16 */
            use_16_for_ws = true;
        }
        if (use_16_for_ws && !iscsilun->lbp.lbpws) {
            /* WRITESAME16 with UNMAP is not supported by the target,
             * fall back and try WRITESAME10/16 without UNMAP */
            flags &= ~BDRV_REQ_MAY_UNMAP;
            use_16_for_ws = iscsilun->use_16_for_rw;
        }
    }

    if (!(flags & BDRV_REQ_MAY_UNMAP) && !iscsilun->has_write_same) {
        /* WRITESAME without UNMAP is not supported by the target */
        return -ENOTSUP;
    }

    lba = offset / iscsilun->block_size;
    nb_blocks = bytes / iscsilun->block_size;

    if (iscsilun->zeroblock == NULL) {
        iscsilun->zeroblock = g_try_malloc0(iscsilun->block_size);
        if (iscsilun->zeroblock == NULL) {
            return -ENOMEM;
        }
    }

    qemu_mutex_lock(&iscsilun->mutex);
    iscsi_co_init_iscsitask(iscsilun, &iTask);
retry:
    if (use_16_for_ws) {
        /*
         * iscsi_writesame16_task num_blocks argument is uint32_t. We rely here
         * on our max_pwrite_zeroes limit.
         */
        assert(nb_blocks <= UINT32_MAX);
        iTask.task = iscsi_writesame16_task(iscsilun->iscsi, iscsilun->lun, lba,
                                            iscsilun->zeroblock, iscsilun->block_size,
                                            nb_blocks, 0, !!(flags & BDRV_REQ_MAY_UNMAP),
                                            0, 0, iscsi_co_generic_cb, &iTask);
    } else {
        /*
         * iscsi_writesame10_task num_blocks argument is uint16_t. We rely here
         * on our max_pwrite_zeroes limit.
         */
        assert(nb_blocks <= UINT16_MAX);
        iTask.task = iscsi_writesame10_task(iscsilun->iscsi, iscsilun->lun, lba,
                                            iscsilun->zeroblock, iscsilun->block_size,
                                            nb_blocks, 0, !!(flags & BDRV_REQ_MAY_UNMAP),
                                            0, 0, iscsi_co_generic_cb, &iTask);
    }
    if (iTask.task == NULL) {
        qemu_mutex_unlock(&iscsilun->mutex);
        return -ENOMEM;
    }

    iscsi_co_wait_for_task(&iTask, iscsilun);

    if (iTask.status == SCSI_STATUS_CHECK_CONDITION &&
        iTask.task->sense.key == SCSI_SENSE_ILLEGAL_REQUEST &&
        (iTask.task->sense.ascq == SCSI_SENSE_ASCQ_INVALID_OPERATION_CODE ||
         iTask.task->sense.ascq == SCSI_SENSE_ASCQ_INVALID_FIELD_IN_CDB)) {
        /* WRITE SAME is not supported by the target */
        iscsilun->has_write_same = false;
        scsi_free_scsi_task(iTask.task);
        r = -ENOTSUP;
        goto out_unlock;
    }

    if (iTask.task != NULL) {
        scsi_free_scsi_task(iTask.task);
        iTask.task = NULL;
    }

    if (iTask.do_retry) {
        iTask.complete = 0;
        goto retry;
    }

    if (iTask.status != SCSI_STATUS_GOOD) {
        iscsi_allocmap_set_invalid(iscsilun, offset, bytes);
        error_report("iSCSI WRITESAME10/16 failed at lba %" PRIu64 ": %s",
                     lba, iTask.err_str);
        r = iTask.err_code;
        goto out_unlock;
    }

    if (flags & BDRV_REQ_MAY_UNMAP) {
        iscsi_allocmap_set_invalid(iscsilun, offset, bytes);
    } else {
        iscsi_allocmap_set_allocated(iscsilun, offset, bytes);
    }

out_unlock:
    qemu_mutex_unlock(&iscsilun->mutex);
    g_free(iTask.err_str);
    return r;
}

static void apply_chap(struct iscsi_context *iscsi, QemuOpts *opts,
                       Error **errp)
{
    const char *user = NULL;
    const char *password = NULL;
    const char *secretid;
    char *secret = NULL;

    user = qemu_opt_get(opts, "user");
    if (!user) {
        return;
    }

    secretid = qemu_opt_get(opts, "password-secret");
    password = qemu_opt_get(opts, "password");
    if (secretid && password) {
        error_setg(errp, "'password' and 'password-secret' properties are "
                   "mutually exclusive");
        return;
    }
    if (secretid) {
        secret = qcrypto_secret_lookup_as_utf8(secretid, errp);
        if (!secret) {
            return;
        }
        password = secret;
    } else if (!password) {
        error_setg(errp, "CHAP username specified but no password was given");
        return;
    }

    if (iscsi_set_initiator_username_pwd(iscsi, user, password)) {
        error_setg(errp, "Failed to set initiator username and password");
    }

    g_free(secret);
}

static void apply_header_digest(struct iscsi_context *iscsi, QemuOpts *opts,
                                Error **errp)
{
    const char *digest = NULL;

    digest = qemu_opt_get(opts, "header-digest");
    if (!digest) {
        iscsi_set_header_digest(iscsi, ISCSI_HEADER_DIGEST_NONE_CRC32C);
    } else if (!strcmp(digest, "crc32c")) {
        iscsi_set_header_digest(iscsi, ISCSI_HEADER_DIGEST_CRC32C);
    } else if (!strcmp(digest, "none")) {
        iscsi_set_header_digest(iscsi, ISCSI_HEADER_DIGEST_NONE);
    } else if (!strcmp(digest, "crc32c-none")) {
        iscsi_set_header_digest(iscsi, ISCSI_HEADER_DIGEST_CRC32C_NONE);
    } else if (!strcmp(digest, "none-crc32c")) {
        iscsi_set_header_digest(iscsi, ISCSI_HEADER_DIGEST_NONE_CRC32C);
    } else {
        error_setg(errp, "Invalid header-digest setting : %s", digest);
    }
}

static char *get_initiator_name(QemuOpts *opts)
{
    const char *name;
    char *iscsi_name;
    UuidInfo *uuid_info;

    name = qemu_opt_get(opts, "initiator-name");
    if (name) {
        return g_strdup(name);
    }

    uuid_info = qmp_query_uuid(NULL);
    if (strcmp(uuid_info->UUID, UUID_NONE) == 0) {
        name = qemu_get_vm_name();
    } else {
        name = uuid_info->UUID;
    }
    iscsi_name = g_strdup_printf("iqn.2008-11.org.linux-kvm%s%s",
                                 name ? ":" : "", name ? name : "");
    qapi_free_UuidInfo(uuid_info);
    return iscsi_name;
}

static void iscsi_nop_timed_event(void *opaque)
{
    IscsiLun *iscsilun = opaque;

    QEMU_LOCK_GUARD(&iscsilun->mutex);
    if (iscsi_get_nops_in_flight(iscsilun->iscsi) >= MAX_NOP_FAILURES) {
        error_report("iSCSI: NOP timeout. Reconnecting...");
        iscsilun->request_timed_out = true;
    } else if (iscsi_nop_out_async(iscsilun->iscsi, NULL, NULL, 0, NULL) != 0) {
        error_report("iSCSI: failed to sent NOP-Out. Disabling NOP messages.");
        return;
    }

    timer_mod(iscsilun->nop_timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + NOP_INTERVAL);
    iscsi_set_events(iscsilun);
}

static void iscsi_readcapacity_sync(IscsiLun *iscsilun, Error **errp)
{
    struct scsi_task *task = NULL;
    struct scsi_readcapacity10 *rc10 = NULL;
    struct scsi_readcapacity16 *rc16 = NULL;
    int retries = ISCSI_CMD_RETRIES; 

    do {
        if (task != NULL) {
            scsi_free_scsi_task(task);
            task = NULL;
        }

        switch (iscsilun->type) {
        case TYPE_DISK:
            task = iscsi_readcapacity16_sync(iscsilun->iscsi, iscsilun->lun);
            if (task != NULL && task->status == SCSI_STATUS_GOOD) {
                rc16 = scsi_datain_unmarshall(task);
                if (rc16 == NULL) {
                    error_setg(errp, "iSCSI: Failed to unmarshall readcapacity16 data.");
                } else {
                    iscsilun->block_size = rc16->block_length;
                    iscsilun->num_blocks = rc16->returned_lba + 1;
                    iscsilun->lbpme = !!rc16->lbpme;
                    iscsilun->lbprz = !!rc16->lbprz;
                    iscsilun->use_16_for_rw = (rc16->returned_lba > 0xffffffff);
                }
                break;
            }
            if (task != NULL && task->status == SCSI_STATUS_CHECK_CONDITION
                && task->sense.key == SCSI_SENSE_UNIT_ATTENTION) {
                break;
            }
            /* Fall through and try READ CAPACITY(10) instead.  */
        case TYPE_ROM:
            task = iscsi_readcapacity10_sync(iscsilun->iscsi, iscsilun->lun, 0, 0);
            if (task != NULL && task->status == SCSI_STATUS_GOOD) {
                rc10 = scsi_datain_unmarshall(task);
                if (rc10 == NULL) {
                    error_setg(errp, "iSCSI: Failed to unmarshall readcapacity10 data.");
                } else {
                    iscsilun->block_size = rc10->block_size;
                    if (rc10->lba == 0) {
                        /* blank disk loaded */
                        iscsilun->num_blocks = 0;
                    } else {
                        iscsilun->num_blocks = rc10->lba + 1;
                    }
                }
            }
            break;
        default:
            return;
        }
    } while (task != NULL && task->status == SCSI_STATUS_CHECK_CONDITION
             && task->sense.key == SCSI_SENSE_UNIT_ATTENTION
             && retries-- > 0);

    if (task == NULL || task->status != SCSI_STATUS_GOOD) {
        error_setg(errp, "iSCSI: failed to send readcapacity10/16 command");
    } else if (!iscsilun->block_size ||
               iscsilun->block_size % BDRV_SECTOR_SIZE) {
        error_setg(errp, "iSCSI: the target returned an invalid "
                   "block size of %d.", iscsilun->block_size);
    }
    if (task) {
        scsi_free_scsi_task(task);
    }
}

static struct scsi_task *iscsi_do_inquiry(struct iscsi_context *iscsi, int lun,
                                          int evpd, int pc, void **inq, Error **errp)
{
    int full_size;
    struct scsi_task *task = NULL;
    task = iscsi_inquiry_sync(iscsi, lun, evpd, pc, 64);
    if (task == NULL || task->status != SCSI_STATUS_GOOD) {
        goto fail;
    }
    full_size = scsi_datain_getfullsize(task);
    if (full_size > task->datain.size) {
        scsi_free_scsi_task(task);

        /* we need more data for the full list */
        task = iscsi_inquiry_sync(iscsi, lun, evpd, pc, full_size);
        if (task == NULL || task->status != SCSI_STATUS_GOOD) {
            goto fail;
        }
    }

    *inq = scsi_datain_unmarshall(task);
    if (*inq == NULL) {
        error_setg(errp, "iSCSI: failed to unmarshall inquiry datain blob");
        goto fail_with_err;
    }

    return task;

fail:
    error_setg(errp, "iSCSI: Inquiry command failed : %s",
               iscsi_get_error(iscsi));
fail_with_err:
    if (task != NULL) {
        scsi_free_scsi_task(task);
    }
    return NULL;
}

static void iscsi_detach_aio_context(BlockDriverState *bs)
{
    IscsiLun *iscsilun = bs->opaque;

    aio_set_fd_handler(iscsilun->aio_context, iscsi_get_fd(iscsilun->iscsi),
                       false, NULL, NULL, NULL, NULL, NULL);
    iscsilun->events = 0;

    if (iscsilun->nop_timer) {
        timer_free(iscsilun->nop_timer);
        iscsilun->nop_timer = NULL;
    }
    if (iscsilun->event_timer) {
        timer_free(iscsilun->event_timer);
        iscsilun->event_timer = NULL;
    }
}

static void iscsi_attach_aio_context(BlockDriverState *bs,
                                     AioContext *new_context)
{
    IscsiLun *iscsilun = bs->opaque;

    iscsilun->aio_context = new_context;
    iscsi_set_events(iscsilun);

    /* Set up a timer for sending out iSCSI NOPs */
    iscsilun->nop_timer = aio_timer_new(iscsilun->aio_context,
                                        QEMU_CLOCK_REALTIME, SCALE_MS,
                                        iscsi_nop_timed_event, iscsilun);
    timer_mod(iscsilun->nop_timer,
              qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + NOP_INTERVAL);

    /* Set up a timer for periodic calls to iscsi_set_events and to
     * scan for command timeout */
    iscsilun->event_timer = aio_timer_new(iscsilun->aio_context,
                                          QEMU_CLOCK_REALTIME, SCALE_MS,
                                          iscsi_timed_check_events, iscsilun);
    timer_mod(iscsilun->event_timer,
              qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + EVENT_INTERVAL);
}

static void iscsi_modesense_sync(IscsiLun *iscsilun)
{
    struct scsi_task *task;
    struct scsi_mode_sense *ms = NULL;
    iscsilun->write_protected = false;
    iscsilun->dpofua = false;

    task = iscsi_modesense6_sync(iscsilun->iscsi, iscsilun->lun,
                                 1, SCSI_MODESENSE_PC_CURRENT,
                                 0x3F, 0, 255);
    if (task == NULL) {
        error_report("iSCSI: Failed to send MODE_SENSE(6) command: %s",
                     iscsi_get_error(iscsilun->iscsi));
        goto out;
    }

    if (task->status != SCSI_STATUS_GOOD) {
        error_report("iSCSI: Failed MODE_SENSE(6), LUN assumed writable");
        goto out;
    }
    ms = scsi_datain_unmarshall(task);
    if (!ms) {
        error_report("iSCSI: Failed to unmarshall MODE_SENSE(6) data: %s",
                     iscsi_get_error(iscsilun->iscsi));
        goto out;
    }
    iscsilun->write_protected = ms->device_specific_parameter & 0x80;
    iscsilun->dpofua          = ms->device_specific_parameter & 0x10;

out:
    if (task) {
        scsi_free_scsi_task(task);
    }
}

static void iscsi_parse_iscsi_option(const char *target, QDict *options)
{
    QemuOptsList *list;
    QemuOpts *opts;
    const char *user, *password, *password_secret, *initiator_name,
               *header_digest, *timeout;

    list = qemu_find_opts("iscsi");
    if (!list) {
        return;
    }

    opts = qemu_opts_find(list, target);
    if (opts == NULL) {
        opts = QTAILQ_FIRST(&list->head);
        if (!opts) {
            return;
        }
    }

    user = qemu_opt_get(opts, "user");
    if (user) {
        qdict_set_default_str(options, "user", user);
    }

    password = qemu_opt_get(opts, "password");
    if (password) {
        qdict_set_default_str(options, "password", password);
    }

    password_secret = qemu_opt_get(opts, "password-secret");
    if (password_secret) {
        qdict_set_default_str(options, "password-secret", password_secret);
    }

    initiator_name = qemu_opt_get(opts, "initiator-name");
    if (initiator_name) {
        qdict_set_default_str(options, "initiator-name", initiator_name);
    }

    header_digest = qemu_opt_get(opts, "header-digest");
    if (header_digest) {
        /* -iscsi takes upper case values, but QAPI only supports lower case
         * enum constant names, so we have to convert here. */
        char *qapi_value = g_ascii_strdown(header_digest, -1);
        qdict_set_default_str(options, "header-digest", qapi_value);
        g_free(qapi_value);
    }

    timeout = qemu_opt_get(opts, "timeout");
    if (timeout) {
        qdict_set_default_str(options, "timeout", timeout);
    }
}

/*
 * We support iscsi url's on the form
 * iscsi://[<username>%<password>@]<host>[:<port>]/<targetname>/<lun>
 */
static void iscsi_parse_filename(const char *filename, QDict *options,
                                 Error **errp)
{
    struct iscsi_url *iscsi_url;
    const char *transport_name;
    char *lun_str;

    iscsi_url = iscsi_parse_full_url(NULL, filename);
    if (iscsi_url == NULL) {
        error_setg(errp, "Failed to parse URL : %s", filename);
        return;
    }

#if LIBISCSI_API_VERSION >= (20160603)
    switch (iscsi_url->transport) {
    case TCP_TRANSPORT:
        transport_name = "tcp";
        break;
    case ISER_TRANSPORT:
        transport_name = "iser";
        break;
    default:
        error_setg(errp, "Unknown transport type (%d)",
                   iscsi_url->transport);
        return;
    }
#else
    transport_name = "tcp";
#endif

    qdict_set_default_str(options, "transport", transport_name);
    qdict_set_default_str(options, "portal", iscsi_url->portal);
    qdict_set_default_str(options, "target", iscsi_url->target);

    lun_str = g_strdup_printf("%d", iscsi_url->lun);
    qdict_set_default_str(options, "lun", lun_str);
    g_free(lun_str);

    /* User/password from -iscsi take precedence over those from the URL */
    iscsi_parse_iscsi_option(iscsi_url->target, options);

    if (iscsi_url->user[0] != '\0') {
        qdict_set_default_str(options, "user", iscsi_url->user);
        qdict_set_default_str(options, "password", iscsi_url->passwd);
    }

    iscsi_destroy_url(iscsi_url);
}

static QemuOptsList runtime_opts = {
    .name = "iscsi",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = "transport",
            .type = QEMU_OPT_STRING,
        },
        {
            .name = "portal",
            .type = QEMU_OPT_STRING,
        },
        {
            .name = "target",
            .type = QEMU_OPT_STRING,
        },
        {
            .name = "user",
            .type = QEMU_OPT_STRING,
        },
        {
            .name = "password",
            .type = QEMU_OPT_STRING,
        },
        {
            .name = "password-secret",
            .type = QEMU_OPT_STRING,
        },
        {
            .name = "lun",
            .type = QEMU_OPT_NUMBER,
        },
        {
            .name = "initiator-name",
            .type = QEMU_OPT_STRING,
        },
        {
            .name = "header-digest",
            .type = QEMU_OPT_STRING,
        },
        {
            .name = "timeout",
            .type = QEMU_OPT_NUMBER,
        },
        { /* end of list */ }
    },
};

static void iscsi_save_designator(IscsiLun *lun,
                                  struct scsi_inquiry_device_identification *inq_di)
{
    struct scsi_inquiry_device_designator *desig, *copy = NULL;

    for (desig = inq_di->designators; desig; desig = desig->next) {
        if (desig->association ||
            desig->designator_type > SCSI_DESIGNATOR_TYPE_NAA) {
            continue;
        }
        /* NAA works better than T10 vendor ID based designator. */
        if (!copy || copy->designator_type < desig->designator_type) {
            copy = desig;
        }
    }
    if (copy) {
        lun->dd = g_new(struct scsi_inquiry_device_designator, 1);
        *lun->dd = *copy;
        lun->dd->next = NULL;
        lun->dd->designator = g_malloc(copy->designator_length);
        memcpy(lun->dd->designator, copy->designator, copy->designator_length);
    }
}

static int iscsi_open(BlockDriverState *bs, QDict *options, int flags,
                      Error **errp)
{
    IscsiLun *iscsilun = bs->opaque;
    struct iscsi_context *iscsi = NULL;
    struct scsi_task *task = NULL;
    struct scsi_inquiry_standard *inq = NULL;
    struct scsi_inquiry_supported_pages *inq_vpd;
    char *initiator_name = NULL;
    QemuOpts *opts;
    Error *local_err = NULL;
    const char *transport_name, *portal, *target;
#if LIBISCSI_API_VERSION >= (20160603)
    enum iscsi_transport_type transport;
#endif
    int i, ret = 0, timeout = 0, lun;

    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    if (!qemu_opts_absorb_qdict(opts, options, errp)) {
        ret = -EINVAL;
        goto out;
    }

    transport_name = qemu_opt_get(opts, "transport");
    portal = qemu_opt_get(opts, "portal");
    target = qemu_opt_get(opts, "target");
    lun = qemu_opt_get_number(opts, "lun", 0);

    if (!transport_name || !portal || !target) {
        error_setg(errp, "Need all of transport, portal and target options");
        ret = -EINVAL;
        goto out;
    }

    if (!strcmp(transport_name, "tcp")) {
#if LIBISCSI_API_VERSION >= (20160603)
        transport = TCP_TRANSPORT;
    } else if (!strcmp(transport_name, "iser")) {
        transport = ISER_TRANSPORT;
#else
        /* TCP is what older libiscsi versions always use */
#endif
    } else {
        error_setg(errp, "Unknown transport: %s", transport_name);
        ret = -EINVAL;
        goto out;
    }

    memset(iscsilun, 0, sizeof(IscsiLun));

    initiator_name = get_initiator_name(opts);

    iscsi = iscsi_create_context(initiator_name);
    if (iscsi == NULL) {
        error_setg(errp, "iSCSI: Failed to create iSCSI context.");
        ret = -ENOMEM;
        goto out;
    }
#if LIBISCSI_API_VERSION >= (20160603)
    if (iscsi_init_transport(iscsi, transport)) {
        error_setg(errp, ("Error initializing transport."));
        ret = -EINVAL;
        goto out;
    }
#endif
    if (iscsi_set_targetname(iscsi, target)) {
        error_setg(errp, "iSCSI: Failed to set target name.");
        ret = -EINVAL;
        goto out;
    }

    /* check if we got CHAP username/password via the options */
    apply_chap(iscsi, opts, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto out;
    }

    if (iscsi_set_session_type(iscsi, ISCSI_SESSION_NORMAL) != 0) {
        error_setg(errp, "iSCSI: Failed to set session type to normal.");
        ret = -EINVAL;
        goto out;
    }

    /* check if we got HEADER_DIGEST via the options */
    apply_header_digest(iscsi, opts, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto out;
    }

    /* timeout handling is broken in libiscsi before 1.15.0 */
    timeout = qemu_opt_get_number(opts, "timeout", 0);
#if LIBISCSI_API_VERSION >= 20150621
    iscsi_set_timeout(iscsi, timeout);
#else
    if (timeout) {
        warn_report("iSCSI: ignoring timeout value for libiscsi <1.15.0");
    }
#endif

    if (iscsi_full_connect_sync(iscsi, portal, lun) != 0) {
        error_setg(errp, "iSCSI: Failed to connect to LUN : %s",
            iscsi_get_error(iscsi));
        ret = -EINVAL;
        goto out;
    }

    iscsilun->iscsi = iscsi;
    iscsilun->aio_context = bdrv_get_aio_context(bs);
    iscsilun->lun = lun;
    iscsilun->has_write_same = true;

    task = iscsi_do_inquiry(iscsilun->iscsi, iscsilun->lun, 0, 0,
                            (void **) &inq, errp);
    if (task == NULL) {
        ret = -EINVAL;
        goto out;
    }
    iscsilun->type = inq->periperal_device_type;
    scsi_free_scsi_task(task);
    task = NULL;

    iscsi_modesense_sync(iscsilun);
    if (iscsilun->dpofua) {
        bs->supported_write_flags = BDRV_REQ_FUA;
    }

    /* Check the write protect flag of the LUN if we want to write */
    if (iscsilun->type == TYPE_DISK && (flags & BDRV_O_RDWR) &&
        iscsilun->write_protected) {
        ret = bdrv_apply_auto_read_only(bs, "LUN is write protected", errp);
        if (ret < 0) {
            goto out;
        }
        flags &= ~BDRV_O_RDWR;
    }

    iscsi_readcapacity_sync(iscsilun, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto out;
    }
    bs->total_sectors = sector_lun2qemu(iscsilun->num_blocks, iscsilun);

    /* We don't have any emulation for devices other than disks and CD-ROMs, so
     * this must be sg ioctl compatible. We force it to be sg, otherwise qemu
     * will try to read from the device to guess the image format.
     */
    if (iscsilun->type != TYPE_DISK && iscsilun->type != TYPE_ROM) {
        bs->sg = true;
    }

    task = iscsi_do_inquiry(iscsilun->iscsi, iscsilun->lun, 1,
                            SCSI_INQUIRY_PAGECODE_SUPPORTED_VPD_PAGES,
                            (void **) &inq_vpd, errp);
    if (task == NULL) {
        ret = -EINVAL;
        goto out;
    }
    for (i = 0; i < inq_vpd->num_pages; i++) {
        struct scsi_task *inq_task;
        struct scsi_inquiry_logical_block_provisioning *inq_lbp;
        struct scsi_inquiry_block_limits *inq_bl;
        struct scsi_inquiry_device_identification *inq_di;
        switch (inq_vpd->pages[i]) {
        case SCSI_INQUIRY_PAGECODE_LOGICAL_BLOCK_PROVISIONING:
            inq_task = iscsi_do_inquiry(iscsilun->iscsi, iscsilun->lun, 1,
                                        SCSI_INQUIRY_PAGECODE_LOGICAL_BLOCK_PROVISIONING,
                                        (void **) &inq_lbp, errp);
            if (inq_task == NULL) {
                ret = -EINVAL;
                goto out;
            }
            memcpy(&iscsilun->lbp, inq_lbp,
                   sizeof(struct scsi_inquiry_logical_block_provisioning));
            scsi_free_scsi_task(inq_task);
            break;
        case SCSI_INQUIRY_PAGECODE_BLOCK_LIMITS:
            inq_task = iscsi_do_inquiry(iscsilun->iscsi, iscsilun->lun, 1,
                                    SCSI_INQUIRY_PAGECODE_BLOCK_LIMITS,
                                    (void **) &inq_bl, errp);
            if (inq_task == NULL) {
                ret = -EINVAL;
                goto out;
            }
            memcpy(&iscsilun->bl, inq_bl,
                   sizeof(struct scsi_inquiry_block_limits));
            scsi_free_scsi_task(inq_task);
            break;
        case SCSI_INQUIRY_PAGECODE_DEVICE_IDENTIFICATION:
            inq_task = iscsi_do_inquiry(iscsilun->iscsi, iscsilun->lun, 1,
                                    SCSI_INQUIRY_PAGECODE_DEVICE_IDENTIFICATION,
                                    (void **) &inq_di, errp);
            if (inq_task == NULL) {
                ret = -EINVAL;
                goto out;
            }
            iscsi_save_designator(iscsilun, inq_di);
            scsi_free_scsi_task(inq_task);
            break;
        default:
            break;
        }
    }
    scsi_free_scsi_task(task);
    task = NULL;

    qemu_mutex_init(&iscsilun->mutex);
    iscsi_attach_aio_context(bs, iscsilun->aio_context);

    /* Guess the internal cluster (page) size of the iscsi target by the means
     * of opt_unmap_gran. Transfer the unmap granularity only if it has a
     * reasonable size */
    if (iscsilun->bl.opt_unmap_gran * iscsilun->block_size >= 4 * 1024 &&
        iscsilun->bl.opt_unmap_gran * iscsilun->block_size <= 16 * 1024 * 1024) {
        iscsilun->cluster_size = iscsilun->bl.opt_unmap_gran *
            iscsilun->block_size;
        if (iscsilun->lbprz) {
            ret = iscsi_allocmap_init(iscsilun, flags);
        }
    }

    if (iscsilun->lbprz && iscsilun->lbp.lbpws) {
        bs->supported_zero_flags = BDRV_REQ_MAY_UNMAP;
    }

out:
    qemu_opts_del(opts);
    g_free(initiator_name);
    if (task != NULL) {
        scsi_free_scsi_task(task);
    }

    if (ret) {
        if (iscsi != NULL) {
            if (iscsi_is_logged_in(iscsi)) {
                iscsi_logout_sync(iscsi);
            }
            iscsi_destroy_context(iscsi);
        }
        memset(iscsilun, 0, sizeof(IscsiLun));
    }

    return ret;
}

static void iscsi_close(BlockDriverState *bs)
{
    IscsiLun *iscsilun = bs->opaque;
    struct iscsi_context *iscsi = iscsilun->iscsi;

    iscsi_detach_aio_context(bs);
    if (iscsi_is_logged_in(iscsi)) {
        iscsi_logout_sync(iscsi);
    }
    iscsi_destroy_context(iscsi);
    if (iscsilun->dd) {
        g_free(iscsilun->dd->designator);
        g_free(iscsilun->dd);
    }
    g_free(iscsilun->zeroblock);
    iscsi_allocmap_free(iscsilun);
    qemu_mutex_destroy(&iscsilun->mutex);
    memset(iscsilun, 0, sizeof(IscsiLun));
}

static void iscsi_refresh_limits(BlockDriverState *bs, Error **errp)
{
    /* We don't actually refresh here, but just return data queried in
     * iscsi_open(): iscsi targets don't change their limits. */

    IscsiLun *iscsilun = bs->opaque;
    uint64_t max_xfer_len = iscsilun->use_16_for_rw ? 0xffffffff : 0xffff;
    unsigned int block_size = MAX(BDRV_SECTOR_SIZE, iscsilun->block_size);

    assert(iscsilun->block_size >= BDRV_SECTOR_SIZE || bdrv_is_sg(bs));

    bs->bl.request_alignment = block_size;

    if (iscsilun->bl.max_xfer_len) {
        max_xfer_len = MIN(max_xfer_len, iscsilun->bl.max_xfer_len);
    }

    if (max_xfer_len * block_size < INT_MAX) {
        bs->bl.max_transfer = max_xfer_len * iscsilun->block_size;
    }

    if (iscsilun->lbp.lbpu) {
        bs->bl.max_pdiscard =
            MIN_NON_ZERO(iscsilun->bl.max_unmap * iscsilun->block_size,
                         (uint64_t)UINT32_MAX * iscsilun->block_size);
        bs->bl.pdiscard_alignment =
            iscsilun->bl.opt_unmap_gran * iscsilun->block_size;
    } else {
        bs->bl.pdiscard_alignment = iscsilun->block_size;
    }

    bs->bl.max_pwrite_zeroes =
        MIN_NON_ZERO(iscsilun->bl.max_ws_len * iscsilun->block_size,
                     max_xfer_len * iscsilun->block_size);

    if (iscsilun->lbp.lbpws) {
        bs->bl.pwrite_zeroes_alignment =
            iscsilun->bl.opt_unmap_gran * iscsilun->block_size;
    } else {
        bs->bl.pwrite_zeroes_alignment = iscsilun->block_size;
    }
    if (iscsilun->bl.opt_xfer_len &&
        iscsilun->bl.opt_xfer_len < INT_MAX / block_size) {
        bs->bl.opt_transfer = pow2floor(iscsilun->bl.opt_xfer_len *
                                        iscsilun->block_size);
    }
}

/* Note that this will not re-establish a connection with an iSCSI target - it
 * is effectively a NOP.  */
static int iscsi_reopen_prepare(BDRVReopenState *state,
                                BlockReopenQueue *queue, Error **errp)
{
    IscsiLun *iscsilun = state->bs->opaque;

    if (state->flags & BDRV_O_RDWR && iscsilun->write_protected) {
        error_setg(errp, "Cannot open a write protected LUN as read-write");
        return -EACCES;
    }
    return 0;
}

static void iscsi_reopen_commit(BDRVReopenState *reopen_state)
{
    IscsiLun *iscsilun = reopen_state->bs->opaque;

    /* the cache.direct status might have changed */
    if (iscsilun->allocmap != NULL) {
        iscsi_allocmap_init(iscsilun, reopen_state->flags);
    }
}

static int coroutine_fn iscsi_co_truncate(BlockDriverState *bs, int64_t offset,
                                          bool exact, PreallocMode prealloc,
                                          BdrvRequestFlags flags, Error **errp)
{
    IscsiLun *iscsilun = bs->opaque;
    int64_t cur_length;
    Error *local_err = NULL;

    if (prealloc != PREALLOC_MODE_OFF) {
        error_setg(errp, "Unsupported preallocation mode '%s'",
                   PreallocMode_str(prealloc));
        return -ENOTSUP;
    }

    if (iscsilun->type != TYPE_DISK) {
        error_setg(errp, "Cannot resize non-disk iSCSI devices");
        return -ENOTSUP;
    }

    iscsi_readcapacity_sync(iscsilun, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return -EIO;
    }

    cur_length = iscsi_getlength(bs);
    if (offset != cur_length && exact) {
        error_setg(errp, "Cannot resize iSCSI devices");
        return -ENOTSUP;
    } else if (offset > cur_length) {
        error_setg(errp, "Cannot grow iSCSI devices");
        return -EINVAL;
    }

    if (iscsilun->allocmap != NULL) {
        iscsi_allocmap_init(iscsilun, bs->open_flags);
    }

    return 0;
}

static int iscsi_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    IscsiLun *iscsilun = bs->opaque;
    bdi->cluster_size = iscsilun->cluster_size;
    return 0;
}

static void coroutine_fn iscsi_co_invalidate_cache(BlockDriverState *bs,
                                                   Error **errp)
{
    IscsiLun *iscsilun = bs->opaque;
    iscsi_allocmap_invalidate(iscsilun);
}

static int coroutine_fn iscsi_co_copy_range_from(BlockDriverState *bs,
                                                 BdrvChild *src,
                                                 int64_t src_offset,
                                                 BdrvChild *dst,
                                                 int64_t dst_offset,
                                                 int64_t bytes,
                                                 BdrvRequestFlags read_flags,
                                                 BdrvRequestFlags write_flags)
{
    return bdrv_co_copy_range_to(src, src_offset, dst, dst_offset, bytes,
                                 read_flags, write_flags);
}

static struct scsi_task *iscsi_xcopy_task(int param_len)
{
    struct scsi_task *task;

    task = g_new0(struct scsi_task, 1);

    task->cdb[0]     = EXTENDED_COPY;
    task->cdb[10]    = (param_len >> 24) & 0xFF;
    task->cdb[11]    = (param_len >> 16) & 0xFF;
    task->cdb[12]    = (param_len >> 8) & 0xFF;
    task->cdb[13]    = param_len & 0xFF;
    task->cdb_size   = 16;
    task->xfer_dir   = SCSI_XFER_WRITE;
    task->expxferlen = param_len;

    return task;
}

static void iscsi_populate_target_desc(unsigned char *desc, IscsiLun *lun)
{
    struct scsi_inquiry_device_designator *dd = lun->dd;

    memset(desc, 0, 32);
    desc[0] = 0xE4; /* IDENT_DESCR_TGT_DESCR */
    desc[4] = dd->code_set;
    desc[5] = (dd->designator_type & 0xF)
        | ((dd->association & 3) << 4);
    desc[7] = dd->designator_length;
    memcpy(desc + 8, dd->designator, MIN(dd->designator_length, 20));

    desc[28] = 0;
    desc[29] = (lun->block_size >> 16) & 0xFF;
    desc[30] = (lun->block_size >> 8) & 0xFF;
    desc[31] = lun->block_size & 0xFF;
}

static void iscsi_xcopy_desc_hdr(uint8_t *hdr, int dc, int cat, int src_index,
                                 int dst_index)
{
    hdr[0] = 0x02; /* BLK_TO_BLK_SEG_DESCR */
    hdr[1] = ((dc << 1) | cat) & 0xFF;
    hdr[2] = (XCOPY_BLK2BLK_SEG_DESC_SIZE >> 8) & 0xFF;
    /* don't account for the first 4 bytes in descriptor header*/
    hdr[3] = (XCOPY_BLK2BLK_SEG_DESC_SIZE - 4 /* SEG_DESC_SRC_INDEX_OFFSET */) & 0xFF;
    hdr[4] = (src_index >> 8) & 0xFF;
    hdr[5] = src_index & 0xFF;
    hdr[6] = (dst_index >> 8) & 0xFF;
    hdr[7] = dst_index & 0xFF;
}

static void iscsi_xcopy_populate_desc(uint8_t *desc, int dc, int cat,
                                      int src_index, int dst_index, int num_blks,
                                      uint64_t src_lba, uint64_t dst_lba)
{
    iscsi_xcopy_desc_hdr(desc, dc, cat, src_index, dst_index);

    /* The caller should verify the request size */
    assert(num_blks < 65536);
    desc[10] = (num_blks >> 8) & 0xFF;
    desc[11] = num_blks & 0xFF;
    desc[12] = (src_lba >> 56) & 0xFF;
    desc[13] = (src_lba >> 48) & 0xFF;
    desc[14] = (src_lba >> 40) & 0xFF;
    desc[15] = (src_lba >> 32) & 0xFF;
    desc[16] = (src_lba >> 24) & 0xFF;
    desc[17] = (src_lba >> 16) & 0xFF;
    desc[18] = (src_lba >> 8) & 0xFF;
    desc[19] = src_lba & 0xFF;
    desc[20] = (dst_lba >> 56) & 0xFF;
    desc[21] = (dst_lba >> 48) & 0xFF;
    desc[22] = (dst_lba >> 40) & 0xFF;
    desc[23] = (dst_lba >> 32) & 0xFF;
    desc[24] = (dst_lba >> 24) & 0xFF;
    desc[25] = (dst_lba >> 16) & 0xFF;
    desc[26] = (dst_lba >> 8) & 0xFF;
    desc[27] = dst_lba & 0xFF;
}

static void iscsi_xcopy_populate_header(unsigned char *buf, int list_id, int str,
                                        int list_id_usage, int prio,
                                        int tgt_desc_len,
                                        int seg_desc_len, int inline_data_len)
{
    buf[0] = list_id;
    buf[1] = ((str & 1) << 5) | ((list_id_usage & 3) << 3) | (prio & 7);
    buf[2] = (tgt_desc_len >> 8) & 0xFF;
    buf[3] = tgt_desc_len & 0xFF;
    buf[8] = (seg_desc_len >> 24) & 0xFF;
    buf[9] = (seg_desc_len >> 16) & 0xFF;
    buf[10] = (seg_desc_len >> 8) & 0xFF;
    buf[11] = seg_desc_len & 0xFF;
    buf[12] = (inline_data_len >> 24) & 0xFF;
    buf[13] = (inline_data_len >> 16) & 0xFF;
    buf[14] = (inline_data_len >> 8) & 0xFF;
    buf[15] = inline_data_len & 0xFF;
}

static void iscsi_xcopy_data(struct iscsi_data *data,
                             IscsiLun *src, int64_t src_lba,
                             IscsiLun *dst, int64_t dst_lba,
                             uint16_t num_blocks)
{
    uint8_t *buf;
    const int src_offset = XCOPY_DESC_OFFSET;
    const int dst_offset = XCOPY_DESC_OFFSET + IDENT_DESCR_TGT_DESCR_SIZE;
    const int seg_offset = dst_offset + IDENT_DESCR_TGT_DESCR_SIZE;

    data->size = XCOPY_DESC_OFFSET +
                 IDENT_DESCR_TGT_DESCR_SIZE * 2 +
                 XCOPY_BLK2BLK_SEG_DESC_SIZE;
    data->data = g_malloc0(data->size);
    buf = data->data;

    /* Initialise the parameter list header */
    iscsi_xcopy_populate_header(buf, 1, 0, 2 /* LIST_ID_USAGE_DISCARD */,
                                0, 2 * IDENT_DESCR_TGT_DESCR_SIZE,
                                XCOPY_BLK2BLK_SEG_DESC_SIZE,
                                0);

    /* Initialise CSCD list with one src + one dst descriptor */
    iscsi_populate_target_desc(&buf[src_offset], src);
    iscsi_populate_target_desc(&buf[dst_offset], dst);

    /* Initialise one segment descriptor */
    iscsi_xcopy_populate_desc(&buf[seg_offset], 0, 0, 0, 1, num_blocks,
                              src_lba, dst_lba);
}

static int coroutine_fn iscsi_co_copy_range_to(BlockDriverState *bs,
                                               BdrvChild *src,
                                               int64_t src_offset,
                                               BdrvChild *dst,
                                               int64_t dst_offset,
                                               int64_t bytes,
                                               BdrvRequestFlags read_flags,
                                               BdrvRequestFlags write_flags)
{
    IscsiLun *dst_lun = dst->bs->opaque;
    IscsiLun *src_lun;
    struct IscsiTask iscsi_task;
    struct iscsi_data data;
    int r = 0;
    int block_size;

    if (src->bs->drv->bdrv_co_copy_range_to != iscsi_co_copy_range_to) {
        return -ENOTSUP;
    }
    src_lun = src->bs->opaque;

    if (!src_lun->dd || !dst_lun->dd) {
        return -ENOTSUP;
    }
    if (!is_byte_request_lun_aligned(dst_offset, bytes, dst_lun)) {
        return -ENOTSUP;
    }
    if (!is_byte_request_lun_aligned(src_offset, bytes, src_lun)) {
        return -ENOTSUP;
    }
    if (dst_lun->block_size != src_lun->block_size ||
        !dst_lun->block_size) {
        return -ENOTSUP;
    }

    block_size = dst_lun->block_size;
    if (bytes / block_size > 65535) {
        return -ENOTSUP;
    }

    iscsi_xcopy_data(&data,
                     src_lun, src_offset / block_size,
                     dst_lun, dst_offset / block_size,
                     bytes / block_size);

    iscsi_co_init_iscsitask(dst_lun, &iscsi_task);

    qemu_mutex_lock(&dst_lun->mutex);
    iscsi_task.task = iscsi_xcopy_task(data.size);
retry:
    if (iscsi_scsi_command_async(dst_lun->iscsi, dst_lun->lun,
                                 iscsi_task.task, iscsi_co_generic_cb,
                                 &data,
                                 &iscsi_task) != 0) {
        r = -EIO;
        goto out_unlock;
    }

    iscsi_co_wait_for_task(&iscsi_task, dst_lun);

    if (iscsi_task.do_retry) {
        iscsi_task.complete = 0;
        goto retry;
    }

    if (iscsi_task.status != SCSI_STATUS_GOOD) {
        r = iscsi_task.err_code;
        goto out_unlock;
    }

out_unlock:

    trace_iscsi_xcopy(src_lun, src_offset, dst_lun, dst_offset, bytes, r);
    g_free(iscsi_task.task);
    qemu_mutex_unlock(&dst_lun->mutex);
    g_free(iscsi_task.err_str);
    return r;
}


static const char *const iscsi_strong_runtime_opts[] = {
    "transport",
    "portal",
    "target",
    "user",
    "password",
    "password-secret",
    "lun",
    "initiator-name",
    "header-digest",

    NULL
};

static BlockDriver bdrv_iscsi = {
    .format_name     = "iscsi",
    .protocol_name   = "iscsi",

    .instance_size          = sizeof(IscsiLun),
    .bdrv_parse_filename    = iscsi_parse_filename,
    .bdrv_file_open         = iscsi_open,
    .bdrv_close             = iscsi_close,
    .bdrv_co_create_opts    = bdrv_co_create_opts_simple,
    .create_opts            = &bdrv_create_opts_simple,
    .bdrv_reopen_prepare    = iscsi_reopen_prepare,
    .bdrv_reopen_commit     = iscsi_reopen_commit,
    .bdrv_co_invalidate_cache = iscsi_co_invalidate_cache,

    .bdrv_getlength  = iscsi_getlength,
    .bdrv_get_info   = iscsi_get_info,
    .bdrv_co_truncate    = iscsi_co_truncate,
    .bdrv_refresh_limits = iscsi_refresh_limits,

    .bdrv_co_block_status  = iscsi_co_block_status,
    .bdrv_co_pdiscard      = iscsi_co_pdiscard,
    .bdrv_co_copy_range_from = iscsi_co_copy_range_from,
    .bdrv_co_copy_range_to  = iscsi_co_copy_range_to,
    .bdrv_co_pwrite_zeroes = iscsi_co_pwrite_zeroes,
    .bdrv_co_readv         = iscsi_co_readv,
    .bdrv_co_writev        = iscsi_co_writev,
    .bdrv_co_flush_to_disk = iscsi_co_flush,

#ifdef __linux__
    .bdrv_aio_ioctl   = iscsi_aio_ioctl,
#endif

    .bdrv_detach_aio_context = iscsi_detach_aio_context,
    .bdrv_attach_aio_context = iscsi_attach_aio_context,

    .strong_runtime_opts = iscsi_strong_runtime_opts,
};

#if LIBISCSI_API_VERSION >= (20160603)
static BlockDriver bdrv_iser = {
    .format_name     = "iser",
    .protocol_name   = "iser",

    .instance_size          = sizeof(IscsiLun),
    .bdrv_parse_filename    = iscsi_parse_filename,
    .bdrv_file_open         = iscsi_open,
    .bdrv_close             = iscsi_close,
    .bdrv_co_create_opts    = bdrv_co_create_opts_simple,
    .create_opts            = &bdrv_create_opts_simple,
    .bdrv_reopen_prepare    = iscsi_reopen_prepare,
    .bdrv_reopen_commit     = iscsi_reopen_commit,
    .bdrv_co_invalidate_cache  = iscsi_co_invalidate_cache,

    .bdrv_getlength  = iscsi_getlength,
    .bdrv_get_info   = iscsi_get_info,
    .bdrv_co_truncate    = iscsi_co_truncate,
    .bdrv_refresh_limits = iscsi_refresh_limits,

    .bdrv_co_block_status  = iscsi_co_block_status,
    .bdrv_co_pdiscard      = iscsi_co_pdiscard,
    .bdrv_co_copy_range_from = iscsi_co_copy_range_from,
    .bdrv_co_copy_range_to  = iscsi_co_copy_range_to,
    .bdrv_co_pwrite_zeroes = iscsi_co_pwrite_zeroes,
    .bdrv_co_readv         = iscsi_co_readv,
    .bdrv_co_writev        = iscsi_co_writev,
    .bdrv_co_flush_to_disk = iscsi_co_flush,

#ifdef __linux__
    .bdrv_aio_ioctl   = iscsi_aio_ioctl,
#endif

    .bdrv_detach_aio_context = iscsi_detach_aio_context,
    .bdrv_attach_aio_context = iscsi_attach_aio_context,

    .strong_runtime_opts = iscsi_strong_runtime_opts,
};
#endif

static void iscsi_block_init(void)
{
    bdrv_register(&bdrv_iscsi);
#if LIBISCSI_API_VERSION >= (20160603)
    bdrv_register(&bdrv_iser);
#endif
}

block_init(iscsi_block_init);
