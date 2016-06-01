/*
 * QEMU Block driver for iSCSI images
 *
 * Copyright (c) 2010-2011 Ronnie Sahlberg <ronniesahlberg@gmail.com>
 * Copyright (c) 2012-2015 Peter Lieven <pl@kamp.de>
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
#include "sysemu/sysemu.h"
#include "qmp-commands.h"
#include "qapi/qmp/qstring.h"
#include "crypto/secret.h"

#include <iscsi/iscsi.h>
#include <iscsi/scsi-lowlevel.h>

#ifdef __linux__
#include <scsi/sg.h>
#include <block/scsi.h>
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
    struct scsi_inquiry_logical_block_provisioning lbp;
    struct scsi_inquiry_block_limits bl;
    unsigned char *zeroblock;
    unsigned long *allocationmap;
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
    QEMUBH *bh;
    IscsiLun *iscsilun;
    QEMUTimer retry_timer;
    int err_code;
} IscsiTask;

typedef struct IscsiAIOCB {
    BlockAIOCB common;
    QEMUIOVector *qiov;
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
    qemu_bh_delete(iTask->bh);
    qemu_coroutine_enter(iTask->co, NULL);
}

static void iscsi_retry_timer_expired(void *opaque)
{
    struct IscsiTask *iTask = opaque;
    iTask->complete = 1;
    if (iTask->co) {
        qemu_coroutine_enter(iTask->co, NULL);
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
        iTask->bh = aio_bh_new(iTask->iscsilun->aio_context,
                               iscsi_co_generic_bh_cb, iTask);
        qemu_bh_schedule(iTask->bh);
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

    iscsi_service(iscsi, POLLIN);
    iscsi_set_events(iscsilun);
}

static void
iscsi_process_write(void *arg)
{
    IscsiLun *iscsilun = arg;
    struct iscsi_context *iscsi = iscsilun->iscsi;

    iscsi_service(iscsi, POLLOUT);
    iscsi_set_events(iscsilun);
}

static int64_t sector_lun2qemu(int64_t sector, IscsiLun *iscsilun)
{
    return sector * iscsilun->block_size / BDRV_SECTOR_SIZE;
}

static int64_t sector_qemu2lun(int64_t sector, IscsiLun *iscsilun)
{
    return sector * BDRV_SECTOR_SIZE / iscsilun->block_size;
}

static bool is_request_lun_aligned(int64_t sector_num, int nb_sectors,
                                      IscsiLun *iscsilun)
{
    if ((sector_num * BDRV_SECTOR_SIZE) % iscsilun->block_size ||
        (nb_sectors * BDRV_SECTOR_SIZE) % iscsilun->block_size) {
            error_report("iSCSI misaligned request: "
                         "iscsilun->block_size %u, sector_num %" PRIi64
                         ", nb_sectors %d",
                         iscsilun->block_size, sector_num, nb_sectors);
            return 0;
    }
    return 1;
}

static unsigned long *iscsi_allocationmap_init(IscsiLun *iscsilun)
{
    return bitmap_try_new(DIV_ROUND_UP(sector_lun2qemu(iscsilun->num_blocks,
                                                       iscsilun),
                                       iscsilun->cluster_sectors));
}

static void iscsi_allocationmap_set(IscsiLun *iscsilun, int64_t sector_num,
                                    int nb_sectors)
{
    if (iscsilun->allocationmap == NULL) {
        return;
    }
    bitmap_set(iscsilun->allocationmap,
               sector_num / iscsilun->cluster_sectors,
               DIV_ROUND_UP(nb_sectors, iscsilun->cluster_sectors));
}

static void iscsi_allocationmap_clear(IscsiLun *iscsilun, int64_t sector_num,
                                      int nb_sectors)
{
    int64_t cluster_num, nb_clusters;
    if (iscsilun->allocationmap == NULL) {
        return;
    }
    cluster_num = DIV_ROUND_UP(sector_num, iscsilun->cluster_sectors);
    nb_clusters = (sector_num + nb_sectors) / iscsilun->cluster_sectors
                  - cluster_num;
    if (nb_clusters > 0) {
        bitmap_clear(iscsilun->allocationmap, cluster_num, nb_clusters);
    }
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

    if (fua) {
        assert(iscsilun->dpofua);
    }
    if (!is_request_lun_aligned(sector_num, nb_sectors, iscsilun)) {
        return -EINVAL;
    }

    if (bs->bl.max_transfer_length && nb_sectors > bs->bl.max_transfer_length) {
        error_report("iSCSI Error: Write of %d sectors exceeds max_xfer_len "
                     "of %d sectors", nb_sectors, bs->bl.max_transfer_length);
        return -EINVAL;
    }

    lba = sector_qemu2lun(sector_num, iscsilun);
    num_sectors = sector_qemu2lun(nb_sectors, iscsilun);
    iscsi_co_init_iscsitask(iscsilun, &iTask);
retry:
    if (iscsilun->use_16_for_rw) {
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
    if (iTask.task == NULL) {
        return -ENOMEM;
    }
    scsi_task_set_iov_out(iTask.task, (struct scsi_iovec *) iov->iov,
                          iov->niov);
    while (!iTask.complete) {
        iscsi_set_events(iscsilun);
        qemu_coroutine_yield();
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
        return iTask.err_code;
    }

    iscsi_allocationmap_set(iscsilun, sector_num, nb_sectors);

    return 0;
}


static bool iscsi_allocationmap_is_allocated(IscsiLun *iscsilun,
                                             int64_t sector_num, int nb_sectors)
{
    unsigned long size;
    if (iscsilun->allocationmap == NULL) {
        return true;
    }
    size = DIV_ROUND_UP(sector_num + nb_sectors, iscsilun->cluster_sectors);
    return !(find_next_bit(iscsilun->allocationmap, size,
                           sector_num / iscsilun->cluster_sectors) == size);
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

    if (!is_request_lun_aligned(sector_num, nb_sectors, iscsilun)) {
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

retry:
    if (iscsi_get_lba_status_task(iscsilun->iscsi, iscsilun->lun,
                                  sector_qemu2lun(sector_num, iscsilun),
                                  8 + 16, iscsi_co_generic_cb,
                                  &iTask) == NULL) {
        ret = -ENOMEM;
        goto out;
    }

    while (!iTask.complete) {
        iscsi_set_events(iscsilun);
        qemu_coroutine_yield();
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
        goto out;
    }

    lbas = scsi_datain_unmarshall(iTask.task);
    if (lbas == NULL) {
        ret = -EIO;
        goto out;
    }

    lbasd = &lbas->descriptors[0];

    if (sector_qemu2lun(sector_num, iscsilun) != lbasd->lba) {
        ret = -EIO;
        goto out;
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
        iscsi_allocationmap_clear(iscsilun, sector_num, *pnum);
    } else {
        iscsi_allocationmap_set(iscsilun, sector_num, *pnum);
    }

    if (*pnum > nb_sectors) {
        *pnum = nb_sectors;
    }
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

    if (!is_request_lun_aligned(sector_num, nb_sectors, iscsilun)) {
        return -EINVAL;
    }

    if (bs->bl.max_transfer_length && nb_sectors > bs->bl.max_transfer_length) {
        error_report("iSCSI Error: Read of %d sectors exceeds max_xfer_len "
                     "of %d sectors", nb_sectors, bs->bl.max_transfer_length);
        return -EINVAL;
    }

    if (iscsilun->lbprz && nb_sectors >= ISCSI_CHECKALLOC_THRES &&
        !iscsi_allocationmap_is_allocated(iscsilun, sector_num, nb_sectors)) {
        int64_t ret;
        int pnum;
        BlockDriverState *file;
        ret = iscsi_co_get_block_status(bs, sector_num, INT_MAX, &pnum, &file);
        if (ret < 0) {
            return ret;
        }
        if (ret & BDRV_BLOCK_ZERO && pnum >= nb_sectors) {
            qemu_iovec_memset(iov, 0, 0x00, iov->size);
            return 0;
        }
    }

    lba = sector_qemu2lun(sector_num, iscsilun);
    num_sectors = sector_qemu2lun(nb_sectors, iscsilun);

    iscsi_co_init_iscsitask(iscsilun, &iTask);
retry:
    if (iscsilun->use_16_for_rw) {
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
    if (iTask.task == NULL) {
        return -ENOMEM;
    }
    scsi_task_set_iov_in(iTask.task, (struct scsi_iovec *) iov->iov, iov->niov);

    while (!iTask.complete) {
        iscsi_set_events(iscsilun);
        qemu_coroutine_yield();
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
        return iTask.err_code;
    }

    return 0;
}

static int coroutine_fn iscsi_co_flush(BlockDriverState *bs)
{
    IscsiLun *iscsilun = bs->opaque;
    struct IscsiTask iTask;

    iscsi_co_init_iscsitask(iscsilun, &iTask);
retry:
    if (iscsi_synchronizecache10_task(iscsilun->iscsi, iscsilun->lun, 0, 0, 0,
                                      0, iscsi_co_generic_cb, &iTask) == NULL) {
        return -ENOMEM;
    }

    while (!iTask.complete) {
        iscsi_set_events(iscsilun);
        qemu_coroutine_yield();
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
        return iTask.err_code;
    }

    return 0;
}

#ifdef __linux__
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
coroutine_fn iscsi_co_discard(BlockDriverState *bs, int64_t sector_num,
                                   int nb_sectors)
{
    IscsiLun *iscsilun = bs->opaque;
    struct IscsiTask iTask;
    struct unmap_list list;

    if (!is_request_lun_aligned(sector_num, nb_sectors, iscsilun)) {
        return -EINVAL;
    }

    if (!iscsilun->lbp.lbpu) {
        /* UNMAP is not supported by the target */
        return 0;
    }

    list.lba = sector_qemu2lun(sector_num, iscsilun);
    list.num = sector_qemu2lun(nb_sectors, iscsilun);

    iscsi_co_init_iscsitask(iscsilun, &iTask);
retry:
    if (iscsi_unmap_task(iscsilun->iscsi, iscsilun->lun, 0, 0, &list, 1,
                     iscsi_co_generic_cb, &iTask) == NULL) {
        return -ENOMEM;
    }

    while (!iTask.complete) {
        iscsi_set_events(iscsilun);
        qemu_coroutine_yield();
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
        return 0;
    }

    if (iTask.status != SCSI_STATUS_GOOD) {
        return iTask.err_code;
    }

    iscsi_allocationmap_clear(iscsilun, sector_num, nb_sectors);

    return 0;
}

static int
coroutine_fn iscsi_co_write_zeroes(BlockDriverState *bs, int64_t sector_num,
                                   int nb_sectors, BdrvRequestFlags flags)
{
    IscsiLun *iscsilun = bs->opaque;
    struct IscsiTask iTask;
    uint64_t lba;
    uint32_t nb_blocks;
    bool use_16_for_ws = iscsilun->use_16_for_rw;

    if (!is_request_lun_aligned(sector_num, nb_sectors, iscsilun)) {
        return -EINVAL;
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

    lba = sector_qemu2lun(sector_num, iscsilun);
    nb_blocks = sector_qemu2lun(nb_sectors, iscsilun);

    if (iscsilun->zeroblock == NULL) {
        iscsilun->zeroblock = g_try_malloc0(iscsilun->block_size);
        if (iscsilun->zeroblock == NULL) {
            return -ENOMEM;
        }
    }

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
        return -ENOMEM;
    }

    while (!iTask.complete) {
        iscsi_set_events(iscsilun);
        qemu_coroutine_yield();
    }

    if (iTask.status == SCSI_STATUS_CHECK_CONDITION &&
        iTask.task->sense.key == SCSI_SENSE_ILLEGAL_REQUEST &&
        (iTask.task->sense.ascq == SCSI_SENSE_ASCQ_INVALID_OPERATION_CODE ||
         iTask.task->sense.ascq == SCSI_SENSE_ASCQ_INVALID_FIELD_IN_CDB)) {
        /* WRITE SAME is not supported by the target */
        iscsilun->has_write_same = false;
        scsi_free_scsi_task(iTask.task);
        return -ENOTSUP;
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
        return iTask.err_code;
    }

    if (flags & BDRV_REQ_MAY_UNMAP) {
        iscsi_allocationmap_clear(iscsilun, sector_num, nb_sectors);
    } else {
        iscsi_allocationmap_set(iscsilun, sector_num, nb_sectors);
    }

    return 0;
}

static void parse_chap(struct iscsi_context *iscsi, const char *target,
                       Error **errp)
{
    QemuOptsList *list;
    QemuOpts *opts;
    const char *user = NULL;
    const char *password = NULL;
    const char *secretid;
    char *secret = NULL;

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

static void parse_header_digest(struct iscsi_context *iscsi, const char *target,
                                Error **errp)
{
    QemuOptsList *list;
    QemuOpts *opts;
    const char *digest = NULL;

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

    digest = qemu_opt_get(opts, "header-digest");
    if (!digest) {
        return;
    }

    if (!strcmp(digest, "CRC32C")) {
        iscsi_set_header_digest(iscsi, ISCSI_HEADER_DIGEST_CRC32C);
    } else if (!strcmp(digest, "NONE")) {
        iscsi_set_header_digest(iscsi, ISCSI_HEADER_DIGEST_NONE);
    } else if (!strcmp(digest, "CRC32C-NONE")) {
        iscsi_set_header_digest(iscsi, ISCSI_HEADER_DIGEST_CRC32C_NONE);
    } else if (!strcmp(digest, "NONE-CRC32C")) {
        iscsi_set_header_digest(iscsi, ISCSI_HEADER_DIGEST_NONE_CRC32C);
    } else {
        error_setg(errp, "Invalid header-digest setting : %s", digest);
    }
}

static char *parse_initiator_name(const char *target)
{
    QemuOptsList *list;
    QemuOpts *opts;
    const char *name;
    char *iscsi_name;
    UuidInfo *uuid_info;

    list = qemu_find_opts("iscsi");
    if (list) {
        opts = qemu_opts_find(list, target);
        if (!opts) {
            opts = QTAILQ_FIRST(&list->head);
        }
        if (opts) {
            name = qemu_opt_get(opts, "initiator-name");
            if (name) {
                return g_strdup(name);
            }
        }
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

static int parse_timeout(const char *target)
{
    QemuOptsList *list;
    QemuOpts *opts;
    const char *timeout;

    list = qemu_find_opts("iscsi");
    if (list) {
        opts = qemu_opts_find(list, target);
        if (!opts) {
            opts = QTAILQ_FIRST(&list->head);
        }
        if (opts) {
            timeout = qemu_opt_get(opts, "timeout");
            if (timeout) {
                return atoi(timeout);
            }
        }
    }

    return 0;
}

static void iscsi_nop_timed_event(void *opaque)
{
    IscsiLun *iscsilun = opaque;

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

/* TODO Convert to fine grained options */
static QemuOptsList runtime_opts = {
    .name = "iscsi",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = "filename",
            .type = QEMU_OPT_STRING,
            .help = "URL to the iscsi image",
        },
        { /* end of list */ }
    },
};

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
                       false, NULL, NULL, NULL);
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

/*
 * We support iscsi url's on the form
 * iscsi://[<username>%<password>@]<host>[:<port>]/<targetname>/<lun>
 */
static int iscsi_open(BlockDriverState *bs, QDict *options, int flags,
                      Error **errp)
{
    IscsiLun *iscsilun = bs->opaque;
    struct iscsi_context *iscsi = NULL;
    struct iscsi_url *iscsi_url = NULL;
    struct scsi_task *task = NULL;
    struct scsi_inquiry_standard *inq = NULL;
    struct scsi_inquiry_supported_pages *inq_vpd;
    char *initiator_name = NULL;
    QemuOpts *opts;
    Error *local_err = NULL;
    const char *filename;
    int i, ret = 0, timeout = 0;

    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto out;
    }

    filename = qemu_opt_get(opts, "filename");

    iscsi_url = iscsi_parse_full_url(iscsi, filename);
    if (iscsi_url == NULL) {
        error_setg(errp, "Failed to parse URL : %s", filename);
        ret = -EINVAL;
        goto out;
    }

    memset(iscsilun, 0, sizeof(IscsiLun));

    initiator_name = parse_initiator_name(iscsi_url->target);

    iscsi = iscsi_create_context(initiator_name);
    if (iscsi == NULL) {
        error_setg(errp, "iSCSI: Failed to create iSCSI context.");
        ret = -ENOMEM;
        goto out;
    }

    if (iscsi_set_targetname(iscsi, iscsi_url->target)) {
        error_setg(errp, "iSCSI: Failed to set target name.");
        ret = -EINVAL;
        goto out;
    }

    if (iscsi_url->user[0] != '\0') {
        ret = iscsi_set_initiator_username_pwd(iscsi, iscsi_url->user,
                                              iscsi_url->passwd);
        if (ret != 0) {
            error_setg(errp, "Failed to set initiator username and password");
            ret = -EINVAL;
            goto out;
        }
    }

    /* check if we got CHAP username/password via the options */
    parse_chap(iscsi, iscsi_url->target, &local_err);
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

    iscsi_set_header_digest(iscsi, ISCSI_HEADER_DIGEST_NONE_CRC32C);

    /* check if we got HEADER_DIGEST via the options */
    parse_header_digest(iscsi, iscsi_url->target, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto out;
    }

    /* timeout handling is broken in libiscsi before 1.15.0 */
    timeout = parse_timeout(iscsi_url->target);
#if defined(LIBISCSI_API_VERSION) && LIBISCSI_API_VERSION >= 20150621
    iscsi_set_timeout(iscsi, timeout);
#else
    if (timeout) {
        error_report("iSCSI: ignoring timeout value for libiscsi <1.15.0");
    }
#endif

    if (iscsi_full_connect_sync(iscsi, iscsi_url->portal, iscsi_url->lun) != 0) {
        error_setg(errp, "iSCSI: Failed to connect to LUN : %s",
            iscsi_get_error(iscsi));
        ret = -EINVAL;
        goto out;
    }

    iscsilun->iscsi = iscsi;
    iscsilun->aio_context = bdrv_get_aio_context(bs);
    iscsilun->lun   = iscsi_url->lun;
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
    bs->request_alignment = iscsilun->block_size;

    /* We don't have any emulation for devices other than disks and CD-ROMs, so
     * this must be sg ioctl compatible. We force it to be sg, otherwise qemu
     * will try to read from the device to guess the image format.
     */
    if (iscsilun->type != TYPE_DISK && iscsilun->type != TYPE_ROM) {
        bs->sg = 1;
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

    iscsi_attach_aio_context(bs, iscsilun->aio_context);

    /* Guess the internal cluster (page) size of the iscsi target by the means
     * of opt_unmap_gran. Transfer the unmap granularity only if it has a
     * reasonable size */
    if (iscsilun->bl.opt_unmap_gran * iscsilun->block_size >= 4 * 1024 &&
        iscsilun->bl.opt_unmap_gran * iscsilun->block_size <= 16 * 1024 * 1024) {
        iscsilun->cluster_sectors = (iscsilun->bl.opt_unmap_gran *
                                     iscsilun->block_size) >> BDRV_SECTOR_BITS;
        if (iscsilun->lbprz) {
            iscsilun->allocationmap = iscsi_allocationmap_init(iscsilun);
            if (iscsilun->allocationmap == NULL) {
                ret = -ENOMEM;
            }
        }
    }

out:
    qemu_opts_del(opts);
    g_free(initiator_name);
    if (iscsi_url != NULL) {
        iscsi_destroy_url(iscsi_url);
    }
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
    g_free(iscsilun->zeroblock);
    g_free(iscsilun->allocationmap);
    memset(iscsilun, 0, sizeof(IscsiLun));
}

static int sector_limits_lun2qemu(int64_t sector, IscsiLun *iscsilun)
{
    return MIN(sector_lun2qemu(sector, iscsilun), INT_MAX / 2 + 1);
}

static void iscsi_refresh_limits(BlockDriverState *bs, Error **errp)
{
    /* We don't actually refresh here, but just return data queried in
     * iscsi_open(): iscsi targets don't change their limits. */

    IscsiLun *iscsilun = bs->opaque;
    uint32_t max_xfer_len = iscsilun->use_16_for_rw ? 0xffffffff : 0xffff;

    if (iscsilun->bl.max_xfer_len) {
        max_xfer_len = MIN(max_xfer_len, iscsilun->bl.max_xfer_len);
    }

    bs->bl.max_transfer_length = sector_limits_lun2qemu(max_xfer_len, iscsilun);

    if (iscsilun->lbp.lbpu) {
        if (iscsilun->bl.max_unmap < 0xffffffff) {
            bs->bl.max_discard =
                sector_limits_lun2qemu(iscsilun->bl.max_unmap, iscsilun);
        }
        bs->bl.discard_alignment =
            sector_limits_lun2qemu(iscsilun->bl.opt_unmap_gran, iscsilun);
    } else {
        bs->bl.discard_alignment = iscsilun->block_size >> BDRV_SECTOR_BITS;
    }

    if (iscsilun->bl.max_ws_len < 0xffffffff) {
        bs->bl.max_write_zeroes =
            sector_limits_lun2qemu(iscsilun->bl.max_ws_len, iscsilun);
    }
    if (iscsilun->lbp.lbpws) {
        bs->bl.write_zeroes_alignment =
            sector_limits_lun2qemu(iscsilun->bl.opt_unmap_gran, iscsilun);
    } else {
        bs->bl.write_zeroes_alignment =
            iscsilun->block_size >> BDRV_SECTOR_BITS;
    }
    bs->bl.opt_transfer_length =
        sector_limits_lun2qemu(iscsilun->bl.opt_xfer_len, iscsilun);
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

static int iscsi_truncate(BlockDriverState *bs, int64_t offset)
{
    IscsiLun *iscsilun = bs->opaque;
    Error *local_err = NULL;

    if (iscsilun->type != TYPE_DISK) {
        return -ENOTSUP;
    }

    iscsi_readcapacity_sync(iscsilun, &local_err);
    if (local_err != NULL) {
        error_free(local_err);
        return -EIO;
    }

    if (offset > iscsi_getlength(bs)) {
        return -EINVAL;
    }

    if (iscsilun->allocationmap != NULL) {
        g_free(iscsilun->allocationmap);
        iscsilun->allocationmap = iscsi_allocationmap_init(iscsilun);
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

    bs = bdrv_new();

    /* Read out options */
    total_size = DIV_ROUND_UP(qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0),
                              BDRV_SECTOR_SIZE);
    bs->opaque = g_new0(struct IscsiLun, 1);
    iscsilun = bs->opaque;

    bs_options = qdict_new();
    qdict_put(bs_options, "filename", qstring_from_str(filename));
    ret = iscsi_open(bs, bs_options, 0, NULL);
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

    .instance_size   = sizeof(IscsiLun),
    .bdrv_needs_filename = true,
    .bdrv_file_open  = iscsi_open,
    .bdrv_close      = iscsi_close,
    .bdrv_create     = iscsi_create,
    .create_opts     = &iscsi_create_opts,
    .bdrv_reopen_prepare  = iscsi_reopen_prepare,

    .bdrv_getlength  = iscsi_getlength,
    .bdrv_get_info   = iscsi_get_info,
    .bdrv_truncate   = iscsi_truncate,
    .bdrv_refresh_limits = iscsi_refresh_limits,

    .bdrv_co_get_block_status = iscsi_co_get_block_status,
    .bdrv_co_discard      = iscsi_co_discard,
    .bdrv_co_write_zeroes = iscsi_co_write_zeroes,
    .bdrv_co_readv         = iscsi_co_readv,
    .bdrv_co_writev_flags  = iscsi_co_writev_flags,
    .bdrv_co_flush_to_disk = iscsi_co_flush,

#ifdef __linux__
    .bdrv_aio_ioctl   = iscsi_aio_ioctl,
#endif

    .bdrv_detach_aio_context = iscsi_detach_aio_context,
    .bdrv_attach_aio_context = iscsi_attach_aio_context,
};

static QemuOptsList qemu_iscsi_opts = {
    .name = "iscsi",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_iscsi_opts.head),
    .desc = {
        {
            .name = "user",
            .type = QEMU_OPT_STRING,
            .help = "username for CHAP authentication to target",
        },{
            .name = "password",
            .type = QEMU_OPT_STRING,
            .help = "password for CHAP authentication to target",
        },{
            .name = "password-secret",
            .type = QEMU_OPT_STRING,
            .help = "ID of the secret providing password for CHAP "
                    "authentication to target",
        },{
            .name = "header-digest",
            .type = QEMU_OPT_STRING,
            .help = "HeaderDigest setting. "
                    "{CRC32C|CRC32C-NONE|NONE-CRC32C|NONE}",
        },{
            .name = "initiator-name",
            .type = QEMU_OPT_STRING,
            .help = "Initiator iqn name to use when connecting",
        },{
            .name = "timeout",
            .type = QEMU_OPT_NUMBER,
            .help = "Request timeout in seconds (default 0 = no timeout)",
        },
        { /* end of list */ }
    },
};

static void iscsi_block_init(void)
{
    bdrv_register(&bdrv_iscsi);
    qemu_add_opts(&qemu_iscsi_opts);
}

block_init(iscsi_block_init);
