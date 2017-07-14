/*
 * QEMU Block driver for iSCSI images
 *
 * Copyright (c) 2010-2011 Ronnie Sahlberg <ronniesahlberg@gmail.com>
 * Copyright (c) 2012-2016 Peter Lieven <pl@kamp.de>
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
#include "qemu-common.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qemu/bitops.h"
#include "qemu/bitmap.h"
#include "block/block_int.h"
#include "block/scsi.h"
#include "qemu/iov.h"
#include "qemu/uuid.h"
#include "qmp-commands.h"
#include "qapi/qmp/qstring.h"
#include "crypto/secret.h"

#include <iscsi/iscsi.h>
#include <iscsi/scsi-lowlevel.h>

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
    int cluster_sectors;
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
} IscsiTask;

typedef struct IscsiAIOCB {
    BlockAIOCB common;
    QEMUBH *bh;
    IscsiLun *iscsilun;
    struct scsi_task *task;
    uint8_t *buf;
    int status;
    int64_t sector_num;
    int nb_sectors;
    int ret;
#ifdef __linux__
    sg_io_hdr_t *ioh;
#endif
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

static void
iscsi_bh_cb(void *p)
{
    IscsiAIOCB *acb = p;

    qemu_bh_delete(acb->bh);

    g_free(acb->buf);
    acb->buf = NULL;

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
    int ret;

    switch (sense->key) {
    case SCSI_SENSE_NOT_READY:
        return -EBUSY;
    case SCSI_SENSE_DATA_PROTECTION:
        return -EACCES;
    case SCSI_SENSE_COMMAND_ABORTED:
        return -ECANCELED;
    case SCSI_SENSE_ILLEGAL_REQUEST:
        /* Parse ASCQ */
        break;
    default:
        return -EIO;
    }
    switch (sense->ascq) {
    case SCSI_SENSE_ASCQ_PARAMETER_LIST_LENGTH_ERROR:
    case SCSI_SENSE_ASCQ_INVALID_OPERATION_CODE:
    case SCSI_SENSE_ASCQ_INVALID_FIELD_IN_CDB:
    case SCSI_SENSE_ASCQ_INVALID_FIELD_IN_PARAMETER_LIST:
        ret = -EINVAL;
        break;
    case SCSI_SENSE_ASCQ_LBA_OUT_OF_RANGE:
        ret = -ENOSPC;
        break;
    case SCSI_SENSE_ASCQ_LOGICAL_UNIT_NOT_SUPPORTED:
        ret = -ENOTSUP;
        break;
    case SCSI_SENSE_ASCQ_MEDIUM_NOT_PRESENT:
    case SCSI_SENSE_ASCQ_MEDIUM_NOT_PRESENT_TRAY_CLOSED:
    case SCSI_SENSE_ASCQ_MEDIUM_NOT_PRESENT_TRAY_OPEN:
        ret = -ENOMEDIUM;
        break;
    case SCSI_SENSE_ASCQ_WRITE_PROTECTED:
        ret = -EACCES;
        break;
    default:
        ret = -EIO;
        break;
    }
    return ret;
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
    iTask->task = task;

    if (status != SCSI_STATUS_GOOD) {
        if (iTask->retries++ < ISCSI_CMD_RETRIES) {
            if (status == SCSI_STATUS_CHECK_CONDITION
                && task->sense.key == SCSI_SENSE_UNIT_ATTENTION) {
                error_report("iSCSI CheckCondition: %s",
                             iscsi_get_error(iscsi));
                iTask->do_retry = 1;
                goto out;
            }
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
                return;
            }
        }
        iTask->err_code = iscsi_translate_sense(&task->sense);
        error_report("iSCSI Failure: %s", iscsi_get_error(iscsi));
    }

out:
    if (iTask->co) {
        aio_bh_schedule_oneshot(iTask->iscsilun->aio_context,
                                 iscsi_co_generic_bh_cb, iTask);
    } else {
        iTask->complete = 1;
    }
}

static void iscsi_co_init_iscsitask(IscsiLun *iscsilun, struct IscsiTask *iTask)
{
    *iTask = (struct IscsiTask) {
        .co         = qemu_coroutine_self(),
        .iscsilun   = iscsilun,
    };
}

static void
iscsi_abort_task_cb(struct iscsi_context *iscsi, int status, void *command_data,
                    void *private_data)
{
    IscsiAIOCB *acb = private_data;

    acb->status = -ECANCELED;
    iscsi_schedule_bh(acb);
}

static void
iscsi_aio_cancel(BlockAIOCB *blockacb)
{
    IscsiAIOCB *acb = (IscsiAIOCB *)blockacb;
    IscsiLun *iscsilun = acb->iscsilun;

    if (acb->status != -EINPROGRESS) {
        return;
    }

    /* send a task mgmt call to the target to cancel the task on the target */
    iscsi_task_mgmt_abort_task_async(iscsilun->iscsi, acb->task,
                                     iscsi_abort_task_cb, acb);

}

static const AIOCBInfo iscsi_aiocb_info = {
    .aiocb_size         = sizeof(IscsiAIOCB),
    .cancel_async       = iscsi_aio_cancel,
};


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
                           NULL,
                           iscsilun);
        iscsilun->events = ev;
    }
}

static void iscsi_timed_check_events(void *opaque)
{
    IscsiLun *iscsilun = opaque;

    /* check for timed out requests */
    iscsi_service(iscsilun->iscsi, 0);

    if (iscsilun->request_timed_out) {
        iscsilun->request_timed_out = false;
        iscsi_reconnect(iscsilun->iscsi);
    }

    /* newer versions of libiscsi may return zero events. Ensure we are able
     * to return to service once this situation changes. */
    iscsi_set_events(iscsilun);

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

static bool is_byte_request_lun_aligned(int64_t offset, int count,
                                        IscsiLun *iscsilun)
{
    if (offset % iscsilun->block_size || count % iscsilun->block_size) {
        error_report("iSCSI misaligned request: "
                     "iscsilun->block_size %u, offset %" PRIi64
                     ", count %d",
                     iscsilun->block_size, offset, count);
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

    iscsilun->allocmap_size =
        DIV_ROUND_UP(sector_lun2qemu(iscsilun->num_blocks, iscsilun),
                     iscsilun->cluster_sectors);

    iscsilun->allocmap = bitmap_try_new(iscsilun->allocmap_size);
    if (!iscsilun->allocmap) {
        return -ENOMEM;
    }

    if (open_flags & BDRV_O_NOCACHE) {
        /* in case that cache.direct = on all allocmap entries are
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
iscsi_allocmap_update(IscsiLun *iscsilun, int64_t sector_num,
                      int nb_sectors, bool allocated, bool valid)
{
    int64_t cl_num_expanded, nb_cls_expanded, cl_num_shrunk, nb_cls_shrunk;

    if (iscsilun->allocmap == NULL) {
        return;
    }
    /* expand to entirely contain all affected clusters */
    cl_num_expanded = sector_num / iscsilun->cluster_sectors;
    nb_cls_expanded = DIV_ROUND_UP(sector_num + nb_sectors,
                                   iscsilun->cluster_sectors) - cl_num_expanded;
    /* shrink to touch only completely contained clusters */
    cl_num_shrunk = DIV_ROUND_UP(sector_num, iscsilun->cluster_sectors);
    nb_cls_shrunk = (sector_num + nb_sectors) / iscsilun->cluster_sectors
                      - cl_num_shrunk;
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
iscsi_allocmap_set_allocated(IscsiLun *iscsilun, int64_t sector_num,
                             int nb_sectors)
{
    iscsi_allocmap_update(iscsilun, sector_num, nb_sectors, true, true);
}

static void
iscsi_allocmap_set_unallocated(IscsiLun *iscsilun, int64_t sector_num,
                               int nb_sectors)
{
    /* Note: if cache.direct=on the fifth argument to iscsi_allocmap_update
     * is ignored, so this will in effect be an iscsi_allocmap_set_invalid.
     */
    iscsi_allocmap_update(iscsilun, sector_num, nb_sectors, false, true);
}

static void iscsi_allocmap_set_invalid(IscsiLun *iscsilun, int64_t sector_num,
                                       int nb_sectors)
{
    iscsi_allocmap_update(iscsilun, sector_num, nb_sectors, false, false);
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
iscsi_allocmap_is_allocated(IscsiLun *iscsilun, int64_t sector_num,
                            int nb_sectors)
{
    unsigned long size;
    if (iscsilun->allocmap == NULL) {
        return true;
    }
    size = DIV_ROUND_UP(sector_num + nb_sectors, iscsilun->cluster_sectors);
    return !(find_next_bit(iscsilun->allocmap, size,
                           sector_num / iscsilun->cluster_sectors) == size);
}

static inline bool iscsi_allocmap_is_valid(IscsiLun *iscsilun,
                                           int64_t sector_num, int nb_sectors)
{
    unsigned long size;
    if (iscsilun->allocmap_valid == NULL) {
        return false;
    }
    size = DIV_ROUND_UP(sector_num + nb_sectors, iscsilun->cluster_sectors);
    return (find_next_zero_bit(iscsilun->allocmap_valid, size,
                               sector_num / iscsilun->cluster_sectors) == size);
}

static int coroutine_fn
iscsi_co_writev_flags(BlockDriverState *bs, int64_t sector_num, int nb_sectors,
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
    while (!iTask.complete) {
        iscsi_set_events(iscsilun);
        qemu_mutex_unlock(&iscsilun->mutex);
        qemu_coroutine_yield();
        qemu_mutex_lock(&iscsilun->mutex);
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
        iscsi_allocmap_set_invalid(iscsilun, sector_num, nb_sectors);
        r = iTask.err_code;
        goto out_unlock;
    }

    iscsi_allocmap_set_allocated(iscsilun, sector_num, nb_sectors);

out_unlock:
    qemu_mutex_unlock(&iscsilun->mutex);
    return r;
}



static int64_t coroutine_fn iscsi_co_get_block_status(BlockDriverState *bs,
                                                  int64_t sector_num,
                                                  int nb_sectors, int *pnum,
                                                  BlockDriverState **file)
{
    IscsiLun *iscsilun = bs->opaque;
    struct scsi_get_lba_status *lbas = NULL;
    struct scsi_lba_status_descriptor *lbasd = NULL;
    struct IscsiTask iTask;
    int64_t ret;

    iscsi_co_init_iscsitask(iscsilun, &iTask);

    if (!is_sector_request_lun_aligned(sector_num, nb_sectors, iscsilun)) {
        ret = -EINVAL;
        goto out;
    }

    /* default to all sectors allocated */
    ret = BDRV_BLOCK_DATA;
    ret |= (sector_num << BDRV_SECTOR_BITS) | BDRV_BLOCK_OFFSET_VALID;
    *pnum = nb_sectors;

    /* LUN does not support logical block provisioning */
    if (!iscsilun->lbpme) {
        goto out;
    }

    qemu_mutex_lock(&iscsilun->mutex);
retry:
    if (iscsi_get_lba_status_task(iscsilun->iscsi, iscsilun->lun,
                                  sector_qemu2lun(sector_num, iscsilun),
                                  8 + 16, iscsi_co_generic_cb,
                                  &iTask) == NULL) {
        ret = -ENOMEM;
        goto out_unlock;
    }

    while (!iTask.complete) {
        iscsi_set_events(iscsilun);
        qemu_mutex_unlock(&iscsilun->mutex);
        qemu_coroutine_yield();
        qemu_mutex_lock(&iscsilun->mutex);
    }

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
        goto out_unlock;
    }

    lbas = scsi_datain_unmarshall(iTask.task);
    if (lbas == NULL) {
        ret = -EIO;
        goto out_unlock;
    }

    lbasd = &lbas->descriptors[0];

    if (sector_qemu2lun(sector_num, iscsilun) != lbasd->lba) {
        ret = -EIO;
        goto out_unlock;
    }

    *pnum = sector_lun2qemu(lbasd->num_blocks, iscsilun);

    if (lbasd->provisioning == SCSI_PROVISIONING_TYPE_DEALLOCATED ||
        lbasd->provisioning == SCSI_PROVISIONING_TYPE_ANCHORED) {
        ret &= ~BDRV_BLOCK_DATA;
        if (iscsilun->lbprz) {
            ret |= BDRV_BLOCK_ZERO;
        }
    }

    if (ret & BDRV_BLOCK_ZERO) {
        iscsi_allocmap_set_unallocated(iscsilun, sector_num, *pnum);
    } else {
        iscsi_allocmap_set_allocated(iscsilun, sector_num, *pnum);
    }

    if (*pnum > nb_sectors) {
        *pnum = nb_sectors;
    }
out_unlock:
    qemu_mutex_unlock(&iscsilun->mutex);
out:
    if (iTask.task != NULL) {
        scsi_free_scsi_task(iTask.task);
    }
    if (ret > 0 && ret & BDRV_BLOCK_OFFSET_VALID) {
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

    if (!is_sector_request_lun_aligned(sector_num, nb_sectors, iscsilun)) {
        return -EINVAL;
    }

    if (bs->bl.max_transfer) {
        assert(nb_sectors << BDRV_SECTOR_BITS <= bs->bl.max_transfer);
    }

    /* if cache.direct is off and we have a valid entry in our allocation map
     * we can skip checking the block status and directly return zeroes if
     * the request falls within an unallocated area */
    if (iscsi_allocmap_is_valid(iscsilun, sector_num, nb_sectors) &&
        !iscsi_allocmap_is_allocated(iscsilun, sector_num, nb_sectors)) {
            qemu_iovec_memset(iov, 0, 0x00, iov->size);
            return 0;
    }

    if (nb_sectors >= ISCSI_CHECKALLOC_THRES &&
        !iscsi_allocmap_is_valid(iscsilun, sector_num, nb_sectors) &&
        !iscsi_allocmap_is_allocated(iscsilun, sector_num, nb_sectors)) {
        int pnum;
        BlockDriverState *file;
        /* check the block status from the beginning of the cluster
         * containing the start sector */
        int64_t ret = iscsi_co_get_block_status(bs,
                          sector_num - sector_num % iscsilun->cluster_sectors,
                          BDRV_REQUEST_MAX_SECTORS, &pnum, &file);
        if (ret < 0) {
            return ret;
        }
        /* if the whole request falls into an unallocated area we can avoid
         * to read and directly return zeroes instead */
        if (ret & BDRV_BLOCK_ZERO &&
            pnum >= nb_sectors + sector_num % iscsilun->cluster_sectors) {
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
    while (!iTask.complete) {
        iscsi_set_events(iscsilun);
        qemu_mutex_unlock(&iscsilun->mutex);
        qemu_coroutine_yield();
        qemu_mutex_lock(&iscsilun->mutex);
    }

    if (iTask.task != NULL) {
        scsi_free_scsi_task(iTask.task);
        iTask.task = NULL;
    }

    if (iTask.do_retry) {
        iTask.complete = 0;
        goto retry;
    }
    qemu_mutex_unlock(&iscsilun->mutex);

    if (iTask.status != SCSI_STATUS_GOOD) {
        return iTask.err_code;
    }

    return 0;
}

static int coroutine_fn iscsi_co_flush(BlockDriverState *bs)
{
    IscsiLun *iscsilun = bs->opaque;
    struct IscsiTask iTask;

    iscsi_co_init_iscsitask(iscsilun, &iTask);
    qemu_mutex_lock(&iscsilun->mutex);
retry:
    if (iscsi_synchronizecache10_task(iscsilun->iscsi, iscsilun->lun, 0, 0, 0,
                                      0, iscsi_co_generic_cb, &iTask) == NULL) {
        qemu_mutex_unlock(&iscsilun->mutex);
        return -ENOMEM;
    }

    while (!iTask.complete) {
        iscsi_set_events(iscsilun);
        qemu_mutex_unlock(&iscsilun->mutex);
        qemu_coroutine_yield();
        qemu_mutex_lock(&iscsilun->mutex);
    }

    if (iTask.task != NULL) {
        scsi_free_scsi_task(iTask.task);
        iTask.task = NULL;
    }

    if (iTask.do_retry) {
        iTask.complete = 0;
        goto retry;
    }
    qemu_mutex_unlock(&iscsilun->mutex);

    if (iTask.status != SCSI_STATUS_GOOD) {
        return iTask.err_code;
    }

    return 0;
}

#ifdef __linux__
/* Called (via iscsi_service) with QemuMutex held.  */
static void
iscsi_aio_ioctl_cb(struct iscsi_context *iscsi, int status,
                     void *command_data, void *opaque)
{
    IscsiAIOCB *acb = opaque;

    g_free(acb->buf);
    acb->buf = NULL;

    acb->status = 0;
    if (status < 0) {
        error_report("Failed to ioctl(SG_IO) to iSCSI lun. %s",
                     iscsi_get_error(iscsi));
        acb->status = iscsi_translate_sense(&acb->task->sense);
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
        ss = (acb->ioh->mx_sb_len >= acb->ioh->sb_len_wr) ?
             acb->ioh->mx_sb_len : acb->ioh->sb_len_wr;
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
    acb->buf         = NULL;
    acb->ioh         = buf;

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
coroutine_fn iscsi_co_pdiscard(BlockDriverState *bs, int64_t offset, int bytes)
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

    while (!iTask.complete) {
        iscsi_set_events(iscsilun);
        qemu_mutex_unlock(&iscsilun->mutex);
        qemu_coroutine_yield();
        qemu_mutex_lock(&iscsilun->mutex);
    }

    if (iTask.task != NULL) {
        scsi_free_scsi_task(iTask.task);
        iTask.task = NULL;
    }

    if (iTask.do_retry) {
        iTask.complete = 0;
        goto retry;
    }

    if (iTask.status == SCSI_STATUS_CHECK_CONDITION) {
        /* the target might fail with a check condition if it
           is not happy with the alignment of the UNMAP request
           we silently fail in this case */
        goto out_unlock;
    }

    if (iTask.status != SCSI_STATUS_GOOD) {
        r = iTask.err_code;
        goto out_unlock;
    }

    iscsi_allocmap_set_invalid(iscsilun, offset >> BDRV_SECTOR_BITS,
                               bytes >> BDRV_SECTOR_BITS);

out_unlock:
    qemu_mutex_unlock(&iscsilun->mutex);
    return r;
}

static int
coroutine_fn iscsi_co_pwrite_zeroes(BlockDriverState *bs, int64_t offset,
                                    int bytes, BdrvRequestFlags flags)
{
    IscsiLun *iscsilun = bs->opaque;
    struct IscsiTask iTask;
    uint64_t lba;
    uint32_t nb_blocks;
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
        iTask.task = iscsi_writesame16_task(iscsilun->iscsi, iscsilun->lun, lba,
                                            iscsilun->zeroblock, iscsilun->block_size,
                                            nb_blocks, 0, !!(flags & BDRV_REQ_MAY_UNMAP),
                                            0, 0, iscsi_co_generic_cb, &iTask);
    } else {
        iTask.task = iscsi_writesame10_task(iscsilun->iscsi, iscsilun->lun, lba,
                                            iscsilun->zeroblock, iscsilun->block_size,
                                            nb_blocks, 0, !!(flags & BDRV_REQ_MAY_UNMAP),
                                            0, 0, iscsi_co_generic_cb, &iTask);
    }
    if (iTask.task == NULL) {
        qemu_mutex_unlock(&iscsilun->mutex);
        return -ENOMEM;
    }

    while (!iTask.complete) {
        iscsi_set_events(iscsilun);
        qemu_mutex_unlock(&iscsilun->mutex);
        qemu_coroutine_yield();
        qemu_mutex_lock(&iscsilun->mutex);
    }

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
        iscsi_allocmap_set_invalid(iscsilun, offset >> BDRV_SECTOR_BITS,
                                   bytes >> BDRV_SECTOR_BITS);
        r = iTask.err_code;
        goto out_unlock;
    }

    if (flags & BDRV_REQ_MAY_UNMAP) {
        iscsi_allocmap_set_invalid(iscsilun, offset >> BDRV_SECTOR_BITS,
                                   bytes >> BDRV_SECTOR_BITS);
    } else {
        iscsi_allocmap_set_allocated(iscsilun, offset >> BDRV_SECTOR_BITS,
                                     bytes >> BDRV_SECTOR_BITS);
    }

out_unlock:
    qemu_mutex_unlock(&iscsilun->mutex);
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

    qemu_mutex_lock(&iscsilun->mutex);
    if (iscsi_get_nops_in_flight(iscsilun->iscsi) >= MAX_NOP_FAILURES) {
        error_report("iSCSI: NOP timeout. Reconnecting...");
        iscsilun->request_timed_out = true;
    } else if (iscsi_nop_out_async(iscsilun->iscsi, NULL, NULL, 0, NULL) != 0) {
        error_report("iSCSI: failed to sent NOP-Out. Disabling NOP messages.");
        goto out;
    }

    timer_mod(iscsilun->nop_timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + NOP_INTERVAL);
    iscsi_set_events(iscsilun);

out:
    qemu_mutex_unlock(&iscsilun->mutex);
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
                       false, NULL, NULL, NULL, NULL);
    iscsilun->events = 0;

    if (iscsilun->nop_timer) {
        timer_del(iscsilun->nop_timer);
        timer_free(iscsilun->nop_timer);
        iscsilun->nop_timer = NULL;
    }
    if (iscsilun->event_timer) {
        timer_del(iscsilun->event_timer);
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
        {
            .name = "filename",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

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
    const char *transport_name, *portal, *target, *filename;
#if LIBISCSI_API_VERSION >= (20160603)
    enum iscsi_transport_type transport;
#endif
    int i, ret = 0, timeout = 0, lun;

    /* If we are given a filename, parse the filename, with precedence given to
     * filename encoded options */
    filename = qdict_get_try_str(options, "filename");
    if (filename) {
        warn_report("'filename' option specified. "
                    "This is an unsupported option, and may be deprecated "
                    "in the future");
        iscsi_parse_filename(filename, options, &local_err);
        if (local_err) {
            ret = -EINVAL;
            error_propagate(errp, local_err);
            goto exit;
        }
    }

    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
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
        error_report("iSCSI: ignoring timeout value for libiscsi <1.15.0");
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
    bs->supported_zero_flags = BDRV_REQ_MAY_UNMAP;

    /* Check the write protect flag of the LUN if we want to write */
    if (iscsilun->type == TYPE_DISK && (flags & BDRV_O_RDWR) &&
        iscsilun->write_protected) {
        error_setg(errp, "Cannot open a write protected LUN as read-write");
        ret = -EACCES;
        goto out;
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
        iscsilun->cluster_sectors = (iscsilun->bl.opt_unmap_gran *
                                     iscsilun->block_size) >> BDRV_SECTOR_BITS;
        if (iscsilun->lbprz) {
            ret = iscsi_allocmap_init(iscsilun, bs->open_flags);
        }
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
exit:
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

    assert(iscsilun->block_size >= BDRV_SECTOR_SIZE || bs->sg);

    bs->bl.request_alignment = block_size;

    if (iscsilun->bl.max_xfer_len) {
        max_xfer_len = MIN(max_xfer_len, iscsilun->bl.max_xfer_len);
    }

    if (max_xfer_len * block_size < INT_MAX) {
        bs->bl.max_transfer = max_xfer_len * iscsilun->block_size;
    }

    if (iscsilun->lbp.lbpu) {
        if (iscsilun->bl.max_unmap < 0xffffffff / block_size) {
            bs->bl.max_pdiscard =
                iscsilun->bl.max_unmap * iscsilun->block_size;
        }
        bs->bl.pdiscard_alignment =
            iscsilun->bl.opt_unmap_gran * iscsilun->block_size;
    } else {
        bs->bl.pdiscard_alignment = iscsilun->block_size;
    }

    if (iscsilun->bl.max_ws_len < 0xffffffff / block_size) {
        bs->bl.max_pwrite_zeroes =
            iscsilun->bl.max_ws_len * iscsilun->block_size;
    }
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

static int iscsi_truncate(BlockDriverState *bs, int64_t offset,
                          PreallocMode prealloc, Error **errp)
{
    IscsiLun *iscsilun = bs->opaque;
    Error *local_err = NULL;

    if (prealloc != PREALLOC_MODE_OFF) {
        error_setg(errp, "Unsupported preallocation mode '%s'",
                   PreallocMode_lookup[prealloc]);
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

    if (offset > iscsi_getlength(bs)) {
        error_setg(errp, "Cannot grow iSCSI devices");
        return -EINVAL;
    }

    if (iscsilun->allocmap != NULL) {
        iscsi_allocmap_init(iscsilun, bs->open_flags);
    }

    return 0;
}

static int iscsi_create(const char *filename, QemuOpts *opts, Error **errp)
{
    int ret = 0;
    int64_t total_size = 0;
    BlockDriverState *bs;
    IscsiLun *iscsilun = NULL;
    QDict *bs_options;
    Error *local_err = NULL;

    bs = bdrv_new();

    /* Read out options */
    total_size = DIV_ROUND_UP(qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0),
                              BDRV_SECTOR_SIZE);
    bs->opaque = g_new0(struct IscsiLun, 1);
    iscsilun = bs->opaque;

    bs_options = qdict_new();
    iscsi_parse_filename(filename, bs_options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
    } else {
        ret = iscsi_open(bs, bs_options, 0, NULL);
    }
    QDECREF(bs_options);

    if (ret != 0) {
        goto out;
    }
    iscsi_detach_aio_context(bs);
    if (iscsilun->type != TYPE_DISK) {
        ret = -ENODEV;
        goto out;
    }
    if (bs->total_sectors < total_size) {
        ret = -ENOSPC;
        goto out;
    }

    ret = 0;
out:
    if (iscsilun->iscsi != NULL) {
        iscsi_destroy_context(iscsilun->iscsi);
    }
    g_free(bs->opaque);
    bs->opaque = NULL;
    bdrv_unref(bs);
    return ret;
}

static int iscsi_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    IscsiLun *iscsilun = bs->opaque;
    bdi->unallocated_blocks_are_zero = iscsilun->lbprz;
    bdi->can_write_zeroes_with_unmap = iscsilun->lbprz && iscsilun->lbp.lbpws;
    bdi->cluster_size = iscsilun->cluster_sectors * BDRV_SECTOR_SIZE;
    return 0;
}

static void iscsi_invalidate_cache(BlockDriverState *bs,
                                   Error **errp)
{
    IscsiLun *iscsilun = bs->opaque;
    iscsi_allocmap_invalidate(iscsilun);
}

static QemuOptsList iscsi_create_opts = {
    .name = "iscsi-create-opts",
    .head = QTAILQ_HEAD_INITIALIZER(iscsi_create_opts.head),
    .desc = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Virtual disk size"
        },
        { /* end of list */ }
    }
};

static BlockDriver bdrv_iscsi = {
    .format_name     = "iscsi",
    .protocol_name   = "iscsi",

    .instance_size          = sizeof(IscsiLun),
    .bdrv_parse_filename    = iscsi_parse_filename,
    .bdrv_file_open         = iscsi_open,
    .bdrv_close             = iscsi_close,
    .bdrv_create            = iscsi_create,
    .create_opts            = &iscsi_create_opts,
    .bdrv_reopen_prepare    = iscsi_reopen_prepare,
    .bdrv_reopen_commit     = iscsi_reopen_commit,
    .bdrv_invalidate_cache  = iscsi_invalidate_cache,

    .bdrv_getlength  = iscsi_getlength,
    .bdrv_get_info   = iscsi_get_info,
    .bdrv_truncate   = iscsi_truncate,
    .bdrv_refresh_limits = iscsi_refresh_limits,

    .bdrv_co_get_block_status = iscsi_co_get_block_status,
    .bdrv_co_pdiscard      = iscsi_co_pdiscard,
    .bdrv_co_pwrite_zeroes = iscsi_co_pwrite_zeroes,
    .bdrv_co_readv         = iscsi_co_readv,
    .bdrv_co_writev_flags  = iscsi_co_writev_flags,
    .bdrv_co_flush_to_disk = iscsi_co_flush,

#ifdef __linux__
    .bdrv_aio_ioctl   = iscsi_aio_ioctl,
#endif

    .bdrv_detach_aio_context = iscsi_detach_aio_context,
    .bdrv_attach_aio_context = iscsi_attach_aio_context,
};

#if LIBISCSI_API_VERSION >= (20160603)
static BlockDriver bdrv_iser = {
    .format_name     = "iser",
    .protocol_name   = "iser",

    .instance_size          = sizeof(IscsiLun),
    .bdrv_parse_filename    = iscsi_parse_filename,
    .bdrv_file_open         = iscsi_open,
    .bdrv_close             = iscsi_close,
    .bdrv_create            = iscsi_create,
    .create_opts            = &iscsi_create_opts,
    .bdrv_reopen_prepare    = iscsi_reopen_prepare,
    .bdrv_reopen_commit     = iscsi_reopen_commit,
    .bdrv_invalidate_cache  = iscsi_invalidate_cache,

    .bdrv_getlength  = iscsi_getlength,
    .bdrv_get_info   = iscsi_get_info,
    .bdrv_truncate   = iscsi_truncate,
    .bdrv_refresh_limits = iscsi_refresh_limits,

    .bdrv_co_get_block_status = iscsi_co_get_block_status,
    .bdrv_co_pdiscard      = iscsi_co_pdiscard,
    .bdrv_co_pwrite_zeroes = iscsi_co_pwrite_zeroes,
    .bdrv_co_readv         = iscsi_co_readv,
    .bdrv_co_writev_flags  = iscsi_co_writev_flags,
    .bdrv_co_flush_to_disk = iscsi_co_flush,

#ifdef __linux__
    .bdrv_aio_ioctl   = iscsi_aio_ioctl,
#endif

    .bdrv_detach_aio_context = iscsi_detach_aio_context,
    .bdrv_attach_aio_context = iscsi_attach_aio_context,
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
