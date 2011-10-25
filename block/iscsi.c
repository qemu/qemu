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
#include "qemu-common.h"
#include "qemu-error.h"
#include "block_int.h"
#include "trace.h"

#include <iscsi/iscsi.h>
#include <iscsi/scsi-lowlevel.h>


typedef struct IscsiLun {
    struct iscsi_context *iscsi;
    int lun;
    int block_size;
    unsigned long num_blocks;
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
    size_t read_size;
    size_t read_offset;
} IscsiAIOCB;

struct IscsiTask {
    IscsiLun *iscsilun;
    BlockDriverState *bs;
    int status;
    int complete;
};

static void
iscsi_abort_task_cb(struct iscsi_context *iscsi, int status, void *command_data,
                    void *private_data)
{
}

static void
iscsi_aio_cancel(BlockDriverAIOCB *blockacb)
{
    IscsiAIOCB *acb = (IscsiAIOCB *)blockacb;
    IscsiLun *iscsilun = acb->iscsilun;

    acb->common.cb(acb->common.opaque, -ECANCELED);
    acb->canceled = 1;

    /* send a task mgmt call to the target to cancel the task on the target */
    iscsi_task_mgmt_abort_task_async(iscsilun->iscsi, acb->task,
                                     iscsi_abort_task_cb, NULL);

    /* then also cancel the task locally in libiscsi */
    iscsi_scsi_task_cancel(iscsilun->iscsi, acb->task);
}

static AIOPool iscsi_aio_pool = {
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

    qemu_aio_set_fd_handler(iscsi_get_fd(iscsi), iscsi_process_read,
                           (iscsi_which_events(iscsi) & POLLOUT)
                           ? iscsi_process_write : NULL,
                           iscsi_process_flush, NULL, iscsilun);
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
iscsi_schedule_bh(QEMUBHFunc *cb, IscsiAIOCB *acb)
{
    acb->bh = qemu_bh_new(cb, acb);
    if (!acb->bh) {
        error_report("oom: could not create iscsi bh");
        return -EIO;
    }

    qemu_bh_schedule(acb->bh);
    return 0;
}

static void
iscsi_readv_writev_bh_cb(void *p)
{
    IscsiAIOCB *acb = p;

    qemu_bh_delete(acb->bh);

    if (acb->canceled == 0) {
        acb->common.cb(acb->common.opaque, acb->status);
    }

    qemu_aio_release(acb);
}


static void
iscsi_aio_write10_cb(struct iscsi_context *iscsi, int status,
                     void *command_data, void *opaque)
{
    IscsiAIOCB *acb = opaque;

    trace_iscsi_aio_write10_cb(iscsi, status, acb, acb->canceled);

    g_free(acb->buf);

    if (acb->canceled != 0) {
        qemu_aio_release(acb);
        scsi_free_scsi_task(acb->task);
        acb->task = NULL;
        return;
    }

    acb->status = 0;
    if (status < 0) {
        error_report("Failed to write10 data to iSCSI lun. %s",
                     iscsi_get_error(iscsi));
        acb->status = -EIO;
    }

    iscsi_schedule_bh(iscsi_readv_writev_bh_cb, acb);
    scsi_free_scsi_task(acb->task);
    acb->task = NULL;
}

static int64_t sector_qemu2lun(int64_t sector, IscsiLun *iscsilun)
{
    return sector * BDRV_SECTOR_SIZE / iscsilun->block_size;
}

static BlockDriverAIOCB *
iscsi_aio_writev(BlockDriverState *bs, int64_t sector_num,
                 QEMUIOVector *qiov, int nb_sectors,
                 BlockDriverCompletionFunc *cb,
                 void *opaque)
{
    IscsiLun *iscsilun = bs->opaque;
    struct iscsi_context *iscsi = iscsilun->iscsi;
    IscsiAIOCB *acb;
    size_t size;
    int fua = 0;

    /* set FUA on writes when cache mode is write through */
    if (!(bs->open_flags & BDRV_O_CACHE_WB)) {
        fua = 1;
    }

    acb = qemu_aio_get(&iscsi_aio_pool, bs, cb, opaque);
    trace_iscsi_aio_writev(iscsi, sector_num, nb_sectors, opaque, acb);

    acb->iscsilun = iscsilun;
    acb->qiov     = qiov;

    acb->canceled   = 0;

    /* XXX we should pass the iovec to write10 to avoid the extra copy */
    /* this will allow us to get rid of 'buf' completely */
    size = nb_sectors * BDRV_SECTOR_SIZE;
    acb->buf = g_malloc(size);
    qemu_iovec_to_buffer(acb->qiov, acb->buf);
    acb->task = iscsi_write10_task(iscsi, iscsilun->lun, acb->buf, size,
                              sector_qemu2lun(sector_num, iscsilun),
                              fua, 0, iscsilun->block_size,
                              iscsi_aio_write10_cb, acb);
    if (acb->task == NULL) {
        error_report("iSCSI: Failed to send write10 command. %s",
                     iscsi_get_error(iscsi));
        g_free(acb->buf);
        qemu_aio_release(acb);
        return NULL;
    }

    iscsi_set_events(iscsilun);

    return &acb->common;
}

static void
iscsi_aio_read10_cb(struct iscsi_context *iscsi, int status,
                    void *command_data, void *opaque)
{
    IscsiAIOCB *acb = opaque;

    trace_iscsi_aio_read10_cb(iscsi, status, acb, acb->canceled);

    if (acb->canceled != 0) {
        qemu_aio_release(acb);
        scsi_free_scsi_task(acb->task);
        acb->task = NULL;
        return;
    }

    acb->status = 0;
    if (status != 0) {
        error_report("Failed to read10 data from iSCSI lun. %s",
                     iscsi_get_error(iscsi));
        acb->status = -EIO;
    }

    iscsi_schedule_bh(iscsi_readv_writev_bh_cb, acb);
    scsi_free_scsi_task(acb->task);
    acb->task = NULL;
}

static BlockDriverAIOCB *
iscsi_aio_readv(BlockDriverState *bs, int64_t sector_num,
                QEMUIOVector *qiov, int nb_sectors,
                BlockDriverCompletionFunc *cb,
                void *opaque)
{
    IscsiLun *iscsilun = bs->opaque;
    struct iscsi_context *iscsi = iscsilun->iscsi;
    IscsiAIOCB *acb;
    size_t qemu_read_size, lun_read_size;
    int i;

    qemu_read_size = BDRV_SECTOR_SIZE * (size_t)nb_sectors;

    acb = qemu_aio_get(&iscsi_aio_pool, bs, cb, opaque);
    trace_iscsi_aio_readv(iscsi, sector_num, nb_sectors, opaque, acb);

    acb->iscsilun = iscsilun;
    acb->qiov     = qiov;

    acb->canceled    = 0;
    acb->read_size   = qemu_read_size;
    acb->buf         = NULL;

    /* If LUN blocksize is bigger than BDRV_BLOCK_SIZE a read from QEMU
     * may be misaligned to the LUN, so we may need to read some extra
     * data.
     */
    acb->read_offset = 0;
    if (iscsilun->block_size > BDRV_SECTOR_SIZE) {
        uint64_t bdrv_offset = BDRV_SECTOR_SIZE * sector_num;

        acb->read_offset  = bdrv_offset % iscsilun->block_size;
    }

    lun_read_size  = (qemu_read_size + iscsilun->block_size
                     + acb->read_offset - 1)
                     / iscsilun->block_size * iscsilun->block_size;
    acb->task = iscsi_read10_task(iscsi, iscsilun->lun,
                             sector_qemu2lun(sector_num, iscsilun),
                             lun_read_size, iscsilun->block_size,
                             iscsi_aio_read10_cb, acb);
    if (acb->task == NULL) {
        error_report("iSCSI: Failed to send read10 command. %s",
                     iscsi_get_error(iscsi));
        qemu_aio_release(acb);
        return NULL;
    }

    for (i = 0; i < acb->qiov->niov; i++) {
        scsi_task_add_data_in_buffer(acb->task,
                acb->qiov->iov[i].iov_len,
                acb->qiov->iov[i].iov_base);
    }

    iscsi_set_events(iscsilun);

    return &acb->common;
}


static void
iscsi_synccache10_cb(struct iscsi_context *iscsi, int status,
                     void *command_data, void *opaque)
{
    IscsiAIOCB *acb = opaque;

    if (acb->canceled != 0) {
        qemu_aio_release(acb);
        scsi_free_scsi_task(acb->task);
        acb->task = NULL;
        return;
    }

    acb->status = 0;
    if (status < 0) {
        error_report("Failed to sync10 data on iSCSI lun. %s",
                     iscsi_get_error(iscsi));
        acb->status = -EIO;
    }

    iscsi_schedule_bh(iscsi_readv_writev_bh_cb, acb);
    scsi_free_scsi_task(acb->task);
    acb->task = NULL;
}

static BlockDriverAIOCB *
iscsi_aio_flush(BlockDriverState *bs,
                BlockDriverCompletionFunc *cb, void *opaque)
{
    IscsiLun *iscsilun = bs->opaque;
    struct iscsi_context *iscsi = iscsilun->iscsi;
    IscsiAIOCB *acb;

    acb = qemu_aio_get(&iscsi_aio_pool, bs, cb, opaque);

    acb->iscsilun = iscsilun;
    acb->canceled   = 0;

    acb->task = iscsi_synchronizecache10_task(iscsi, iscsilun->lun,
                                         0, 0, 0, 0,
                                         iscsi_synccache10_cb,
                                         acb);
    if (acb->task == NULL) {
        error_report("iSCSI: Failed to send synchronizecache10 command. %s",
                     iscsi_get_error(iscsi));
        qemu_aio_release(acb);
        return NULL;
    }

    iscsi_set_events(iscsilun);

    return &acb->common;
}

static int64_t
iscsi_getlength(BlockDriverState *bs)
{
    IscsiLun *iscsilun = bs->opaque;
    int64_t len;

    len  = iscsilun->num_blocks;
    len *= iscsilun->block_size;

    return len;
}

static void
iscsi_readcapacity10_cb(struct iscsi_context *iscsi, int status,
                        void *command_data, void *opaque)
{
    struct IscsiTask *itask = opaque;
    struct scsi_readcapacity10 *rc10;
    struct scsi_task *task = command_data;

    if (status != 0) {
        error_report("iSCSI: Failed to read capacity of iSCSI lun. %s",
                     iscsi_get_error(iscsi));
        itask->status   = 1;
        itask->complete = 1;
        scsi_free_scsi_task(task);
        return;
    }

    rc10 = scsi_datain_unmarshall(task);
    if (rc10 == NULL) {
        error_report("iSCSI: Failed to unmarshall readcapacity10 data.");
        itask->status   = 1;
        itask->complete = 1;
        scsi_free_scsi_task(task);
        return;
    }

    itask->iscsilun->block_size = rc10->block_size;
    itask->iscsilun->num_blocks = rc10->lba;
    itask->bs->total_sectors = (uint64_t)rc10->lba *
                               rc10->block_size / BDRV_SECTOR_SIZE ;

    itask->status   = 0;
    itask->complete = 1;
    scsi_free_scsi_task(task);
}


static void
iscsi_connect_cb(struct iscsi_context *iscsi, int status, void *command_data,
                 void *opaque)
{
    struct IscsiTask *itask = opaque;
    struct scsi_task *task;

    if (status != 0) {
        itask->status   = 1;
        itask->complete = 1;
        return;
    }

    task = iscsi_readcapacity10_task(iscsi, itask->iscsilun->lun, 0, 0,
                                   iscsi_readcapacity10_cb, opaque);
    if (task == NULL) {
        error_report("iSCSI: failed to send readcapacity command.");
        itask->status   = 1;
        itask->complete = 1;
        return;
    }
}

/*
 * We support iscsi url's on the form
 * iscsi://[<username>%<password>@]<host>[:<port>]/<targetname>/<lun>
 */
static int iscsi_open(BlockDriverState *bs, const char *filename, int flags)
{
    IscsiLun *iscsilun = bs->opaque;
    struct iscsi_context *iscsi = NULL;
    struct iscsi_url *iscsi_url = NULL;
    struct IscsiTask task;
    int ret;

    if ((BDRV_SECTOR_SIZE % 512) != 0) {
        error_report("iSCSI: Invalid BDRV_SECTOR_SIZE. "
                     "BDRV_SECTOR_SIZE(%lld) is not a multiple "
                     "of 512", BDRV_SECTOR_SIZE);
        return -EINVAL;
    }

    memset(iscsilun, 0, sizeof(IscsiLun));

    /* Should really append the KVM name after the ':' here */
    iscsi = iscsi_create_context("iqn.2008-11.org.linux-kvm:");
    if (iscsi == NULL) {
        error_report("iSCSI: Failed to create iSCSI context.");
        ret = -ENOMEM;
        goto failed;
    }

    iscsi_url = iscsi_parse_full_url(iscsi, filename);
    if (iscsi_url == NULL) {
        error_report("Failed to parse URL : %s %s", filename,
                     iscsi_get_error(iscsi));
        ret = -EINVAL;
        goto failed;
    }

    if (iscsi_set_targetname(iscsi, iscsi_url->target)) {
        error_report("iSCSI: Failed to set target name.");
        ret = -EINVAL;
        goto failed;
    }

    if (iscsi_url->user != NULL) {
        ret = iscsi_set_initiator_username_pwd(iscsi, iscsi_url->user,
                                              iscsi_url->passwd);
        if (ret != 0) {
            error_report("Failed to set initiator username and password");
            ret = -EINVAL;
            goto failed;
        }
    }
    if (iscsi_set_session_type(iscsi, ISCSI_SESSION_NORMAL) != 0) {
        error_report("iSCSI: Failed to set session type to normal.");
        ret = -EINVAL;
        goto failed;
    }

    iscsi_set_header_digest(iscsi, ISCSI_HEADER_DIGEST_NONE_CRC32C);

    task.iscsilun = iscsilun;
    task.status = 0;
    task.complete = 0;
    task.bs = bs;

    iscsilun->iscsi = iscsi;
    iscsilun->lun   = iscsi_url->lun;

    if (iscsi_full_connect_async(iscsi, iscsi_url->portal, iscsi_url->lun,
                                 iscsi_connect_cb, &task)
        != 0) {
        error_report("iSCSI: Failed to start async connect.");
        ret = -EINVAL;
        goto failed;
    }

    while (!task.complete) {
        iscsi_set_events(iscsilun);
        qemu_aio_wait();
    }
    if (task.status != 0) {
        error_report("iSCSI: Failed to connect to LUN : %s",
                     iscsi_get_error(iscsi));
        ret = -EINVAL;
        goto failed;
    }

    if (iscsi_url != NULL) {
        iscsi_destroy_url(iscsi_url);
    }
    return 0;

failed:
    if (iscsi_url != NULL) {
        iscsi_destroy_url(iscsi_url);
    }
    if (iscsi != NULL) {
        iscsi_destroy_context(iscsi);
    }
    memset(iscsilun, 0, sizeof(IscsiLun));
    return ret;
}

static void iscsi_close(BlockDriverState *bs)
{
    IscsiLun *iscsilun = bs->opaque;
    struct iscsi_context *iscsi = iscsilun->iscsi;

    qemu_aio_set_fd_handler(iscsi_get_fd(iscsi), NULL, NULL, NULL, NULL, NULL);
    iscsi_destroy_context(iscsi);
    memset(iscsilun, 0, sizeof(IscsiLun));
}

static BlockDriver bdrv_iscsi = {
    .format_name     = "iscsi",
    .protocol_name   = "iscsi",

    .instance_size   = sizeof(IscsiLun),
    .bdrv_file_open  = iscsi_open,
    .bdrv_close      = iscsi_close,

    .bdrv_getlength  = iscsi_getlength,

    .bdrv_aio_readv  = iscsi_aio_readv,
    .bdrv_aio_writev = iscsi_aio_writev,
    .bdrv_aio_flush  = iscsi_aio_flush,
};

static void iscsi_block_init(void)
{
    bdrv_register(&bdrv_iscsi);
}

block_init(iscsi_block_init);
