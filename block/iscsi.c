/*
 * QEMU Block driver for iSCSI images
 *
 * Copyright (c) 2010-2011 Ronnie Sahlberg <ronniesahlberg@gmail.com>
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

#include "config-host.h"

#include <poll.h>
#include <arpa/inet.h>
#include "qemu-common.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "block/block_int.h"
#include "trace.h"
#include "block/scsi.h"
#include "qemu/iov.h"

#include <iscsi/iscsi.h>
#include <iscsi/scsi-lowlevel.h>

#ifdef __linux__
#include <scsi/sg.h>
#include <block/scsi.h>
#endif

typedef struct IscsiLun {
    struct iscsi_context *iscsi;
    int lun;
    enum scsi_inquiry_peripheral_device_type type;
    int block_size;
    uint64_t num_blocks;
    int events;
    QEMUTimer *nop_timer;
} IscsiLun;

typedef struct IscsiAIOCB {
    BlockDriverAIOCB common;
    QEMUIOVector *qiov;
    QEMUBH *bh;
    IscsiLun *iscsilun;
    struct scsi_task *task;
    uint8_t *buf;
    int status;
    int canceled;
    int retries;
    int64_t sector_num;
    int nb_sectors;
#ifdef __linux__
    sg_io_hdr_t *ioh;
#endif
} IscsiAIOCB;

#define NOP_INTERVAL 5000
#define MAX_NOP_FAILURES 3
#define ISCSI_CMD_RETRIES 5

static void
iscsi_bh_cb(void *p)
{
    IscsiAIOCB *acb = p;

    qemu_bh_delete(acb->bh);

    g_free(acb->buf);
    acb->buf = NULL;

    if (acb->canceled == 0) {
        acb->common.cb(acb->common.opaque, acb->status);
    }

    if (acb->task != NULL) {
        scsi_free_scsi_task(acb->task);
        acb->task = NULL;
    }

    qemu_aio_release(acb);
}

static void
iscsi_schedule_bh(IscsiAIOCB *acb)
{
    if (acb->bh) {
        return;
    }
    acb->bh = qemu_bh_new(iscsi_bh_cb, acb);
    qemu_bh_schedule(acb->bh);
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
iscsi_aio_cancel(BlockDriverAIOCB *blockacb)
{
    IscsiAIOCB *acb = (IscsiAIOCB *)blockacb;
    IscsiLun *iscsilun = acb->iscsilun;

    if (acb->status != -EINPROGRESS) {
        return;
    }

    acb->canceled = 1;

    /* send a task mgmt call to the target to cancel the task on the target */
    iscsi_task_mgmt_abort_task_async(iscsilun->iscsi, acb->task,
                                     iscsi_abort_task_cb, acb);

    while (acb->status == -EINPROGRESS) {
        qemu_aio_wait();
    }
}

static const AIOCBInfo iscsi_aiocb_info = {
    .aiocb_size         = sizeof(IscsiAIOCB),
    .cancel             = iscsi_aio_cancel,
};


static void iscsi_process_read(void *arg);
static void iscsi_process_write(void *arg);

static int iscsi_process_flush(void *arg)
{
    IscsiLun *iscsilun = arg;

    return iscsi_queue_length(iscsilun->iscsi) > 0;
}

static void
iscsi_set_events(IscsiLun *iscsilun)
{
    struct iscsi_context *iscsi = iscsilun->iscsi;
    int ev;

    /* We always register a read handler.  */
    ev = POLLIN;
    ev |= iscsi_which_events(iscsi);
    if (ev != iscsilun->events) {
        qemu_aio_set_fd_handler(iscsi_get_fd(iscsi),
                      iscsi_process_read,
                      (ev & POLLOUT) ? iscsi_process_write : NULL,
                      iscsi_process_flush,
                      iscsilun);

    }

    iscsilun->events = ev;
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

static int
iscsi_aio_writev_acb(IscsiAIOCB *acb);

static void
iscsi_aio_write16_cb(struct iscsi_context *iscsi, int status,
                     void *command_data, void *opaque)
{
    IscsiAIOCB *acb = opaque;

    trace_iscsi_aio_write16_cb(iscsi, status, acb, acb->canceled);

    g_free(acb->buf);
    acb->buf = NULL;

    if (acb->canceled != 0) {
        return;
    }

    acb->status = 0;
    if (status != 0) {
        if (status == SCSI_STATUS_CHECK_CONDITION
            && acb->task->sense.key == SCSI_SENSE_UNIT_ATTENTION
            && acb->retries-- > 0) {
            scsi_free_scsi_task(acb->task);
            acb->task = NULL;
            if (iscsi_aio_writev_acb(acb) == 0) {
                iscsi_set_events(acb->iscsilun);
                return;
            }
        }
        error_report("Failed to write16 data to iSCSI lun. %s",
                     iscsi_get_error(iscsi));
        acb->status = -EIO;
    }

    iscsi_schedule_bh(acb);
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
            error_report("iSCSI misaligned request: iscsilun->block_size %u, sector_num %ld, nb_sectors %d",
                         iscsilun->block_size, sector_num, nb_sectors);
            return 0;
    }
    return 1;
}

static int
iscsi_aio_writev_acb(IscsiAIOCB *acb)
{
    struct iscsi_context *iscsi = acb->iscsilun->iscsi;
    size_t size;
    uint32_t num_sectors;
    uint64_t lba;
#if !defined(LIBISCSI_FEATURE_IOVECTOR)
    struct iscsi_data data;
#endif
    int ret;

    acb->canceled   = 0;
    acb->bh         = NULL;
    acb->status     = -EINPROGRESS;
    acb->buf        = NULL;

    /* this will allow us to get rid of 'buf' completely */
    size = acb->nb_sectors * BDRV_SECTOR_SIZE;

#if !defined(LIBISCSI_FEATURE_IOVECTOR)
    data.size = MIN(size, acb->qiov->size);

    /* if the iovec only contains one buffer we can pass it directly */
    if (acb->qiov->niov == 1) {
        data.data = acb->qiov->iov[0].iov_base;
    } else {
        acb->buf = g_malloc(data.size);
        qemu_iovec_to_buf(acb->qiov, 0, acb->buf, data.size);
        data.data = acb->buf;
    }
#endif

    acb->task = malloc(sizeof(struct scsi_task));
    if (acb->task == NULL) {
        error_report("iSCSI: Failed to allocate task for scsi WRITE16 "
                     "command. %s", iscsi_get_error(iscsi));
        return -1;
    }
    memset(acb->task, 0, sizeof(struct scsi_task));

    acb->task->xfer_dir = SCSI_XFER_WRITE;
    acb->task->cdb_size = 16;
    acb->task->cdb[0] = 0x8a;
    lba = sector_qemu2lun(acb->sector_num, acb->iscsilun);
    *(uint32_t *)&acb->task->cdb[2]  = htonl(lba >> 32);
    *(uint32_t *)&acb->task->cdb[6]  = htonl(lba & 0xffffffff);
    num_sectors = size / acb->iscsilun->block_size;
    *(uint32_t *)&acb->task->cdb[10] = htonl(num_sectors);
    acb->task->expxferlen = size;

#if defined(LIBISCSI_FEATURE_IOVECTOR)
    ret = iscsi_scsi_command_async(iscsi, acb->iscsilun->lun, acb->task,
                                   iscsi_aio_write16_cb,
                                   NULL,
                                   acb);
#else
    ret = iscsi_scsi_command_async(iscsi, acb->iscsilun->lun, acb->task,
                                   iscsi_aio_write16_cb,
                                   &data,
                                   acb);
#endif
    if (ret != 0) {
        scsi_free_scsi_task(acb->task);
        g_free(acb->buf);
        return -1;
    }

#if defined(LIBISCSI_FEATURE_IOVECTOR)
    scsi_task_set_iov_out(acb->task, (struct scsi_iovec*) acb->qiov->iov, acb->qiov->niov);
#endif

    return 0;
}

static BlockDriverAIOCB *
iscsi_aio_writev(BlockDriverState *bs, int64_t sector_num,
                 QEMUIOVector *qiov, int nb_sectors,
                 BlockDriverCompletionFunc *cb,
                 void *opaque)
{
    IscsiLun *iscsilun = bs->opaque;
    IscsiAIOCB *acb;

    if (!is_request_lun_aligned(sector_num, nb_sectors, iscsilun)) {
        return NULL;
    }

    acb = qemu_aio_get(&iscsi_aiocb_info, bs, cb, opaque);
    trace_iscsi_aio_writev(iscsilun->iscsi, sector_num, nb_sectors, opaque, acb);

    acb->iscsilun    = iscsilun;
    acb->qiov        = qiov;
    acb->nb_sectors  = nb_sectors;
    acb->sector_num  = sector_num;
    acb->retries     = ISCSI_CMD_RETRIES;

    if (iscsi_aio_writev_acb(acb) != 0) {
        qemu_aio_release(acb);
        return NULL;
    }

    iscsi_set_events(iscsilun);
    return &acb->common;
}

static int
iscsi_aio_readv_acb(IscsiAIOCB *acb);

static void
iscsi_aio_read16_cb(struct iscsi_context *iscsi, int status,
                    void *command_data, void *opaque)
{
    IscsiAIOCB *acb = opaque;

    trace_iscsi_aio_read16_cb(iscsi, status, acb, acb->canceled);

    if (acb->canceled != 0) {
        return;
    }

    acb->status = 0;
    if (status != 0) {
        if (status == SCSI_STATUS_CHECK_CONDITION
            && acb->task->sense.key == SCSI_SENSE_UNIT_ATTENTION
            && acb->retries-- > 0) {
            scsi_free_scsi_task(acb->task);
            acb->task = NULL;
            if (iscsi_aio_readv_acb(acb) == 0) {
                iscsi_set_events(acb->iscsilun);
                return;
            }
        }
        error_report("Failed to read16 data from iSCSI lun. %s",
                     iscsi_get_error(iscsi));
        acb->status = -EIO;
    }

    iscsi_schedule_bh(acb);
}

static int
iscsi_aio_readv_acb(IscsiAIOCB *acb)
{
    struct iscsi_context *iscsi = acb->iscsilun->iscsi;
    size_t size;
    uint64_t lba;
    uint32_t num_sectors;
    int ret;
#if !defined(LIBISCSI_FEATURE_IOVECTOR)
    int i;
#endif

    acb->canceled    = 0;
    acb->bh          = NULL;
    acb->status      = -EINPROGRESS;
    acb->buf         = NULL;

    size = acb->nb_sectors * BDRV_SECTOR_SIZE;

    acb->task = malloc(sizeof(struct scsi_task));
    if (acb->task == NULL) {
        error_report("iSCSI: Failed to allocate task for scsi READ16 "
                     "command. %s", iscsi_get_error(iscsi));
        return -1;
    }
    memset(acb->task, 0, sizeof(struct scsi_task));

    acb->task->xfer_dir = SCSI_XFER_READ;
    acb->task->expxferlen = size;
    lba = sector_qemu2lun(acb->sector_num, acb->iscsilun);
    num_sectors = sector_qemu2lun(acb->nb_sectors, acb->iscsilun);

    switch (acb->iscsilun->type) {
    case TYPE_DISK:
        acb->task->cdb_size = 16;
        acb->task->cdb[0]  = 0x88;
        *(uint32_t *)&acb->task->cdb[2]  = htonl(lba >> 32);
        *(uint32_t *)&acb->task->cdb[6]  = htonl(lba & 0xffffffff);
        *(uint32_t *)&acb->task->cdb[10] = htonl(num_sectors);
        break;
    default:
        acb->task->cdb_size = 10;
        acb->task->cdb[0]  = 0x28;
        *(uint32_t *)&acb->task->cdb[2] = htonl(lba);
        *(uint16_t *)&acb->task->cdb[7] = htons(num_sectors);
        break;
    }

    ret = iscsi_scsi_command_async(iscsi, acb->iscsilun->lun, acb->task,
                                   iscsi_aio_read16_cb,
                                   NULL,
                                   acb);
    if (ret != 0) {
        scsi_free_scsi_task(acb->task);
        return -1;
    }

#if defined(LIBISCSI_FEATURE_IOVECTOR)
    scsi_task_set_iov_in(acb->task, (struct scsi_iovec*) acb->qiov->iov, acb->qiov->niov);
#else
    for (i = 0; i < acb->qiov->niov; i++) {
        scsi_task_add_data_in_buffer(acb->task,
                acb->qiov->iov[i].iov_len,
                acb->qiov->iov[i].iov_base);
    }
#endif
    return 0;
}

static BlockDriverAIOCB *
iscsi_aio_readv(BlockDriverState *bs, int64_t sector_num,
                QEMUIOVector *qiov, int nb_sectors,
                BlockDriverCompletionFunc *cb,
                void *opaque)
{
    IscsiLun *iscsilun = bs->opaque;
    IscsiAIOCB *acb;

    if (!is_request_lun_aligned(sector_num, nb_sectors, iscsilun)) {
        return NULL;
    }

    acb = qemu_aio_get(&iscsi_aiocb_info, bs, cb, opaque);
    trace_iscsi_aio_readv(iscsilun->iscsi, sector_num, nb_sectors, opaque, acb);

    acb->nb_sectors  = nb_sectors;
    acb->sector_num  = sector_num;
    acb->iscsilun    = iscsilun;
    acb->qiov        = qiov;
    acb->retries     = ISCSI_CMD_RETRIES;

    if (iscsi_aio_readv_acb(acb) != 0) {
        qemu_aio_release(acb);
        return NULL;
    }

    iscsi_set_events(iscsilun);
    return &acb->common;
}

static int
iscsi_aio_flush_acb(IscsiAIOCB *acb);

static void
iscsi_synccache10_cb(struct iscsi_context *iscsi, int status,
                     void *command_data, void *opaque)
{
    IscsiAIOCB *acb = opaque;

    if (acb->canceled != 0) {
        return;
    }

    acb->status = 0;
    if (status != 0) {
        if (status == SCSI_STATUS_CHECK_CONDITION
            && acb->task->sense.key == SCSI_SENSE_UNIT_ATTENTION
            && acb->retries-- > 0) {
            scsi_free_scsi_task(acb->task);
            acb->task = NULL;
            if (iscsi_aio_flush_acb(acb) == 0) {
                iscsi_set_events(acb->iscsilun);
                return;
            }
        }
        error_report("Failed to sync10 data on iSCSI lun. %s",
                     iscsi_get_error(iscsi));
        acb->status = -EIO;
    }

    iscsi_schedule_bh(acb);
}

static int
iscsi_aio_flush_acb(IscsiAIOCB *acb)
{
    struct iscsi_context *iscsi = acb->iscsilun->iscsi;

    acb->canceled   = 0;
    acb->bh         = NULL;
    acb->status     = -EINPROGRESS;
    acb->buf        = NULL;

    acb->task = iscsi_synchronizecache10_task(iscsi, acb->iscsilun->lun,
                                         0, 0, 0, 0,
                                         iscsi_synccache10_cb,
                                         acb);
    if (acb->task == NULL) {
        error_report("iSCSI: Failed to send synchronizecache10 command. %s",
                     iscsi_get_error(iscsi));
        return -1;
    }

    return 0;
}

static BlockDriverAIOCB *
iscsi_aio_flush(BlockDriverState *bs,
                BlockDriverCompletionFunc *cb, void *opaque)
{
    IscsiLun *iscsilun = bs->opaque;

    IscsiAIOCB *acb;

    acb = qemu_aio_get(&iscsi_aiocb_info, bs, cb, opaque);

    acb->iscsilun    = iscsilun;
    acb->retries     = ISCSI_CMD_RETRIES;

    if (iscsi_aio_flush_acb(acb) != 0) {
        qemu_aio_release(acb);
        return NULL;
    }

    iscsi_set_events(iscsilun);

    return &acb->common;
}

static int iscsi_aio_discard_acb(IscsiAIOCB *acb);

static void
iscsi_unmap_cb(struct iscsi_context *iscsi, int status,
                     void *command_data, void *opaque)
{
    IscsiAIOCB *acb = opaque;

    if (acb->canceled != 0) {
        return;
    }

    acb->status = 0;
    if (status != 0) {
        if (status == SCSI_STATUS_CHECK_CONDITION
            && acb->task->sense.key == SCSI_SENSE_UNIT_ATTENTION
            && acb->retries-- > 0) {
            scsi_free_scsi_task(acb->task);
            acb->task = NULL;
            if (iscsi_aio_discard_acb(acb) == 0) {
                iscsi_set_events(acb->iscsilun);
                return;
            }
        }
        error_report("Failed to unmap data on iSCSI lun. %s",
                     iscsi_get_error(iscsi));
        acb->status = -EIO;
    }

    iscsi_schedule_bh(acb);
}

static int iscsi_aio_discard_acb(IscsiAIOCB *acb) {
    struct iscsi_context *iscsi = acb->iscsilun->iscsi;
    struct unmap_list list[1];

    acb->canceled   = 0;
    acb->bh         = NULL;
    acb->status     = -EINPROGRESS;
    acb->buf        = NULL;

    list[0].lba = sector_qemu2lun(acb->sector_num, acb->iscsilun);
    list[0].num = acb->nb_sectors * BDRV_SECTOR_SIZE / acb->iscsilun->block_size;

    acb->task = iscsi_unmap_task(iscsi, acb->iscsilun->lun,
                                 0, 0, &list[0], 1,
                                 iscsi_unmap_cb,
                                 acb);
    if (acb->task == NULL) {
        error_report("iSCSI: Failed to send unmap command. %s",
                     iscsi_get_error(iscsi));
        return -1;
    }

    return 0;
}

static BlockDriverAIOCB *
iscsi_aio_discard(BlockDriverState *bs,
                  int64_t sector_num, int nb_sectors,
                  BlockDriverCompletionFunc *cb, void *opaque)
{
    IscsiLun *iscsilun = bs->opaque;
    IscsiAIOCB *acb;

    acb = qemu_aio_get(&iscsi_aiocb_info, bs, cb, opaque);

    acb->iscsilun    = iscsilun;
    acb->nb_sectors  = nb_sectors;
    acb->sector_num  = sector_num;
    acb->retries     = ISCSI_CMD_RETRIES;

    if (iscsi_aio_discard_acb(acb) != 0) {
        qemu_aio_release(acb);
        return NULL;
    }

    iscsi_set_events(iscsilun);

    return &acb->common;
}

#ifdef __linux__
static void
iscsi_aio_ioctl_cb(struct iscsi_context *iscsi, int status,
                     void *command_data, void *opaque)
{
    IscsiAIOCB *acb = opaque;

    g_free(acb->buf);
    acb->buf = NULL;

    if (acb->canceled != 0) {
        return;
    }

    acb->status = 0;
    if (status < 0) {
        error_report("Failed to ioctl(SG_IO) to iSCSI lun. %s",
                     iscsi_get_error(iscsi));
        acb->status = -EIO;
    }

    acb->ioh->driver_status = 0;
    acb->ioh->host_status   = 0;
    acb->ioh->resid         = 0;

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

static BlockDriverAIOCB *iscsi_aio_ioctl(BlockDriverState *bs,
        unsigned long int req, void *buf,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    IscsiLun *iscsilun = bs->opaque;
    struct iscsi_context *iscsi = iscsilun->iscsi;
    struct iscsi_data data;
    IscsiAIOCB *acb;

    assert(req == SG_IO);

    acb = qemu_aio_get(&iscsi_aiocb_info, bs, cb, opaque);

    acb->iscsilun = iscsilun;
    acb->canceled    = 0;
    acb->bh          = NULL;
    acb->status      = -EINPROGRESS;
    acb->buf         = NULL;
    acb->ioh         = buf;

    acb->task = malloc(sizeof(struct scsi_task));
    if (acb->task == NULL) {
        error_report("iSCSI: Failed to allocate task for scsi command. %s",
                     iscsi_get_error(iscsi));
        qemu_aio_release(acb);
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
#if defined(LIBISCSI_FEATURE_IOVECTOR)
            scsi_task_set_iov_out(acb->task,
                                 (struct scsi_iovec *) acb->ioh->dxferp,
                                 acb->ioh->iovec_count);
#else
            struct iovec *iov = (struct iovec *)acb->ioh->dxferp;

            acb->buf = g_malloc(acb->ioh->dxfer_len);
            data.data = acb->buf;
            data.size = iov_to_buf(iov, acb->ioh->iovec_count, 0,
                                   acb->buf, acb->ioh->dxfer_len);
#endif
        }
    }

    if (iscsi_scsi_command_async(iscsi, iscsilun->lun, acb->task,
                                 iscsi_aio_ioctl_cb,
                                 (data.size > 0) ? &data : NULL,
                                 acb) != 0) {
        scsi_free_scsi_task(acb->task);
        qemu_aio_release(acb);
        return NULL;
    }

    /* tell libiscsi to read straight into the buffer we got from ioctl */
    if (acb->task->xfer_dir == SCSI_XFER_READ) {
        if (acb->ioh->iovec_count == 0) {
            scsi_task_add_data_in_buffer(acb->task,
                                         acb->ioh->dxfer_len,
                                         acb->ioh->dxferp);
        } else {
#if defined(LIBISCSI_FEATURE_IOVECTOR)
            scsi_task_set_iov_in(acb->task,
                                 (struct scsi_iovec *) acb->ioh->dxferp,
                                 acb->ioh->iovec_count);
#else
            int i;
            for (i = 0; i < acb->ioh->iovec_count; i++) {
                struct iovec *iov = (struct iovec *)acb->ioh->dxferp;

                scsi_task_add_data_in_buffer(acb->task,
                    iov[i].iov_len,
                    iov[i].iov_base);
            }
#endif
        }
    }

    iscsi_set_events(iscsilun);

    return &acb->common;
}


static void ioctl_cb(void *opaque, int status)
{
    int *p_status = opaque;
    *p_status = status;
}

static int iscsi_ioctl(BlockDriverState *bs, unsigned long int req, void *buf)
{
    IscsiLun *iscsilun = bs->opaque;
    int status;

    switch (req) {
    case SG_GET_VERSION_NUM:
        *(int *)buf = 30000;
        break;
    case SG_GET_SCSI_ID:
        ((struct sg_scsi_id *)buf)->scsi_type = iscsilun->type;
        break;
    case SG_IO:
        status = -EINPROGRESS;
        iscsi_aio_ioctl(bs, req, buf, ioctl_cb, &status);

        while (status == -EINPROGRESS) {
            qemu_aio_wait();
        }

        return 0;
    default:
        return -1;
    }
    return 0;
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

static int parse_chap(struct iscsi_context *iscsi, const char *target)
{
    QemuOptsList *list;
    QemuOpts *opts;
    const char *user = NULL;
    const char *password = NULL;

    list = qemu_find_opts("iscsi");
    if (!list) {
        return 0;
    }

    opts = qemu_opts_find(list, target);
    if (opts == NULL) {
        opts = QTAILQ_FIRST(&list->head);
        if (!opts) {
            return 0;
        }
    }

    user = qemu_opt_get(opts, "user");
    if (!user) {
        return 0;
    }

    password = qemu_opt_get(opts, "password");
    if (!password) {
        error_report("CHAP username specified but no password was given");
        return -1;
    }

    if (iscsi_set_initiator_username_pwd(iscsi, user, password)) {
        error_report("Failed to set initiator username and password");
        return -1;
    }

    return 0;
}

static void parse_header_digest(struct iscsi_context *iscsi, const char *target)
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
        error_report("Invalid header-digest setting : %s", digest);
    }
}

static char *parse_initiator_name(const char *target)
{
    QemuOptsList *list;
    QemuOpts *opts;
    const char *name = NULL;
    const char *iscsi_name = qemu_get_vm_name();

    list = qemu_find_opts("iscsi");
    if (list) {
        opts = qemu_opts_find(list, target);
        if (!opts) {
            opts = QTAILQ_FIRST(&list->head);
        }
        if (opts) {
            name = qemu_opt_get(opts, "initiator-name");
        }
    }

    if (name) {
        return g_strdup(name);
    } else {
        return g_strdup_printf("iqn.2008-11.org.linux-kvm%s%s",
                               iscsi_name ? ":" : "",
                               iscsi_name ? iscsi_name : "");
    }
}

#if defined(LIBISCSI_FEATURE_NOP_COUNTER)
static void iscsi_nop_timed_event(void *opaque)
{
    IscsiLun *iscsilun = opaque;

    if (iscsi_get_nops_in_flight(iscsilun->iscsi) > MAX_NOP_FAILURES) {
        error_report("iSCSI: NOP timeout. Reconnecting...");
        iscsi_reconnect(iscsilun->iscsi);
    }

    if (iscsi_nop_out_async(iscsilun->iscsi, NULL, NULL, 0, NULL) != 0) {
        error_report("iSCSI: failed to sent NOP-Out. Disabling NOP messages.");
        return;
    }

    qemu_mod_timer(iscsilun->nop_timer, qemu_get_clock_ms(rt_clock) + NOP_INTERVAL);
    iscsi_set_events(iscsilun);
}
#endif

static int iscsi_readcapacity_sync(IscsiLun *iscsilun)
{
    struct scsi_task *task = NULL;
    struct scsi_readcapacity10 *rc10 = NULL;
    struct scsi_readcapacity16 *rc16 = NULL;
    int ret = 0;
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
                    error_report("iSCSI: Failed to unmarshall readcapacity16 data.");
                    ret = -EINVAL;
                } else {
                    iscsilun->block_size = rc16->block_length;
                    iscsilun->num_blocks = rc16->returned_lba + 1;
                }
            }
            break;
        case TYPE_ROM:
            task = iscsi_readcapacity10_sync(iscsilun->iscsi, iscsilun->lun, 0, 0);
            if (task != NULL && task->status == SCSI_STATUS_GOOD) {
                rc10 = scsi_datain_unmarshall(task);
                if (rc10 == NULL) {
                    error_report("iSCSI: Failed to unmarshall readcapacity10 data.");
                    ret = -EINVAL;
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
            return 0;
        }
    } while (task != NULL && task->status == SCSI_STATUS_CHECK_CONDITION
             && task->sense.key == SCSI_SENSE_UNIT_ATTENTION
             && retries-- > 0);

    if (task == NULL || task->status != SCSI_STATUS_GOOD) {
        error_report("iSCSI: failed to send readcapacity10 command.");
        ret = -EINVAL;
    }
    if (task) {
        scsi_free_scsi_task(task);
    }
    return ret;
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

/*
 * We support iscsi url's on the form
 * iscsi://[<username>%<password>@]<host>[:<port>]/<targetname>/<lun>
 */
static int iscsi_open(BlockDriverState *bs, QDict *options, int flags)
{
    IscsiLun *iscsilun = bs->opaque;
    struct iscsi_context *iscsi = NULL;
    struct iscsi_url *iscsi_url = NULL;
    struct scsi_task *task = NULL;
    struct scsi_inquiry_standard *inq = NULL;
    char *initiator_name = NULL;
    QemuOpts *opts;
    Error *local_err = NULL;
    const char *filename;
    int ret;

    if ((BDRV_SECTOR_SIZE % 512) != 0) {
        error_report("iSCSI: Invalid BDRV_SECTOR_SIZE. "
                     "BDRV_SECTOR_SIZE(%lld) is not a multiple "
                     "of 512", BDRV_SECTOR_SIZE);
        return -EINVAL;
    }

    opts = qemu_opts_create_nofail(&runtime_opts);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (error_is_set(&local_err)) {
        qerror_report_err(local_err);
        error_free(local_err);
        ret = -EINVAL;
        goto out;
    }

    filename = qemu_opt_get(opts, "filename");


    iscsi_url = iscsi_parse_full_url(iscsi, filename);
    if (iscsi_url == NULL) {
        error_report("Failed to parse URL : %s", filename);
        ret = -EINVAL;
        goto out;
    }

    memset(iscsilun, 0, sizeof(IscsiLun));

    initiator_name = parse_initiator_name(iscsi_url->target);

    iscsi = iscsi_create_context(initiator_name);
    if (iscsi == NULL) {
        error_report("iSCSI: Failed to create iSCSI context.");
        ret = -ENOMEM;
        goto out;
    }

    if (iscsi_set_targetname(iscsi, iscsi_url->target)) {
        error_report("iSCSI: Failed to set target name.");
        ret = -EINVAL;
        goto out;
    }

    if (iscsi_url->user != NULL) {
        ret = iscsi_set_initiator_username_pwd(iscsi, iscsi_url->user,
                                              iscsi_url->passwd);
        if (ret != 0) {
            error_report("Failed to set initiator username and password");
            ret = -EINVAL;
            goto out;
        }
    }

    /* check if we got CHAP username/password via the options */
    if (parse_chap(iscsi, iscsi_url->target) != 0) {
        error_report("iSCSI: Failed to set CHAP user/password");
        ret = -EINVAL;
        goto out;
    }

    if (iscsi_set_session_type(iscsi, ISCSI_SESSION_NORMAL) != 0) {
        error_report("iSCSI: Failed to set session type to normal.");
        ret = -EINVAL;
        goto out;
    }

    iscsi_set_header_digest(iscsi, ISCSI_HEADER_DIGEST_NONE_CRC32C);

    /* check if we got HEADER_DIGEST via the options */
    parse_header_digest(iscsi, iscsi_url->target);

    if (iscsi_full_connect_sync(iscsi, iscsi_url->portal, iscsi_url->lun) != 0) {
        error_report("iSCSI: Failed to connect to LUN : %s",
            iscsi_get_error(iscsi));
        ret = -EINVAL;
        goto out;
    }

    iscsilun->iscsi = iscsi;
    iscsilun->lun   = iscsi_url->lun;

    task = iscsi_inquiry_sync(iscsi, iscsilun->lun, 0, 0, 36);

    if (task == NULL || task->status != SCSI_STATUS_GOOD) {
        error_report("iSCSI: failed to send inquiry command.");
        ret = -EINVAL;
        goto out;
    }

    inq = scsi_datain_unmarshall(task);
    if (inq == NULL) {
        error_report("iSCSI: Failed to unmarshall inquiry data.");
        ret = -EINVAL;
        goto out;
    }

    iscsilun->type = inq->periperal_device_type;

    if ((ret = iscsi_readcapacity_sync(iscsilun)) != 0) {
        goto out;
    }
    bs->total_sectors    = iscsilun->num_blocks *
                           iscsilun->block_size / BDRV_SECTOR_SIZE ;

    /* Medium changer or tape. We dont have any emulation for this so this must
     * be sg ioctl compatible. We force it to be sg, otherwise qemu will try
     * to read from the device to guess the image format.
     */
    if (iscsilun->type == TYPE_MEDIUM_CHANGER ||
        iscsilun->type == TYPE_TAPE) {
        bs->sg = 1;
    }

#if defined(LIBISCSI_FEATURE_NOP_COUNTER)
    /* Set up a timer for sending out iSCSI NOPs */
    iscsilun->nop_timer = qemu_new_timer_ms(rt_clock, iscsi_nop_timed_event, iscsilun);
    qemu_mod_timer(iscsilun->nop_timer, qemu_get_clock_ms(rt_clock) + NOP_INTERVAL);
#endif

out:
    qemu_opts_del(opts);
    if (initiator_name != NULL) {
        g_free(initiator_name);
    }
    if (iscsi_url != NULL) {
        iscsi_destroy_url(iscsi_url);
    }
    if (task != NULL) {
        scsi_free_scsi_task(task);
    }

    if (ret) {
        if (iscsi != NULL) {
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

    if (iscsilun->nop_timer) {
        qemu_del_timer(iscsilun->nop_timer);
        qemu_free_timer(iscsilun->nop_timer);
    }
    qemu_aio_set_fd_handler(iscsi_get_fd(iscsi), NULL, NULL, NULL, NULL);
    iscsi_destroy_context(iscsi);
    memset(iscsilun, 0, sizeof(IscsiLun));
}

static int iscsi_truncate(BlockDriverState *bs, int64_t offset)
{
    IscsiLun *iscsilun = bs->opaque;
    int ret = 0;

    if (iscsilun->type != TYPE_DISK) {
        return -ENOTSUP;
    }

    if ((ret = iscsi_readcapacity_sync(iscsilun)) != 0) {
        return ret;
    }

    if (offset > iscsi_getlength(bs)) {
        return -EINVAL;
    }

    return 0;
}

static int iscsi_has_zero_init(BlockDriverState *bs)
{
    return 0;
}

static int iscsi_create(const char *filename, QEMUOptionParameter *options)
{
    int ret = 0;
    int64_t total_size = 0;
    BlockDriverState bs;
    IscsiLun *iscsilun = NULL;
    QDict *bs_options;

    memset(&bs, 0, sizeof(BlockDriverState));

    /* Read out options */
    while (options && options->name) {
        if (!strcmp(options->name, "size")) {
            total_size = options->value.n / BDRV_SECTOR_SIZE;
        }
        options++;
    }

    bs.opaque = g_malloc0(sizeof(struct IscsiLun));
    iscsilun = bs.opaque;

    bs_options = qdict_new();
    qdict_put(bs_options, "filename", qstring_from_str(filename));
    ret = iscsi_open(&bs, bs_options, 0);
    QDECREF(bs_options);

    if (ret != 0) {
        goto out;
    }
    if (iscsilun->nop_timer) {
        qemu_del_timer(iscsilun->nop_timer);
        qemu_free_timer(iscsilun->nop_timer);
    }
    if (iscsilun->type != TYPE_DISK) {
        ret = -ENODEV;
        goto out;
    }
    if (bs.total_sectors < total_size) {
        ret = -ENOSPC;
        goto out;
    }

    ret = 0;
out:
    if (iscsilun->iscsi != NULL) {
        iscsi_destroy_context(iscsilun->iscsi);
    }
    g_free(bs.opaque);
    return ret;
}

static QEMUOptionParameter iscsi_create_options[] = {
    {
        .name = BLOCK_OPT_SIZE,
        .type = OPT_SIZE,
        .help = "Virtual disk size"
    },
    { NULL }
};

static BlockDriver bdrv_iscsi = {
    .format_name     = "iscsi",
    .protocol_name   = "iscsi",

    .instance_size   = sizeof(IscsiLun),
    .bdrv_file_open  = iscsi_open,
    .bdrv_close      = iscsi_close,
    .bdrv_create     = iscsi_create,
    .create_options  = iscsi_create_options,

    .bdrv_getlength  = iscsi_getlength,
    .bdrv_truncate   = iscsi_truncate,

    .bdrv_aio_readv  = iscsi_aio_readv,
    .bdrv_aio_writev = iscsi_aio_writev,
    .bdrv_aio_flush  = iscsi_aio_flush,

    .bdrv_aio_discard = iscsi_aio_discard,
    .bdrv_has_zero_init = iscsi_has_zero_init,

#ifdef __linux__
    .bdrv_ioctl       = iscsi_ioctl,
    .bdrv_aio_ioctl   = iscsi_aio_ioctl,
#endif
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
            .name = "header-digest",
            .type = QEMU_OPT_STRING,
            .help = "HeaderDigest setting. "
                    "{CRC32C|CRC32C-NONE|NONE-CRC32C|NONE}",
        },{
            .name = "initiator-name",
            .type = QEMU_OPT_STRING,
            .help = "Initiator iqn name to use when connecting",
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
